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
 *  $Id: dev_scc.c,v 1.6 2004-01-24 21:13:05 debug Exp $
 *  
 *  Serial controller on some DECsystems (5000 ?).
 *
 *  TODO:  Make it do more stuff than just outputing chars :-)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "misc.h"
#include "console.h"
#include "devices.h"

#include "sccreg.h"


struct scc_data {
	int		irq_nr;
	int		use_fb;
};


/*
 *  dev_scc_access():
 *
 *  Returns 1 if ok, 0 on error.
 */
int dev_scc_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *extra)
{
	uint64_t idata = 0, odata = 0;

	idata = memory_readmax64(cpu, data, len);

	switch (relative_addr) {
	case 1:
		if (writeflag==MEM_READ) {
			/*  debug("[ scc: read from 0x%08lx ]\n", (long)relative_addr);  */
			odata = 0xffffffff;
		} else {
			/*  debug("[ scc: write to  0x%08lx: 0x%08x ]\n", (long)relative_addr, idata);  */
		}
		break;
	case 5:
		if (writeflag==MEM_READ) {
			if (console_charavail())
				odata = console_readchar();
			debug("[ scc: read from 0x%08lx: 0x%02x ]\n", (long)relative_addr, odata);
		} else {
			/*  debug("[ scc: write to  0x%08lx: 0x%08x ]\n", (long)relative_addr, idata);  */
			console_putchar(idata);
		}
		break;
	default:
		if (writeflag==MEM_READ) {
			debug("[ scc: read from 0x%08lx ]\n", (long)relative_addr);
		} else {
			debug("[ scc: write to  0x%08lx: 0x%08x ]\n", (long)relative_addr, idata);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_scc_init():
 */
void dev_scc_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr, int irq_nr, int use_fb)
{
	struct scc_data *d;

	d = malloc(sizeof(struct scc_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct scc_data));
	d->irq_nr = irq_nr;
	d->use_fb = use_fb;

	memory_device_register(mem, "scc", baseaddr, DEV_SCC_LENGTH, dev_scc_access, d);
}

