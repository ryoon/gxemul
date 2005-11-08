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
 *  $Id: bus_pci.c,v 1.13 2005-11-08 11:01:46 debug Exp $
 *  
 *  Generic PCI bus framework. It is not a normal "device", but is used by
 *  individual PCI controllers and devices.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "device.h"
#include "memory.h"
#include "misc.h"

#include "bus_pci.h"


/*  #define debug fatal  */


/*
 *  bus_pci_data_access():
 */
void bus_pci_data_access(struct cpu *cpu, struct memory *mem,
	uint64_t *data, int len, int writeflag, struct pci_data *pci_data)
{
	struct pci_device *dev, *found = NULL;
	int bus, device, function, registernr;

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
			return;
		}

		*data = 0;

		if (pci_data->last_was_write_ffffffff &&
		    registernr >= 0x10 && registernr <= 0x24) {
			/*  TODO:  real length!!!  */
			*data = 0x00400000 - 1;
		} else if (registernr + len - 1 < PCI_CFG_MEM_SIZE) {
			/*  Read data as little-endian:  */
			*data = found->cfg_mem[registernr];
			if (len > 1)
				*data |= (found->cfg_mem[registernr+1] << 8);
			if (len > 2)
				*data |= (found->cfg_mem[registernr+2] << 16);
			if (len > 3)
				*data |= (found->cfg_mem[registernr+3] << 24);
			if (len > 4)
				fatal("TODO: more than 32-bit PCI access?\n");
		}

		pci_data->last_was_write_ffffffff = 0;

		debug("[ bus_pci: read from PCI DATA, addr = 0x%08lx "
		    "(bus %i, device %i, function %i, register "
		    "0x%02x): 0x%08lx ]\n", (long)pci_data->pci_addr,
		    bus, device, function, registernr, (long)*data);
	}
}


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
	uint64_t *data, int len, int writeflag, struct pci_data *pci_data)
{
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
		bus_pci_data_access(cpu, mem, data, len, writeflag, pci_data);
		break;

	default:
		if (writeflag == MEM_READ) {
			debug("[ bus_pci: read from unimplemented addr "
			    "0x%x ]\n", (int)relative_addr);
			*data = 0;
		} else {
			debug("[ bus_pci: write to unimplemented addr "
			    "0x%x: 0x%llx ]\n", (int)relative_addr,
			    (long long)data);
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
	char *name)
{
	struct pci_device *pd;
	void (*init)(struct machine *, struct memory *, struct pci_device *);

	if (pci_data == NULL) {
		fatal("bus_pci_add(): pci_data == NULL!\n");
		exit(1);
	}

	/*  Find the PCI device:  */
	init = pci_lookup_initf(name);

	/*  Make sure this bus/device/function number isn't already in use:  */
	pd = pci_data->first_device;
	while (pd != NULL) {
		if (pd->bus == bus && pd->device == device &&
		    pd->function == function) {
			fatal("bus_pci_add(): (bus %i, device %i, function"
			    " %i) already in use\n", bus, device, function);
			exit(1);
		}
		pd = pd->next;
	}

	pd = malloc(sizeof(struct pci_device));
	if (pd == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}

	memset(pd, 0, sizeof(struct pci_device));

	/*  Add the new device first in the PCI bus' chain:  */
	pd->next = pci_data->first_device;
	pci_data->first_device = pd;

	pd->bus      = bus;
	pd->device   = device;
	pd->function = function;

	/*  Initialize some default values:  */
	PCI_SET_DATA(PCI_COMMAND_STATUS_REG, 0xffffffffULL);	/*  TODO  */

	if (init == NULL) {
		fatal("No init function for PCI device \"%s\"?\n", name);
		exit(1);
	}

	/*  Call the PCI device' init function:  */
	init(machine, mem, pd);
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

