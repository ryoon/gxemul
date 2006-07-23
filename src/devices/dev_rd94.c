/*
 *  Copyright (C) 2003-2006  Anders Gavare.  All rights reserved.
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
 *  $Id: dev_rd94.c,v 1.36 2006-07-23 14:37:34 debug Exp $
 *  
 *  Used by NEC-RD94, -R94, and -R96.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bus_pci.h"
#include "cop0.h"
#include "cpu.h"
#include "cpu_mips.h"
#include "device.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"

#include "rd94.h"


#define	RD94_TICK_SHIFT		14

#define	DEV_RD94_LENGTH		0x1000

struct rd94_data {
	struct pci_data *pci_data;
	uint32_t	reg[DEV_RD94_LENGTH / 4];
	int		pciirq;

	int		intmask;
	int		interval;
	int		interval_start;
};


DEVICE_TICK(rd94)
{
	struct rd94_data *d = extra;

	/*  TODO: hm... intmask !=0 ?  */
	if (d->interval_start > 0 && d->interval > 0 && d->intmask != 0) {
		d->interval --;
		if (d->interval <= 0) {
			debug("[ rd94: interval timer interrupt ]\n");
			cpu_interrupt(cpu, 5);
		}
	}
}


DEVICE_ACCESS(rd94)
{
	struct rd94_data *d = (struct rd94_data *) extra;
	uint64_t idata = 0, odata = 0;
	int regnr, bus, dev, func, pcireg;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);

	regnr = relative_addr / sizeof(uint32_t);

	switch (relative_addr) {

	case RD94_SYS_CONFIG:
		if (writeflag == MEM_WRITE) {
			fatal("[ rd94: write to CONFIG: 0x%llx ]\n",
			    (long long)idata);
		} else {
			odata = 0;
			fatal("[ rd94: read from CONFIG: 0x%llx ]\n",
			    (long long)odata);
		}
		break;

	case RD94_SYS_INTSTAT1:		/*  LB (Local Bus ???)  */
		if (writeflag == MEM_WRITE) {
		} else {
			/*  Return value is (irq level + 1) << 2  */
			odata = (8+1) << 2;

			/*  Ugly hack:  */
			if ((cpu->cd.mips.coproc[0]->reg[COP0_CAUSE] & 0x800)
			    == 0)
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
		fatal("[ rd94: intstat4 ]\n");
		cpu_interrupt_ack(cpu, 6);
		break;

	case RD94_SYS_CPUID:
		if (writeflag == MEM_WRITE) {
			fatal("[ rd94: write to CPUID: 0x%llx ]\n",
			    (long long)idata);
		} else {
			odata = cpu->cpu_id;
			fatal("[ rd94: read from CPUID: 0x%llx ]\n",
			    (long long)odata);
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
			debug("[ rd94: setting Interval Timer value to %i ]\n",
			    (int)idata);
		} else {
			odata = d->interval_start;
			/*  TODO: or d->interval ?  */;
		}
		break;

	case RD94_SYS_PCI_CONFADDR:
		bus_pci_decompose_1(idata, &bus, &dev, &func, &pcireg);
		bus_pci_setaddr(cpu, d->pci_data, bus, dev, func, pcireg);
		break;

	case RD94_SYS_PCI_CONFDATA:
		bus_pci_data_access(cpu, d->pci_data, writeflag == MEM_READ?
		    &odata : &idata, len, writeflag);
		break;

	default:if (writeflag == MEM_WRITE) {
			fatal("[ rd94: unimplemented write to address 0x%x, "
			    "data=0x%02x ]\n", (int)relative_addr, (int)idata);
		} else {
			fatal("[ rd94: unimplemented read from address 0x%x"
			    " ]\n", (int)relative_addr);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


DEVINIT(rd94)
{
	struct rd94_data *d = malloc(sizeof(struct rd94_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct rd94_data));
	d->pciirq   = devinit->irq_nr;
	d->pci_data = bus_pci_init(devinit->machine, d->pciirq,
	    0,0, 0,0,0, 0,0,0);

	memory_device_register(devinit->machine->memory, devinit->name,
	    devinit->addr, DEV_RD94_LENGTH,
	    dev_rd94_access, (void *)d, DM_DEFAULT, NULL);

	machine_add_tickfunction(devinit->machine, dev_rd94_tick,
	    d, RD94_TICK_SHIFT, 0.0);

	devinit->return_ptr = d->pci_data;

	return 1;
}

