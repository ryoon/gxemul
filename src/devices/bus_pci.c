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
 *  $Id: bus_pci.c,v 1.45 2005-11-28 07:00:34 debug Exp $
 *  
 *  Generic PCI bus framework. This is not a normal "device", but is used by
 *  individual PCI controllers and devices.
 *
 *  See NetBSD's pcidevs.h for more PCI vendor and device identifiers.
 *
 *  TODO:
 *
 *	x) Allow guest OSes to do runtime address fixups (i.e. actually
 *	   move a device from one address to another).
 *
 *	x) Generalize the PCI and legacy ISA interrupt routing stuff.
 *
 *	x) Make sure that pci_little_endian is used correctly everywhere.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUS_PCI_C

#include "bus_pci.h"
#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "diskimage.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"

extern int verbose;


/*  #define debug fatal  */


static void reverse(uint64_t *p, int len)
{
	uint64_t x = *p, y = 0;
	int i;
	for (i=0; i<len; i++) {
		y <<= 8;
		y |= ((x >> (8*i)) & 0xff);
	}
	*p = y;
}


/*
 *  bus_pci_data_access():
 *
 *  Reads from and writes to the PCI configuration registers of a device.
 */
void bus_pci_data_access(struct cpu *cpu, struct memory *mem,
	uint64_t *data, int len, int writeflag, struct pci_data *pci_data)
{
	struct pci_device *dev;
	int bus, device, function, registernr;
	unsigned char *cfg_base;
	uint64_t x, idata = *data;
	int already_native_byteorder = 0;

	if (len & PCI_ALREADY_NATIVE_BYTEORDER) {
		len &= ~PCI_ALREADY_NATIVE_BYTEORDER;
		already_native_byteorder = 1;
	}

	if (cpu->byte_order == EMUL_BIG_ENDIAN &&
	    !already_native_byteorder)
		reverse(&idata, len);

	/*  Get the bus, device, and function numbers from the address:  */
	bus        = (pci_data->pci_addr >> 16) & 0xff;
	device     = (pci_data->pci_addr >> 11) & 0x1f;
	function   = (pci_data->pci_addr >> 8)  & 0x7;
	registernr = (pci_data->pci_addr)       & 0xff;

	/*  Scan through the list of pci_device entries.  */
	dev = pci_data->first_device;
	while (dev != NULL) {
		if (dev->bus == bus && dev->function == function &&
		    dev->device == device)
			break;
		dev = dev->next;
	}

	/*  No device? Then return emptiness.  */
	if (dev == NULL) {
		if ((pci_data->pci_addr & 0xff) == 0)
			*data = 0xffffffff;
		else
			*data = 0;
		return;
	}

	/*  Return normal config data, or length data?  */
	if (pci_data->last_was_write_ffffffff &&
	    registernr >= PCI_MAPREG_START && registernr <= PCI_MAPREG_END - 4)
		cfg_base = dev->cfg_mem_size;
	else
		cfg_base = dev->cfg_mem;

	/*  Read data as little-endian:  */
	x = 0;
	if (registernr + len - 1 < PCI_CFG_MEM_SIZE) {
		x = cfg_base[registernr];
		if (len > 1)
			x |= (cfg_base[registernr+1] << 8);
		if (len > 2)
			x |= (cfg_base[registernr+2] << 16);
		if (len > 3)
			x |= ((uint64_t)cfg_base[registernr+3] << 24);
		if (len > 4)
			fatal("TODO: more than 32-bit PCI access?\n");
	}

	/*  Register write:  */
	if (writeflag == MEM_WRITE) {
		debug("[ bus_pci: write to PCI DATA: data = 0x%016llx ]\n",
		    (long long)idata);
		if (idata == 0xffffffffULL && registernr >= PCI_MAPREG_START
		    && registernr <= PCI_MAPREG_END - 4) {
			pci_data->last_was_write_ffffffff = 1;
			return;
		}
		/*  Writes are not really supported yet:  */
		if (idata != x) {
			debug("[ bus_pci: write to PCI DATA: data = 0x%08llx"
			    " differs from current value 0x%08llx; NOT YET"
			    " SUPPORTED. bus %i, device %i, function %i (%s)"
			    " register 0x%02x ]\n", (long long)idata,
			    (long long)x, bus, device, function, dev->name,
			    registernr);
		}
		return;
	}

	/*  Register read:  */
	if (cpu->byte_order == EMUL_BIG_ENDIAN &&
	    !already_native_byteorder)
		reverse(&x, len);
	*data = x;

	pci_data->last_was_write_ffffffff = 0;

	debug("[ bus_pci: read from PCI DATA, addr = 0x%08lx (bus %i, device "
	    "%i, function %i (%s) register 0x%02x): 0x%08lx ]\n", (long)
	    pci_data->pci_addr, bus, device, function, dev->name,
	    registernr, (long)*data);
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
	int already_native_byteorder = 0;

	if (writeflag == MEM_READ)
		*data = 0;

	if (len & PCI_ALREADY_NATIVE_BYTEORDER)
		already_native_byteorder = 1;

	switch (relative_addr) {

	case BUS_PCI_ADDR:
		if (writeflag == MEM_WRITE) {
			debug("[ bus_pci: write to PCI ADDR: data = 0x%016llx"
			    " ]\n", (long long)*data);
			pci_data->pci_addr = *data;

			/*
			 *  Big-endian systems (e.g. PowerPC) seem to access
			 *  PCI config data using little-endian accesses.
			 */
			if (cpu->byte_order == EMUL_BIG_ENDIAN &&
			    !already_native_byteorder) {
				uint32_t t = pci_data->pci_addr;
				pci_data->pci_addr = ((t >> 24) & 255)
				    | ((t >> 8) & 0xff00)
				    | ((t << 8) & 0xff0000)
				    | ((t << 24) & 0xff000000);
			}

			/*  Linux seems to use type 0 even when it does
			    type 1 detection. Hm. This is commented for now.  */
			/*  if (pci_data->pci_addr & 1)
				fatal("[ bus_pci: WARNING! pci type 0 not"
				    " yet implemented! ]\n");  */
		} else {
			debug("[ bus_pci: read from PCI ADDR (data = "
			    "0x%016llx) ]\n", (long long)pci_data->pci_addr);
			*data = pci_data->pci_addr;
		}
		break;

	case BUS_PCI_DATA:
		bus_pci_data_access(cpu, mem, data, len, writeflag, pci_data);
		break;

	default:if (writeflag == MEM_READ) {
			debug("[ bus_pci: read from unimplemented addr "
			    "0x%x ]\n", (int)relative_addr);
			*data = 0;
		} else {
			debug("[ bus_pci: write to unimplemented addr "
			    "0x%x: 0x%llx ]\n", (int)relative_addr,
			    (long long)*data);
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

	pd->pcibus   = pci_data;
	pd->name     = strdup(name);
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
	PCI_SET_DATA(PCI_COMMAND_STATUS_REG, 0x00ffffffULL);
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
 *  allocate_device_space():
 *
 *  Used by glue code (see below) to allocate space for a PCI device.
 *
 *  The returned values in portp and memp are the actual (emulated) addresses
 *  that the device should use. (Normally only one of these is actually used.)
 *
 *  TODO: PCI irqs?
 */
static void allocate_device_space(struct pci_device *pd,
	uint64_t portsize, uint64_t memsize,
	uint64_t *portp, uint64_t *memp)
{
	uint64_t port, mem;

	/*  Calculate an aligned starting port:  */
	port = pd->pcibus->cur_pci_portbase;
	if (portsize != 0) {
		port = ((port - 1) | (portsize - 1)) + 1;
		pd->pcibus->cur_pci_portbase = port;
		PCI_SET_DATA(PCI_MAPREG_START, port | PCI_MAPREG_TYPE_IO);
		PCI_SET_DATA_SIZE(PCI_MAPREG_START, portsize - 1);
	}

	/*  Calculate an aligned starting memory location:  */
	mem = pd->pcibus->cur_pci_membase;
	if (memsize != 0) {
		mem = ((mem - 1) | (memsize - 1)) + 1;
		pd->pcibus->cur_pci_membase = mem;
		PCI_SET_DATA(PCI_MAPREG_START + 0x04, mem);
		PCI_SET_DATA_SIZE(PCI_MAPREG_START + 0x04, memsize - 1);
	}

	*portp = port + pd->pcibus->pci_actual_io_offset;
	*memp  = mem  + pd->pcibus->pci_actual_mem_offset;

	if (verbose >= 2) {
		debug("pci device '%s' at", pd->name);
		if (portsize != 0)
			debug(" port 0x%llx-0x%llx", (long long)pd->pcibus->
			    cur_pci_portbase, (long long)(pd->pcibus->
			    cur_pci_portbase + portsize - 1));
		if (memsize != 0)
			debug(" mem 0x%llx-0x%llx", (long long)pd->pcibus->
			    cur_pci_membase, (long long)(pd->pcibus->
			    cur_pci_membase + memsize - 1));
		debug("\n");
	}

	pd->pcibus->cur_pci_portbase += portsize;
	pd->pcibus->cur_pci_membase += memsize;
}


/*
 *  bus_pci_init():
 *
 *  This doesn't register a device, but instead returns a pointer to a struct
 *  which should be passed to bus_pci_access() when accessing the PCI bus.
 *
 *  irq_nr is the (optional) IRQ nr that this PCI bus interrupts at.
 *
 *  pci_portbase, pci_membase, and pci_irqbase are the port, memory, and
 *  interrupt bases for PCI devices (as found in the configuration registers).
 *
 *  pci_actual_io_offset and pci_actual_mem_offset are the offset from
 *  the values in the configuration registers to the actual (emulated) device.
 *
 *  isa_portbase, isa_membase, and isa_irqbase are the port, memory, and
 *  interrupt bases for legacy ISA devices.
 */
struct pci_data *bus_pci_init(int irq_nr,
	uint64_t pci_actual_io_offset, uint64_t pci_actual_mem_offset,
	uint64_t pci_portbase, uint64_t pci_membase, int pci_irqbase,
	uint64_t isa_portbase, uint64_t isa_membase, int isa_irqbase)
{
	struct pci_data *d;

	d = malloc(sizeof(struct pci_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct pci_data));
	d->irq_nr                = irq_nr;
	d->pci_actual_io_offset  = pci_actual_io_offset;
	d->pci_actual_mem_offset = pci_actual_mem_offset;
	d->pci_portbase          = pci_portbase;
	d->pci_membase           = pci_membase;
	d->pci_irqbase           = pci_irqbase;
	d->isa_portbase          = isa_portbase;
	d->isa_membase           = isa_membase;
	d->isa_irqbase           = isa_irqbase;

	/*  Assume that the first 64KB could be used by legacy ISA devices:  */
	d->cur_pci_portbase = d->pci_portbase + 0x10000;
	d->cur_pci_membase  = d->pci_membase + 0x10000;

	return d;
}



/******************************************************************************
 *
 *  The following is glue code for PCI controllers and devices. The glue code
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
	PCI_SET_DATA(0x10, 0x08000000);

	dev_vga_init(machine, mem, pd->pcibus->isa_membase + 0xa0000,
	    0x88800000 + 0x3c0, machine->machine_name);
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

	dev_vga_init(machine, mem, pd->pcibus->isa_membase + 0xa0000,
	    pd->pcibus->isa_portbase + 0x3c0, machine->machine_name);
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

	PCI_SET_DATA(PCI_CLASS_REG, PCI_CLASS_CODE(PCI_CLASS_BRIDGE,
	    PCI_SUBCLASS_BRIDGE_ISA, 0) + 0xc3);

	PCI_SET_DATA(PCI_BHLC_REG,
	    PCI_BHLC_CODE(0,0, 1 /* multi-function */, 0x40,0));

	/*  Linux uses these to detect which IRQ the IDE controller uses:  */
	PCI_SET_DATA(0x44, 0x0000000e);
	PCI_SET_DATA(0x58, 0x00000003);
}

PCIINIT(ali_m5229)
{
	char tmpstr[300];

	PCI_SET_DATA(PCI_ID_REG,
	    PCI_ID_CODE(PCI_VENDOR_ALI, PCI_PRODUCT_ALI_M5229));

	PCI_SET_DATA(PCI_CLASS_REG, PCI_CLASS_CODE(PCI_CLASS_MASS_STORAGE,
	    PCI_SUBCLASS_MASS_STORAGE_IDE, 0x60) + 0xc1);

	if (diskimage_exist(machine, 0, DISKIMAGE_IDE) ||
	    diskimage_exist(machine, 1, DISKIMAGE_IDE)) {
		snprintf(tmpstr, sizeof(tmpstr), "wdc addr=0x%llx irq=%i",
		    (long long)(pd->pcibus->isa_portbase + 0x1f0),
		    pd->pcibus->isa_irqbase + 14);
		device_add(machine, tmpstr);
	}

	/*  The secondary channel is disabled. TODO: fix this.  */
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

	PCI_SET_DATA(PCI_CLASS_REG, PCI_CLASS_CODE(PCI_CLASS_MASS_STORAGE,
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

	/*
	 *  TODO:  this address is based on what NetBSD/sgimips uses
	 *  on SGI IP32 (O2). Fix this!
	 */

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

	PCI_SET_DATA(PCI_CLASS_REG, PCI_CLASS_CODE(PCI_CLASS_BRIDGE,
	    PCI_SUBCLASS_BRIDGE_HOST, 0) + 0x02);	/*  Revision 2?  */

	switch (machine->machine_type) {
	case MACHINE_EVBMIPS:
		PCI_SET_DATA(PCI_MAPREG_START + 0x10, 0x1be00000);
		break;
	}
}



/*
 *  Intel 82371AB PIIX4 PCI-ISA bridge and IDE controller
 *  and 82378ZB System I/O controller.
 */

#define	PCI_VENDOR_INTEL		0x8086
#define	PCI_PRODUCT_INTEL_82371AB_ISA	0x7110
#define	PCI_PRODUCT_INTEL_82371AB_IDE	0x7111
#define	PCI_PRODUCT_INTEL_SIO		0x0484

PCIINIT(i82371ab_isa)
{
	PCI_SET_DATA(PCI_ID_REG, PCI_ID_CODE(PCI_VENDOR_INTEL,
	    PCI_PRODUCT_INTEL_82371AB_ISA));

	PCI_SET_DATA(PCI_CLASS_REG, PCI_CLASS_CODE(PCI_CLASS_BRIDGE,
	    PCI_SUBCLASS_BRIDGE_ISA, 0) + 0x01);	/*  Rev 1  */

	PCI_SET_DATA(PCI_BHLC_REG,
	    PCI_BHLC_CODE(0,0, 1 /* multi-function */, 0x40,0));
}

PCIINIT(i82378zb)
{
	PCI_SET_DATA(PCI_ID_REG, PCI_ID_CODE(PCI_VENDOR_INTEL,
	    PCI_PRODUCT_INTEL_SIO));

	PCI_SET_DATA(PCI_CLASS_REG, PCI_CLASS_CODE(PCI_CLASS_BRIDGE,
	    PCI_SUBCLASS_BRIDGE_ISA, 0) + 0x43);

	PCI_SET_DATA(PCI_BHLC_REG,
	    PCI_BHLC_CODE(0,0, 1 /* multi-function */, 0x40,0));
}

PCIINIT(i82371ab_ide)
{
	char tmpstr[100];

	PCI_SET_DATA(PCI_ID_REG, PCI_ID_CODE(PCI_VENDOR_INTEL,
	    PCI_PRODUCT_INTEL_82371AB_IDE));

	/*  Possibly not correct:  */
	PCI_SET_DATA(PCI_CLASS_REG, PCI_CLASS_CODE(PCI_CLASS_MASS_STORAGE,
	    PCI_SUBCLASS_MASS_STORAGE_IDE, 0x00) + 0x01);

	/*  PIIX_IDETIM (see NetBSD's pciide_piix_reg.h)  */
	/*  channel 0 and 1 enabled as IDE  */
	PCI_SET_DATA(0x40, 0x80008000);

	if (diskimage_exist(machine, 0, DISKIMAGE_IDE) ||
	    diskimage_exist(machine, 1, DISKIMAGE_IDE)) {
		snprintf(tmpstr, sizeof(tmpstr), "wdc addr=0x%llx irq=%i",
		    (long long)(pd->pcibus->isa_portbase + 0x1f0),
		    pd->pcibus->isa_irqbase + 14);
		device_add(machine, tmpstr);
	}

	if (diskimage_exist(machine, 2, DISKIMAGE_IDE) ||
	    diskimage_exist(machine, 3, DISKIMAGE_IDE)) {
		snprintf(tmpstr, sizeof(tmpstr), "wdc addr=0x%llx irq=%i",
		    (long long)(pd->pcibus->isa_portbase + 0x170),
		    pd->pcibus->isa_irqbase + 15);
		device_add(machine, tmpstr);
	}
}



/*
 *  IBM ISA bridge (used by at least one PReP machine).
 */

#define	PCI_VENDOR_IBM			0x1014
#define	PCI_PRODUCT_IBM_ISABRIDGE	0x000a

PCIINIT(ibm_isa)
{
	PCI_SET_DATA(PCI_ID_REG, PCI_ID_CODE(PCI_VENDOR_IBM,
	    PCI_PRODUCT_IBM_ISABRIDGE));

	PCI_SET_DATA(PCI_CLASS_REG, PCI_CLASS_CODE(PCI_CLASS_BRIDGE,
	    PCI_SUBCLASS_BRIDGE_ISA, 0) + 0x02);

	PCI_SET_DATA(PCI_BHLC_REG,
	    PCI_BHLC_CODE(0,0, 1 /* multi-function */, 0x40,0));
}



/*
 *  Heuricon PCI host bridge for PM/PPC.
 */

#define	PCI_VENDOR_HEURICON		0x1223
#define	PCI_PRODUCT_HEURICON_PMPPC	0x000e

PCIINIT(heuricon_pmppc)
{
	PCI_SET_DATA(PCI_ID_REG, PCI_ID_CODE(PCI_VENDOR_HEURICON,
	    PCI_PRODUCT_HEURICON_PMPPC));

	PCI_SET_DATA(PCI_CLASS_REG, PCI_CLASS_CODE(PCI_CLASS_BRIDGE,
	    PCI_SUBCLASS_BRIDGE_HOST, 0) + 0x00);   /*  Revision?  */

	PCI_SET_DATA(PCI_BHLC_REG,
	    PCI_BHLC_CODE(0,0, 1 /* multi-function */, 0x40,0));
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
	    PCI_BHLC_CODE(0,0, 1 /* multi-function */, 0x40,0));
}

PCIINIT(vt82c586_ide)
{
	char tmpstr[100];

	PCI_SET_DATA(PCI_ID_REG, PCI_ID_CODE(PCI_VENDOR_VIATECH,
	    PCI_PRODUCT_VIATECH_VT82C586_IDE));

	/*  Possibly not correct:  */
	PCI_SET_DATA(PCI_CLASS_REG, PCI_CLASS_CODE(PCI_CLASS_MASS_STORAGE,
	    PCI_SUBCLASS_MASS_STORAGE_IDE, 0x00) + 0x01);

	/*  APO_IDECONF  */
	/*  channel 0 and 1 enabled  */
	PCI_SET_DATA(0x40, 0x00000003);

	if (diskimage_exist(machine, 0, DISKIMAGE_IDE) ||
	    diskimage_exist(machine, 1, DISKIMAGE_IDE)) {
		snprintf(tmpstr, sizeof(tmpstr), "wdc addr=0x%llx irq=%i",
		    (long long)(pd->pcibus->isa_portbase + 0x1f0),
		    pd->pcibus->isa_irqbase + 14);
		device_add(machine, tmpstr);
	}

	if (diskimage_exist(machine, 2, DISKIMAGE_IDE) ||
	    diskimage_exist(machine, 3, DISKIMAGE_IDE)) {
		snprintf(tmpstr, sizeof(tmpstr), "wdc addr=0x%llx irq=%i",
		    (long long)(pd->pcibus->isa_portbase + 0x170),
		    pd->pcibus->isa_irqbase + 15);
		device_add(machine, tmpstr);
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
	    PCI_BHLC_CODE(0,0, 1 /* multi-function */, 0x40,0));
}

PCIINIT(symphony_82c105)
{
	char tmpstr[100];

	PCI_SET_DATA(PCI_ID_REG, PCI_ID_CODE(PCI_VENDOR_SYMPHONY,
	    PCI_PRODUCT_SYMPHONY_82C105));

	/*  Possibly not correct:  */
	PCI_SET_DATA(PCI_CLASS_REG, PCI_CLASS_CODE(PCI_CLASS_MASS_STORAGE,
	    PCI_SUBCLASS_MASS_STORAGE_IDE, 0x00) + 0x05);

	/*  APO_IDECONF  */
	/*  channel 0 and 1 enabled  */
	PCI_SET_DATA(0x40, 0x00000003);

	if (diskimage_exist(machine, 0, DISKIMAGE_IDE) ||
	    diskimage_exist(machine, 1, DISKIMAGE_IDE)) {
		snprintf(tmpstr, sizeof(tmpstr), "wdc addr=0x%llx irq=%i",
		    (long long)(pd->pcibus->isa_portbase + 0x1f0),
		    pd->pcibus->isa_irqbase + 14);
		device_add(machine, tmpstr);
	}

	if (diskimage_exist(machine, 2, DISKIMAGE_IDE) ||
	    diskimage_exist(machine, 3, DISKIMAGE_IDE)) {
		snprintf(tmpstr, sizeof(tmpstr), "wdc addr=0x%llx irq=%i",
		    (long long)(pd->pcibus->isa_portbase + 0x170),
		    pd->pcibus->isa_irqbase + 15);
		device_add(machine, tmpstr);
	}
}



/*
 *  DEC 21143 ("Tulip") PCI ethernet.
 */

#define PCI_VENDOR_DEC          0x1011 /* Digital Equipment */
#define PCI_PRODUCT_DEC_21142   0x0019 /* DECchip 21142/21143 10/100 Ethernet */

PCIINIT(dec21143)
{
	uint64_t port, memaddr;
	int irq = 0;		/*  TODO  */
	int pci_int_line = 0x101;
	char tmpstr[200];

	PCI_SET_DATA(PCI_ID_REG, PCI_ID_CODE(PCI_VENDOR_DEC,
	    PCI_PRODUCT_DEC_21142));

	PCI_SET_DATA(PCI_COMMAND_STATUS_REG, 0x02000017);

	PCI_SET_DATA(PCI_CLASS_REG, PCI_CLASS_CODE(PCI_CLASS_NETWORK,
	    PCI_SUBCLASS_NETWORK_ETHERNET, 0x00) + 0x41);

	PCI_SET_DATA(PCI_BHLC_REG, PCI_BHLC_CODE(0,0,0, 0x40,0));

	switch (machine->machine_type) {
	case MACHINE_CATS:
		/*  CATS int 18 = PCI.  */
		irq = 18;
		pci_int_line = 0x101;
		break;
	case MACHINE_COBALT:
		/*  On Cobalt, IRQ 7 = PCI.  */
		irq = 8 + 7;
		pci_int_line = 0x407;
		break;
	case MACHINE_PREP:
		irq = 32 + 10;
		pci_int_line = 0x20a;
		break;
	case MACHINE_PMPPC:
		/*  TODO, not working yet  */
		irq = 31 - 21;
		pci_int_line = 0x201;
		break;
	case MACHINE_MACPPC:
		/*  TODO, not working yet  */
		irq = 25;
		pci_int_line = 0x101;
		break;
	}

	PCI_SET_DATA(PCI_INTERRUPT_REG, 0x28140000 | pci_int_line);

	allocate_device_space(pd, 0x100, 0x100, &port, &memaddr);

	snprintf(tmpstr, sizeof(tmpstr), "dec21143 addr=0x%llx addr2=0x%llx "
	    "irq=%i pci_little_endian=1", (long long)port, (long long)memaddr,
	    irq);
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
	 *
	 *  TODO: Base address, pci_little_endian, ...
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



/*
 *  Motorola MPC105 "Eagle" Host Bridge
 *
 *  Used in at least PReP and BeBox.
 */

#define	PCI_VENDOR_MOT			0x1057
#define	PCI_PRODUCT_MOT_MPC105		0x0001

PCIINIT(eagle)
{
	PCI_SET_DATA(PCI_ID_REG, PCI_ID_CODE(PCI_VENDOR_MOT,
	    PCI_PRODUCT_MOT_MPC105));

	PCI_SET_DATA(PCI_CLASS_REG, PCI_CLASS_CODE(PCI_CLASS_BRIDGE,
	    PCI_SUBCLASS_BRIDGE_HOST, 0) + 0x24);

	PCI_SET_DATA(PCI_BHLC_REG,
	    PCI_BHLC_CODE(0,0, 1 /* multi-function */, 0x40,0));
}



/*
 *  Uni-North (MacPPC PCI controller)
 */

#define	PCI_VENDOR_APPLE		0x106b
#define	PCI_PRODUCT_APPLE_UNINORTH1	0x001e

PCIINIT(uninorth)
{
	PCI_SET_DATA(PCI_ID_REG, PCI_ID_CODE(PCI_VENDOR_APPLE,
	    PCI_PRODUCT_APPLE_UNINORTH1));

	PCI_SET_DATA(PCI_CLASS_REG, PCI_CLASS_CODE(PCI_CLASS_BRIDGE,
	    PCI_SUBCLASS_BRIDGE_HOST, 0) + 0xff);

	PCI_SET_DATA(PCI_BHLC_REG,
	    PCI_BHLC_CODE(0,0, 1 /* multi-function */, 0x40,0));
}



/*
 *  ATI graphics cards
 */

#define	PCI_VENDOR_ATI			0x1002
#define	PCI_PRODUCT_ATI_RADEON_9200_2	0x5962

PCIINIT(ati_radeon_9200_2)
{
	PCI_SET_DATA(PCI_ID_REG, PCI_ID_CODE(PCI_VENDOR_ATI,
	    PCI_PRODUCT_ATI_RADEON_9200_2));

	/*  TODO: other subclass?  */
	PCI_SET_DATA(PCI_CLASS_REG, PCI_CLASS_CODE(PCI_CLASS_DISPLAY,
	    PCI_SUBCLASS_DISPLAY_VGA, 0) + 0x03);
}


