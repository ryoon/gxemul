/*
 *  Copyright (C) 2005-2007  Anders Gavare.  All rights reserved.
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
 *  $Id: dev_gc.c,v 1.10 2007-01-28 11:29:52 debug Exp $
 *  
 *  Grand Central Interrupt controller (used by MacPPC).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "device.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"


#define	DEV_GC_LENGTH		0x100

struct gc_data {
	struct interrupt cpu_irq;

	uint32_t	status_hi;
	uint32_t	status_lo;
	uint32_t	enable_hi;
	uint32_t	enable_lo;
};


#if 0
void gc_interrupt(struct machine *m, struct cpu *cpu, int irq_nr, int 
assrt)
{
        uint32_t mask = 1 << (irq_nr & 31);
        if (irq_nr < 32) {
                if (assrt)
                        m->md_int.gc_data->status_lo |= mask;
                else
                        m->md_int.gc_data->status_lo &= ~mask;
        }
        if (irq_nr >= 32 && irq_nr < 64) {
                if (assrt)
                        m->md_int.gc_data->status_hi |= mask;
                else
                        m->md_int.gc_data->status_hi &= ~mask;
        }
                
#if 0
        printf("status = %08x %08x  enable = %08x %08x\n",
            m->md_int.gc_data->status_hi, m->md_int.gc_data->status_lo,
            m->md_int.gc_data->enable_hi, m->md_int.gc_data->enable_lo);
#endif

        if (m->md_int.gc_data->status_lo & m->md_int.gc_data->enable_lo ||
            m->md_int.gc_data->status_hi & m->md_int.gc_data->enable_hi)
                cpu_interrupt(m->cpus[0], 65);
        else
                cpu_interrupt_ack(m->cpus[0], 65);
}               
                        

#endif

DEVICE_ACCESS(gc)
{
	struct gc_data *d = extra;
	uint64_t idata = 0, odata = 0;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);

	switch (relative_addr) {

#if 0
#define INT_STATE_REG_H  (interrupt_reg + 0x00)
#define INT_ENABLE_REG_H (interrupt_reg + 0x04)
#define INT_CLEAR_REG_H  (interrupt_reg + 0x08)
#define INT_LEVEL_REG_H  (interrupt_reg + 0x0c)
#define INT_STATE_REG_L  (interrupt_reg + 0x10)
#define INT_ENABLE_REG_L (interrupt_reg + 0x14)
#define INT_CLEAR_REG_L  (interrupt_reg + 0x18)
#define INT_LEVEL_REG_L  (interrupt_reg + 0x1c)
#endif

	case 0x10:
		if (writeflag == MEM_READ)
			odata = d->status_hi & d->enable_hi;
		break;

	case 0x14:
		if (writeflag == MEM_READ)
			odata = d->enable_hi;
		else {
			int old_assert = (d->status_lo & d->enable_lo
			    || d->status_hi & d->enable_hi);
			int new_assert;
			d->enable_hi = idata;

			new_assert = (d->status_lo & d->enable_lo ||
			    d->status_hi & d->enable_hi);

			if (old_assert && !new_assert)
				INTERRUPT_DEASSERT(d->cpu_irq);
			else if (!old_assert && new_assert)
				INTERRUPT_ASSERT(d->cpu_irq);
		}
		break;

	case 0x18:
		if (writeflag == MEM_WRITE) {
			int old_assert = (d->status_lo & d->enable_lo
			    || d->status_hi & d->enable_hi);
			int new_assert;
			d->status_hi &= ~idata;

			new_assert = (d->status_lo & d->enable_lo ||
			    d->status_hi & d->enable_hi);

			if (old_assert && !new_assert)
				INTERRUPT_DEASSERT(d->cpu_irq);
			else if (!old_assert && new_assert)
				INTERRUPT_ASSERT(d->cpu_irq);
		}
		break;

	case 0x20:
		if (writeflag == MEM_READ)
			odata = d->status_lo & d->enable_lo;
		break;

	case 0x24:
		if (writeflag == MEM_READ)
			odata = d->enable_lo;
		else {
			int old_assert = (d->status_lo & d->enable_lo
			    || d->status_hi & d->enable_hi);
			int new_assert;
			d->enable_lo = idata;

			new_assert = (d->status_lo & d->enable_lo ||
			    d->status_hi & d->enable_hi);

			if (old_assert && !new_assert)
				INTERRUPT_DEASSERT(d->cpu_irq);
			else if (!old_assert && new_assert)
				INTERRUPT_ASSERT(d->cpu_irq);
		}
		break;

	case 0x28:
		if (writeflag == MEM_WRITE) {
			int old_assert = (d->status_lo & d->enable_lo
			    || d->status_hi & d->enable_hi);
			int new_assert;
			d->status_lo &= ~idata;

			new_assert = (d->status_lo & d->enable_lo ||
			    d->status_hi & d->enable_hi);

			if (old_assert && !new_assert)
				INTERRUPT_DEASSERT(d->cpu_irq);
			else if (!old_assert && new_assert)
				INTERRUPT_ASSERT(d->cpu_irq);
		}
		break;

	case 0x2c:
		/*  Avoid a debug message.  */
		break;

	default:if (writeflag == MEM_WRITE) {
			fatal("[ gc: unimplemented write to "
			    "offset 0x%x: data=0x%x ]\n", (int)
			    relative_addr, (int)idata);
		} else {
			fatal("[ gc: unimplemented read from "
			    "offset 0x%x ]\n", (int)relative_addr);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


DEVINIT(gc)
{
	struct gc_data *d;

	d = malloc(sizeof(struct gc_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct gc_data));

	/*  Connect to the CPU:  */
	INTERRUPT_CONNECT(devinit->interrupt_path, d->cpu_irq);

	memory_device_register(devinit->machine->memory, "gc",
	    devinit->addr, DEV_GC_LENGTH, dev_gc_access, d, DM_DEFAULT, NULL);

	return 1;
}

