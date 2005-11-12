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
 *  $Id: dev_vga.c,v 1.89 2005-11-12 11:34:30 debug Exp $
 *
 *  VGA charcell and graphics device.
 *
 *  It should work with 80x25 and 40x25 text modes, and with a few graphics
 *  modes as long as no fancy VGA features are used.
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

#include "vga.h"

/*  These are generated from binary font files:  */
#include "fonts/font8x8.c"
#include "fonts/font8x10.c"
#include "fonts/font8x16.c"


/*  For videomem -> framebuffer updates:  */
#define	VGA_TICK_SHIFT		18

#define	MAX_RETRACE_SCANLINES	420
#define	N_IS1_READ_THRESHOLD	50

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

	int		fb_max_x;		/*  pixels  */
	int		fb_max_y;		/*  pixels  */
	int		max_x;			/*  charcells or pixels  */
	int		max_y;			/*  charcells or pixels  */

	/*  Selects charcell mode or graphics mode:  */
	int		cur_mode;

	/*  Common for text and graphics modes:  */
	int		pixel_repx, pixel_repy;

	/*  Textmode:  */
	int		font_width;
	int		font_height;
	unsigned char	*font;
	size_t		charcells_size;
	unsigned char	*charcells;		/*  2 bytes per char  */
	unsigned char	*charcells_outputed;	/*  text  */
	unsigned char	*charcells_drawn;	/*  framebuffer  */

	/*  Graphics:  */
	int		graphics_mode;
	int		bits_per_pixel;
	unsigned char	*gfx_mem;
	size_t		gfx_mem_size;

	/*  Registers:  */
	int		attribute_state;	/*  0 or 1  */
	unsigned char	attribute_reg_select;
	unsigned char	attribute_reg[256];

	unsigned char	misc_output_reg;

	unsigned char	sequencer_reg_select;
	unsigned char	sequencer_reg[256];

	unsigned char	graphcontr_reg_select;
	unsigned char	graphcontr_reg[256];

	unsigned char	crtc_reg_select;
	unsigned char	crtc_reg[256];

	unsigned char	palette_read_index;
	char		palette_read_subindex;
	unsigned char	palette_write_index;
	char		palette_write_subindex;

	int		current_retrace_line;
	int		input_status_1;

	/*  Palette per scanline during retrace:  */
	unsigned char	*retrace_palette;
	int		use_palette_per_line;
	int64_t		n_is1_reads;

	/*  Misc.:  */
	int		console_handle;

	int		cursor_x;
	int		cursor_y;

	int		modified;
	int		update_x1;
	int		update_y1;
	int		update_x2;
	int		update_y2;
};


/*
 *  recalc_cursor_position():
 *
 *  Should be called whenever the cursor location _or_ the display
 *  base has been changed.
 */
static void recalc_cursor_position(struct vga_data *d)
{
	int base = (d->crtc_reg[VGA_CRTC_START_ADDR_HIGH] << 8)
	    + d->crtc_reg[VGA_CRTC_START_ADDR_LOW];
	int ofs = d->crtc_reg[VGA_CRTC_CURSOR_LOCATION_HIGH] * 256 +
	    d->crtc_reg[VGA_CRTC_CURSOR_LOCATION_LOW];
	ofs -= base;
	d->cursor_x = ofs % d->max_x;
	d->cursor_y = ofs / d->max_x;
}


/*
 *  register_reset():
 *
 *  Resets many registers to sane values.
 */
static void register_reset(struct vga_data *d)
{
	/*  Home cursor and start at the top:  */
	d->crtc_reg[VGA_CRTC_CURSOR_LOCATION_HIGH] =
	    d->crtc_reg[VGA_CRTC_CURSOR_LOCATION_LOW] = 0;
	d->crtc_reg[VGA_CRTC_START_ADDR_HIGH] =
	    d->crtc_reg[VGA_CRTC_START_ADDR_LOW] = 0;

	recalc_cursor_position(d);

	/*  Reset cursor scanline stuff:  */
	d->crtc_reg[VGA_CRTC_CURSOR_SCANLINE_START] = d->font_height - 2;
	d->crtc_reg[VGA_CRTC_CURSOR_SCANLINE_END] = d->font_height - 1;

	d->sequencer_reg[VGA_SEQ_MAP_MASK] = 0x0f;
	d->graphcontr_reg[VGA_GRAPHCONTR_MASK] = 0xff;

	d->misc_output_reg = VGA_MISC_OUTPUT_IOAS;
	d->n_is1_reads = 0;
}


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
	int i, r, g, b;

	/*  TODO: default values for entry 16..255?  */
	for (i=16; i<256; i++)
		d->fb->rgb_palette[i*3 + 0] = d->fb->rgb_palette[i*3 + 1] =
		    d->fb->rgb_palette[i*3 + 2] = (i & 15) * 4;

	i = 0;

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
	struct vga_data *d, int base, int start, int end)
{
	char s[50];
	int i, oldcolor = -1, printed_last = 0;

	for (i=start; i<=end; i+=2) {
		unsigned char ch = d->charcells[base+i];
		int fg = d->charcells[base+i+1] & 15;
		int bg = (d->charcells[base+i+1] >> 4) & 15;
			/*  top bit of bg = blink  */
		int x = (i/2) % d->max_x;
		int y = (i/2) / d->max_x;

		if (d->charcells[base+i] == d->charcells_outputed[i] &&
		    d->charcells[base+i+1] == d->charcells_outputed[i+1]) {
			printed_last = 0;
			continue;
		}

		d->charcells_outputed[i] = d->charcells[base+i];
		d->charcells_outputed[i+1] = d->charcells[base+i+1];

		if (!printed_last || x == 0) {
			snprintf(s, sizeof(s), "\033[%i;%iH", y + 1, x + 1);
			c_putstr(d, s);
		}
		if (oldcolor < 0 || (bg<<4)+fg != oldcolor || !printed_last) {
			snprintf(s, sizeof(s), "\033[0;"); c_putstr(d, s);

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
		}

		if (ch >= 0x20 && ch != 127)
			console_putchar(d->console_handle, ch);

		oldcolor = (bg << 4) + fg;
		printed_last = 1;
	}

	/*  Restore the terminal's cursor position:  */
	snprintf(s, sizeof(s), "\033[%i;%iH", d->cursor_y + 1, d->cursor_x + 1);
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
	int fg, bg, i, x,y, subx, line, start, end, base;
	int font_size = d->font_height;
	int font_width = d->font_width;
	unsigned char *pal = d->fb->rgb_palette;

	/*  Hm... I'm still using the old start..end code:  */
	start = (d->max_x * y1 + x1) * 2;
	end   = (d->max_x * y2 + x2) * 2;

	start &= ~1;
	end |= 1;

	if (end >= d->charcells_size)
		end = d->charcells_size - 1;

	base = ((d->crtc_reg[VGA_CRTC_START_ADDR_HIGH] << 8)
	    + d->crtc_reg[VGA_CRTC_START_ADDR_LOW]) * 2;

	if (!machine->use_x11)
		vga_update_textmode(machine, d, base, start, end);

	for (i=start; i<=end; i+=2) {
		unsigned char ch = d->charcells[i + base];

		if (d->charcells_drawn[i] == ch &&
		    d->charcells_drawn[i+1] == d->charcells[i+base+1])
			continue;

		d->charcells_drawn[i] = ch;
		d->charcells_drawn[i+1] = d->charcells[i + base + 1];

		fg = d->charcells[i+base + 1] & 15;
		bg = (d->charcells[i+base + 1] >> 4) & 7;

		/*  Blink is hard to do :-), but inversion might be ok too:  */
		if (d->charcells[i+base + 1] & 128) {
			int tmp = fg; fg = bg; bg = tmp;
		}

		x = (i/2) % d->max_x; x *= font_width;
		y = (i/2) / d->max_x; y *= font_size;

		/*  Draw the character:  */
		for (line = 0; line < font_size; line++) {
			for (subx = 0; subx < font_width; subx++) {
				int ix, iy, color_index;

				if (d->use_palette_per_line) {
					int sline = d->pixel_repy * (line+y);
					if (sline < MAX_RETRACE_SCANLINES)
						pal = d->retrace_palette
						    + sline * 256*3;
					else
						pal = d->fb->rgb_palette;
				}

				if (d->font[ch * font_size + line] &
				    (128 >> subx))
					color_index = fg;
				else
					color_index = bg;

				for (iy=0; iy<d->pixel_repy; iy++)
				    for (ix=0; ix<d->pixel_repx; ix++) {
					int addr = (d->fb_max_x* (d->pixel_repy
					    * (line+y) + iy) + (x+subx) *
					    d->pixel_repx + ix) * 3;

					if (addr >= d->fb_size)
						continue;
					dev_fb_access(machine->cpus[0],
					    machine->memory, addr,
					    &pal[color_index * 3], 3,
					    MEM_WRITE, d->fb);
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
	int onoff = 1, height = d->crtc_reg[VGA_CRTC_CURSOR_SCANLINE_END]
	    - d->crtc_reg[VGA_CRTC_CURSOR_SCANLINE_START] + 1;

	if (d->cur_mode != MODE_CHARCELL)
		onoff = 0;

	if (d->crtc_reg[VGA_CRTC_CURSOR_SCANLINE_START] >
	    d->crtc_reg[VGA_CRTC_CURSOR_SCANLINE_END]) {
		onoff = 0;
		height = 1;
	}

	if (d->crtc_reg[VGA_CRTC_CURSOR_SCANLINE_START] >= d->font_height)
		onoff = 0;

	dev_fb_setcursor(d->fb,
	    d->cursor_x * d->font_width * d->pixel_repx, (d->cursor_y *
	    d->font_height + d->crtc_reg[VGA_CRTC_CURSOR_SCANLINE_START]) *
	    d->pixel_repy, onoff, d->font_width * d->pixel_repx, height *
	    d->pixel_repy);
}


/*
 *  dev_vga_tick():
 */
void dev_vga_tick(struct cpu *cpu, void *extra)
{
	struct vga_data *d = extra;
	int64_t low = -1, high;

	vga_update_cursor(cpu->machine, d);

	/*  TODO: text vs graphics tick?  */
	memory_device_dyntrans_access(cpu, cpu->mem, extra,
	    (uint64_t *) &low, (uint64_t *) &high);

	if (low != -1) {
		int base = ((d->crtc_reg[VGA_CRTC_START_ADDR_HIGH] << 8)
		    + d->crtc_reg[VGA_CRTC_START_ADDR_LOW]) * 2;
		int new_u_y1, new_u_y2;
		debug("[ dev_vga_tick: dyntrans access, %llx .. %llx ]\n",
		    (long long)low, (long long)high);
		low -= base;
		high -= base;
		d->update_x1 = 0;
		d->update_x2 = d->max_x - 1;
		new_u_y1 = (low/2) / d->max_x;
		new_u_y2 = ((high/2) / d->max_x) + 1;
		if (new_u_y1 < d->update_y1)
			d->update_y1 = new_u_y1;
		if (new_u_y2 > d->update_y2)
			d->update_y2 = new_u_y2;
		if (d->update_y1 < 0)
			d->update_y1 = 0;
		if (d->update_y2 >= d->max_y)
			d->update_y2 = d->max_y - 1;
		d->modified = 1;
	}

	if (d->n_is1_reads > N_IS1_READ_THRESHOLD &&
	    d->retrace_palette != NULL) {
		d->use_palette_per_line = 1;
		d->update_x1 = 0;
		d->update_x2 = d->max_x - 1;
		d->update_y1 = 0;
		d->update_y2 = d->max_y - 1;
		d->modified = 1;
	} else {
		if (d->use_palette_per_line) {
			d->use_palette_per_line = 0;
			d->update_x1 = 0;
			d->update_x2 = d->max_x - 1;
			d->update_y1 = 0;
			d->update_y2 = d->max_y - 1;
			d->modified = 1;
		}
	}

	if (!cpu->machine->use_x11) {
		/*  NOTE: 2 > 0, so this only updates the cursor, no
		    character cells.  */
		vga_update_textmode(cpu->machine, d, 0, 2, 0);
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

	if (d->n_is1_reads > N_IS1_READ_THRESHOLD)
		d->n_is1_reads = 0;
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
					int pixelmask = 1 << (7-j);
					int b = data[i] & pixelmask;
					int m = d->sequencer_reg[
					    VGA_SEQ_MAP_MASK] & 0x0f;
					int addr = (y * d->max_x + x + i*8 + j)
					    * d->bits_per_pixel / 8;
					unsigned char byte;
					if (!(d->graphcontr_reg[
					    VGA_GRAPHCONTR_MASK] & pixelmask))
						continue;
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
			fatal("TODO: 4 bit graphics read, mask=0x%02x\n",
			    d->sequencer_reg[VGA_SEQ_MAP_MASK]);
			for (i=0; i<len; i++)
				data[i] = random();
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
	int i, x, y, x2, y2, r, base;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);

	base = ((d->crtc_reg[VGA_CRTC_START_ADDR_HIGH] << 8)
	    + d->crtc_reg[VGA_CRTC_START_ADDR_LOW]) * 2;
	r = relative_addr - base;
	y = r / (d->max_x * 2);
	x = (r/2) % d->max_x;
	y2 = (r+len-1) / (d->max_x * 2);
	x2 = ((r+len-1)/2) % d->max_x;

	if (relative_addr + len - 1 < d->charcells_size) {
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
 *  vga_crtc_reg_write():
 *
 *  Writes to VGA CRTC registers.
 */
static void vga_crtc_reg_write(struct machine *machine, struct vga_data *d,
	int regnr, int idata)
{
	int grayscale;

	switch (regnr) {
	case VGA_CRTC_CURSOR_SCANLINE_START:		/*  0x0a  */
	case VGA_CRTC_CURSOR_SCANLINE_END:		/*  0x0b  */
		break;
	case VGA_CRTC_START_ADDR_HIGH:			/*  0x0c  */
	case VGA_CRTC_START_ADDR_LOW:			/*  0x0d  */
		d->update_x1 = 0;
		d->update_x2 = d->max_x - 1;
		d->update_y1 = 0;
		d->update_y2 = d->max_y - 1;
		d->modified = 1;
		recalc_cursor_position(d);
		break;
	case VGA_CRTC_CURSOR_LOCATION_HIGH:		/*  0x0e  */
	case VGA_CRTC_CURSOR_LOCATION_LOW:		/*  0x0f  */
		recalc_cursor_position(d);
		break;
	case 0xff:
		grayscale = 0;
		switch (d->crtc_reg[0xff]) {
		case 0x00:
			grayscale = 1;
		case 0x01:
			d->cur_mode = MODE_CHARCELL;
			d->max_x = 40; d->max_y = 25;
			d->pixel_repx = machine->x11_scaleup * 2;
			d->pixel_repy = machine->x11_scaleup;
			d->font_width = 8;
			d->font_height = 16;
			d->font = font8x16;
			break;
		case 0x02:
			grayscale = 1;
		case 0x03:
			d->cur_mode = MODE_CHARCELL;
			d->max_x = 80; d->max_y = 25;
			d->pixel_repx = d->pixel_repy = machine->x11_scaleup;
			d->font_width = 8;
			d->font_height = 16;
			d->font = font8x16;
			break;
		case 0x08:
			d->cur_mode = MODE_GRAPHICS;
			d->max_x = 160;	d->max_y = 200;
			d->graphics_mode = GRAPHICS_MODE_4BIT;
			d->bits_per_pixel = 4;
			d->pixel_repx = 4 * machine->x11_scaleup;
			d->pixel_repy = 2 * machine->x11_scaleup;
			break;
		case 0x09:
		case 0x0d:
			d->cur_mode = MODE_GRAPHICS;
			d->max_x = 320;	d->max_y = 200;
			d->graphics_mode = GRAPHICS_MODE_4BIT;
			d->bits_per_pixel = 4;
			d->pixel_repx = d->pixel_repy =
			    2 * machine->x11_scaleup;
			break;
		case 0x0e:
			d->cur_mode = MODE_GRAPHICS;
			d->max_x = 640;	d->max_y = 200;
			d->graphics_mode = GRAPHICS_MODE_4BIT;
			d->bits_per_pixel = 4;
			d->pixel_repx = machine->x11_scaleup;
			d->pixel_repy = machine->x11_scaleup * 2;
			break;
		case 0x10:
			d->cur_mode = MODE_GRAPHICS;
			d->max_x = 640; d->max_y = 350;
			d->graphics_mode = GRAPHICS_MODE_4BIT;
			d->bits_per_pixel = 4;
			d->pixel_repx = d->pixel_repy = machine->x11_scaleup;
			break;
		case 0x12:
			d->cur_mode = MODE_GRAPHICS;
			d->max_x = 640; d->max_y = 480;
			d->graphics_mode = GRAPHICS_MODE_4BIT;
			d->bits_per_pixel = 4;
			d->pixel_repx = d->pixel_repy = machine->x11_scaleup;
			break;
		case 0x13:
			d->cur_mode = MODE_GRAPHICS;
			d->max_x = 320;	d->max_y = 200;
			d->graphics_mode = GRAPHICS_MODE_8BIT;
			d->bits_per_pixel = 8;
			d->pixel_repx = d->pixel_repy =
			    2 * machine->x11_scaleup;
			break;
		default:
			fatal("TODO! video mode change hack (mode 0x%02x)\n",
			    d->crtc_reg[0xff]);
			exit(1);
		}

		if (d->cur_mode == MODE_CHARCELL) {
			dev_fb_resize(d->fb, d->max_x * d->font_width *
			    d->pixel_repx, d->max_y * d->font_height *
			    d->pixel_repy);
			d->fb_size = d->max_x * d->pixel_repx * d->font_width *
			     d->max_y * d->pixel_repy * d->font_height * 3;
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
		memset(d->charcells_drawn, 0, d->charcells_size);
		memset(d->gfx_mem, 0, d->gfx_mem_size);
		d->update_x1 = 0;
		d->update_x2 = d->max_x - 1;
		d->update_y1 = 0;
		d->update_y2 = d->max_y - 1;
		d->modified = 1;
		reset_palette(d, grayscale);
		register_reset(d);
		break;
	default:fatal("[ vga_crtc_reg_write: regnr=0x%02x idata=0x%02x ]\n",
		    regnr, idata);
	}
}


/*
 *  vga_sequencer_reg_write():
 *
 *  Writes to VGA Sequencer registers.
 */
static void vga_sequencer_reg_write(struct machine *machine, struct vga_data *d,
	int regnr, int idata)
{
	switch (regnr) {
	case VGA_SEQ_RESET:
	case VGA_SEQ_MAP_MASK:
	case VGA_SEQ_SEQUENCER_MEMORY_MODE:
		debug("[ vga_sequencer_reg_write: select %i: TODO ]\n", regnr);
		break;
	default:fatal("[ vga_sequencer_reg_write: select %i ]\n", regnr);
		/*  cpu->running = 0;  */
	}
}


/*
 *  vga_graphcontr_reg_write():
 *
 *  Writes to VGA Graphics Controller registers.
 */
static void vga_graphcontr_reg_write(struct machine *machine,
	struct vga_data *d, int regnr, int idata)
{
	switch (regnr) {
	case VGA_GRAPHCONTR_READMAPSELECT:
	case VGA_GRAPHCONTR_GRAPHICSMODE:
	case VGA_GRAPHCONTR_MISC:
	case VGA_GRAPHCONTR_MASK:
		debug("[ vga_graphcontr_reg_write: select %i: TODO ]\n", regnr);
		break;
	default:fatal("[ vga_graphcontr_reg_write: select %i ]\n", regnr);
		/*  cpu->running = 0;  */
	}
}


/*
 *  vga_attribute_reg_write():
 *
 *  Writes to VGA Attribute registers.
 */
static void vga_attribute_reg_write(struct machine *machine, struct vga_data *d,
	int regnr, int idata)
{
	/*  0-15 are palette registers: TODO  */
	if (regnr >= 0 && regnr <= 0xf)
		return;

	switch (regnr) {
	default:fatal("[ vga_attribute_reg_write: select %i ]\n", regnr);
		/*  cpu->running = 0;  */
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
	int i;
	uint64_t idata = 0, odata = 0;

	for (i=0; i<len; i++) {
		idata = data[i];

		/*  0x3C0 + relative_addr...  */

		switch (relative_addr) {

		case VGA_ATTRIBUTE_ADDR:		/*  0x00  */
			switch (d->attribute_state) {
			case 0:	if (writeflag == MEM_READ)
					odata = d->attribute_reg_select;
				else {
					d->attribute_reg_select = 1;
					d->attribute_state = 1;
				}
				break;
			case 1:	d->attribute_state = 0;
				d->attribute_reg[d->attribute_reg_select] =
				    idata;
				vga_attribute_reg_write(cpu->machine, d,
				    d->attribute_reg_select, idata);
				break;
			}
			break;
		case VGA_ATTRIBUTE_DATA_READ:		/*  0x01  */
			if (writeflag == MEM_WRITE)
				fatal("[ dev_vga: WARNING: Write to "
				    "VGA_ATTRIBUTE_DATA_READ? ]\n");
			else {
				if (d->attribute_state == 0)
					fatal("[ dev_vga: WARNING: Read from "
					    "VGA_ATTRIBUTE_DATA_READ, but no"
					    " register selected? ]\n");
				else
					odata = d->attribute_reg[
					    d->attribute_reg_select];
			}
			break;

		case VGA_MISC_OUTPUT_W:			/*  0x02  */
			if (writeflag == MEM_WRITE)
				d->misc_output_reg = idata;
			else {
				/*  Reads: Input Status 0  */
				odata = 0x00;
			}
			break;

		case VGA_SEQUENCER_ADDR:		/*  0x04  */
			if (writeflag == MEM_READ)
				odata = d->sequencer_reg_select;
			else
				d->sequencer_reg_select = idata;
			break;
		case VGA_SEQUENCER_DATA:		/*  0x05  */
			if (writeflag == MEM_READ)
				odata = d->sequencer_reg[
				    d->sequencer_reg_select];
			else {
				d->sequencer_reg[d->
				    sequencer_reg_select] = idata;
				vga_sequencer_reg_write(cpu->machine, d,
				    d->sequencer_reg_select, idata);
			}
			break;

		case VGA_DAC_ADDR_READ:			/*  0x07  */
			if (writeflag == MEM_WRITE) {
				d->palette_read_index = idata;
				d->palette_read_subindex = 0;
			} else {
				debug("[ dev_vga: WARNING: Read from "
				    "VGA_DAC_ADDR_READ? TODO ]\n");
				/*  TODO  */
			}
			break;
		case VGA_DAC_ADDR_WRITE:		/*  0x08  */
			if (writeflag == MEM_WRITE) {
				d->palette_write_index = idata;
				d->palette_write_subindex = 0;

				/*  TODO: Is this correct?  */
				d->palette_read_index = idata;
				d->palette_read_subindex = 0;
			} else {
				fatal("[ dev_vga: WARNING: Read from "
				    "VGA_DAC_ADDR_WRITE? ]\n");
				odata = d->palette_write_index;
			}
			break;
		case VGA_DAC_DATA:			/*  0x09  */
			if (writeflag == MEM_WRITE) {
				int new = (idata & 63) << 2;
				int old = d->fb->rgb_palette[d->
				    palette_write_index*3+d->
				    palette_write_subindex];
				d->fb->rgb_palette[d->palette_write_index * 3 +
				    d->palette_write_subindex] = new;
				/*  Redraw whole screen, if the
				    palette changed:  */
				if (new != old) {
					d->modified = 1;
					d->update_x1 = d->update_y1 = 0;
					d->update_x2 = d->max_x - 1;
					d->update_y2 = d->max_y - 1;
				}
				d->palette_write_subindex ++;
				if (d->palette_write_subindex == 3) {
					d->palette_write_index ++;
					d->palette_write_subindex = 0;
				}
			} else {
				odata = (d->fb->rgb_palette[d->
				    palette_read_index * 3 +
				    d->palette_read_subindex] >> 2) & 63;
				d->palette_read_subindex ++;
				if (d->palette_read_subindex == 3) {
					d->palette_read_index ++;
					d->palette_read_subindex = 0;
				}
			}
			break;

		case VGA_MISC_OUTPUT_R:
			odata = d->misc_output_reg;
			break;

		case VGA_GRAPHCONTR_ADDR:		/*  0x0e  */
			if (writeflag == MEM_READ)
				odata = d->graphcontr_reg_select;
			else
				d->graphcontr_reg_select = idata;
			break;
		case VGA_GRAPHCONTR_DATA:		/*  0x0f  */
			if (writeflag == MEM_READ)
				odata = d->graphcontr_reg[
				    d->graphcontr_reg_select];
			else {
				d->graphcontr_reg[d->
				    graphcontr_reg_select] = idata;
				vga_graphcontr_reg_write(cpu->machine, d,
				    d->graphcontr_reg_select, idata);
			}
			break;

		case VGA_CRTC_ADDR:			/*  0x14  */
			if (writeflag == MEM_READ)
				odata = d->crtc_reg_select;
			else
				d->crtc_reg_select = idata;
			break;
		case VGA_CRTC_DATA:			/*  0x15  */
			if (writeflag == MEM_READ)
				odata = d->crtc_reg[d->crtc_reg_select];
			else {
				d->crtc_reg[d->crtc_reg_select] = idata;
				vga_crtc_reg_write(cpu->machine, d,
				    d->crtc_reg_select, idata);
			}
			break;

		case VGA_INPUT_STATUS_1:	/*  0x1A  */
			odata = 0;
			d->n_is1_reads ++;
			d->current_retrace_line ++;
			d->current_retrace_line %= (MAX_RETRACE_SCANLINES * 8);
			/*  Whenever we are "inside" a scan line, copy the
			    current palette into retrace_palette[][]:  */
			if ((d->current_retrace_line & 7) == 7) {
				if (d->retrace_palette == NULL &&
				    d->n_is1_reads > N_IS1_READ_THRESHOLD) {
					d->retrace_palette = malloc(
					    MAX_RETRACE_SCANLINES * 256*3);
					if (d->retrace_palette == NULL) {
						fatal("out of memory\n");
						exit(1);
					}
				}
				if (d->retrace_palette != NULL)
					memcpy(d->retrace_palette + (d->
					    current_retrace_line >> 3) * 256*3,
					    d->fb->rgb_palette, d->cur_mode ==
					    MODE_CHARCELL? (16*3) : (256*3));
			}
			/*  These need to go on and off, to fake the
			    real vertical and horizontal retrace info.  */
			if (d->current_retrace_line < 20*8)
				odata |= VGA_IS1_DISPLAY_VRETRACE;
			else {
				if ((d->current_retrace_line & 7) == 0)
					odata = VGA_IS1_DISPLAY_DISPLAY_DISABLE;
			}
			break;

		default:
			if (writeflag==MEM_READ) {
				debug("[ vga_ctrl: read from 0x%08lx ]\n",
				    (long)relative_addr);
			} else {
				debug("[ vga_ctrl: write to  0x%08lx: 0x%08x"
				    " ]\n", (long)relative_addr, (int)idata);
			}
		}

		if (writeflag == MEM_READ)
			data[i] = odata;

		/*  For multi-byte accesses:  */
		relative_addr ++;
	}

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
	int i;
	size_t allocsize;

	d = malloc(sizeof(struct vga_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct vga_data));

	d->console_handle = console_start_slave(machine, name);

	d->videomem_base  = videomem_base;
	d->control_base   = control_base;
	d->max_x          = 80;
	d->max_y          = 25;
	d->cur_mode       = MODE_CHARCELL;
	d->crtc_reg[0xff] = 0x03;
	d->charcells_size = 0x8000;
	d->gfx_mem_size   = 1;	/*  Nothing, as we start in text mode  */
	d->pixel_repx = d->pixel_repy = machine->x11_scaleup;

	/*  Allocate in full pages, to make it possible to use bintrans:  */
	allocsize = ((d->charcells_size-1) | (machine->arch_pagesize-1)) + 1;
	d->charcells = malloc(d->charcells_size);
	d->charcells_outputed = malloc(d->charcells_size);
	d->charcells_drawn = malloc(d->charcells_size);
	d->gfx_mem = malloc(d->gfx_mem_size);
	if (d->charcells == NULL || d->charcells_outputed == NULL ||
	    d->charcells_drawn == NULL || d->gfx_mem == NULL) {
		fprintf(stderr, "out of memory in dev_vga_init()\n");
		exit(1);
	}

	memset(d->charcells_drawn, 0, d->charcells_size);

	for (i=0; i<d->charcells_size; i+=2) {
		d->charcells[i] = ' ';
		d->charcells[i+1] = 0x07;  /*  Default color  */
		d->charcells_drawn[i] = ' ';
		d->charcells_drawn[i+1] = 0x07;
	}

	memset(d->charcells_outputed, 0, d->charcells_size);
	memset(d->gfx_mem, 0, d->gfx_mem_size);

	d->font = font8x16;
	d->font_width  = 8;
	d->font_height = 16;

	d->fb_max_x = d->pixel_repx * d->max_x;
	d->fb_max_y = d->pixel_repy * d->max_y;
	if (d->cur_mode == MODE_CHARCELL) {
		d->fb_max_x *= d->font_width;
		d->fb_max_y *= d->font_height;
	}

	memory_device_register(mem, "vga_charcells", videomem_base + 0x18000,
	    allocsize, dev_vga_access, d, MEM_DYNTRANS_OK |
	    MEM_DYNTRANS_WRITE_OK | MEM_READING_HAS_NO_SIDE_EFFECTS,
	    d->charcells);
	memory_device_register(mem, "vga_gfx", videomem_base, GFX_ADDR_WINDOW,
	    dev_vga_graphics_access, d, MEM_DEFAULT |
	    MEM_READING_HAS_NO_SIDE_EFFECTS, d->gfx_mem);
	memory_device_register(mem, "vga_ctrl", control_base,
	    32, dev_vga_ctrl_access, d, MEM_DEFAULT, NULL);

	d->fb = dev_fb_init(machine, mem, VGA_FB_ADDR, VFB_GENERIC,
	    d->fb_max_x, d->fb_max_y, d->fb_max_x, d->fb_max_y, 24, "VGA");
	d->fb_size = d->fb_max_x * d->fb_max_y * 3;

	reset_palette(d, 0);

	/*  This will force an initial redraw/resynch:  */
	d->update_x1 = 0;
	d->update_x2 = d->max_x - 1;
	d->update_y1 = 0;
	d->update_y2 = d->max_y - 1;
	d->modified = 1;

	machine_add_tickfunction(machine, dev_vga_tick, d, VGA_TICK_SHIFT);

	register_reset(d);

	vga_update_cursor(machine, d);
}

