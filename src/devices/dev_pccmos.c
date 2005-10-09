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
 *  $Id: dev_pccmos.c,v 1.6 2005-10-09 21:32:08 debug Exp $
 *  
 *  PC CMOS/RTC device.
 *
 *  The main point of this device is to be a "PC style wrapper" for accessing
 *  the MC146818 (the RTC). In most other respects, this device is bogus, and
 *  just acts as a 256-byte RAM device.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "emul.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"


#define	DEV_PCCMOS_LENGTH		2
#define	PCCMOS_MC146818_FAKE_ADDR	0x1d00000000ULL

struct pccmos_data {
	unsigned char	select;
	unsigned char	ram[256];
};


/*
 *  dev_pccmos_access():
 */
int dev_pccmos_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	struct pccmos_data *d = (struct pccmos_data *) extra;
	uint64_t idata = 0, odata = 0;
	unsigned char b;

	b = idata = memory_readmax64(cpu, data, len);

	/*
	 *  Accesses to CMOS register 0 .. 0xd are rerouted to the
	 *  RTC; all other access are treated as CMOS RAM read/writes.
	 */

	switch (relative_addr) {
	case 0:	if (writeflag == MEM_WRITE) {
			d->select = idata;
			if (idata <= 0x0d) {
				cpu->memory_rw(cpu, cpu->mem,
				    PCCMOS_MC146818_FAKE_ADDR, &b, 1,
				    MEM_WRITE, PHYSICAL);
			}
		} else
			odata = d->select;
		break;
	case 1:	if (d->select <= 0x0d) {
			if (writeflag == MEM_WRITE) {
				cpu->memory_rw(cpu, cpu->mem,
				    PCCMOS_MC146818_FAKE_ADDR + 1, &b, 1,
				    MEM_WRITE, PHYSICAL);
			} else {
				cpu->memory_rw(cpu, cpu->mem,
				    PCCMOS_MC146818_FAKE_ADDR + 1, &b, 1,
				    MEM_READ, PHYSICAL);
				odata = b;
			}
		} else {
			if (writeflag == MEM_WRITE)
				d->ram[d->select] = idata;
			else
				odata = d->ram[d->select];
		}
		break;
	default:
		if (writeflag == MEM_WRITE) {
			fatal("[ pccmos: unimplemented write to address 0x%x"
			    " data=0x%02x ]\n", (int)relative_addr, (int)idata);
		} else {
			fatal("[ pccmos: unimplemented read from address 0x%x "
			    "]\n", (int)relative_addr);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  devinit_pccmos():
 */
int devinit_pccmos(struct devinit *devinit)
{
	struct pccmos_data *d = malloc(sizeof(struct pccmos_data));
	int irq_nr, type = MC146818_PC_CMOS;

	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct pccmos_data));

	memory_device_register(devinit->machine->memory, devinit->name,
	    devinit->addr, DEV_PCCMOS_LENGTH, dev_pccmos_access, (void *)d,
	    MEM_DEFAULT, NULL);

	/*
	 *  Different machines use different IRQ schemes.
	 */
	switch (devinit->machine->machine_type) {
	case MACHINE_CATS:
		irq_nr = 32 + 8;
		type = MC146818_CATS;
		d->ram[0x48] = 20;		/*  century  */
		break;
	case MACHINE_X86:
		irq_nr = 16;	/*  "No" irq  */
		break;
	default:fatal("devinit_pccmos(): unimplemented machine type"
		    " %i\n", devinit->machine->machine_type);
		exit(1);
	}

	dev_mc146818_init(devinit->machine, devinit->machine->memory,
	    PCCMOS_MC146818_FAKE_ADDR, irq_nr, type, 1);

	return 1;
}

