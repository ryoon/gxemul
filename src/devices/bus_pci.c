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
 *  $Id: bus_pci.c,v 1.15 2005-11-08 11:57:25 debug Exp $
 *  
 *  Generic PCI bus framework. It is not a normal "device", but is used by
 *  individual PCI controllers and devices.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "device.h"
#include "devices.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"

#include "bus_pci.h"


/*  #define debug fatal  */


/*
 *  bus_pci_data_access():
 *
 *  Reads from and writes to the PCI configuration registers of a device.
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
		return;
	}

	/*  Get the bus, device, and function numbers from the address:  */
	bus        = (pci_data->pci_addr >> 16) & 0xff;
	device     = (pci_data->pci_addr >> 11) & 0x1f;
	function   = (pci_data->pci_addr >> 8)  & 0x7;
	registernr = (pci_data->pci_addr)       & 0xff;

	/*  Scan through the list of pci_device entries.  */
	dev = pci_data->first_device;
	while (dev != NULL && found == NULL) {
		if (dev->bus == bus && dev->function == function &&
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
	    registernr >= PCI_MAPREG_START &&
	    registernr <= PCI_MAPREG_END - 4) {
		/*
		 *  TODO:  real length!!!
		 */
fatal("READING LENGTH!\n");
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

	debug("[ bus_pci: read from PCI DATA, addr = 0x%08lx (bus %i, device "
	    "%i, function %i, register 0x%02x): 0x%08lx ]\n",
	    (long)pci_data->pci_addr, bus, device, function, registernr,
	    (long)*data);
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



/******************************************************************************
 *
 *  The following is glue code for PCI controllers and devices. The glue
 *  does the minimal stuff necessary to get an emulated OS to detect the
 *  device (i.e. set up PCI configuration registers), and then if necessary
 *  add a "normal" device.
 *
 ******************************************************************************/



/*
 *  Integraphics Systems "igsfb" Framebuffer (graphics) card.
 *
 *  TODO
 */

#define	PCI_VENDOR_INTEGRAPHICS		0x10ea

PCIINIT(igsfb)
{
	PCI_SET_DATA(PCI_ID_REG,
	    PCI_ID_CODE(PCI_VENDOR_INTEGRAPHICS, 0x2010));

	PCI_SET_DATA(PCI_CLASS_REG,
	    PCI_CLASS_CODE(PCI_CLASS_DISPLAY,
	    PCI_SUBCLASS_DISPLAY_VGA, 0) + 0x01);

	/*  TODO  */
	PCI_SET_DATA(0x10, 0xb0000000);

	/*  TODO: This is just a dummy so far.  */
}



/*
 *  S3 ViRGE graphics.
 *
 *  TODO: Only emulates a standard VGA card, so far.
 */

#define PCI_VENDOR_S3			0x5333
#define PCI_PRODUCT_S3_VIRGE		0x5631
#define	PCI_PRODUCT_S3_VIRGE_DX		0x8a01

PCIINIT(s3_virge)
{
	PCI_SET_DATA(PCI_ID_REG,
	    PCI_ID_CODE(PCI_VENDOR_S3, PCI_PRODUCT_S3_VIRGE_DX));

	PCI_SET_DATA(PCI_CLASS_REG,
	    PCI_CLASS_CODE(PCI_CLASS_DISPLAY,
	    PCI_SUBCLASS_DISPLAY_VGA, 0) + 0x01);

	if (machine->machine_type != MACHINE_CATS) {
		fatal("TODO: s3 virge in non-CATS\n");
		exit(1);
	}

	/*  TODO: CATS specific:  */
	dev_vga_init(machine, mem, 0x800a0000ULL, 0x7c0003c0,
	    machine->machine_name);
}



/*
 *  Acer Labs M5229 PCI-IDE (UDMA) controller.
 *  Acer Labs M1543 PCI->ISA bridge.
 */

#define PCI_VENDOR_ALI			0x10b9
#define PCI_PRODUCT_ALI_M1543		0x1533	/*  NOTE: not 1543  */
#define	PCI_PRODUCT_ALI_M5229		0x5229

PCIINIT(ali_m1543)
{
	PCI_SET_DATA(PCI_ID_REG,
	    PCI_ID_CODE(PCI_VENDOR_ALI, PCI_PRODUCT_ALI_M1543));

	PCI_SET_DATA(PCI_CLASS_REG,
	    PCI_CLASS_CODE(PCI_CLASS_BRIDGE,
	    PCI_SUBCLASS_BRIDGE_ISA, 0) + 0xc3);

	PCI_SET_DATA(PCI_BHLC_REG,
	    PCI_BHLC_CODE(0,0, 1 /* multi-function */, 0,0));
}

PCIINIT(ali_m5229)
{
	PCI_SET_DATA(PCI_ID_REG,
	    PCI_ID_CODE(PCI_VENDOR_ALI, PCI_PRODUCT_ALI_M5229));

	PCI_SET_DATA(PCI_CLASS_REG,
	    PCI_CLASS_CODE(PCI_CLASS_MASS_STORAGE,
	    PCI_SUBCLASS_MASS_STORAGE_IDE, 0x60) + 0xc1);

#if 0
/*  TODO:  */
case 0x10:
	return 0x000001f1;
case 0x14:
	return 0x000003f7;
case 0x18:
	return 0x00000171;
case 0x1c:
	return 0x00000377;
case 60:
	return 0x8e;	/*  ISA int 14  */
case 61:
	return 0x04;
#endif


	/*
	 *  TODO: The check for machine type shouldn't be here?
	 */

	switch (machine->machine_type) {

	case MACHINE_CATS:
		device_add(machine, "wdc addr=0x7c0001f0 irq=46");/* primary  */
		/*  The secondary channel is disabled. TODO: fix this.  */
		/*  device_add(machine, "wdc addr=0x7c000170 irq=47");  */
		break;

	default:fatal("ali_m5229: unimplemented machine type\n");
		exit(1);
	}
}



/*
 *  Adaptec AHC SCSI controller.
 */

#define PCI_VENDOR_ADP  0x9004          /* Adaptec */
#define PCI_VENDOR_ADP2 0x9005          /* Adaptec (2nd PCI Vendor ID) */
#define PCI_PRODUCT_ADP_2940U   0x8178          /* AHA-2940 Ultra */
#define PCI_PRODUCT_ADP_2940UP  0x8778          /* AHA-2940 Ultra Pro */

PCIINIT(ahc)
{
	/*  Numbers taken from a Adaptec 2940U:  */
	/*  http://mail-index.netbsd.org/netbsd-bugs/2000/04/29/0000.html  */

	PCI_SET_DATA(PCI_ID_REG, PCI_ID_CODE(PCI_VENDOR_ADP,
	    PCI_PRODUCT_ADP_2940U));

	PCI_SET_DATA(PCI_COMMAND_STATUS_REG, 0x02900007);

	PCI_SET_DATA(PCI_CLASS_REG,
	    PCI_CLASS_CODE(PCI_CLASS_MASS_STORAGE,
	    PCI_SUBCLASS_MASS_STORAGE_SCSI, 0) + 0x01);

	PCI_SET_DATA(PCI_BHLC_REG, 0x00004008);

	/*  1 = type i/o. 0x0000e801;  address?  */
	/*  second address reg = 0xf1002000?  */
	PCI_SET_DATA(PCI_MAPREG_START + 0x00, 0x00000001);
	PCI_SET_DATA(PCI_MAPREG_START + 0x04, 0x00000000);

	PCI_SET_DATA(PCI_MAPREG_START + 0x08, 0x00000000);
	PCI_SET_DATA(PCI_MAPREG_START + 0x0c, 0x00000000);
	PCI_SET_DATA(PCI_MAPREG_START + 0x10, 0x00000000);
	PCI_SET_DATA(PCI_MAPREG_START + 0x14, 0x00000000);
	PCI_SET_DATA(PCI_MAPREG_START + 0x18, 0x00000000);

	/*  Subsystem vendor ID? 0x78819004?  */
	PCI_SET_DATA(PCI_MAPREG_START + 0x1c, 0x00000000);

	PCI_SET_DATA(0x30, 0xef000000);
	PCI_SET_DATA(PCI_CAPLISTPTR_REG, 0x000000dc);
	PCI_SET_DATA(0x38, 0x00000000);
	PCI_SET_DATA(PCI_INTERRUPT_REG, 0x08080109);	/*  interrupt pin A  */

	/*  TODO:  this address is based on what NetBSD/sgimips uses
	    on SGI IP32 (O2). Fix this.  */

	device_add(machine, "ahc addr=0x18000000");

	/*  OpenBSD/sgi snapshots sometime between 2005-03-11 and
	    2005-04-04 changed to using 0x1a000000:  */
	dev_ram_init(machine, 0x1a000000, 0x2000000, DEV_RAM_MIRROR,
	    0x18000000);
}


