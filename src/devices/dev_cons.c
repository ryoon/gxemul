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
 *  $Id: dev_cons.c,v 1.37 2006-07-23 14:37:34 debug Exp $
 *  
 *  A simple console device, useful for simple tests.
 *
 *  This device provides memory mapped I/O for a simple console supporting
 *  putchar (writing to memory) and getchar (reading from memory), and
 *  support for halting the emulator.  (This is useful for regression tests,
 *  Hello World-style test programs, and other simple experiments.)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "console.h"
#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"

#include "testmachine/dev_cons.h"


#define	CONS_TICK_SHIFT		14


DEVICE_TICK(cons)
{
	struct cpu *c = cpu->machine->cpus[0];
	struct cons_data *d = extra;

	cpu_interrupt_ack(c, d->irq_nr);

	if (console_charavail(d->console_handle))
		cpu_interrupt(c, d->irq_nr);
}


DEVICE_ACCESS(cons)
{
	struct cons_data *d = extra;
	unsigned int i;

	/*  Exit the emulator:  */
	if (relative_addr == DEV_CONS_HALT) {
		/*  cpu->running = 0;
		    cpu->machine->exit_without_entering_debugger = 1;
		    return 1;  */
		/*  TODO: this doesn't work yet. for now, let's
		    simply use exit()  */
		exit(1);
	}

	if (writeflag == MEM_WRITE) {
		for (i=0; i<len; i++) {
			if (data[i] != 0) {
				if (cpu->machine->register_dump ||
				    cpu->machine->instruction_trace)
					debug("putchar '");

				console_putchar(d->console_handle, data[i]);

				if (cpu->machine->register_dump ||
				    cpu->machine->instruction_trace)
					debug("'\n");
				fflush(stdout);
			}
		}
        } else {
		int ch = console_readchar(d->console_handle);
		if (ch < 0)
			ch = 0;
		for (i=0; i<len; i++)
			data[i] = ch;
	}

	dev_cons_tick(cpu, extra);

	return 1;
}


DEVINIT(cons)
{
	struct cons_data *d;
	char *name3;
	size_t nlen;

	d = malloc(sizeof(struct cons_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct cons_data));

	nlen = strlen(devinit->name) + 10;
	if (devinit->name2 != NULL)
		nlen += strlen(devinit->name2) + 10;
	name3 = malloc(nlen);
	if (name3 == NULL) {
		fprintf(stderr, "out of memory in dev_cons_init()\n");
		exit(1);
	}
	if (devinit->name2 != NULL && devinit->name2[0])
		snprintf(name3, nlen, "%s [%s]", devinit->name, devinit->name2);
	else
		snprintf(name3, nlen, "%s", devinit->name);

	d->irq_nr = devinit->irq_nr;
	d->in_use = devinit->in_use;
	d->console_handle = console_start_slave(devinit->machine, name3,
	    d->in_use);

	memory_device_register(devinit->machine->memory, name3,
	    devinit->addr, DEV_CONS_LENGTH, dev_cons_access, d,
	    DM_DEFAULT, NULL);
	machine_add_tickfunction(devinit->machine, dev_cons_tick,
	    d, CONS_TICK_SHIFT, 0.0);

	/*  NOTE: Ugly cast into pointer  */
	devinit->return_ptr = (void *)(size_t)d->console_handle;
	return 1;
}

