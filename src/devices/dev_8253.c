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
 *  $Id: dev_8253.c,v 1.4 2005-05-21 07:41:11 debug Exp $
 *  
 *  8253/8254 Programmable Interval Timer.
 *
 *  This is mostly bogus.
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


#define	DEV_8253_LENGTH		4
#define	TICK_SHIFT		14


struct pit8253_data {
	int		irq_nr;
	int		counter_select;
};


/*
 *  dev_8253_tick():
 */     
void dev_8253_tick(struct cpu *cpu, void *extra)
{
	struct pit8253_data *d = (struct pit8253_data *) extra;
	cpu_interrupt(cpu, d->irq_nr);
}


/*
 *  dev_8253_access():
 */
int dev_8253_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	struct pit8253_data *d = (struct pit8253_data *) extra;
	uint64_t idata = 0, odata = 0;

	idata = memory_readmax64(cpu, data, len);

	/*  TODO: ack somewhere else  */
	cpu_interrupt_ack(cpu, d->irq_nr);

	switch (relative_addr) {
	case 0x00:
		if (writeflag == MEM_WRITE) {
			/*  TODO  */
		} else {
			/*  TODO  */
			odata = 1;
odata = random();
		}
		break;
	case 0x03:
		if (writeflag == MEM_WRITE) {
			d->counter_select = idata >> 6;
			/*  TODO: other bits  */
		} else {
			odata = d->counter_select << 6;
		}
		break;
	default:
		if (writeflag == MEM_WRITE) {
			fatal("[ 8253: unimplemented write to address 0x%x"
			    " data=0x%02x ]\n", (int)relative_addr, (int)idata);
		} else {
			fatal("[ 8253: unimplemented read from address 0x%x "
			    "]\n", (int)relative_addr);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  devinit_8253():
 */
int devinit_8253(struct devinit *devinit)
{
	struct pit8253_data *d = malloc(sizeof(struct pit8253_data));

	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct pit8253_data));
	d->irq_nr = devinit->irq_nr;

	memory_device_register(devinit->machine->memory, devinit->name,
	    devinit->addr, DEV_8253_LENGTH, dev_8253_access, (void *)d,
	    MEM_DEFAULT, NULL);

	machine_add_tickfunction(devinit->machine, dev_8253_tick,
	    d, TICK_SHIFT);

	return 1;
}

