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
 *  $Id: dev_vga.c,v 1.4 2004-08-03 19:07:30 debug Exp $
 *  
 *  VGA text console device.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "misc.h"
#include "devices.h"

#include "fonts/font8x16.c"


#define	MAX_X		80
#define	MAX_Y		25

#define	VGA_FB_ADDR	0x123000000000ULL


struct vga_data {
	uint64_t	videomem_base;
	uint64_t	control_base;

	struct vfb_data *fb;

	unsigned char	videomem[MAX_X * MAX_Y * 2];

	unsigned char	selected_register;
	unsigned char	reg[256];
};


/*
 *  vga_update():
 */
void vga_update(struct cpu *cpu, struct vga_data *d, int start, int end)
{
	int fg, bg, i, x,y, subx, line;

	start &= ~1;
	end |= 1;

	for (i=start; i<=end; i+=2) {
		unsigned char ch = d->videomem[i];
		fg = d->videomem[i+1] & 15;
		bg = (d->videomem[i+1] >> 4) & 7;

		x = (i/2) % MAX_X; x *= 8;
		y = (i/2) / MAX_X; y *= 16;

		for (line = 0; line < 16; line++) {
			for (subx = 0; subx < 8; subx++) {
				unsigned char pixel[3];
				int addr = (MAX_X*8 * (line+y) + x + subx) * 3;

				pixel[0] = d->fb->rgb_palette[bg * 3 + 0];
				pixel[1] = d->fb->rgb_palette[bg * 3 + 1];
				pixel[2] = d->fb->rgb_palette[bg * 3 + 2];

				if (font8x16[ch * 16 + line] & (128 >> subx)) {
					pixel[0] = d->fb->rgb_palette[fg * 3 + 0];
					pixel[1] = d->fb->rgb_palette[fg * 3 + 1];
					pixel[2] = d->fb->rgb_palette[fg * 3 + 2];
				}

				dev_fb_access(cpu, cpu->mem, addr, &pixel[0], sizeof(pixel), MEM_WRITE, d->fb);
			}
		}
	}
}


/*
 *  dev_vga_access():
 */
int dev_vga_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr,
	unsigned char *data, size_t len, int writeflag, void *extra)
{
	struct vga_data *d = extra;
	uint64_t idata = 0, odata = 0;

	idata = memory_readmax64(cpu, data, len);

	if (relative_addr < sizeof(d->videomem)) {
		if (writeflag == MEM_WRITE) {
			memcpy(d->videomem + relative_addr, data, len);
			vga_update(cpu, d, relative_addr, relative_addr + len-1);
		} else
			memcpy(data, d->videomem + relative_addr, len);
		return 1;
	}

	switch (relative_addr) {
	default:
		if (writeflag==MEM_READ) {
			debug("[ vga: read from 0x%08lx ]\n", (long)relative_addr);
		} else {
			debug("[ vga: write to  0x%08lx: 0x%08x ]\n", (long)relative_addr, idata);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  vga_reg_write():
 */
void vga_reg_write(struct vga_data *d, int regnr, int idata)
{
	debug("[ vga_reg_write: regnr=0x%02x idata=0x%02x ]\n", regnr, idata);
}


/*
 *  dev_vga_ctrl_access():
 */
int dev_vga_ctrl_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr,
	unsigned char *data, size_t len, int writeflag, void *extra)
{
	struct vga_data *d = extra;
	uint64_t idata = 0, odata = 0;

	idata = memory_readmax64(cpu, data, len);

	switch (relative_addr) {
	case 4:	/*  register select  */
		if (writeflag == MEM_READ)
			odata = d->selected_register;
		else
			d->selected_register = idata;
		break;
	case 5:	if (writeflag == MEM_READ)
			odata = d->reg[d->selected_register];
		else {
			d->reg[d->selected_register] = idata;
			vga_reg_write(d, d->selected_register, idata);
		}
		break;
	default:
		if (writeflag==MEM_READ) {
			debug("[ vga_ctrl: read from 0x%08lx ]\n", (long)relative_addr);
		} else {
			debug("[ vga_ctrl: write to  0x%08lx: 0x%08x ]\n", (long)relative_addr, idata);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_vga_init():
 */
void dev_vga_init(struct cpu *cpu, struct memory *mem, uint64_t videomem_base,
	uint64_t control_base)
{
	struct vga_data *d;
	int r,g,b,i;

	d = malloc(sizeof(struct vga_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct vga_data));
	d->videomem_base = videomem_base;
	d->control_base  = control_base;

	d->fb = dev_fb_init(cpu, mem, VGA_FB_ADDR, VFB_GENERIC,
	    8*MAX_X, 16*MAX_Y, 8*MAX_X, 16*MAX_Y, 24, "VGA");

	i = 0;
	for (r=0; r<2; r++)
		for (g=0; g<2; g++)
			for (b=0; b<2; b++) {
				d->fb->rgb_palette[i + 0] = r * 0x7f;
				d->fb->rgb_palette[i + 1] = g * 0x7f;
				d->fb->rgb_palette[i + 2] = b * 0x7f;
				i+=3;
			}
	for (r=0; r<2; r++)
		for (g=0; g<2; g++)
			for (b=0; b<2; b++) {
				d->fb->rgb_palette[i + 0] = r * 0x7f + 0x80;
				d->fb->rgb_palette[i + 1] = g * 0x7f + 0x80;
				d->fb->rgb_palette[i + 2] = b * 0x7f + 0x80;
				i+=3;
			}

	memory_device_register(mem, "vga_mem", videomem_base, sizeof(d->videomem),
	    dev_vga_access, d);
	memory_device_register(mem, "vga_ctrl", control_base, 16,
	    dev_vga_ctrl_access, d);
}

