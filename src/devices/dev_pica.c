/*
 *  Copyright (C) 2004  Anders Gavare.  All rights reserved.
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
 *  $Id: dev_pica.c,v 1.3 2004-10-17 15:31:39 debug Exp $
 *  
 *  Acer PICA-61 stuff.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "misc.h"
#include "devices.h"

#include "pica.h"


#define	DEV_PICA_TICKSHIFT		9

#define	PICA_TIMER_IRQ			1


/*
 *  dev_pica_tick():
 */
void dev_pica_tick(struct cpu *cpu, void *extra)
{
	struct pica_data *d = extra;

	if (d->interval_start > 0 && d->interval > 0
	    && (d->int_enable_mask & (1 << PICA_TIMER_IRQ))) {
		d->interval --;
		if (d->interval <= 0) {
			debug("[ pica: interval timer interrupt ]\n");
			cpu_interrupt(cpu, 8 + PICA_TIMER_IRQ);
		}
	}
}


/*
 *  dev_pica_access():
 */
int dev_pica_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	struct pica_data *d = (struct pica_data *) extra;
	uint64_t idata = 0, odata = 0;
	int regnr;

	idata = memory_readmax64(cpu, data, len);
	regnr = relative_addr / sizeof(uint32_t);

	switch (relative_addr) {
	case R4030_SYS_ISA_VECTOR:
		/*  ?  */
		{
			uint32_t x = d->int_asserted & d->int_enable_mask;
			odata = 0;
			while (odata < 16) {
				if (x & (1 << odata))
					break;
				odata ++;
			}
			if (odata >= 16)
				odata = 0;
		}
		break;
	case R4030_SYS_IT_VALUE:  /*  Interval timer reload value  */
		if (writeflag == MEM_WRITE) {
			d->interval_start = idata;
			d->interval = d->interval_start;
		} else
			odata = d->interval_start;
		break;
	case R4030_SYS_IT_STAT:
		/*  Accessing this word seems to acknowledge interrupts?  */
		cpu_interrupt_ack(cpu, 8 + PICA_TIMER_IRQ);
		if (writeflag == MEM_WRITE)
			d->interval = idata;
		else
			odata = d->interval;
		d->interval = d->interval_start;
		break;
	case R4030_SYS_EXT_IMASK:
		if (writeflag == MEM_WRITE) {
			d->int_enable_mask = idata;
			/*  Do a "nonsense" interrupt recalibration:  */
			cpu_interrupt_ack(cpu, 8);
		} else
			odata = d->int_enable_mask;
		break;
	default:
		if (writeflag == MEM_WRITE) {
			fatal("[ pica: unimplemented write to address 0x%x"
			    ", data=0x%02x ]\n", relative_addr, idata);
		} else {
			fatal("[ pica: unimplemented read from address 0x%x"
			    " ]\n", relative_addr);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_pica_init():
 */
struct pica_data *dev_pica_init(struct cpu *cpu, struct memory *mem,
	uint64_t baseaddr)
{
	struct pica_data *d = malloc(sizeof(struct pica_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct pica_data));

	memory_device_register(mem, "pica", baseaddr, DEV_PICA_LENGTH,
	    dev_pica_access, (void *)d);
	cpu_add_tickfunction(cpu, dev_pica_tick, d, DEV_PICA_TICKSHIFT);

	return d;
}

