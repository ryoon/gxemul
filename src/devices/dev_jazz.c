/*
 *  Copyright (C) 2004 by Anders Gavare.  All rights reserved.
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
 *  $Id: dev_jazz.c,v 1.2 2004-10-14 12:22:18 debug Exp $
 *  
 *  Jazz stuff. (Acer PICA-61, etc.)
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "misc.h"
#include "devices.h"

#include "pica.h"


/*
 *  dev_jazz_tick():
 */
void dev_jazz_tick(struct cpu *cpu, void *extra)
{
	struct jazz_data *d = extra;

	if (d->interval_start > 0 && d->interval > 0) {
		d->interval --;
		if (d->interval <= 0) {
			debug("[ jazz: interval timer interrupt ]\n");
			cpu_interrupt(cpu, 5);
		}
	}
}


/*
 *  dev_jazz_access():
 */
int dev_jazz_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	struct jazz_data *d = (struct jazz_data *) extra;
	uint64_t idata = 0, odata = 0;
	int regnr;

	idata = memory_readmax64(cpu, data, len);
	regnr = relative_addr / sizeof(uint32_t);

	switch (relative_addr) {
	default:
		if (writeflag == MEM_WRITE) {
			fatal("[ jazz: unimplemented write to address 0x%x"
			    ", data=0x%02x ]\n", relative_addr, idata);
		} else {
			fatal("[ jazz: unimplemented read from address 0x%x"
			    " ]\n", relative_addr);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_jazz_init():
 */
struct jazz_data *dev_jazz_init(struct cpu *cpu, struct memory *mem,
	uint64_t baseaddr)
{
	struct jazz_data *d = malloc(sizeof(struct jazz_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct jazz_data));

	memory_device_register(mem, "jazz", baseaddr, DEV_JAZZ_LENGTH,
	    dev_jazz_access, (void *)d);
	cpu_add_tickfunction(cpu, dev_jazz_tick, d, 10);

	return d;
}

