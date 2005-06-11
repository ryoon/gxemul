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
 *  $Id: dev_cons.c,v 1.25 2005-06-11 21:04:31 debug Exp $
 *  
 *  A console device.  (Fake, only useful for simple tests.)
 *  It is hardwared to the lowest available MIPS hardware IRQ, and only
 *  interrupts CPU 0.
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
#include "devices.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"


#define	CONS_TICK_SHIFT		14

struct cons_data {
	int		console_handle;
	int		irq_nr;
};


/*
 *  dev_cons_tick():
 */
void dev_cons_tick(struct cpu *cpu, void *extra)
{
	struct cpu *c = cpu->machine->cpus[0];
	struct cons_data *d = extra;

	cpu_interrupt_ack(c, d->irq_nr);

	if (console_charavail(d->console_handle))
		cpu_interrupt(c, d->irq_nr);
}


/*
 *  dev_cons_access():
 */
int dev_cons_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	struct cons_data *d = extra;
	int i;

	/*  Exit the emulator:  */
	if (relative_addr == DEV_CONS_HALT) {
		cpu->running = 0;
		cpu->machine->exit_without_entering_debugger = 1;
		return 1;
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


/*
 *  dev_cons_init():
 */
int dev_cons_init(struct machine *machine, struct memory *mem,
	uint64_t baseaddr, char *name, int irq_nr)
{
	struct cons_data *d;
	char *name2;

	d = malloc(sizeof(struct cons_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct cons_data));
	d->irq_nr = irq_nr;
	d->console_handle = console_start_slave(machine, name);

	name2 = malloc(strlen(name) + 20);
	if (name2 == NULL) {
		fprintf(stderr, "out of memory in dev_cons_init()\n");
		exit(1);
	}
	if (name != NULL && name[0])
		sprintf(name2, "cons [%s]", name);
	else
		sprintf(name2, "cons");

	memory_device_register(mem, name2, baseaddr, DEV_CONS_LENGTH,
	    dev_cons_access, d, MEM_DEFAULT, NULL);
	machine_add_tickfunction(machine, dev_cons_tick, d, CONS_TICK_SHIFT);

	return d->console_handle;
}

