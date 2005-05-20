/*
 *  Copyright (C) 2004-2005  Anders Gavare.  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright  
 *     notice, this list of conditions and the following disclaimer in the 
 *     documentation and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
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
 *  $Id: dev_vga.c,v 1.57 2005-05-20 20:07:25 debug Exp $
 *
 *  VGA charcell and graphics device.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "console.h"
#include "cpu.h"
#include "devices.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"

/*  These are generated from binary font files:  */
#include "fonts/font8x8.c"
#include "fonts/font8x10.c"
#include "fonts/font8x16.c"


/*  For bintranslated videomem -> framebuffer updates:  */
#define	VGA_TICK_SHIFT		14

#define	VGA_MEM_MAXY		60
#define	VGA_MEM_ALLOCY		60
#define	GFX_ADDR_WINDOW		0x18000

#define	VGA_FB_ADDR	0x1c00000000ULL

#define	MODE_CHARCELL		1
#define	MODE_GRAPHICS		2

#define	GRAPHICS_MODE_8BIT	1
#define	GRAPHICS_MODE_4BIT	2

struct vga_data {
	uint64_t	videomem_base;
	uint64_t	control_base;

	struct vfb_data *fb;
	size_t		fb_size;

	int		fb_max_x;
	int		max_x;
	int		max_y;

	/*  Selects charcell mode or graphics mode:  */
	int		cur_mode;

	/*  Common for text and graphics modes:  */
	int		pixel_repx, pixel_repy;

	/*  Textmode:  */
	int		font_size;
	unsigned char	*font;
	size_t		charcells_size;
	unsigned char	*charcells;		/*  2 bytes per char  */
	unsigned char	*charcells_outputed;

	/*  Graphics:  */
	int		graphics_mode;
	int		bits_per_pixel;
	unsigned char	*gfx_mem;
	size_t		gfx_mem_size;

	unsigned char	selected_register;
	unsigned char	reg[256];

	int		other_select;
	unsigned char	mask_reg;

	int		palette_index;
	int		palette_subindex;

	int		console_handle;

	int		cursor_x;
	int		cursor_y;
	int		cursor_scanline_start;
	int		cursor_scanline_end;

	int		modified;
	int		update_x1;
	int		update_y1;
	int		update_x2;
	int		update_y2;
};


static void c_putstr(struct vga_data *d, char *s)
{
	while (*s)
		console_putchar(d->console_handle, *s++);
}


/*
 *  reset_palette():
 */
static void reset_palette(struct vga_data *d, int grayscale)
{
	int i = 0, r, g, b;

	if (grayscale) {
		for (r=0; r<2; r++)
		    for (g=0; g<2; g++)
			for (b=0; b<2; b++) {
				d->fb->rgb_palette[i + 0] =
				    d->fb->rgb_palette[i + 1] =
				    d->fb->rgb_palette[i + 2] =
				    (r+g+b) * 0xaa / 3;
				d->fb->rgb_palette[i + 8*3 + 0] =
				    d->fb->rgb_palette[i + 8*3 + 1] =
				    d->fb->rgb_palette[i + 8*3 + 2] =
				    (r+g+b) * 0xaa / 3 + 0x55;
				i+=3;
			}
		return;
	}

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
}


/*
 *  vga_update_textmode():
 *
 *  Called from vga_update() when use_x11 is false. This causes modified
 *  character cells to be "simulated" by outputing ANSI escape sequences
 *  that draw the characters in a terminal window instead.
 */
static void vga_update_textmode(struct machine *machine,
	struct vga_data *d, int start, int end)
{
	char s[50];
	int i;

	for (i=start; i<=end; i+=2) {
		unsigned char ch = d->charcells[i];
		int fg = d->charcells[i+1] & 15;
		int bg = (d->charcells[i+1] >> 4) & 15;	/*  top bit = blink  */
		int x = (i/2) % d->max_x;
		int y = (i/2) / d->max_x;

		if (d->charcells[i] == d->charcells_outputed[i] &&
		    d->charcells[i+1] == d->charcells_outputed[i+1])
			continue;

		d->charcells_outputed[i] = d->charcells[i];
		d->charcells_outputed[i+1] = d->charcells[i+1];

		sprintf(s, "\033[%i;%iH\033[0;", y + 1, x + 1);
		c_putstr(d, s);

		switch (fg & 7) {
		case 0:	c_putstr(d, "30"); break;
		case 1:	c_putstr(d, "34"); break;
		case 2:	c_putstr(d, "32"); break;
		case 3:	c_putstr(d, "36"); break;
		case 4:	c_putstr(d, "31"); break;
		case 5:	c_putstr(d, "35"); break;
		case 6:	c_putstr(d, "33"); break;
		case 7:	c_putstr(d, "37"); break;
		}
		if (fg & 8)
			c_putstr(d, ";1");
		c_putstr(d, ";");
		switch (bg & 7) {
		case 0:	c_putstr(d, "40"); break;
		case 1:	c_putstr(d, "44"); break;
		case 2:	c_putstr(d, "42"); break;
		case 3:	c_putstr(d, "46"); break;
		case 4:	c_putstr(d, "41"); break;
		case 5:	c_putstr(d, "45"); break;
		case 6:	c_putstr(d, "43"); break;
		case 7:	c_putstr(d, "47"); break;
		}
		/*  TODO: blink  */
		c_putstr(d, "m");

		if (ch >= 0x20)
			console_putchar(d->console_handle, ch);
	}

	/*  Restore the terminal's cursor position:  */
	sprintf(s, "\033[%i;%iH", d->cursor_y + 1, d->cursor_x + 1);
	c_putstr(d, s);
}


/*
 *  vga_update_graphics():
 *
 *  This function should be called whenever any part of d->gfx_mem[] has
 *  been written to. It will redraw all pixels within the range x1,y1
 *  .. x2,y2 using the right palette.
 */
static void vga_update_graphics(struct machine *machine, struct vga_data *d,
	int x1, int y1, int x2, int y2)
{
	int x, y, ix, iy, c, rx = d->pixel_repx, ry = d->pixel_repy;
	unsigned char pixel[3];

	for (y=y1; y<=y2; y++)
		for (x=x1; x<=x2; x++) {
			/*  addr is where to read from VGA memory, addr2 is
			    where to write on the 24-bit framebuffer device  */
			int addr = (y * d->max_x + x) * d->bits_per_pixel;
			switch (d->bits_per_pixel) {
			case 8:	addr >>= 3;
				c = d->gfx_mem[addr];
				pixel[0] = d->fb->rgb_palette[c*3+0];
				pixel[1] = d->fb->rgb_palette[c*3+1];
				pixel[2] = d->fb->rgb_palette[c*3+2];
				break;
			case 4:	addr >>= 2;
				if (addr & 1)
					c = d->gfx_mem[addr >> 1] >> 4;
				else
					c = d->gfx_mem[addr >> 1] & 0xf;
				pixel[0] = d->fb->rgb_palette[c*3+0];
				pixel[1] = d->fb->rgb_palette[c*3+1];
				pixel[2] = d->fb->rgb_palette[c*3+2];
				break;
			}
			for (iy=y*ry; iy<(y+1)*ry; iy++)
				for (ix=x*rx; ix<(x+1)*rx; ix++) {
					int addr2 = (d->fb_max_x * iy + ix) * 3;
					if (addr2 < d->fb_size)
						dev_fb_access(machine->cpus[0],
						    machine->memory, addr2,
						    pixel, sizeof(pixel),
						    MEM_WRITE, d->fb);
				}
		}
}


/*
 *  vga_update_text():
 *
 *  This function should be called whenever any part of d->charcells[] has
 *  been written to. It will redraw all characters within the range x1,y1
 *  .. x2,y2 using the right palette.
 */
static void vga_update_text(struct machine *machine, struct vga_data *d,
	int x1, int y1, int x2, int y2)
{
	int fg, bg, i, x,y, subx, line, start, end;

	/*  Hm... I'm still using the old start..end code:  */
	start = (d->max_x * y1 + x1) * 2;
	end   = (d->max_x * y2 + x2) * 2;

	start &= ~1;
	end |= 1;

	if (end >= d->charcells_size)
		end = d->charcells_size - 1;

	if (!machine->use_x11)
		vga_update_textmode(machine, d, start, end);

	for (i=start; i<=end; i+=2) {
		unsigned char ch = d->charcells[i];
		fg = d->charcells[i+1] & 15;
		bg = (d->charcells[i+1] >> 4) & 7;

		/*  Blink is hard to do :-), but inversion might be ok too:  */
		if (d->charcells[i+1] & 128) {
			int tmp = fg; fg = bg; bg = tmp;
		}

		x = (i/2) % d->max_x; x *= 8;
		y = (i/2) / d->max_x; y *= d->font_size;

		for (line = 0; line < d->font_size; line++) {
			for (subx = 0; subx < 8; subx++) {
				unsigned char pixel[3];
				int line2readfrom = line;
				int actualfontheight = d->font_size;
				int ix, iy;

				if (d->font_size == 11) {
					actualfontheight = 10;
					if (line == 10)
						line2readfrom = 9;
				}

				pixel[0] = d->fb->rgb_palette[bg * 3 + 0];
				pixel[1] = d->fb->rgb_palette[bg * 3 + 1];
				pixel[2] = d->fb->rgb_palette[bg * 3 + 2];

				if (d->font[ch * actualfontheight +
				    line2readfrom] & (128 >> subx)) {
					pixel[0] = d->fb->rgb_palette
					    [fg * 3 + 0];
					pixel[1] = d->fb->rgb_palette
					    [fg * 3 + 1];
					pixel[2] = d->fb->rgb_palette
					    [fg * 3 + 2];
				}

				for (iy=0; iy<d->pixel_repy; iy++)
				    for (ix=0; ix<d->pixel_repx; ix++) {
					int addr = (d->fb_max_x* (d->pixel_repy
					    * (line+y) + iy) + (x+subx) *
					    d->pixel_repx + ix) * 3;

					if (addr >= d->fb_size)
						continue;
					dev_fb_access(machine->cpus[0],
					    machine->memory, addr, &pixel[0],
					    sizeof(pixel), MEM_WRITE, d->fb);
				    }
			}
		}
	}
}


/*
 *  vga_update_cursor():
 */
static void vga_update_cursor(struct machine *machine, struct vga_data *d)
{
	int onoff = 1, height = d->cursor_scanline_end -
	    d->cursor_scanline_start + 1;

	if (d->cur_mode != MODE_CHARCELL)
		onoff = 0;

	if (d->cursor_scanline_start > d->cursor_scanline_end) {
		onoff = 0;
		height = 1;
	}

	if (d->cursor_scanline_start >= d->font_size)
		onoff = 0;

	dev_fb_setcursor(d->fb,
	    d->cursor_x * 8 * d->pixel_repx, (d->cursor_y * d->font_size +
	    d->cursor_scanline_start) * d->pixel_repy, onoff,
	    8*d->pixel_repx, height * d->pixel_repy);
}


/*
 *  dev_vga_tick():
 */
void dev_vga_tick(struct cpu *cpu, void *extra)
{
	struct vga_data *d = extra;
	uint64_t low = (uint64_t)-1, high;

	/*  TODO: text vs graphics tick?  */
	memory_device_bintrans_access(cpu, cpu->mem, extra, &low, &high);

	if ((int64_t)low != -1) {
		debug("[ dev_vga_tick: bintrans access, %llx .. %llx ]\n",
		    (long long)low, (long long)high);
		d->update_x1 = 0;
		d->update_x2 = d->max_x - 1;
		d->update_y1 = (low/2) / d->max_x;
		d->update_y2 = ((high/2) / d->max_x) + 1;
		if (d->update_y2 >= d->max_y)
			d->update_y2 = d->max_y - 1;
		d->modified = 1;
	}

	if (!cpu->machine->use_x11) {
		/*  NOTE: 2 > 0, so this only updates the cursor, no
		    character cells.  */
		vga_update_textmode(cpu->machine, d, 2, 0);
	}

	if (d->modified) {
		if (d->cur_mode == MODE_CHARCELL)
			vga_update_text(cpu->machine, d, d->update_x1,
			    d->update_y1, d->update_x2, d->update_y2);
		else
			vga_update_graphics(cpu->machine, d, d->update_x1,
			    d->update_y1, d->update_x2, d->update_y2);

		d->modified = 0;
		d->update_x1 = 999999;
		d->update_x2 = -1;
		d->update_y1 = 999999;
		d->update_y2 = -1;
	}
}


/*
 *  vga_graphics_access():
 *
 *  Reads and writes to the VGA video memory (pixels).
 */
int dev_vga_graphics_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	struct vga_data *d = extra;
	int i,j, x=0, y=0, x2=0, y2=0, modified = 0;

	if (relative_addr + len >= GFX_ADDR_WINDOW)
		return 0;

	if (d->cur_mode != MODE_GRAPHICS)
		return 1;

	switch (d->graphics_mode) {
	case GRAPHICS_MODE_8BIT:
		y = relative_addr / d->max_x;
		x = relative_addr % d->max_x;
		y2 = (relative_addr+len-1) / d->max_x;
		x2 = (relative_addr+len-1) % d->max_x;

		if (writeflag == MEM_WRITE) {
			memcpy(d->gfx_mem + relative_addr, data, len);
			modified = 1;
		} else
			memcpy(data, d->gfx_mem + relative_addr, len);
		break;
	case GRAPHICS_MODE_4BIT:
		y = relative_addr * 8 / d->max_x;
		x = relative_addr * 8 % d->max_x;
		y2 = ((relative_addr+len)*8-1) / d->max_x;
		x2 = ((relative_addr+len)*8-1) % d->max_x;
		/*  TODO: color stuff  */

		/*  Read/write d->gfx_mem in 4-bit color:  */
		if (writeflag == MEM_WRITE) {
			/*  i is byte index to write, j is bit index  */
			for (i=0; i<len; i++)
				for (j=0; j<8; j++) {
					int b = data[i] & (1 << (7-j));
					int m = d->mask_reg & 0x0f;
					int addr = (y * d->max_x + x + i*8 + j)
					    * d->bits_per_pixel / 8;
					unsigned char byte;
					if (addr >= d->gfx_mem_size)
						continue;
					byte = d->gfx_mem[addr];
					if (b && j&1)
						byte |= m << 4;
					if (b && !(j&1))
						byte |= m;
					if (!b && j&1)
						byte &= ~(m << 4);
					if (!b && !(j&1))
						byte &= ~m;
					d->gfx_mem[addr] = byte;
				}
			modified = 1;
		} else {
			fatal("TODO: 4 bit graphics read\n");
		}
		break;
	default:fatal("dev_vga: Unimplemented graphics mode %i\n",
		    d->graphics_mode);
		cpu->running = 0;
	}

	if (modified) {
		d->modified = 1;
		if (x < d->update_x1)  d->update_x1 = x;
		if (x > d->update_x2)  d->update_x2 = x;
		if (y < d->update_y1)  d->update_y1 = y;
		if (y > d->update_y2)  d->update_y2 = y;
		if (x2 < d->update_x1)  d->update_x1 = x2;
		if (x2 > d->update_x2)  d->update_x2 = x2;
		if (y2 < d->update_y1)  d->update_y1 = y2;
		if (y2 > d->update_y2)  d->update_y2 = y2;
		if (y != y2) {
			d->update_x1 = 0;
			d->update_x2 = d->max_x - 1;
		}
	}
	return 1;
}


/*
 *  dev_vga_access():
 *
 *  Reads and writes to the VGA video memory (charcells).
 */
int dev_vga_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr,
	unsigned char *data, size_t len, int writeflag, void *extra)
{
	struct vga_data *d = extra;
	uint64_t idata = 0, odata = 0;
	int i, x, y, x2, y2;

	idata = memory_readmax64(cpu, data, len);

	y = relative_addr / (d->max_x * 2);
	x = (relative_addr/2) % d->max_x;

	y2 = (relative_addr+len-1) / (d->max_x * 2);
	x2 = ((relative_addr+len-1)/2) % d->max_x;

	if (relative_addr < d->charcells_size) {
		if (writeflag == MEM_WRITE) {
			for (i=0; i<len; i++) {
				int old = d->charcells[relative_addr + i];
				if (old != data[i]) {
					d->charcells[relative_addr + i] =
					    data[i];
					d->modified = 1;
				}
			}

			if (d->modified) {
				if (x < d->update_x1)  d->update_x1 = x;
				if (x > d->update_x2)  d->update_x2 = x;
				if (y < d->update_y1)  d->update_y1 = y;
				if (y > d->update_y2)  d->update_y2 = y;
				if (x2 < d->update_x1)  d->update_x1 = x2;
				if (x2 > d->update_x2)  d->update_x2 = x2;
				if (y2 < d->update_y1)  d->update_y1 = y2;
				if (y2 > d->update_y2)  d->update_y2 = y2;

				if (y != y2) {
					d->update_x1 = 0;
					d->update_x2 = d->max_x - 1;
				}
			}
		} else
			memcpy(data, d->charcells + relative_addr, len);
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
static void vga_reg_write(struct machine *machine, struct vga_data *d,
	int regnr, int idata)
{
	int ofs, grayscale;

	switch (regnr) {
	case 0x0a:
		d->cursor_scanline_start = d->reg[0x0a];
		vga_update_cursor(machine, d);
		break;
	case 0x0b:
		d->cursor_scanline_end = d->reg[0x0b];
		vga_update_cursor(machine, d);
		break;
	case 0x0e:
	case 0x0f:
		ofs = d->reg[0x0e] * 256 + d->reg[0x0f];
		d->cursor_x = ofs % d->max_x;
		d->cursor_y = ofs / d->max_x;
		vga_update_cursor(machine, d);
		break;
	case 0xff:
		grayscale = 0;
		switch (d->reg[0xff]) {
		case 0x00:
			grayscale = 1;
		case 0x01:
			d->cur_mode = MODE_CHARCELL;
			d->max_x = 40; d->max_y = 25;
			d->pixel_repx = 2; d->pixel_repy = 1;
			d->font_size = 16;
			break;
		case 0x02:
			grayscale = 1;
		case 0x03:
			d->cur_mode = MODE_CHARCELL;
			d->max_x = 80; d->max_y = 25;
			d->pixel_repx = d->pixel_repy = 1;
			d->font_size = 16;
			break;
		case 0x12:
			d->cur_mode = MODE_GRAPHICS;
			d->max_x = 640; d->max_y = 480;
			d->graphics_mode = GRAPHICS_MODE_4BIT;
			d->bits_per_pixel = 4;
			d->pixel_repx = d->pixel_repy = 1;
			break;
		case 0x0d:
			d->cur_mode = MODE_GRAPHICS;
			d->max_x = 320;	d->max_y = 200;
			d->graphics_mode = GRAPHICS_MODE_4BIT;
			d->bits_per_pixel = 4;
			d->pixel_repx = d->pixel_repy = 2;
			break;
		case 0x13:
			d->cur_mode = MODE_GRAPHICS;
			d->max_x = 320;	d->max_y = 200;
			d->graphics_mode = GRAPHICS_MODE_8BIT;
			d->bits_per_pixel = 8;
			d->pixel_repx = d->pixel_repy = 2;
			break;
		default:
			fatal("TODO! video mode change hack (mode 0x%02x)\n",
			    d->reg[0xff]);
			exit(1);
		}

		if (d->cur_mode == MODE_CHARCELL) {
			dev_fb_resize(d->fb, d->max_x * 8 * d->pixel_repx,
			    d->max_y * d->font_size * d->pixel_repy);
			d->fb_size = d->max_x * d->pixel_repx * 8 *
			     d->max_y * d->pixel_repy * d->font_size * 3;
		} else {
			dev_fb_resize(d->fb, d->max_x * d->pixel_repx,
			    d->max_y * d->pixel_repy);
			d->fb_size = d->max_x * d->pixel_repx *
			     d->max_y * d->pixel_repy * 3;
		}

		if (d->gfx_mem != NULL)
			free(d->gfx_mem);
		d->gfx_mem_size = 1;
		if (d->cur_mode == MODE_GRAPHICS)
			d->gfx_mem_size = d->max_x * d->max_y /
			    (d->graphics_mode == GRAPHICS_MODE_8BIT? 1 : 2);
		d->gfx_mem = malloc(d->gfx_mem_size);

		/*  Clear screen and reset the palette:  */
		memset(d->charcells_outputed, 0, d->charcells_size);
		memset(d->gfx_mem, 0, d->gfx_mem_size);
		d->update_x1 = 0;
		d->update_x2 = d->max_x - 1;
		d->update_y1 = 0;
		d->update_y2 = d->max_y - 1;
		d->modified = 1;
		reset_palette(d, grayscale);

		/*  Home cursor:  */
		d->cursor_x = d->cursor_y = 0;
		d->reg[0x0e] = d->reg[0x0f] = 0;
		vga_update_cursor(machine, d);

		/*  Reset cursor scanline stuff:  */
		d->cursor_scanline_start = d->font_size - 4;
		d->cursor_scanline_end = d->font_size - 2;
		d->reg[0x0a] = d->cursor_scanline_start;
		d->reg[0x0b] = d->cursor_scanline_end;

		d->other_select = -1;
		d->mask_reg = 0x0f;
		break;
	default:fatal("[ vga_reg_write: regnr=0x%02x idata=0x%02x ]\n",
		    regnr, idata);
	}
}


/*
 *  vga_other():
 */
static int vga_other(struct cpu *cpu, struct vga_data *d, int value,
	int writeflag)
{
	int retval = 0;
	switch (d->other_select) {
	case 0x02:
		if (writeflag)
			d->mask_reg = value;
		else
			retval = d->mask_reg;
		break;
	default:fatal("[ vga_other: %s select %i ]\n", writeflag?
	    "write to" : "read from", d->other_select);
		cpu->running = 0;
	}
	return retval;
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
	int i;
	uint64_t idata = 0, odata = 0;

	for (i=0; i<len; i++) {
		idata = data[i];

		switch (relative_addr) {
		case 0x01:	/*  "Other video attributes"  */
			odata = 0xff;	/*  ?  */
			break;
		case 0x04:
			if (writeflag == MEM_WRITE) {
				if (d->other_select < 0)
					d->other_select = idata;
				else {
					vga_other(cpu, d, idata, MEM_WRITE);
					d->other_select = -1;
				}
			} else {
				odata = vga_other(cpu, d, idata, MEM_READ);
			}
			break;
		case 0x08:
			if (writeflag == MEM_WRITE) {
				d->palette_index = idata;
				d->palette_subindex = 0;
			} else {
				odata = d->palette_index;
			}
			break;
		case 0x09:
			if (writeflag == MEM_WRITE) {
				int new = (idata & 63) << 2;
				int old = d->fb->rgb_palette[d->palette_index*3+
				    d->palette_subindex];
				d->fb->rgb_palette[d->palette_index * 3 +
				    d->palette_subindex] = new;
				/*  Redraw whole screen, if the
				    palette changed:  */
				if (new != old) {
					d->modified = 1;
					d->update_x1 = d->update_y1 = 0;
					d->update_x2 = d->max_x - 1;
					d->update_y2 = d->max_y - 1;
				}
			} else {
				odata = (d->fb->rgb_palette[d->palette_index*3+
				    d->palette_subindex] >> 2) & 63;
			}
			d->palette_subindex ++;
			if (d->palette_subindex == 3) {
				d->palette_index ++;
				d->palette_subindex = 0;
			}
			d->palette_index &= 255;
			break;
		case 0x0c:	/*  VGA graphics 1 position  */
			odata = 1;	/*  ?  */
			break;
		case 0x14:	/*  register select  */
			if (writeflag == MEM_READ)
				odata = d->selected_register;
			else
				d->selected_register = idata;
			break;
		case 0x15:	if (writeflag == MEM_READ)
				odata = d->reg[d->selected_register];
			else {
				d->reg[d->selected_register] = idata;
				vga_reg_write(cpu->machine, d,
				    d->selected_register, idata);
			}
			break;
		case 0x1a:	/*  Status register  */
			odata = 1;	/*  Display enabled  */
			/*  odata |= 16;  */  /*  Vertical retrace  */
			break;
		default:
			if (writeflag==MEM_READ) {
				fatal("[ vga_ctrl: read from 0x%08lx ]\n",
				    (long)relative_addr);
			} else {
				fatal("[ vga_ctrl: write to  0x%08lx: 0x%08x"
				    " ]\n", (long)relative_addr, (int)idata);
			}
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
void dev_vga_init(struct machine *machine, struct memory *mem,
	uint64_t videomem_base, uint64_t control_base, char *name)
{
	struct vga_data *d;
	int r,g,b,i, x,y, tmpi;
	size_t allocsize;

	d = malloc(sizeof(struct vga_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct vga_data));

	d->console_handle = console_start_slave(machine, name);

	d->videomem_base = videomem_base;
	d->control_base  = control_base;
	d->max_x         = 80;
	d->max_y         = 25;
	d->pixel_repx    = 1;
	d->pixel_repy    = 1;
	d->cur_mode      = MODE_CHARCELL;
	d->charcells_size = d->max_x * VGA_MEM_MAXY * 2;
	d->gfx_mem_size = 1;	/*  Nothing, as we start in text mode  */

	/*  Allocate in 4KB pages, to make it possible to use bintrans:  */
	allocsize = ((d->charcells_size - 1) | 0xfff) + 1;
	d->charcells = malloc(d->charcells_size);
	d->charcells_outputed = malloc(d->charcells_size);
	d->gfx_mem = malloc(d->gfx_mem_size);
	if (d->charcells == NULL || d->charcells_outputed == NULL ||
	    d->gfx_mem == NULL) {
		fprintf(stderr, "out of memory in dev_vga_init()\n");
		exit(1);
	}

	for (y=0; y<VGA_MEM_MAXY; y++) {
		for (x=0; x<d->max_x; x++) {
			char ch = ' ';
			i = (x + d->max_x * y) * 2;
			d->charcells[i] = ch;
			d->charcells[i+1] = 0x07;  /*  Default color  */
		}
	}

	memset(d->charcells_outputed, 0, d->charcells_size);
	memset(d->gfx_mem, 0, d->gfx_mem_size);

	d->font_size = 16;
	d->font = font8x16;
	d->fb_max_x = 8*d->max_x;

	d->cursor_scanline_start = d->font_size - 4;
	d->cursor_scanline_end = d->font_size - 2;

	d->fb = dev_fb_init(machine, mem, VGA_FB_ADDR, VFB_GENERIC,
	    d->fb_max_x, 16*d->max_y, d->fb_max_x, 16*d->max_y, 24, "VGA", 0);
	d->fb_size = d->fb_max_x * d->font_size*d->max_y * 3;

	reset_palette(d, 0);

	/*  MEM_BINTRANS_WRITE_OK  <-- This works with OpenBSD/arc, but not
	    with Windows NT yet. Why? */
	memory_device_register(mem, "vga_charcells", videomem_base + 0x18000,
	    allocsize, dev_vga_access, d, MEM_BINTRANS_OK, d->charcells);
	memory_device_register(mem, "vga_gfx", videomem_base, GFX_ADDR_WINDOW,
	    dev_vga_graphics_access, d, MEM_DEFAULT, d->gfx_mem);
	memory_device_register(mem, "vga_ctrl", control_base,
	    32, dev_vga_ctrl_access, d, MEM_DEFAULT, NULL);

	/*  This will force an initial redraw/resynch:  */
	d->update_x1 = 0;
	d->update_x2 = d->max_x - 1;
	d->update_y1 = 0;
	d->update_y2 = d->max_y - 1;
	d->modified = 1;

	machine_add_tickfunction(machine, dev_vga_tick, d, VGA_TICK_SHIFT);

	vga_update_cursor(machine, d);

	d->reg[0x0a] = d->cursor_scanline_start;
	d->reg[0x0b] = d->cursor_scanline_end;

	tmpi = d->cursor_y * d->max_x + d->cursor_x;
	d->reg[0x0e] = tmpi >> 8;
	d->reg[0x0f] = tmpi;

	d->reg[0xff] = 0x03;

	d->other_select = -1;
	d->mask_reg = 0x0f;
}

