/*
 *  Copyright (C) 2003 by Anders Gavare.  All rights reserved.
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
 */

/*
 *  dev_le.c  --  LANCE ethernet
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "misc.h"


#define	N_REGISTERS	(256 / 0x4)

struct le_data {
	int		irq_nr;
	uint64_t	buf_start;
	uint64_t	buf_end;

	int		register_choice;
	uint32_t	reg[N_REGISTERS];
};


/*
 *  dev_le_access():
 *
 *  Returns 1 if ok, 0 on error.
 */
int dev_le_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *extra)
{
	int regnr, i;
	int idata = 0, odata=0, odata_set=0;
	struct le_data *d = extra;

	/*  Switch byte order for incoming data, if neccessary:  */
	if (cpu->byte_order == EMUL_BIG_ENDIAN)
		for (i=0; i<len; i++) {
			idata <<= 8;
			idata |= data[i];
		}
	else
		for (i=len-1; i>=0; i--) {
			idata <<= 8;
			idata |= data[i];
		}

	if (relative_addr & 3) {
		debug("[ le relative_addr = 0x%x !!! ]\n",
		    (int) relative_addr);
		return 0;
	}

	regnr = relative_addr / 4;

	switch (relative_addr) {
	default:
		if (writeflag==MEM_READ) {
			debug("[ le read from %08lx ]\n", (long)relative_addr);
			odata_set = 1;
			odata = d->reg[regnr];
		} else {
			debug("[ le write to %08lx:", (long)relative_addr);
			for (i=0; i<len; i++)
				debug(" %02x", data[i]);
			debug(" ]\n");
			d->reg[regnr] = idata;
			return 1;
		}
	}

/*  odata = random() & 0xffff;  */

	if (odata_set) {
		if (cpu->byte_order == EMUL_LITTLE_ENDIAN) {
			for (i=0; i<len; i++)
				data[i] = (odata >> (i*8)) & 255;
		} else {
			for (i=0; i<len; i++)
				data[len - 1 - i] = (odata >> (i*8)) & 255;
		}
		return 1;
	}

	return 0;
}


/*
 *  dev_le_init():
 */
void dev_le_init(struct memory *mem, uint64_t baseaddr, uint64_t buf_start, uint64_t buf_end, int irq_nr)
{
	struct le_data *d = malloc(sizeof(struct le_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}

	memset(d, 0, sizeof(struct le_data));
	d->irq_nr    = irq_nr;
	d->buf_start = buf_start;
	d->buf_end   = buf_end;

	memory_device_register(mem, "le", baseaddr, DEV_LE_LENGTH, dev_le_access, (void *)d);
}

