/*
 *  Copyright (C) 2003-2004  Anders Gavare.  All rights reserved.
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
 *  $Id: dev_vdac.c,v 1.8 2004-12-18 06:01:15 debug Exp $
 *  
 *  Color map used by DECstation 3100.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "misc.h"
#include "devices.h"


struct vdac_data {
	unsigned char	vdac_reg[DEV_VDAC_LENGTH];

	int		color_fb_flag;

	unsigned char	cur_read_addr;
	unsigned char	cur_write_addr;

	int		sub_color;		/*  subcolor to be written next. 0, 1, or 2  */
	unsigned char	cur_rgb[3];

	unsigned char	*rgb_palette;		/*  ptr to 256 * 3 (r,g,b)  */

	unsigned char	cur_read_addr_overlay;
	unsigned char	cur_write_addr_overlay;

	int		sub_color_overlay;	/*  subcolor to be written next. 0, 1, or 2  */
	unsigned char	cur_rgb_overlay[3];

	unsigned char	rgb_palette_overlay[16 * 3];	/*  16 * 3 (r,g,b)  */
};


/*
 *  dev_vdac_access():
 */
int dev_vdac_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	struct vdac_data *d = (struct vdac_data *) extra;

	/*  Read from/write to the vdac:  */
	switch (relative_addr) {
	case DEV_VDAC_MAPWA:
		if (writeflag == MEM_WRITE) {
			d->cur_write_addr = data[0];
			d->sub_color = 0;
		} else {
			debug("[ vdac: read from MAPWA ]\n");
			data[0] = d->vdac_reg[relative_addr];
		}
		break;
	case DEV_VDAC_MAP:
		if (writeflag == MEM_WRITE) {
			d->cur_rgb[d->sub_color] = data[0];
			d->sub_color++;

			if (d->sub_color > 2) {
				/*  (Only update for color, not mono mode)  */
				if (d->color_fb_flag)
					memcpy(d->rgb_palette + 3*d->cur_write_addr, d->cur_rgb, 3);

				d->sub_color = 0;
				d->cur_write_addr ++;
			}
		} else {
			if (d->sub_color == 0) {
				memcpy(d->cur_rgb, d->rgb_palette + 3*d->cur_read_addr, 3);
			}
			data[0] = d->cur_rgb[d->sub_color];
			d->sub_color++;
			if (d->sub_color > 2) {
				d->sub_color = 0;
				d->cur_read_addr ++;
			}
		}
		break;
	case DEV_VDAC_MAPRA:
		if (writeflag == MEM_WRITE) {
			d->cur_read_addr = data[0];
			d->sub_color = 0;
		} else {
			debug("[ vdac: read from MAPRA ]\n");
			data[0] = d->vdac_reg[relative_addr];
		}
		break;
	case DEV_VDAC_OVERWA:
		if (writeflag == MEM_WRITE) {
			d->cur_write_addr_overlay = data[0];
			d->sub_color_overlay = 0;
		} else {
			debug("[ vdac: read from OVERWA ]\n");
			data[0] = d->vdac_reg[relative_addr];
		}
		break;
	case DEV_VDAC_OVER:
		if (writeflag == MEM_WRITE) {
			d->cur_rgb_overlay[d->sub_color_overlay] = data[0];
			d->sub_color_overlay++;

			if (d->sub_color_overlay > 2) {
				/*  (Only update for color, not mono mode)  */
				if (d->color_fb_flag)
					memcpy(d->rgb_palette_overlay + 3*d->cur_write_addr_overlay, d->cur_rgb_overlay, 3);

				d->sub_color_overlay = 0;
				d->cur_write_addr_overlay ++;
				if (d->cur_write_addr_overlay > 15)
					d->cur_write_addr_overlay = 0;
			}
		} else {
			if (d->sub_color_overlay == 0) {
				memcpy(d->cur_rgb_overlay, d->rgb_palette_overlay + 3*d->cur_read_addr_overlay, 3);
			}
			data[0] = d->cur_rgb_overlay[d->sub_color_overlay];
			d->sub_color_overlay++;
			if (d->sub_color_overlay > 2) {
				d->sub_color_overlay = 0;
				d->cur_read_addr_overlay ++;
				if (d->cur_read_addr_overlay > 15)
					d->cur_read_addr_overlay = 0;
			}
		}
		break;
	case DEV_VDAC_OVERRA:
		if (writeflag == MEM_WRITE) {
			d->cur_read_addr_overlay = data[0];
			d->sub_color_overlay = 0;
		} else {
			debug("[ vdac: read from OVERRA ]\n");
			data[0] = d->vdac_reg[relative_addr];
		}
		break;
	default:
		if (writeflag == MEM_WRITE) {
			debug("[ vdac: unimplemented write to address 0x%x, data=0x%02x ]\n", relative_addr, data[0]);
			d->vdac_reg[relative_addr] = data[0];
		} else {
			debug("[ vdac: unimplemented read from address 0x%x ]\n", relative_addr);
			data[0] = d->vdac_reg[relative_addr];
		}
	}

	/*  Pretend it was ok:  */
	return 1;
}


/*
 *  dev_vdac_init():
 */
void dev_vdac_init(struct memory *mem, uint64_t baseaddr,
	unsigned char *rgb_palette, int color_fb_flag)
{
	struct vdac_data *d = malloc(sizeof(struct vdac_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct vdac_data));
	d->rgb_palette   = rgb_palette;
	d->color_fb_flag = color_fb_flag;

	memory_device_register(mem, "vdac", baseaddr, DEV_VDAC_LENGTH,
	    dev_vdac_access, (void *)d, MEM_DEFAULT, NULL);
}

