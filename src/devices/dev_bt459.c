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
 *  $Id: dev_bt459.c,v 1.6 2004-03-08 03:22:29 debug Exp $
 *  
 *  Brooktree 459 vdac, used by TURBOchannel graphics cards.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "misc.h"
#include "devices.h"

#include "bt459.h"


struct bt459_data {
	uint32_t	bt459_reg[DEV_BT459_NREGS];

	unsigned char	cur_addr_hi;
	unsigned char	cur_addr_lo;

	int		planes;
	int		cursor_x;
	int		cursor_y;

	struct vfb_data *vfb_data;
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
	uint64_t idata = 0, odata = 0;
	int btaddr, new_cursor_x, new_cursor_y;

	idata = memory_readmax64(cpu, data, len);

	/*  ID register is read-only, should always be 0x4a or 0x4a4a4a:  */
	if (d->planes == 24)
		d->bt459_reg[BT459_REG_ID] = 0x4a4a4a;
	else {
		/*
		 *  TODO:  Is it really 0x4a, or 0x4a0000?
		 *  Ultrix panics with a "bad VDAC ID" message if 0x4a is returned.
		 */
		d->bt459_reg[BT459_REG_ID] = 0x4a0000;
	}

	btaddr = ((d->cur_addr_hi << 8) + d->cur_addr_lo) % DEV_BT459_NREGS;

	/*  Read from/write to the bt459:  */
	switch (relative_addr) {
	case 0x00:		/*  Low byte of address:  */
		if (writeflag == MEM_WRITE) {
			debug("[ bt459: write to Low Address Byte, 0x%02x ]\n", idata);
			d->cur_addr_lo = idata;
		} else {
			odata = d->cur_addr_lo;
			debug("[ bt459: read from Low Address Byte: 0x%0x ]\n", odata);
		}
		break;
	case 0x04:		/*  High byte of address:  */
		if (writeflag == MEM_WRITE) {
			debug("[ bt459: write to High Address Byte, 0x%02x ]\n", idata);
			d->cur_addr_hi = idata;
		} else {
			odata = d->cur_addr_hi;
			debug("[ bt459: read from High Address Byte: 0x%0x ]\n", odata);
		}
		break;
	case 0x08:		/*  Register access:  */
		if (writeflag == MEM_WRITE) {
			debug("[ bt459: write to BT459 register 0x%04x, value 0x%02x ]\n", btaddr, idata);
			d->bt459_reg[btaddr] = idata;
		} else {
			odata = d->bt459_reg[btaddr];
			debug("[ bt459: read from BT459 register 0x%04x, value 0x%02x ]\n", btaddr, odata);
		}

		/*  Go to next register:  */
		d->cur_addr_lo ++;
		if (d->cur_addr_lo == 0)
			d->cur_addr_hi ++;
		break;
	default:
		if (writeflag == MEM_WRITE) {
			debug("[ bt459: unimplemented write to address 0x%x, data=0x%02x ]\n", relative_addr, idata);
		} else {
			debug("[ bt459: unimplemented read from address 0x%x ]\n", relative_addr);
		}
	}

	/*  The magic 370,37 values are from a NetBSD source code comment.  :-)  */
	new_cursor_x = (d->bt459_reg[BT459_REG_CXLO] & 255) + ((d->bt459_reg[BT459_REG_CXHI] & 255) << 8) - 370;
	new_cursor_y = (d->bt459_reg[BT459_REG_CYLO] & 255) + ((d->bt459_reg[BT459_REG_CYHI] & 255) << 8) - 37;

	if (new_cursor_x != d->cursor_x || new_cursor_y != d->cursor_y) {
		/*  TODO: what do the bits in the CCR do?  */
		int on = d->bt459_reg[BT459_REG_CCR] ? 1 : 0;
		d->cursor_x = new_cursor_x;
		d->cursor_y = new_cursor_y;
on = 0;
		debug("[ bt459: cursor = %03i,%03i ]\n", d->cursor_x, d->cursor_y);
		dev_fb_setcursor(d->vfb_data, d->cursor_x, d->cursor_y, on, 10, 22);
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_bt459_init():
 */
void dev_bt459_init(struct memory *mem, uint64_t baseaddr, struct vfb_data *vfb_data, int planes)
{
	struct bt459_data *d = malloc(sizeof(struct bt459_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct bt459_data));
	d->vfb_data     = vfb_data;
	d->rgb_palette  = vfb_data->rgb_palette;
	d->planes       = planes;
	d->cursor_x     = -1;
	d->cursor_y     = -1;

	memory_device_register(mem, "bt459", baseaddr, DEV_BT459_LENGTH, dev_bt459_access, (void *)d);
}

