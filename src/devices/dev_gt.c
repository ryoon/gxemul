/*
 *  Copyright (C) 2003-2005  Anders Gavare.  All rights reserved.
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
 *  $Id: dev_gt.c,v 1.31 2005-11-11 19:01:27 debug Exp $
 *  
 *  Galileo Technology GT-64xxx PCI controller.
 *
 *	GT-64011	Used in Cobalt machines.
 *	GT-64120	Used in evbmips machines (Malta).
 *
 *  TODO: This more or less just a dummy device, so far. It happens to work
 *        with NetBSD/cobalt and /evbmips, and in some cases it might happen
 *        to work with Linux as well, but don't rely on it for anything else.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bus_pci.h"
#include "cpu.h"
#include "devices.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"


#define	TICK_SHIFT		14

/*  #define debug fatal  */

#define PCI_PRODUCT_GALILEO_GT64011  0x4146    /*  GT-64011  */
#define	PCI_PRODUCT_GALILEO_GT64120  0x4620    /*  GT-64120  */

struct gt_data {
	int	irqnr;
	int	pciirq;
	int	type;

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
 */
int dev_gt_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr,
	unsigned char *data, size_t len, int writeflag, void *extra)
{
	uint64_t idata = 0, odata = 0;
	int i, asserted;
	struct gt_data *d = extra;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);

	switch (relative_addr) {

	case 0x48:
		switch (d->type) {
		case PCI_PRODUCT_GALILEO_GT64120:
			/*
			 *  This is needed for Linux on Malta, according
			 *  to Alec Voropay.  (TODO: Remove this hack when
			 *  things have stabilized.)
			 */
			if (writeflag == MEM_READ) {
				odata = 0x18000000 >> 21;
				debug("[ gt: read from 0x48: 0x%08x ]\n",
				    (int)odata);
			}
			break;
		default:
			fatal("[ gt: access to 0x48? (type %i) ]\n", d->type);
		}
		break;

	case 0xc18:
		if (writeflag == MEM_WRITE) {
			debug("[ gt: write to  0xc18: 0x%08x ]\n", (int)idata);
			return 1;
		} else {
			odata = 0xffffffffULL;
			/*
			 *  ???  interrupt something...
			 *
			 *  TODO: Remove this hack when things have stabilized.
			 */
			odata = 0x00000100;
			/*  netbsd/cobalt cobalt/machdep.c:cpu_intr()  */

			cpu_interrupt_ack(cpu, d->irqnr);

			debug("[ gt: read from 0xc18 (0x%08x) ]\n", (int)odata);
		}
		break;

	case 0xc34:	/*  GT_PCI0_INTR_ACK  */
		odata = cpu->machine->isa_pic_data.last_int;
		cpu_interrupt_ack(cpu, 8 + odata);
		break;

	case 0xcf8:	/*  PCI ADDR  */
	case 0xcfc:	/*  PCI DATA  */
		if (writeflag == MEM_WRITE) {
			bus_pci_access(cpu, mem, relative_addr, &idata,
			    len, writeflag, d->pci_data);
		} else {
			bus_pci_access(cpu, mem, relative_addr, &odata,
			    len, writeflag, d->pci_data);
		}
		break;
	default:
		if (writeflag == MEM_READ) {
			debug("[ gt: read from addr 0x%x ]\n",
			    (int)relative_addr);
		} else {
			debug("[ gt: write to addr 0x%x:", (int)relative_addr);
			for (i=0; i<len; i++)
				debug(" %02x", data[i]);
			debug(" ]\n");
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_gt_init():
 *
 *  Initialize a GT device.  Return a pointer to the pci_data used, so that
 *  the caller may add PCI devices.  First, however, we add the GT device
 *  itself.
 */
struct pci_data *dev_gt_init(struct machine *machine, struct memory *mem,
	uint64_t baseaddr, int irq_nr, int pciirq, int type)
{
	struct gt_data *d;

	d = malloc(sizeof(struct gt_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct gt_data));
	d->irqnr    = irq_nr;
	d->pciirq   = pciirq;
	d->pci_data = bus_pci_init(pciirq);

	switch (type) {
	case 11:
		d->type = PCI_PRODUCT_GALILEO_GT64011;
		break;
	case 120:
		d->type = PCI_PRODUCT_GALILEO_GT64120;
		break;
	default:fatal("dev_gt_init(): type must be 11 or 120.\n");
		exit(1);
	}

	/*
	 *  According to NetBSD/cobalt:
	 *  pchb0 at pci0 dev 0 function 0: Galileo GT-64011
	 *  System Controller, rev 1
	 */
	bus_pci_add(machine, d->pci_data, mem, 0, 0, 0,
	    d->type == PCI_PRODUCT_GALILEO_GT64011? "gt64011" : "gt64120");

	memory_device_register(mem, "gt", baseaddr, DEV_GT_LENGTH,
	    dev_gt_access, d, MEM_DEFAULT, NULL);
	machine_add_tickfunction(machine, dev_gt_tick, d, TICK_SHIFT);

	return d->pci_data;
}

