/*
 *  Copyright (C) 2005-2006  Anders Gavare.  All rights reserved.
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
 *  $Id: dev_gc.c,v 1.6 2006-01-01 13:17:16 debug Exp $
 *  
 *  Grand Central Interrupt controller (used by MacPPC).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"


/*
 *  dev_gc_access():
 */
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
			odata = d->status_hi
& d->enable_hi
;
		break;

	case 0x14:
		if (writeflag == MEM_READ)
			odata = d->enable_hi;
		else {
			uint32_t old_enable_hi = d->enable_hi;
			d->enable_hi = idata;
			if (d->enable_hi != old_enable_hi)
				cpu_interrupt(cpu, d->reassert_irq);
		}
		break;

	case 0x18:
		if (writeflag == MEM_WRITE) {
			uint32_t old_status_hi = d->status_hi;
			d->status_hi &= ~idata;
			if (d->status_hi != old_status_hi)
				cpu_interrupt(cpu, d->reassert_irq);
		}
		break;

	case 0x20:
		if (writeflag == MEM_READ)
			odata = d->status_lo
& d->enable_lo
;
		break;

	case 0x24:
		if (writeflag == MEM_READ)
			odata = d->enable_lo;
		else {
			uint32_t old_enable_lo = d->enable_lo;
			d->enable_lo = idata;
			if (d->enable_lo != old_enable_lo)
				cpu_interrupt(cpu, d->reassert_irq);
		}
		break;

	case 0x28:
		if (writeflag == MEM_WRITE) {
			uint32_t old_status_lo = d->status_lo;
			d->status_lo &= ~idata;
			if (d->status_lo != old_status_lo)
				cpu_interrupt(cpu, d->reassert_irq);
		}
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


/*
 *  dev_gc_init():
 */
struct gc_data *dev_gc_init(struct machine *machine, struct memory *mem,
	uint64_t addr, int reassert_irq)
{
	struct gc_data *d;

	d = malloc(sizeof(struct gc_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct gc_data));
	d->reassert_irq = reassert_irq;

	memory_device_register(mem, "gc", addr, 0x100,
	    dev_gc_access, d, DM_DEFAULT, NULL);

	return d;
}

