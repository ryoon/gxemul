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
 *  dev_bt459.c  --  Brooktree 459 vdac, used by TURBOchannel graphics cards
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "misc.h"
#include "bt459.h"


struct bt459_data {
	uint32_t	bt459_reg[DEV_BT459_NREGS];

	unsigned char	cur_addr_hi;
	unsigned char	cur_addr_lo;

	int		planes;

	unsigned char	*rgb_palette;		/*  ptr to 256 * 3 (r,g,b)  */
};


/*
 *  dev_bt459_access():
 *
 *  Returns 1 if ok, 0 on error.
 */
int dev_bt459_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *extra)
{
	struct bt459_data *d = (struct bt459_data *) extra;
	int btaddr;
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

	/*  ID register is read-only, should always be 0x4a or 0x4a4a4a:  */
	if (d->planes == 24)
		d->bt459_reg[BT459_REG_ID] = 0x4a4a4a;
	else
		d->bt459_reg[BT459_REG_ID] = 0x4a;

	btaddr = ((d->cur_addr_hi << 8) + d->cur_addr_lo) % DEV_BT459_NREGS;

	/*  Read from/write to the bt459:  */
	switch (relative_addr) {
	case 0x00:		/*  Low byte of address:  */
		if (writeflag == MEM_WRITE) {
			debug("[ bt459: write to Low Address Byte, 0x%02x ]\n", idata);
			d->cur_addr_lo = idata;
		} else {
			odata = d->cur_addr_lo;
			odata_set = 1;
			debug("[ bt459: read from Low Address Byte: 0x%0x ]\n", odata);
		}
		break;
	case 0x04:		/*  High byte of address:  */
		if (writeflag == MEM_WRITE) {
			debug("[ bt459: write to High Address Byte, 0x%02x ]\n", idata);
			d->cur_addr_hi = idata;
		} else {
			odata = d->cur_addr_hi;
			odata_set = 1;
			debug("[ bt459: read from High Address Byte: 0x%0x ]\n", odata);
		}
		break;
	case 0x08:		/*  Register access:  */
		if (writeflag == MEM_WRITE) {
			debug("[ bt459: write to BT459 register 0x%04x, value 0x%02x ]\n", btaddr, idata);
			d->bt459_reg[btaddr] = idata;
		} else {
			odata = d->bt459_reg[btaddr];
			odata_set = 1;
			debug("[ bt459: read from BT459 register 0x%04x, value 0x%02x ]\n", btaddr, odata);
		}
		break;
	default:
		if (writeflag == MEM_WRITE) {
			debug("[ bt459: unimplemented write to address 0x%x, data=0x%02x ]\n", relative_addr, idata);
		} else {
			debug("[ bt459: unimplemented read from address 0x%x ]\n", relative_addr);
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
 *  dev_bt459_init():
 */
void dev_bt459_init(struct memory *mem, uint64_t baseaddr, unsigned char *rgb_palette, int planes)
{
	struct bt459_data *d = malloc(sizeof(struct bt459_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct bt459_data));
	d->rgb_palette  = rgb_palette;
	d->planes       = planes;

	memory_device_register(mem, "bt459", baseaddr, DEV_BT459_LENGTH, dev_bt459_access, (void *)d);
}

