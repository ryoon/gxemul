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
 *  $Id: bus_pci.c,v 1.17 2005-11-09 07:41:04 debug Exp $
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
	unsigned char *cfg_base;

	if (writeflag == MEM_WRITE) {
		debug("[ bus_pci: write to PCI DATA: data = 0x%016llx ]\n",
		    (long long)*data);
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

	if (pci_data->last_was_write_ffffffff &&
	    registernr >= PCI_MAPREG_START && registernr <= PCI_MAPREG_END - 4)
		cfg_base = found->cfg_mem_size;
	else
		cfg_base = found->cfg_mem;

	/*  Read data as little-endian:  */
	*data = 0;
	if (registernr + len - 1 < PCI_CFG_MEM_SIZE) {
		*data = cfg_base[registernr];
		if (len > 1)
			*data |= (cfg_base[registernr+1] << 8);
		if (len > 2)
			*data |= (cfg_base[registernr+2] << 16);
		if (len > 3)
			*data |= (cfg_base[registernr+3] << 24);
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
			if (pci_data->pci_addr & 1)
				fatal("[ bus_pci: WARNING! pci type 0 not"
				    " yet implemented! ]\n");
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
	int ofs;
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

	/*
	 *  Initialize with some default values:
	 *
	 *  TODO:  The command status register is best to set up per device,
	 *         just enabling all bits like this is not really good.
	 *         The size registers should also be set up on a per-device
	 *         basis.
	 */
	PCI_SET_DATA(PCI_COMMAND_STATUS_REG, 0xffffffffULL);
	for (ofs = PCI_MAPREG_START; ofs < PCI_MAPREG_END; ofs += 4)
		PCI_SET_DATA_SIZE(ofs, 0x00400000 - 1);

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



/*
 *  Galileo Technology GT-64xxx PCI controller.
 *
 *	GT-64011	Used in Cobalt machines.
 *	GT-64120	Used in evbmips machines (Malta).
 *
 *  NOTE: This works in the opposite way compared to other devices; the PCI
 *  device is added from the normal device instead of the other way around.
 */

#define PCI_VENDOR_GALILEO           0x11ab    /* Galileo Technology */
#define PCI_PRODUCT_GALILEO_GT64011  0x4146    /* GT-64011 System Controller */
#define	PCI_PRODUCT_GALILEO_GT64120  0x4620    /* GT-64120 */

PCIINIT(gt64011)
{
	PCI_SET_DATA(PCI_ID_REG, PCI_ID_CODE(PCI_VENDOR_GALILEO,
	    PCI_PRODUCT_GALILEO_GT64011));

	PCI_SET_DATA(PCI_CLASS_REG,
	    PCI_CLASS_CODE(PCI_CLASS_BRIDGE,
	    PCI_SUBCLASS_BRIDGE_HOST, 0) + 0x01);	/*  Revision 1  */
}

PCIINIT(gt64120)
{
	PCI_SET_DATA(PCI_ID_REG, PCI_ID_CODE(PCI_VENDOR_GALILEO,
	    PCI_PRODUCT_GALILEO_GT64120));

	PCI_SET_DATA(PCI_CLASS_REG,
	    PCI_CLASS_CODE(PCI_CLASS_BRIDGE,
	    PCI_SUBCLASS_BRIDGE_HOST, 0) + 0x02);	/*  Revision 2?  */
}



/*
 *  Intel 82371AB PIIX4 PCI-ISA bridge and IDE controller
 */

#define	PCI_VENDOR_INTEL		0x8086
#define	PCI_PRODUCT_INTEL_82371AB_ISA	0x7110
#define	PCI_PRODUCT_INTEL_82371AB_IDE	0x7111

PCIINIT(i82371ab_isa)
{
	PCI_SET_DATA(PCI_ID_REG, PCI_ID_CODE(PCI_VENDOR_INTEL,
	    PCI_PRODUCT_INTEL_82371AB_ISA));

	PCI_SET_DATA(PCI_CLASS_REG, PCI_CLASS_CODE(PCI_CLASS_BRIDGE,
	    PCI_SUBCLASS_BRIDGE_ISA, 0) + 0x01);	/*  Rev 1  */

	PCI_SET_DATA(PCI_BHLC_REG,
	    PCI_BHLC_CODE(0,0, 1 /* multi-function */, 0,0));
}

PCIINIT(i82371ab_ide)
{
	PCI_SET_DATA(PCI_ID_REG, PCI_ID_CODE(PCI_VENDOR_INTEL,
	    PCI_PRODUCT_INTEL_82371AB_IDE));

	/*  Possibly not correct:  */
	PCI_SET_DATA(PCI_CLASS_REG,
	    PCI_CLASS_CODE(PCI_CLASS_MASS_STORAGE,
	    PCI_SUBCLASS_MASS_STORAGE_IDE, 0x00) + 0x01);

	/*  PIIX_IDETIM (see NetBSD's pciide_piix_reg.h)  */
	/*  channel 0 and 1 enabled as IDE  */
	PCI_SET_DATA(0x40, 0x80008000);

	/*
	 *  TODO: The check for machine type shouldn't be here?
	 */

	switch (machine->machine_type) {

	case MACHINE_EVBMIPS:
		/*  TODO: Irqs...  */
		device_add(machine, "wdc addr=0x180001f0 irq=22");/* primary  */
		device_add(machine, "wdc addr=0x18000170 irq=23");/* secondary*/
		break;

	default:fatal("i82371ab_ide: unimplemented machine type\n");
		exit(1);
	}
}



/*
 *  VIATECH VT82C586 devices:
 *
 *	vt82c586_isa	PCI->ISA bridge
 *	vt82c586_ide	IDE controller
 *
 *  TODO:  This more or less just a dummy device, so far.
 */

#define PCI_VENDOR_VIATECH                0x1106  /* VIA Technologies */
#define PCI_PRODUCT_VIATECH_VT82C586_IDE  0x1571  /* VT82C586 (Apollo VP)
					             IDE Controller */
#define PCI_PRODUCT_VIATECH_VT82C586_ISA  0x0586  /* VT82C586 (Apollo VP)
						     PCI-ISA Bridge */

PCIINIT(vt82c586_isa)
{
	PCI_SET_DATA(PCI_ID_REG, PCI_ID_CODE(PCI_VENDOR_VIATECH,
	    PCI_PRODUCT_VIATECH_VT82C586_ISA));

	PCI_SET_DATA(PCI_CLASS_REG, PCI_CLASS_CODE(PCI_CLASS_BRIDGE,
	    PCI_SUBCLASS_BRIDGE_ISA, 0) + 0x39);   /*  Revision 37 or 39  */

	PCI_SET_DATA(PCI_BHLC_REG,
	    PCI_BHLC_CODE(0,0, 1 /* multi-function */, 0,0));
}

PCIINIT(vt82c586_ide)
{
	PCI_SET_DATA(PCI_ID_REG, PCI_ID_CODE(PCI_VENDOR_VIATECH,
	    PCI_PRODUCT_VIATECH_VT82C586_IDE));

	/*  Possibly not correct:  */
	PCI_SET_DATA(PCI_CLASS_REG,
	    PCI_CLASS_CODE(PCI_CLASS_MASS_STORAGE,
	    PCI_SUBCLASS_MASS_STORAGE_IDE, 0x00) + 0x01);

	/*  APO_IDECONF  */
	/*  channel 0 and 1 enabled  */
	PCI_SET_DATA(0x40, 0x00000003);

	/*
	 *  TODO: The check for machine type shouldn't be here?
	 */

	switch (machine->machine_type) {

	case MACHINE_COBALT:
		/*  irq 14,15 (+8)  */
		device_add(machine, "wdc addr=0x100001f0 irq=22");/* primary  */
		device_add(machine, "wdc addr=0x10000170 irq=23");/* secondary*/
		break;

	case MACHINE_EVBMIPS:
		/*  TODO: Irqs...  */
		device_add(machine, "wdc addr=0x180001f0 irq=22");/* primary  */
		device_add(machine, "wdc addr=0x18000170 irq=23");/* secondary*/
		break;

	default:fatal("vt82c586_ide: unimplemented machine type\n");
		exit(1);
	}
}



/*
 *  Symphony Labs 83C553 PCI->ISA bridge.
 *  Symphony Labs 82C105 PCIIDE controller.
 */

#define PCI_VENDOR_SYMPHONY		0x10ad
#define PCI_PRODUCT_SYMPHONY_83C553	0x0565
#define	PCI_PRODUCT_SYMPHONY_82C105	0x0105

PCIINIT(symphony_83c553)
{
	PCI_SET_DATA(PCI_ID_REG, PCI_ID_CODE(PCI_VENDOR_SYMPHONY,
	    PCI_PRODUCT_SYMPHONY_83C553));

	PCI_SET_DATA(PCI_CLASS_REG, PCI_CLASS_CODE(PCI_CLASS_BRIDGE,
	    PCI_SUBCLASS_BRIDGE_ISA, 0) + 0x10);

	PCI_SET_DATA(PCI_BHLC_REG,
	    PCI_BHLC_CODE(0,0, 1 /* multi-function */, 0,0));
}

PCIINIT(symphony_82c105)
{
	PCI_SET_DATA(PCI_ID_REG, PCI_ID_CODE(PCI_VENDOR_SYMPHONY,
	    PCI_PRODUCT_SYMPHONY_82C105));

	/*  Possibly not correct:  */
	PCI_SET_DATA(PCI_CLASS_REG,
	    PCI_CLASS_CODE(PCI_CLASS_MASS_STORAGE,
	    PCI_SUBCLASS_MASS_STORAGE_IDE, 0x00) + 0x05);

	/*  APO_IDECONF  */
	/*  channel 0 and 1 enabled  */
	PCI_SET_DATA(0x40, 0x00000003);

	/*
	 *  TODO: The check for machine type shouldn't be here?
	 */

	switch (machine->machine_type) {

	case MACHINE_NETWINDER:
		device_add(machine, "wdc addr=0x7c0001f0 irq=46");/* primary  */
		device_add(machine, "wdc addr=0x7c000170 irq=47");/* secondary*/
		break;

	default:fatal("symphony_82c105: unimplemented machine "
		    "type %i\n", machine->machine_type);
		exit(1);
	}
}



/*
 *  DEC 21143 ("Tulip") PCI ethernet.
 */

#define PCI_VENDOR_DEC          0x1011 /* Digital Equipment */
#define PCI_PRODUCT_DEC_21142   0x0019 /* DECchip 21142/21143 10/100 Ethernet */

PCIINIT(dec21143)
{
	uint64_t base = 0;
	int irq = 0;
	char tmpstr[200];

	PCI_SET_DATA(PCI_ID_REG, PCI_ID_CODE(PCI_VENDOR_DEC,
	    PCI_PRODUCT_DEC_21142));

	PCI_SET_DATA(PCI_CLASS_REG, PCI_CLASS_CODE(PCI_CLASS_NETWORK,
	    PCI_SUBCLASS_NETWORK_ETHERNET, 0x00) + 0x41);

	/*
	 *  Experimental:
	 */

	switch (machine->machine_type) {
	case MACHINE_CATS:
		base = 0x00200000;
		/*  Works with at least NetBSD and OpenBSD:  */
		PCI_SET_DATA(PCI_INTERRUPT_REG, 0x08080101);
		irq = 18;
		break;
	case MACHINE_COBALT:
		base = 0x9ca00000;
		PCI_SET_DATA(PCI_INTERRUPT_REG, 0x00000100);
		/*  TODO: IRQ  */
		break;
	default:fatal("dec21143 in non-implemented machine type %i\n",
		    machine->machine_type);
		exit(1);
	}

	PCI_SET_DATA(PCI_MAPREG_START,        base + 1);
	PCI_SET_DATA(PCI_MAPREG_START + 0x04, base + 0x10000);

	PCI_SET_DATA_SIZE(PCI_MAPREG_START,        0x100 - 1);
	PCI_SET_DATA_SIZE(PCI_MAPREG_START + 0x04, 0x100 - 1);

	snprintf(tmpstr, sizeof(tmpstr), "dec21143 addr=0x%llx irq=%i",
	    (long long)(base + 0x80000000ULL), irq);
	device_add(machine, tmpstr);
}



/*
 *  DEC 21030 "tga" graphics.
 */

#define PCI_PRODUCT_DEC_21030  0x0004          /*  DECchip 21030 ("TGA")  */

PCIINIT(dec21030)
{
	uint64_t base = 0;
	char tmpstr[200];

	PCI_SET_DATA(PCI_ID_REG, PCI_ID_CODE(PCI_VENDOR_DEC,
	    PCI_PRODUCT_DEC_21030));

	PCI_SET_DATA(PCI_COMMAND_STATUS_REG, 0x02800087);  /*  TODO  */

	PCI_SET_DATA(PCI_CLASS_REG, PCI_CLASS_CODE(PCI_CLASS_DISPLAY,
	    PCI_SUBCLASS_DISPLAY_VGA, 0x00) + 0x03);

	/*
	 *  See http://mail-index.netbsd.org/port-arc/2001/08/13/0000.html
	 *  for more info.
	 */

	PCI_SET_DATA(PCI_BHLC_REG, 0x0000ff00);

	/*  8 = prefetchable  */
	PCI_SET_DATA(0x10, 0x00000008);
	PCI_SET_DATA(0x30, 0x08000001);
	PCI_SET_DATA(PCI_INTERRUPT_REG, 0x00000100);	/*  interrupt pin A?  */

	/*
	 *  Experimental:
	 */

	switch (machine->machine_type) {
	case MACHINE_ARC:
		base = 0x100000000ULL;
		break;
	default:fatal("dec21030 in non-implemented machine type %i\n",
		    machine->machine_type);
		exit(1);
	}

	snprintf(tmpstr, sizeof(tmpstr), "dec21030 addr=0x%llx",
	    (long long)(base));
	device_add(machine, tmpstr);
}

