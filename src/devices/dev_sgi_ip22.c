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
 *  $Id: dev_sgi_ip22.c,v 1.1 2004-01-05 03:27:42 debug Exp $
 *  
 *  SGI IP22 timer stuff.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "misc.h"
#include "devices.h"


struct sgi_ip22_data {
	uint32_t	reg[DEV_SGI_IP22_LENGTH / 4];
};


/*
 *  dev_sgi_ip22_tick():
 */
void dev_sgi_ip22_tick(struct cpu *cpu, void *extra)
{
}


/*
 *  dev_sgi_ip22_access():
 *
 *  Returns 1 if ok, 0 on error.
 */
int dev_sgi_ip22_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *extra)
{
	struct sgi_ip22_data *d = (struct sgi_ip22_data *) extra;
	int regnr;
	int idata = 0, odata=0, odata_set=0, i;

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

	regnr = relative_addr / sizeof(uint32_t);

	if (writeflag == MEM_WRITE) {
		d->reg[regnr] = idata;
	} else {
		odata_set = 1;
		odata = d->reg[regnr];
	}

	/*  Read from/write to the sgi_ip22:  */
	switch (relative_addr) {
	case 0x38:	/*  timer count  */
		/*  Two byte values are written to this address, sequentially...  TODO  */
		if (writeflag == MEM_WRITE) {
		} else {
			if (d->reg[regnr] > 0 && (random() & 0xff) == 0)
				d->reg[regnr]--;	/*  Actually, the tick function should count down this to zero...  */
		}
		break;
	case 0x3c:	/*  timer control  */
		if (writeflag == MEM_WRITE) {
		} else {
			odata_set = 1;
		}
		break;
	default:
		if (writeflag == MEM_WRITE) {
			debug("[ sgi_ip22: unimplemented write to address 0x%x, data=0x%02x ]\n", relative_addr, idata);
		} else {
			debug("[ sgi_ip22: unimplemented read from address 0x%x ]\n", relative_addr);
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
	}

	return 1;
}


/*
 *  dev_sgi_ip22_init():
 */
void dev_sgi_ip22_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr)
{
	struct sgi_ip22_data *d = malloc(sizeof(struct sgi_ip22_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct sgi_ip22_data));

	memory_device_register(mem, "sgi_ip22", baseaddr, DEV_SGI_IP22_LENGTH, dev_sgi_ip22_access, (void *)d);
	cpu_add_tickfunction(cpu, dev_sgi_ip22_tick, d, 10);
}

