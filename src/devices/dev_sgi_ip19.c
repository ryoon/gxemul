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
 *  $Id: dev_sgi_ip19.c,v 1.7 2004-12-20 02:48:39 debug Exp $
 *  
 *  SGI IP19 (and IP25) stuff.  The stuff in here is mostly guesswork.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "devices.h"
#include "emul.h"
#include "memory.h"
#include "misc.h"


struct sgi_ip19_data {
	uint64_t	cycle_counter;
};


/*
 *  dev_sgi_ip19_access():
 */
int dev_sgi_ip19_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	struct sgi_ip19_data *d = (struct sgi_ip19_data *) extra;
	uint64_t idata = 0, odata = 0;
	int regnr;

	idata = memory_readmax64(cpu, data, len);
	regnr = relative_addr / sizeof(uint32_t);

	switch (relative_addr) {
	case 0x08:	/*  cpu id  */
		if (writeflag == MEM_WRITE) {
			debug("[ sgi_ip19: unimplemented write to address 0x%x (cpu id), data=0x%08x ]\n", relative_addr, (int)idata);
		} else {
			odata = cpu->cpu_id;	/*  ?  TODO  */
		}
		break;
	case 0x200:	/*  cpu available mask?  */
		if (writeflag == MEM_WRITE) {
			debug("[ sgi_ip19: unimplemented write to address 0x%x (cpu available mask), data=0x%08x ]\n", relative_addr, (int)idata);
		} else {
			/*  Max 16 cpus?  */
			odata = ((1 << cpu->emul->ncpus) - 1)<< 16;
		}
		break;
	case 0x20000:	/*  cycle counter or clock  */
		if (writeflag == MEM_WRITE) {
			debug("[ sgi_ip19: unimplemented write to address 0x%x (cycle counter), data=0x%08x ]\n", relative_addr, (int)idata);
		} else {
			d->cycle_counter += 100;
			odata = d->cycle_counter;
		}

		break;
	default:
		if (writeflag == MEM_WRITE) {
			debug("[ sgi_ip19: unimplemented write to address 0x%x, data=0x%08x ]\n", relative_addr, (int)idata);
		} else {
			debug("[ sgi_ip19: unimplemented read from address 0x%x: 0x%08x ]\n", relative_addr, (int)odata);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_sgi_ip19_init():
 */
void dev_sgi_ip19_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr)
{
	struct sgi_ip19_data *d = malloc(sizeof(struct sgi_ip19_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct sgi_ip19_data));

	memory_device_register(mem, "sgi_ip19", baseaddr, DEV_SGI_IP19_LENGTH,
	    dev_sgi_ip19_access, (void *)d, MEM_DEFAULT, NULL);
}

