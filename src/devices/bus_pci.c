/*
 *  Copyright (C) 2004-2005  Anders Gavare.  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright  
 *     notice, this list of conditions and the following disclaimer in the 
 *     documentation and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 *  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE   
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 *  OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 *  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 *  OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 *  SUCH DAMAGE.
 *   
 *
 *  $Id: bus_pci.c,v 1.10 2005-03-18 23:20:52 debug Exp $
 *  
 *  This is a generic PCI bus device, used by even lower level devices.
 *  For example, the "gt" device used in Cobalt machines contains a PCI
 *  device.
 *
 *  TODO:  This more or less just a dummy bus device, so far.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "misc.h"

#include "bus_pci.h"


/*
 *  bus_pci_access():
 *
 *  relative_addr should be either BUS_PCI_ADDR or BUS_PCI_DATA. The uint64_t
 *  pointed to by data should contain the word to be written to the pci bus,
 *  or a placeholder for information read from the bus.
 *
 *  Returns 1 if ok, 0 on error.
 */
int bus_pci_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr,
	uint64_t *data, int writeflag, struct pci_data *pci_data)
{
	struct pci_device *dev, *found;
	int bus, device, function, registernr;

	if (writeflag == MEM_READ)
		*data = 0;

	switch (relative_addr) {
	case BUS_PCI_ADDR:
		if (writeflag == MEM_WRITE) {
			debug("[ bus_pci: write to  PCI ADDR: data = 0x%016llx"
			    " ]\n", (long long)*data);
			pci_data->pci_addr = *data;
		} else {
			debug("[ bus_pci: read from PCI ADDR (data = "
			    "0x%016llx) ]\n", (long long)pci_data->pci_addr);
			*data = pci_data->pci_addr;
		}
		break;
	case BUS_PCI_DATA:
		if (writeflag == MEM_WRITE) {
			debug("[ bus_pci: write to PCI DATA: data = "
			    "0x%016llx ]\n", (long long)*data);
			if (*data == 0xffffffffULL)
				pci_data->last_was_write_ffffffff = 1;
		} else {
			/*  Get the bus, device, and function numbers from
			    the address:  */
			bus        = (pci_data->pci_addr >> 16) & 0xff;
			device     = (pci_data->pci_addr >> 11) & 0x1f;
			function   = (pci_data->pci_addr >> 8)  & 0x7;
			registernr = (pci_data->pci_addr)       & 0xff;

			/*  Scan through the list of pci_device entries.  */
			dev = pci_data->first_device;
			found = NULL;

			while (dev != NULL && found == NULL) {
				if (dev->bus == bus &&
				    dev->function == function &&
				    dev->device == device)
					found = dev;
				dev = dev->next;
			}

			if (found == NULL) {
				if ((pci_data->pci_addr & 0xff) == 0)
					*data = 0xffffffff;
				else
					*data = 0;
				return 1;
			}

			*data = 0;

			if (pci_data->last_was_write_ffffffff &&
			    registernr >= 0x10 && registernr <= 0x24) {
				/*  TODO:  real length!!!  */
				*data = 0x00400000 - 1;
			} else if (found->read_register != NULL)
				*data = found->read_register(registernr);

			pci_data->last_was_write_ffffffff = 0;

			debug("[ bus_pci: read from PCI DATA, addr = 0x%08lx "
			    "(bus %i, device %i, function %i, register "
			    "0x%02x): 0x%08lx ]\n", (long)pci_data->pci_addr,
			    bus, device, function, registernr, (long)*data);
		}

		break;
	default:
		if (writeflag==MEM_READ) {
			debug("[ bus_pci: read from unimplemented addr "
			    "0x%x ]\n", (int)relative_addr);
			*data = 0;
		} else {
			debug("[ bus_pci: write to unimplemented addr "
			    "0x%x:", (int)relative_addr);
		}
	}

	return 1;
}


/*
 *  bus_pci_add():
 *
 *  Add a PCI device to a bus_pci device.
 */
void bus_pci_add(struct machine *machine, struct pci_data *pci_data,
	struct memory *mem, int bus, int device, int function,
	void (*init)(struct machine *, struct memory *),
	uint32_t (*read_register)(int reg))
{
	struct pci_device *new_device;

	/*  Make sure this bus/device/function number isn't already in use:  */
	new_device = pci_data->first_device;
	while (new_device != NULL) {
		if (new_device->bus == bus &&
		    new_device->device == device &&
		    new_device->function == function) {
			fatal("bus_pci_add(): (bus %i, device %i, function"
			    " %i) already in use\n", bus, device, function);
			return;
		}
		new_device = new_device->next;
	}

	new_device = malloc(sizeof(struct pci_device));
	if (new_device == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}

	memset(new_device, 0, sizeof(struct pci_device));
	new_device->bus           = bus;
	new_device->device        = device;
	new_device->function      = function;
	new_device->init          = init;
	new_device->read_register = read_register;

	/*  Add the new device first in the PCI bus' chain:  */
	new_device->next = pci_data->first_device;
	pci_data->first_device = new_device;

	/*  Call the PCI device' init function:  */
	if (init != NULL)
		init(machine, mem);
}


/*
 *  bus_pci_init():
 *
 *  This doesn't register a device, but instead returns a pointer to a struct
 *  which should be passed to bus_pci_access() when accessing the PCI bus.
 */
struct pci_data *bus_pci_init(int irq_nr)
{
	struct pci_data *d;

	d = malloc(sizeof(struct pci_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct pci_data));
	d->irq_nr = irq_nr;

	return d;
}

