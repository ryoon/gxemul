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
 *  $Id: dev_malta.c,v 1.2 2005-06-22 00:39:45 debug Exp $
 *
 *  Malta (evbmips) interrupt controller.
 *
 *  TODO: This is basically a dummy device so far.
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


/*
 *  dev_malta_hi_access():
 */
int dev_malta_hi_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	struct malta_data *d = (struct malta_data *) extra;
	uint64_t idata = 0, odata = 0;
	int i;

	idata = memory_readmax64(cpu, data, len);

	switch (relative_addr) {
	case 0:	if (writeflag == MEM_READ) {
			odata = 0;
			for (i=0; i<7; i++)
				if (d->assert_hi & (1<<i))
					odata = i;
			if (odata)
				odata |= 0x80;
			d->assert_hi = 0;
			cpu_interrupt_ack(cpu, 8 + 2);
		} else {
			/*  TODO  */
		}
		break;
	default:if (writeflag == MEM_WRITE) {
			fatal("[ malta: hi unimplemented write to "
			    "offset 0x%x: data=0x%02x ]\n", (int)
			    relative_addr, (int)idata);
		} else {
			fatal("[ malta: hi unimplemented read from "
			    "offset 0x%x ]\n", (int)relative_addr);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_malta_access():
 */
int dev_malta_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	struct malta_data *d = (struct malta_data *) extra;
	uint64_t idata = 0, odata = 0;
	int i;

	idata = memory_readmax64(cpu, data, len);

	switch (relative_addr) {
	case 0:	if (writeflag == MEM_READ) {
			odata = 0;
			for (i=0; i<7; i++)
				if (d->assert_lo & (1<<i))
					odata = i;
			if (odata)
				odata |= 0x80;
			d->assert_lo = 0;
			cpu_interrupt_ack(cpu, 8 + 16);
		} else {
			/*  TODO  */
		}
		break;
	default:if (writeflag == MEM_WRITE) {
			fatal("[ malta: unimplemented write to "
			    "offset 0x%x: data=0x%02x ]\n", (int)
			    relative_addr, (int)idata);
		} else {
			fatal("[ malta: unimplemented read from "
			    "offset 0x%x ]\n", (int)relative_addr);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  devinit_malta():
 */
int devinit_malta(struct devinit *devinit)
{
	struct malta_data *d = malloc(sizeof(struct malta_data));

	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct malta_data));

	memory_device_register(devinit->machine->memory, devinit->name,
	    devinit->addr, 1,
	    dev_malta_access, (void *)d, MEM_DEFAULT, NULL);
	memory_device_register(devinit->machine->memory, devinit->name,
	    devinit->addr + 0x80, 1,
	    dev_malta_hi_access, (void *)d, MEM_DEFAULT, NULL);

	devinit->return_ptr = d;

	return 1;
}

