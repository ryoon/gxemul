/*
 *  Copyright (C) 2003-2004 by Anders Gavare.  All rights reserved.
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
 *  $Id: dev_dec5800.c,v 1.2 2004-03-09 00:05:55 debug Exp $
 *  
 *  DEC 5800 (SMP capable system).
 *
 *  TODO:  This hardware is not very easy to find docs about.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "misc.h"
#include "console.h"
#include "devices.h"


/*
 *  dev_dec5800_tick():
 */
void dev_dec5800_tick(struct cpu *cpu, void *extra)
{
	struct dec5800_data *d = extra;

	/*  Timer interrupts?  */
	if (d->csr & 0x8000) {
		debug("[ dec5800: timer interrupt! ]\n");

		/*  Set timer interrupt pending bit:  */
		d->csr |= 0x20000000;

		cpu_interrupt(cpu, 3);
	}
}


/*
 *  dev_dec5800_vectors_access():
 *
 *  Returns 1 if ok, 0 on error.
 */
int dev_dec5800_vectors_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *extra)
{
	uint64_t idata = 0, odata = 0;
	struct dec5800_data *d = extra;

	idata = memory_readmax64(cpu, data, len);

	if (writeflag == MEM_READ) {
		/*  TODO  */
		/*  0xfc = transmit interrupt, 0xf8 = receive interrupt, 0x80 = IPI  */
		odata = d->vector_0x50;
/* odata = 0xfc; */
		debug("[ dec5800_vectors: read from 0x%02x: 0x%02x ]\n", (int)relative_addr, (int)odata);
	} else {
		d->vector_0x50 = idata;
		debug("[ dec5800_vectors: write to 0x%02x: 0x%02x ]\n", (int)relative_addr, (int)idata);
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_dec5800_access():
 *
 *  Returns 1 if ok, 0 on error.
 */
int dev_dec5800_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *extra)
{
	uint64_t idata = 0, odata = 0;
	struct dec5800_data *d = extra;

	idata = memory_readmax64(cpu, data, len);

	/*  Lowest 4 bits of csr contain cpu id:  */
	d->csr = (d->csr & ~0xf) | (cpu->cpu_id & 0xf);

	switch (relative_addr) {
	case 0x0000:	/*  csr  */
		if (writeflag == MEM_READ) {
			odata = d->csr;
			odata ^= random() & 0x10000;
			debug("[ dec5800: read from csr: 0x%08x ]\n", (int)odata);
		} else {
			d->csr = idata;

			/*  Ack. timer interrupts:  */
			d->csr &= ~0x20000000;
			cpu_interrupt_ack(cpu, 3);

			debug("[ dec5800: write to csr: 0x%08x ]\n", (int)idata);
		}
		break;
	default:
		if (writeflag==MEM_READ) {
			debug("[ dec5800: read from 0x%08lx ]\n", (long)relative_addr);
		} else {
			debug("[ dec5800: write to  0x%08lx: 0x%08x ]\n", (long)relative_addr, idata);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_dec5800_init():
 */
struct dec5800_data *dev_dec5800_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr)
{
	struct dec5800_data *d;

	d = malloc(sizeof(struct dec5800_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct dec5800_data));

	memory_device_register(mem, "dec5800", baseaddr, DEV_DEC5800_LENGTH, dev_dec5800_access, d);
	memory_device_register(mem, "dec5800_vectors", baseaddr + 0x30000000, 0x100, dev_dec5800_vectors_access, d);
	cpu_add_tickfunction(cpu, dev_dec5800_tick, d, 13);

	return d;
}

