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
 *   
 *
 *  $Id: dev_ps2_dmac.c,v 1.2 2003-11-06 13:56:07 debug Exp $
 *  
 *  Playstation 2 DMA controller.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "misc.h"
#include "ps2_dmacreg.h"


#define	N_DMA_CHANNELS		10

struct dmac_data {
	uint64_t	reg[DMAC_REGSIZE / 0x10];

	struct memory	*other_memory[N_DMA_CHANNELS];
};


/*
 *  dev_ps2_dmac_access():
 *
 *  Returns 1 if ok, 0 on error.
 */
int dev_ps2_dmac_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *extra)
{
	int i, regnr;
	uint64_t idata = 0, odata=0, odata_set=0;
	struct dmac_data *d = extra;

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

	regnr = relative_addr / 16;
	if (relative_addr & 0xf) {
		debug("[ ps2_dmac unaligned access, addr 0x%x ]\n", (int)relative_addr);
		return 0;
	}

	switch (relative_addr) {
	case D2_CHCR_REG:
		if (writeflag==MEM_READ) {
			/*  debug("[ ps2_dmac read from D2_CHCR (0x%llx) ]\n", (long long)d->reg[regnr]);  */
			odata = d->reg[regnr];
			odata_set = 1;
		} else {
			/*  debug("[ ps2_dmac write to D2_CHCR, data 0x%016llx ]\n", (long long) idata);  */
			if (idata & D_CHCR_STR) {
				int length = d->reg[D2_QWC_REG/0x10] * 16;
				uint64_t from_addr = 0xa0000000 + d->reg[D2_MADR_REG/0x10];
				uint64_t to_addr   = 0xa0000000 + d->reg[D2_TADR_REG/0x10];
				unsigned char *copy_buf;

				debug("[ ps2_dmac: [ch2] transfer addr=0x%016llx len=0x%lx ]\n",
				    (long long)d->reg[D2_MADR_REG/0x10], (long)length);

				copy_buf = malloc(length);
				memory_rw(cpu, cpu->mem, from_addr, copy_buf, length, MEM_READ, CACHE_NONE);
				memory_rw(cpu, d->other_memory[2], to_addr, copy_buf, length, MEM_WRITE, CACHE_NONE);
				free(copy_buf);

				/*  Done with the transfer:  */
				d->reg[D2_QWC_REG/0x10] = 0;
				idata &= ~D_CHCR_STR;
			} else
				debug("[ ps2_dmac: [ch2] stopping transfer ]\n");
			d->reg[regnr] = idata;
			return 1;
		}
		break;
	default:
		if (writeflag==MEM_READ) {
/*			debug("[ ps2_dmac read from addr 0x%x ]\n", (int)relative_addr); */
			odata = d->reg[regnr];
			odata_set = 1;
		} else {
/*			debug("[ ps2_dmac write to addr 0x%x:", (int)relative_addr);
			for (i=0; i<len; i++)
				debug(" %02x", data[i]);
			debug(" ]\n"); */
			d->reg[regnr] = idata;
			return 1;
		}
	}

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
 *  dev_ps2_dmac_init():
 *
 *	mem_gif			pointer to the GIF's memory
 */
void dev_ps2_dmac_init(struct memory *mem, uint64_t baseaddr, struct memory *mem_gif)
{
	struct dmac_data *d;

	d = malloc(sizeof(struct dmac_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct dmac_data));

	d->other_memory[DMA_CH_GIF] = mem_gif;

	memory_device_register(mem, "ps2_dmac", baseaddr, DEV_PS2_DMAC_LENGTH, dev_ps2_dmac_access, d);
}

