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
 *  $Id: dev_sgi_mardigras.c,v 1.1 2004-07-17 18:50:19 debug Exp $
 *  
 *  "MardiGras" graphics controller on SGI IP30 (Octane).
 *
 *  TODO
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "misc.h"

#include "memory.h"
#include "console.h"
#include "devices.h"


#define	MARDIGRAS_FAKE_OFFSET	0x500000000 /*  hopefully available for use  */
#define	MARDIGRAS_XSIZE		640
#define	MARDIGRAS_YSIZE		480

#define	MICROCODE_START		0x50000
#define	MICROCODE_END		0x55000

struct sgi_mardigras_data {
	struct vfb_data	*fb;
	unsigned char	microcode_ram[MICROCODE_END - MICROCODE_START];
};


/*
 *  dev_sgi_mardigras_access():
 */
int dev_sgi_mardigras_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	uint64_t idata = 0, odata = 0;
	struct sgi_mardigras_data *d = extra;

	idata = memory_readmax64(cpu, data, len);

	/*
	 *  Access to the microcode_ram works like ordinary ram:
	 */
	if (relative_addr >= MICROCODE_START &&
	    relative_addr <  MICROCODE_END) {
		relative_addr -= MICROCODE_START;
		if (writeflag == MEM_WRITE)
			memcpy(d->microcode_ram + relative_addr, data, len);
		else
			memcpy(data, d->microcode_ram + relative_addr, len);
		return 1;
	}

	/*  TODO  */

	if (relative_addr == 0x71208)
		odata = 8;

	if (writeflag==MEM_READ) {
		debug("[ sgi_mardigras: read from 0x%08lx ]\n", (long)relative_addr);
	} else {
		debug("[ sgi_mardigras: write to  0x%08lx: 0x%016llx ]\n", (long)relative_addr, (long long)idata);
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_sgi_mardigras_init():
 */
void dev_sgi_mardigras_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr)
{
	struct sgi_mardigras_data *d;

	d = malloc(sizeof(struct sgi_mardigras_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct sgi_mardigras_data));

	d->fb = dev_fb_init(cpu, mem, MARDIGRAS_FAKE_OFFSET,
	    VFB_GENERIC, MARDIGRAS_XSIZE, MARDIGRAS_YSIZE,
	    MARDIGRAS_XSIZE, MARDIGRAS_YSIZE, 24, "SGI MardiGras");
	if (d->fb == NULL) {
		fprintf(stderr, "dev_sgi_mardigras_init(): out of memory\n");
		exit(1);
	}

	memory_device_register(mem, "sgi_mardigras", baseaddr,
	    DEV_SGI_MARDIGRAS_LENGTH, dev_sgi_mardigras_access, d);
}

