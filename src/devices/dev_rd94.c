/*
 *  Copyright (C) 2003-2004  Anders Gavare.  All rights reserved.
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
 *  $Id: dev_rd94.c,v 1.11 2004-11-17 20:37:39 debug Exp $
 *  
 *  RD94 jazzio.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "misc.h"
#include "devices.h"

#include "bus_pci.h"
#include "rd94.h"


struct rd94_data {
	struct pci_data *pci_data;
	uint32_t	reg[DEV_RD94_LENGTH / 4];
	int		pciirq;

	int		intmask;
	int		interval;
	int		interval_start;
};


/*
 *  dev_rd94_tick():
 */
void dev_rd94_tick(struct cpu *cpu, void *extra)
{
	struct rd94_data *d = extra;

	if (d->interval_start > 0 && d->interval > 0 && d->intmask!=0) {		/*  ??? !=0  */
		d->interval --;
		if (d->interval <= 0) {
			debug("[ rd94: interval timer interrupt ]\n");
			cpu_interrupt(cpu, 5);
		}
	}
}


/*
 *  dev_rd94_access():
 */
int dev_rd94_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	struct rd94_data *d = (struct rd94_data *) extra;
	uint64_t idata = 0, odata = 0;
	int regnr;

	idata = memory_readmax64(cpu, data, len);
	regnr = relative_addr / sizeof(uint32_t);

	switch (relative_addr) {
	case RD94_SYS_INTSTAT1:		/*  LB (Local Bus ???)  */
		if (writeflag == MEM_WRITE) {
		} else {
			/*  Return value is (irq level + 1) << 2  */
			odata = (8+1) << 2;

			/*  Ugly hack:  */
			if ((cpu->coproc[0]->reg[COP0_CAUSE] & 0x800) == 0)
				odata = 0;
		}
		debug("[ rd94: intstat1 ]\n");
/*		cpu_interrupt_ack(cpu, 3); */
		break;
	case RD94_SYS_INTSTAT2:		/*  PCI/EISA  */
		if (writeflag == MEM_WRITE) {
		} else {
			odata = 0;	/*  TODO  */
		}
		debug("[ rd94: intstat2 ]\n");
/*		cpu_interrupt_ack(cpu, 4); */
		break;
	case RD94_SYS_INTSTAT3:		/*  IT (Interval Timer)  */
		if (writeflag == MEM_WRITE) {
		} else {
			odata = 0;	/*  return value does not matter?  */
		}
		debug("[ rd94: intstat3 ]\n");
		cpu_interrupt_ack(cpu, 5);
		d->interval = d->interval_start;
		break;
	case RD94_SYS_INTSTAT4:		/*  IPI  */
		if (writeflag == MEM_WRITE) {
		} else {
			odata = 0;	/*  return value does not matter?  */
		}
		debug("[ rd94: intstat4 ]\n");
		cpu_interrupt_ack(cpu, 6);
		break;
	case RD94_SYS_CPUID:
		if (writeflag == MEM_WRITE) {
		} else {
			odata = cpu->cpu_id;
		}
		break;
	case RD94_SYS_EXT_IMASK:
		if (writeflag == MEM_WRITE) {
			d->intmask = idata;
		} else {
			odata = d->intmask;
		}
		break;
	case RD94_SYS_IT_VALUE:
		if (writeflag == MEM_WRITE) {
			d->interval = d->interval_start = idata;
			debug("[ rd94: setting Interval Timer value to %i ]\n", idata);
		} else {
			odata = d->interval_start;	/*  TODO: or d->interval ?  */;
		}
		break;
	case RD94_SYS_PCI_CONFADDR:
	case RD94_SYS_PCI_CONFDATA:
		if (writeflag == MEM_WRITE) {
			bus_pci_access(cpu, mem, relative_addr == RD94_SYS_PCI_CONFADDR? BUS_PCI_ADDR : BUS_PCI_DATA, &idata, writeflag, d->pci_data);
		} else {
			bus_pci_access(cpu, mem, relative_addr == RD94_SYS_PCI_CONFADDR? BUS_PCI_ADDR : BUS_PCI_DATA, &odata, writeflag, d->pci_data);
			/*  odata = 0;  */
		}
		break;
	default:
		if (writeflag == MEM_WRITE) {
			debug("[ rd94: unimplemented write to address 0x%x, data=0x%02x ]\n", relative_addr, idata);
		} else {
			debug("[ rd94: unimplemented read from address 0x%x ]\n", relative_addr);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_rd94_init():
 */
struct pci_data *dev_rd94_init(struct cpu *cpu, struct memory *mem,
	uint64_t baseaddr, int pciirq)
{
	struct rd94_data *d = malloc(sizeof(struct rd94_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct rd94_data));
	d->pciirq   = pciirq;
	d->pci_data = bus_pci_init(mem, pciirq);

	memory_device_register(mem, "rd94", baseaddr, DEV_RD94_LENGTH,
	    dev_rd94_access, (void *)d, MEM_DEFAULT, NULL);
	cpu_add_tickfunction(cpu, dev_rd94_tick, d, 10);

	return d->pci_data;
}

