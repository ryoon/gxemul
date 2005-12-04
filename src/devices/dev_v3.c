/*
 *  Copyright (C) 2005  Anders Gavare.  All rights reserved.
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
 *  $Id: dev_v3.c,v 1.1 2005-12-04 14:25:48 debug Exp $
 *  
 *  V3 Semiconductor PCI controller.
 *
 *  TODO: This doesn't really work yet.
 *  See NetBSD's src/sys/arch/algor/pci/ for details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bus_pci.h"
#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"


/*
 *  dev_v3_pci_access():
 *
 *  Passes semi-direct PCI accesses onto bus_pci.
 */
int dev_v3_pci_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	uint64_t idata = 0, odata = 0;
	int bus, dev, func, reg;
	struct v3_data *d = extra;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len|MEM_PCI_LITTLE_ENDIAN);

	/*  Decompose the tag:  */
	relative_addr &= 0xfffff;
	relative_addr |= ((d->lb_map0 & 0xfff0) << 16);
	bus = 0;
	for (dev=24; dev<32; dev++)
		if (relative_addr & (1 << dev))
			break;
	dev -= 24;
	if (dev == 8) {
		fatal("[ v3_pci: NO DEVICE? ]\n");
		dev = 0;
	}
	func = (relative_addr >> 8) & 7;
	reg  = relative_addr & 0xfc;
	bus_pci_setaddr(cpu, d->pci_data, bus, dev, func, reg);

	bus_pci_data_access(cpu, d->pci_data, writeflag == MEM_READ?
	    &odata : &idata, len, writeflag);

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len|MEM_PCI_LITTLE_ENDIAN, odata);

	return 1;
}


/*
 *  dev_v3_access():
 *
 *  The PCI controller registers.
 */
int dev_v3_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	struct v3_data *d = extra;
	uint64_t idata = 0, odata = 0;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);

	switch (relative_addr) {

	case 0x18:	/*  PCI DMA base 1  */
		odata = 0x13000000;
		break;

	case 0x5e:	/*  LB MAP0  */
		if (writeflag == MEM_READ)
			odata = d->lb_map0;
		else
			d->lb_map0 = idata;
		break;

	case 0x62:	/*  PCI mem base 1  */
		odata = 0x1d00;
		break;

	default:if (writeflag == MEM_WRITE) {
			fatal("[ v3: unimplemented write to "
			    "offset 0x%x: data=0x%x ]\n", (int)
			    relative_addr, (int)idata);
		} else {
			fatal("[ v3: unimplemented read from "
			    "offset 0x%x ]\n", (int)relative_addr);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_v3_init():
 */
struct v3_data *dev_v3_init(struct machine *machine, struct memory *mem)
{
	struct v3_data *d;

	d = malloc(sizeof(struct v3_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct v3_data));

	/*  Register a PCI bus:  */
	d->pci_data = bus_pci_init(
	    machine,
	    0			/*  pciirq: TODO  */,
	    0x1d000000,		/*  pci device io offset  */
	    0x11000000,		/*  pci device mem offset  */
	    0x00070000,		/*  PCI portbase: TODO  */
	    0x00070000,		/*  PCI membase: TODO  */
	    0x00000000,		/*  PCI irqbase: TODO  */
	    0x1d000000,		/*  ISA portbase  */
	    0x10000000,		/*  ISA membase  */
	    8);			/*  ISA irqbase  */

	switch (machine->machine_type) {
	case MACHINE_ALGOR:
		bus_pci_add(machine, d->pci_data, mem, 0, 2, 0, "piix3_isa");
		bus_pci_add(machine, d->pci_data, mem, 0, 2, 1, "piix3_ide");
		break;
	default:fatal("!\n! WARNING: v3 for non-implemented machine"
		    " type\n!\n");
		exit(1);
	}

	/*  PCI configuration space:  */
	memory_device_register(mem, "v3_pci", 0x1ee00000, 0x100000,
	    dev_v3_pci_access, d, DM_DEFAULT, NULL);

	/*  PCI controller:  */
	memory_device_register(mem, "v3", 0x1ef00000, 0x1000,
	    dev_v3_access, d, DM_DEFAULT, NULL);

	return d;
}

