/*
 *  Copyright (C) 2003-2004 by Anders Gavare.  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright  
 *     notice, this list of conditions and the following disclaimer in the 
 *     documentation and/or other materials provided with the distribution.
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
 *  $Id: dev_gt.c,v 1.7 2004-01-06 09:01:15 debug Exp $
 *  
 *  The "gt" device used in Cobalt machines.
 *
 *  TODO:  This more or less just a dummy device, so far.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "misc.h"
#include "devices.h"
#include "bus_pci.h"


#define	TICK_STEPS_SHIFT	16

struct gt_data {
	int	reg[8];
	int	irqnr;

	struct pci_data *pci_data;
};


/*
 *  dev_gt_tick():
 */
void dev_gt_tick(struct cpu *cpu, void *extra)
{
	struct gt_data *gt_data = extra;

	cpu_interrupt(cpu, gt_data->irqnr);
}


/*
 *  dev_gt_access():
 *
 *  Returns 1 if ok, 0 on error.
 */
int dev_gt_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *extra)
{
	uint64_t idata = 0, odata = 0;
	int i;
	struct gt_data *d = extra;

	idata = memory_readmax64(cpu, data, len);

	switch (relative_addr) {
	case 0xc18:
		if (writeflag == MEM_WRITE) {
			debug("[ gt write to  0xc18: data = 0x%08lx ]\n", (long)idata);
			return 1;
		} else {
			odata = 0xffffffff;	/*  ???  interrupt something...  */
cpu_interrupt_ack(cpu, d->irqnr);
			debug("[ gt read from 0xc18 (data = 0x%08lx) ]\n", (long)odata);
		}
		break;
	case 0xcf8:	/*  PCI ADDR  */
	case 0xcfc:	/*  PCI DATA  */
		if (writeflag == MEM_WRITE) {
			bus_pci_access(cpu, mem, relative_addr, &idata, writeflag, d->pci_data);
		} else {
			bus_pci_access(cpu, mem, relative_addr, &odata, writeflag, d->pci_data);
		}
		break;
	default:
		if (writeflag==MEM_READ) {
			debug("[ gt read from addr 0x%x ]\n", (int)relative_addr);
			odata = d->reg[relative_addr];
		} else {
			debug("[ gt write to addr 0x%x:", (int)relative_addr);
			for (i=0; i<len; i++)
				debug(" %02x", data[i]);
			debug(" ]\n");
			d->reg[relative_addr] = idata;
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  pci_gt_rr():
 */
uint32_t pci_gt_rr(int reg)
{
	switch (reg) {
	case 0x00:
		return PCI_VENDOR_GALILEO + (PCI_PRODUCT_GALILEO_GT64011 << 16);
	case 0x08:
		return 0x01;	/*  Revision 1  */
	default:
		return 0;
	}
}


/*
 *  pci_gt_init():
 */
void pci_gt_init(struct memory *mem)
{
}


/*
 *  dev_gt_init():
 *
 *  Initialize a GT device.  Return a pointer to the pci_data used, so that
 *  the caller may add PCI devices.  First, however, we add the GT device
 *  itself.
 */
struct pci_data *dev_gt_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr, int irq_nr)
{
	struct gt_data *d;

	d = malloc(sizeof(struct gt_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct gt_data));
	d->irqnr = irq_nr;
	d->pci_data = bus_pci_init(mem);

	/*
	 *  According to NetBSD/cobalt:
	 *  pchb0 at pci0 dev 0 function 0: Galileo GT-64011 System Controller, rev 1
	 */
	bus_pci_add(d->pci_data, mem, 0, 0, 0, pci_gt_init, pci_gt_rr);

	memory_device_register(mem, "gt", baseaddr, DEV_GT_LENGTH, dev_gt_access, d);
	cpu_add_tickfunction(cpu, dev_gt_tick, d, TICK_STEPS_SHIFT);

	return d->pci_data;
}

