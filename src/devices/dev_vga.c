/*
 *  Copyright (C) 2004  Anders Gavare.  All rights reserved.
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
 *  $Id: dev_vga.c,v 1.14 2004-12-03 22:44:23 debug Exp $
 *  
 *  VGA text console device.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "misc.h"
#include "devices.h"

#include "fonts/font8x8.c"
#include "fonts/font8x16.c"


#define	VGA_MEM_MAXY		60
#define	VGA_MEM_ALLOCY		67


#define	VGA_FB_ADDR	0x123000000000ULL

struct vga_data {
	uint64_t	videomem_base;
	uint64_t	control_base;

	struct vfb_data *fb;

	int		use_8x8_font;

	int		max_x;
	int		max_y;
	size_t		videomem_size;
	unsigned char	*videomem;	/*  2 bytes per char  */

	unsigned char	selected_register;
	unsigned char	reg[256];

	int		cursor_x;
	int		cursor_y;
};


/*
 *  vga_update():
 *
 *  This function should be called whenever any part of d->videomem[] has
 *  been written to. It will redraw all characters within the range start..end
 *  using the right palette.
 */
void vga_update(struct cpu *cpu, struct vga_data *d, int start, int end)
{
	int fg, bg, i, x,y, subx, line;
	int font_height = d->use_8x8_font? 8 : 16;

	start &= ~1;
	end |= 1;

	for (i=start; i<=end; i+=2) {
		unsigned char ch = d->videomem[i];
		fg = d->videomem[i+1] & 15;
		bg = (d->videomem[i+1] >> 4) & 7;

		/*  Blink is hard to do :-), but inversion might be ok too:  */
		if (d->videomem[i+1] & 128) {
			int tmp = fg; fg = bg; bg = tmp;
		}

		x = (i/2) % d->max_x; x *= 8;
		y = (i/2) / d->max_x; y *= font_height;

		for (line = 0; line < font_height; line++) {
			for (subx = 0; subx < 8; subx++) {
				unsigned char pixel[3];
				unsigned char *font =
				    d->use_8x8_font? font8x8 : font8x16;

				int addr = (d->max_x*8 * (line+y) + x + subx)
				    * 3;

				pixel[0] = d->fb->rgb_palette[bg * 3 + 0];
				pixel[1] = d->fb->rgb_palette[bg * 3 + 1];
				pixel[2] = d->fb->rgb_palette[bg * 3 + 2];

				if (font[ch * font_height + line] &
				    (128 >> subx)) {
					pixel[0] = d->fb->rgb_palette
					    [fg * 3 + 0];
					pixel[1] = d->fb->rgb_palette
					    [fg * 3 + 1];
					pixel[2] = d->fb->rgb_palette
					    [fg * 3 + 2];
				}

				dev_fb_access(cpu, cpu->mem, addr, &pixel[0],
				    sizeof(pixel), MEM_WRITE, d->fb);
			}
		}
	}
}


/*
 *  dev_vga_access():
 *
 *  Reads and writes to the VGA video memory.
 */
int dev_vga_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr,
	unsigned char *data, size_t len, int writeflag, void *extra)
{
	struct vga_data *d = extra;
	uint64_t idata = 0, odata = 0;
	int modified, i;
	int y;

	idata = memory_readmax64(cpu, data, len);

	y = relative_addr / (d->max_x * 2);
	if (y >= d->max_y && !d->use_8x8_font) {
		/*  Switch to 8x8 font:  */
		fatal("SWITCHING to 8x8 font\n");

		d->use_8x8_font = 1;

		d->max_y = VGA_MEM_MAXY;
		vga_update(cpu, d, 0, d->max_x * d->max_y * 2 -1);
	}

	if (relative_addr < d->videomem_size) {
		if (writeflag == MEM_WRITE) {
			modified = 0;
			for (i=0; i<len; i++) {
				int old = d->videomem[relative_addr + i];
				if (old != data[i]) {
					d->videomem[relative_addr + i] =
					    data[i];
					modified = 1;
				}
			}
			if (modified)
				vga_update(cpu, d, relative_addr,
				    relative_addr + len-1);
		} else
			memcpy(data, d->videomem + relative_addr, len);
		return 1;
	}

	switch (relative_addr) {
	default:
		if (writeflag==MEM_READ) {
			debug("[ vga: read from 0x%08lx ]\n",
			    (long)relative_addr);
		} else {
			debug("[ vga: write to  0x%08lx: 0x%08x ]\n",
			    (long)relative_addr, idata);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  vga_reg_write():
 *
 *  Writes to VGA control registers.
 */
void vga_reg_write(struct vga_data *d, int regnr, int idata)
{
	debug("[ vga_reg_write: regnr=0x%02x idata=0x%02x ]\n", regnr, idata);

	if (regnr == 0xe || regnr == 0xf) {
		int ofs = d->reg[0x0e] * 256 + d->reg[0x0f];
		d->cursor_x = ofs % d->max_x;
		d->cursor_y = ofs / d->max_x;

		/*  TODO: Don't hardcode the cursor size.  */

		/*  Block:  */
		/*  dev_fb_setcursor(d->fb,
		    d->cursor_x * 8, d->cursor_y * 16, 1, 8, 16);  */
		/*  Line:  */
		dev_fb_setcursor(d->fb,
		    d->cursor_x * 8, d->cursor_y * 16 + 12, 1, 8, 3);
	}
}


/*
 *  dev_vga_ctrl_access():
 *
 *  Reads and writes of the VGA control registers.
 */
int dev_vga_ctrl_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
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
			debug("[ vga_ctrl: read from 0x%08lx ]\n",
			    (long)relative_addr);
		} else {
			debug("[ vga_ctrl: write to  0x%08lx: 0x%08x ]\n",
			    (long)relative_addr, idata);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_vga_init():
 *
 *  Register a VGA text console device. max_x and max_y could be something
 *  like 80 and 25, respectively.
 */
void dev_vga_init(struct cpu *cpu, struct memory *mem, uint64_t videomem_base,
	uint64_t control_base, int max_x, int max_y)
{
	struct vga_data *d;
	int r,g,b,i, x,y;

	d = malloc(sizeof(struct vga_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct vga_data));
	d->videomem_base = videomem_base;
	d->control_base  = control_base;
	d->max_x         = max_x;
	d->max_y         = max_y;
	d->videomem_size = max_x * VGA_MEM_MAXY * 2;
	d->videomem = malloc(d->videomem_size);
	if (d->videomem == NULL) {
		fprintf(stderr, "out of memory in dev_vga_init()\n");
		exit(1);
	}

	for (y=0; y<VGA_MEM_MAXY; y++)
		for (x=0; x<max_x; x++) {
			i = (x + max_x * y) * 2;
			d->videomem[i] = ' ';
			d->videomem[i+1] = 0x07;	/*  Default color  */
		}

	d->use_8x8_font = 0;
	d->fb = dev_fb_init(cpu, mem, VGA_FB_ADDR, VFB_GENERIC,
	    8*max_x, 16*max_y, 8*max_x, 16*max_y, 24, "VGA", 0);

	i = 0;
	for (r=0; r<2; r++)
		for (g=0; g<2; g++)
			for (b=0; b<2; b++) {
				d->fb->rgb_palette[i + 0] = r * 0xaa;
				d->fb->rgb_palette[i + 1] = g * 0xaa;
				d->fb->rgb_palette[i + 2] = b * 0xaa;
				i+=3;
			}
	for (r=0; r<2; r++)
		for (g=0; g<2; g++)
			for (b=0; b<2; b++) {
				d->fb->rgb_palette[i + 0] = r * 0xaa + 0x55;
				d->fb->rgb_palette[i + 1] = g * 0xaa + 0x55;
				d->fb->rgb_palette[i + 2] = b * 0xaa + 0x55;
				i+=3;
			}

	memory_device_register(mem, "vga_mem", videomem_base,
	    VGA_MEM_ALLOCY * max_x * 2,
	    dev_vga_access, d, MEM_DEFAULT, NULL);	/*  TODO: BINTRANS  */
	memory_device_register(mem, "vga_ctrl", control_base,
	    16, dev_vga_ctrl_access, d, MEM_DEFAULT, NULL);

	vga_update(cpu, d, 0, max_x * max_y * 2 -1);
}

