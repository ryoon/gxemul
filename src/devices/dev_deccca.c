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
 *  $Id: dev_deccca.c,v 1.2 2004-06-22 22:24:25 debug Exp $
 *  
 *  "Console Communication Area" for a DEC 5800 SMP system.
 *
 *  TODO:  This hardware is not very easy to find docs about.
 *  Perhaps VAX 6000/300 docs?
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "misc.h"
#include "console.h"
#include "devices.h"


extern int ncpus;

struct deccca_data {
	int		dummy;
};


/*
 *  dev_deccca_access():
 *
 *  Returns 1 if ok, 0 on error.
 */
int dev_deccca_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *extra)
{
	uint64_t idata = 0, odata = 0;
	/*  struct deccca_data *d = extra;  */

	idata = memory_readmax64(cpu, data, len);

	switch (relative_addr) {
	case 6:
	case 7:
		/*  CCA "ID" bytes? These must be here, or Ultrix complains.  */
		if (writeflag == MEM_READ)
			odata = 67;
		break;
	case 8:
		if (writeflag == MEM_READ)
			odata = ncpus;
		break;
	case 20:
		if (writeflag == MEM_READ)
			odata = (1 << ncpus) - 1;	/*  one bit for each cpu  */
		break;
	case 28:
		if (writeflag == MEM_READ)
			odata = (1 << ncpus) - 1;	/*  one bit for each enabled(?) cpu  */
		break;
	default:
		if (writeflag==MEM_READ) {
			debug("[ deccca: read from 0x%08lx ]\n", (long)relative_addr);
		} else {
			debug("[ deccca: write to  0x%08lx: 0x%08x ]\n", (long)relative_addr, idata);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_deccca_init():
 */
void dev_deccca_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr)
{
	struct deccca_data *d;

	d = malloc(sizeof(struct deccca_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct deccca_data));

	memory_device_register(mem, "deccca", baseaddr, DEV_DECCCA_LENGTH, dev_deccca_access, d);
}

