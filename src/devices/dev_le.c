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
 *  $Id: dev_le.c,v 1.6 2004-02-23 23:11:45 debug Exp $
 *  
 *  LANCE ethernet.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "misc.h"
#include "devices.h"


#define	N_REGISTERS	(256 / 0x4)

struct le_data {
	int		irq_nr;
	uint64_t	buf_start;
	uint64_t	buf_end;
	int		len;

	int		register_choice;
	uint32_t	reg[N_REGISTERS];
	uint32_t	csr;
};


/*
 *  dev_le_access():
 *
 *  Returns 1 if ok, 0 on error.
 */
int dev_le_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *extra)
{
	uint64_t idata = 0, odata = 0;
	int regnr, i;
	struct le_data *d = extra;

	idata = memory_readmax64(cpu, data, len);

	if (relative_addr >= 0x1c0000 && relative_addr <= 0x1c0017) {
		/*  Read from station's ROM ethernet address:  */
		int i = (relative_addr & 0xff) / 4;

		if (writeflag==MEM_READ)
			odata = i;		/*  TODO: actual address  */
		else {
			debug("[ le write to station's ethernet address (%08lx):", (long)relative_addr);
			for (i=0; i<len; i++)
				debug(" %02x", data[i]);
			debug(" ]\n");
		}
	} else {
		switch (relative_addr) {
		case 0x100000:
			if (writeflag==MEM_READ) {
				odata = d->csr;
				debug("[ le read from %08lx: 0x%llx ]\n", (long)relative_addr, (long long)odata);
			} else {
				debug("[ le write to %08lx: 0x%llx ]\n", (long)relative_addr, (long long)idata);
				d->csr = idata;
			}
			break;
		default:
			regnr = relative_addr / 4;
			if (relative_addr & 3) {
				debug("[ le relative_addr = 0x%x !!! ]\n",
				    (int) relative_addr);
				return 0;
			}

			if (writeflag==MEM_READ) {
				debug("[ le read from %08lx ]\n", (long)relative_addr);
				if (regnr < N_REGISTERS)
					odata = d->reg[regnr];
			} else {
				debug("[ le write to %08lx:", (long)relative_addr);
				for (i=0; i<len; i++)
					debug(" %02x", data[i]);
				debug(" ]\n");
				if (regnr < N_REGISTERS)
					d->reg[regnr] = idata;
				return 1;
			}
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_le_init():
 */
void dev_le_init(struct memory *mem, uint64_t baseaddr, uint64_t buf_start, uint64_t buf_end, int irq_nr, int len)
{
	struct le_data *d = malloc(sizeof(struct le_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}

	memset(d, 0, sizeof(struct le_data));
	d->irq_nr    = irq_nr;
	d->len       = len;
	d->buf_start = buf_start;
	d->buf_end   = buf_end;

	memory_device_register(mem, "le", baseaddr, len, dev_le_access, (void *)d);
}

