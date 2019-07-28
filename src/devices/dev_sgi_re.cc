/*
 *  Copyright (C) 2003-2018  Anders Gavare.  All rights reserved.
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
 *  COMMENT: SGI O2 "Rendering Engine"
 *
 *  Guesswork, based on how Linux, NetBSD, and OpenBSD use the graphics on
 *  the SGI O2. Using NetBSD terminology (from crmfbreg.h):
 *
 *  dev_sgi_re.cc (THIS FILE):
 *	0x15001000	rendering engine (TLBs)
 *	0x15002000	drawing engine
 *	0x15003000	memory transfer engine
 *	0x15004000	status registers for drawing engine
 *
 *  dev_sgi_gbe.cc:
 *	0x16000000	crm (or GBE) framebuffer control / video output
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

#include "thirdparty/crmfbreg.h"
#include "thirdparty/sgi_gl.h"


struct sgi_re_data {
	// Rendering engine registers:
	uint16_t	re_tlb_a[256];
	uint16_t	re_tlb_b[256];
	uint16_t	re_tlb_c[256];
	uint16_t	re_tex[112];
	// todo: clip_ids registers.
	uint32_t	re_linear_a[32];
	uint32_t	re_linear_b[32];

	// Drawing engine registers:
	uint32_t	de_reg[DEV_SGI_DE_LENGTH / sizeof(uint32_t)];

	// Memory transfer engine registers:
	uint32_t	mte_reg[DEV_SGI_MTE_LENGTH / sizeof(uint32_t)];
};


/*
 *  horrible_getputpixel():
 *
 *  This routine gets/puts a pixel in one of the tiles, from the perspective of
 *  the rendering/drawing engine. Given x and y, it figures out which tile
 *  number it is, and then finally does a slow read/write to get/put the pixel
 *  at the correct sub-coordinates within the tile.
 *
 *  Tiles are always 512 _bytes_ wide, and 128 pixels high. For 32-bit color
 *  modes, for example, that means 128 x 128 pixels.
 *
 *  For "linear" modes, y is ignored and x is an offset to select which linear
 *  TLB entry to use.
 */
void horrible_getputpixel(bool put, struct cpu* cpu, struct sgi_re_data* d,
	int x, int y, uint32_t* color, int mode)
{
	uint32_t color_mode = mode & DE_MODE_TYPE_MASK;
	int bufdepth = 1 << ((mode >> 8) & 3); 

	// dst_mode (see NetBSD's crmfbreg.h):
	// #define DE_MODE_TLB_A           0x00000000
	// #define DE_MODE_TLB_B           0x00000400
	// #define DE_MODE_TLB_C           0x00000800
	// #define DE_MODE_LIN_A           0x00001000
	// #define DE_MODE_LIN_B           0x00001400
	uint32_t tlb_mode = (mode >> 10) & 0x7;
	bool linear = tlb_mode > 3;

	if (!linear && (x < 0 || y < 0 || x >= 2048 || y >= 2048))
		return;

	int tilewidth_in_pixels = 512 / bufdepth;
	
	int tile_nr_x = x / tilewidth_in_pixels;
	int tile_nr_y = y >> 7;

	unsigned int tile_nr = tile_nr_y * 16 + tile_nr_x;

	y &= 127;
	int xofs = (x % tilewidth_in_pixels) * bufdepth;
	int ofs = 512 * y + xofs;

	uint32_t tileptr = 0;

	switch (tlb_mode) {
	case 0:	tileptr = d->re_tlb_a[tile_nr] << 16;
		break;
	case 1:	tileptr = d->re_tlb_b[tile_nr] << 16;
		break;
	case 2:	tileptr = d->re_tlb_c[tile_nr] << 16;
		break;
	case 4:	tile_nr = x >> 12;
		if (tile_nr >= 32)
			return;
		tileptr = 0x80000000 | (d->re_linear_a[tile_nr] << 12);
		ofs = x & 4095;
		// TODO... probably not correct!
		// printf("tileptr = %08x x = %i y = %i tile_nr = %i ofs = %i\n", tileptr, x, y, tile_nr, ofs);
		break;
	default:fatal("unimplemented dst_mode %i for horrible_getputpixel (%s), x=%i y=%i\n",
			mode, put ? "put" : "get", x, y);
		// exit(1);
		*color = random();
		return;
	}

	// The highest bit seems to be set for a "valid" tile pointer.
	if (!(tileptr & 0x80000000)) {
		//printf("dst_mode %i, tile_nr = %i,  tileptr = 0x%llx\n", dst_mode, tile_nr, (long long)tileptr);
		//fatal("sgi gbe horrible_getputpixel: unexpected non-set high bit of tileptr?\n");
		//exit(1);
		return;
	}
	
	tileptr &= ~0x80000000;

	uint8_t buf[4];
	if (put) {
		switch (color_mode) {
		case DE_MODE_TYPE_CI:
			buf[0] = *color;
			break;
		case DE_MODE_TYPE_RGB:
			buf[0] = *color >> 24;
			buf[1] = *color >> 16;
			buf[2] = *color >> 8;
			buf[3] = 0;
			break;
		case DE_MODE_TYPE_RGBA:
			buf[0] = *color >> 24;
			buf[1] = *color >> 16;
			buf[2] = *color >> 8;
			buf[3] = *color;
			break;
		case DE_MODE_TYPE_ABGR:
			buf[0] = *color;
			buf[1] = *color >> 8;
			buf[2] = *color >> 16;
			buf[3] = *color >> 24;
			break;
		default:buf[0] = random();
			buf[1] = random();
			buf[2] = random();
			buf[3] = random();
			// TODO.
			fatal("[ put: color mode = 0x%x ]\n", color_mode);
		}

		cpu->memory_rw(cpu, cpu->mem, tileptr + ofs,
		    buf, bufdepth, MEM_WRITE, NO_EXCEPTIONS | PHYSICAL);
	} else {
		cpu->memory_rw(cpu, cpu->mem, tileptr + ofs,
		    buf, bufdepth, MEM_READ, NO_EXCEPTIONS | PHYSICAL);

		switch (color_mode) {
		case DE_MODE_TYPE_CI:
			*color = buf[0];
			break;
		case DE_MODE_TYPE_RGB:
			*color = (buf[1] << 24) + (buf[2] << 16) + (buf[3] << 8);
			break;
		case DE_MODE_TYPE_RGBA:
			*color = (buf[0] << 24) + (buf[1] << 16) + (buf[2] << 8) + buf[3];
			break;
		case DE_MODE_TYPE_ABGR:
			*color = (buf[3] << 24) + (buf[2] << 16) + (buf[1] << 8) + buf[0];
			break;
		default:// Read "raw" 32-bit value:
			*color = (buf[3] << 24) + (buf[2] << 16) + (buf[1] << 8) + buf[0];
			fatal("[ get: color mode = 0x%x ]\n", color_mode);
		}
	}
}


/*
 *  SGI "re", NetBSD sources describes it as a "rendering engine".
 */

DEVICE_ACCESS(sgi_re)
{
	struct sgi_re_data *d = (struct sgi_re_data *) extra;
	uint64_t idata = 0, odata = 0;

	idata = memory_readmax64(cpu, data, len);

	relative_addr += 0x1000;

	if (relative_addr >= CRIME_RE_TLB_A && relative_addr < CRIME_RE_TLB_B) {
		if (len != 8) {
			fatal("TODO: unimplemented len=%i for CRIME_RE_TLB_A\n", len);
			exit(1);
		}

		if (writeflag == MEM_WRITE) {
			int tlbi = ((relative_addr & 0x1ff) >> 1) & 0xff;
			for (size_t hwi = 0; hwi < len; hwi += sizeof(uint16_t)) {
				d->re_tlb_a[tlbi] = data[hwi]*256 + data[hwi+1];
				debug("d->re_tlb_a[%i] = 0x%04x\n", tlbi, d->re_tlb_a[tlbi]);
				tlbi++;
			}
		} else {
			fatal("TODO: read from CRIME_RE_TLB_A\n");
			exit(1);
		}
	} else if (relative_addr >= CRIME_RE_TLB_B && relative_addr < CRIME_RE_TLB_C) {
		if (len != 8) {
			fatal("TODO: unimplemented len=%i for CRIME_RE_TLB_B\n", len);
			exit(1);
		}

		if (writeflag == MEM_WRITE) {
			int tlbi = ((relative_addr & 0x1ff) >> 1) & 0xff;
			for (size_t hwi = 0; hwi < len; hwi += sizeof(uint16_t)) {
				d->re_tlb_b[tlbi] = data[hwi]*256 + data[hwi+1];
				debug("d->re_tlb_b[%i] = 0x%04x\n", tlbi, d->re_tlb_b[tlbi]);
				tlbi++;
			}
		} else {
			fatal("TODO: read from CRIME_RE_TLB_B\n");
			exit(1);
		}
	} else if (relative_addr >= CRIME_RE_TLB_C && relative_addr < CRIME_RE_TLB_C + 0x200) {
		if (len != 8) {
			fatal("TODO: unimplemented len=%i for CRIME_RE_TLB_C\n", len);
			exit(1);
		}

		if (writeflag == MEM_WRITE) {
			int tlbi = ((relative_addr & 0x1ff) >> 1) & 0xff;
			for (size_t hwi = 0; hwi < len; hwi += sizeof(uint16_t)) {
				d->re_tlb_c[tlbi] = data[hwi]*256 + data[hwi+1];
				debug("d->re_tlb_c[%i] = 0x%04x\n", tlbi, d->re_tlb_c[tlbi]);
				tlbi++;
			}
		} else {
			fatal("TODO: read from CRIME_RE_TLB_C\n");
			exit(1);
		}
	} else if (relative_addr >= CRIME_RE_TEX && relative_addr < CRIME_RE_TEX + 0xe0) {
		if (len != 8) {
			fatal("TODO: unimplemented len=%i for CRIME_RE_TEX\n", len);
			exit(1);
		}

		if (writeflag == MEM_WRITE) {
			int tlbi = ((relative_addr & 0xff) >> 3) & 0xff;
			for (size_t hwi = 0; hwi < len; hwi += sizeof(uint16_t)) {
				d->re_tex[tlbi] = data[hwi]*256 + data[hwi+1];
				debug("d->re_tex[%i] = 0x%04x\n", tlbi, d->re_tex[tlbi]);
				tlbi++;
			}
		} else {
			fatal("TODO: read from CRIME_RE_TEX\n");
			exit(1);
		}
	} else if (relative_addr >= CRIME_RE_LINEAR_A && relative_addr < CRIME_RE_LINEAR_A + 0x80) {
		if (len != 8) {
			fatal("TODO: unimplemented len=%i for CRIME_RE_LINEAR_A\n", len);
			exit(1);
		}

		if (writeflag == MEM_WRITE) {
			/*
			 *  Very interesting. NetBSD writes stuff such as:
			 *
			 *  CRIME_RE_LINEAR_A IDATA = 8000173080001731
			 *  CRIME_RE_LINEAR_A IDATA = 8000173280001733
			 *  CRIME_RE_LINEAR_A IDATA = 8000173480001735
			 *  ...
			 *
			 *  but the PROM writes something which looks like wrongly
			 *  32-bit sign-extended words:
			 *
			 *  CRIME_RE_LINEAR_A IDATA = ffffffff80040001
			 *  CRIME_RE_LINEAR_A IDATA = ffffffff80040003
			 *  CRIME_RE_LINEAR_A IDATA = ffffffff80040005
			 *  ...
			 *
			 *  followed by
			 *  [ sgi_mte: STARTING TRANSFER: mode=0x00000011
			 *    dst0=0x0000000040000000, dst1=0x0000000040007fff
			 *    (length 0x8000), dst_y_step=0 bg=0x0, bytemask=0xffffffff ]
			 *
			 *  indicating that it really meant to put both 0x80040000
			 *  and 0x80040001 into the first LINEAR_A entry.
			 *
			 *  The first guess would be a bug in the implementation of
			 *  one or more instructions in the emulator while coming up
			 *  with those values, but debugging the PROM so far has NOT
			 *  revealed any such bug. It may even be that the PROM code
			 *  is buggy (?) and never really set the LINEAR entries
			 *  correctly. Perhaps the hardware simply ignores the
			 *  weird values and does not fill it (using the MTE)
			 *  when asked to.
			 */
			// printf("CRIME_RE_LINEAR_A IDATA = %016llx\n", (long long)idata);
			int tlbi = ((relative_addr & 0x7f) >> 2) & 0x1f;
			d->re_linear_a[tlbi] = idata >> 32ULL;
			d->re_linear_a[tlbi+1] = idata;
			debug("[ d->re_linear_a[%i] = 0x%08x, [%i] = 0x%08x ]\n",
				tlbi, d->re_linear_a[tlbi], tlbi+1, d->re_linear_a[tlbi+1]);
		} else {
			fatal("TODO: read from CRIME_RE_LINEAR_A\n");
			exit(1);
		}
	} else if (relative_addr >= CRIME_RE_LINEAR_B && relative_addr < CRIME_RE_LINEAR_B + 0x80) {
		if (len != 8) {
			fatal("TODO: unimplemented len=%i for CRIME_RE_LINEAR_B\n", len);
			exit(1);
		}

		if (writeflag == MEM_WRITE) {
			int tlbi = ((relative_addr & 0x7f) >> 2) & 0x1f;
			d->re_linear_b[tlbi] = idata >> 32ULL;
			d->re_linear_b[tlbi+1] = idata;
			debug("[ d->re_linear_b[%i] = 0x%08x, [%i] = 0x%08x ]\n",
				tlbi, d->re_linear_b[tlbi], tlbi+1, d->re_linear_b[tlbi+1]);
		} else {
			fatal("TODO: read from CRIME_RE_LINEAR_B\n");
			exit(1);
		}
	} else {
		if (writeflag == MEM_WRITE)
			fatal("[ sgi_re: unimplemented write to "
			    "address 0x%llx, data=0x%016llx ]\n",
			    (long long)relative_addr, (long long)idata);
		else
			fatal("[ sgi_re: unimplemented read from address"
			    " 0x%llx ]\n", (long long)relative_addr);
		exit(1);
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_sgi_re_init():
 */
void dev_sgi_re_init(struct machine *machine, struct memory *mem, uint64_t baseaddr)
{
	struct sgi_re_data *d;

	CHECK_ALLOCATION(d = (struct sgi_re_data *) malloc(sizeof(struct sgi_re_data)));
	memset(d, 0, sizeof(struct sgi_re_data));

	memory_device_register(mem, "sgi_re", baseaddr + 0x1000, DEV_SGI_RE_LENGTH,
	    dev_sgi_re_access, d, DM_DEFAULT, NULL);

	dev_sgi_de_init(mem, baseaddr + 0x2000, d);
	dev_sgi_mte_init(mem, baseaddr + 0x3000, d);
	dev_sgi_de_status_init(mem, baseaddr + 0x4000, d);
}



/****************************************************************************/

/*
 *  SGI "de", NetBSD sources describes it as a "drawing engine".
 */

void draw_primitive(struct cpu* cpu, struct sgi_re_data *d)
{
	uint32_t op = d->de_reg[(CRIME_DE_PRIMITIVE - 0x2000) / sizeof(uint32_t)];
	uint32_t drawmode = d->de_reg[(CRIME_DE_DRAWMODE - 0x2000) / sizeof(uint32_t)];
	uint32_t dst_mode = d->de_reg[(CRIME_DE_MODE_DST - 0x2000) / sizeof(uint32_t)];
	uint32_t src_mode = d->de_reg[(CRIME_DE_MODE_SRC - 0x2000) / sizeof(uint32_t)];
	uint32_t fg = d->de_reg[(CRIME_DE_FG - 0x2000) / sizeof(uint32_t)];
	uint32_t bg = d->de_reg[(CRIME_DE_BG - 0x2000) / sizeof(uint32_t)];
	uint32_t rop = d->de_reg[(CRIME_DE_ROP - 0x2000) / sizeof(uint32_t)];

	uint32_t stipple_mode = d->de_reg[(CRIME_DE_STIPPLE_MODE - 0x2000) / sizeof(uint32_t)];
	uint32_t pattern = d->de_reg[(CRIME_DE_STIPPLE_PAT - 0x2000) / sizeof(uint32_t)];
	int nr_of_bits_to_strip_to_the_left = (stipple_mode >> DE_STIP_STRTIDX_SHIFT) & 31;
	int nr_of_bits_to_strip_to_the_right = 31 - ((stipple_mode >> DE_STIP_MAXIDX_SHIFT) & 31);
	pattern >>= nr_of_bits_to_strip_to_the_right;
	pattern <<= nr_of_bits_to_strip_to_the_right;
	pattern <<= nr_of_bits_to_strip_to_the_left;

	int nr_of_bits_in_the_middle = 32 - nr_of_bits_to_strip_to_the_left - nr_of_bits_to_strip_to_the_right;

	if (stipple_mode & 0xe0e0ffff)
		fatal("[ sgi_de: UNIMPLEMENTED stipple_mode bits: 0x%08x ]\n", stipple_mode);

	uint32_t x1 = (d->de_reg[(CRIME_DE_X_VERTEX_0 - 0x2000) / sizeof(uint32_t)] >> 16) & 0x7ff;
	uint32_t y1 = d->de_reg[(CRIME_DE_X_VERTEX_0 - 0x2000) / sizeof(uint32_t)]& 0x7ff;
	uint32_t x2 = (d->de_reg[(CRIME_DE_X_VERTEX_1 - 0x2000) / sizeof(uint32_t)] >> 16) & 0x7ff;
	uint32_t y2 = d->de_reg[(CRIME_DE_X_VERTEX_1 - 0x2000) / sizeof(uint32_t)]& 0x7ff;
	size_t x, y;

	debug("[ sgi_de: STARTING DRAWING COMMAND: op = 0x%08x,"
	    " drawmode=0x%x src_mode=0x%x dst_mode=0x%x x1=%i y1=%i"
	    " x2=%i y2=%i fg=0x%x bg=0x%x pattern=0x%08x ]\n",
	    op, drawmode, src_mode, dst_mode, x1, y1, x2, y2, fg, bg, pattern);

	// bufdepth = 1, 2, or 4.
	// int dst_bufdepth = 1 << ((dst_mode >> 8) & 3);
	int src_bufdepth = 1 << ((src_mode >> 8) & 3);

	bool src_is_linear = false;
	int src_x = -1, src_y = -1;
	int32_t step_x = 0;
	if (drawmode & DE_DRAWMODE_XFER_EN) {
		uint32_t addr_src = d->de_reg[(CRIME_DE_XFER_ADDR_SRC - 0x2000) / sizeof(uint32_t)];
		uint32_t strd_src = d->de_reg[(CRIME_DE_XFER_STRD_SRC - 0x2000) / sizeof(uint32_t)];
		step_x = d->de_reg[(CRIME_DE_XFER_STEP_X - 0x2000) / sizeof(uint32_t)];
		int32_t step_y = d->de_reg[(CRIME_DE_XFER_STEP_Y - 0x2000) / sizeof(uint32_t)];
		uint32_t addr_dst = d->de_reg[(CRIME_DE_XFER_ADDR_DST - 0x2000) / sizeof(uint32_t)];
		uint32_t strd_dst = d->de_reg[(CRIME_DE_XFER_STRD_DST - 0x2000) / sizeof(uint32_t)];

		src_is_linear = ((src_mode & 0x00001c00) >> 10) > 3;

		if (src_is_linear) {
			src_x = addr_src;
			src_y = 0;
		} else {
			src_x = (addr_src >> 16) & 0x7ff;
			src_y = addr_src & 0x7ff;
		}

		if (step_x != src_bufdepth || (step_y != 0 && step_y != 1)) {
			fatal("[ sgi_de: unimplemented XFER addr_src=0x%x src_bufdepth=%i "
				"strd_src=0x%x step_x=0x%x step_y=0x%x "
				"addr_dst=0x%x strd_dst=0x%x ]\n",
				addr_src, src_bufdepth, strd_src, step_x, step_y, addr_dst, strd_dst);

			// exit(1);
		}
	}

	if (!(drawmode & DE_DRAWMODE_PLANEMASK)) {
		printf("!DE_DRAWMODE_PLANEMASK: TODO\n");
	}
	
	if ((drawmode & DE_DRAWMODE_BYTEMASK) != DE_DRAWMODE_BYTEMASK) {
		printf("not all DE_DRAWMODE_BYTEMASK set: TODO\n");
	}
	
	// primitive rendering direction (not for Lines? and presumably
	// not for Points either :-)
	int dx = op & DE_PRIM_RL ? -1 :  1;
	int dy = op & DE_PRIM_TB ?  1 : -1;

	uint16_t saved_src_x = src_x;

	/*
	 *  Drawing is limited to 2048 x 2048 pixel space.
	 *
	 *  TODO: MAYBE it is really -2048 to 2027, i.e. 12 bits of
	 *  pixel space. Perhaps it is possible to figure out
	 *  experimentally some day, e.g. by drawing lines from
	 *  -10,10 to 500,20 and see whether the real hardware
	 *  interprets -10 as -10 or 0x800 - 10.
	 */
	uint16_t endx = (x2 + dx) & 0x7ff;
	uint16_t endy = (y2 + dy) & 0x7ff;

	int lx = abs((int)(x2 - x1)), ly = abs((int)(y2 - y1));
	int linelen = lx > ly ? lx : ly;

	switch (op & 0xff000000) {
	case DE_PRIM_LINE:
		if (drawmode & DE_DRAWMODE_XFER_EN)
			fatal("[ sgi_de: XFER_EN for LINE op? ]\n");

		// The PROM uses width 32, but NetBSD documents it as "half pixels".
		// if ((op & DE_PRIM_LINE_WIDTH_MASK) != 2)
		//	fatal("[ sgi_de: LINE_WIDTH_MASK = %i ]\n", op & DE_PRIM_LINE_WIDTH_MASK);

		if (linelen == 0)
			linelen ++;

		for (int i = 0; i < ((op & DE_PRIM_LINE_SKIP_END)? linelen : linelen+1); ++i) {
			x = (x2 * i + x1 * (linelen-i)) / linelen;
			y = (y2 * i + y1 * (linelen-i)) / linelen;

			uint32_t color = fg;
			uint32_t oldcolor = fg;

			if (drawmode & DE_DRAWMODE_ROP && rop != OPENGL_LOGIC_OP_COPY)
				horrible_getputpixel(false, cpu, d,
					x, y, &oldcolor, dst_mode);

			bool draw = true;
			if (drawmode & DE_DRAWMODE_LINE_STIP) {
				if (drawmode & DE_DRAWMODE_OPAQUE_STIP)
					color = (pattern & 0x80000000UL) ? fg : bg;
				else
					draw = (pattern & 0x80000000UL)? true : false;
			}

			// Raster-OP.
			// TODO: Other ops.
			// TODO: Should this be before or after other things?
			if (drawmode & DE_DRAWMODE_ROP) {
				switch (rop) {
				case OPENGL_LOGIC_OP_COPY:
					// color = color;
					break;
				case OPENGL_LOGIC_OP_XOR:
					color = oldcolor ^ color;
					break;
				case OPENGL_LOGIC_OP_COPY_INVERTED:
					color = 0xffffffff - oldcolor;
					break;
				default:{
						static char rop_used[256];
						static bool first = true;

						if (first) {
						memset(rop_used, 0, sizeof(rop_used));
						first = false;
						}
						if (!rop_used[rop & 255]) {
							rop_used[rop & 255] = 1;
							fatal("[ sgi_de: LINE: rop[0x%02x] used! ]\n", rop & 255);
						}

						if (rop >> 8) {
							fatal("[ sgi_de: LINE: rop > 255: 0x%08x ]\n", rop);
						}
					}
				}
			}

			if (draw)
				horrible_getputpixel(true, cpu, d,
					x, y, &color, dst_mode);

			// Rotate the stipple pattern:
			pattern = (pattern << 1) | (pattern >> (nr_of_bits_in_the_middle-1));
		}
		break;

	case DE_PRIM_RECTANGLE:
		for (y = y1; y != endy; y = (y + dy) & 0x7ff) {
			src_x = saved_src_x;
			for (x = x1; x != endx; x = (x + dx) & 0x7ff) {
				uint32_t color = fg;
				uint32_t oldcolor = fg;

				if (drawmode & DE_DRAWMODE_ROP && rop != OPENGL_LOGIC_OP_COPY)
					horrible_getputpixel(false, cpu, d,
						x, y, &oldcolor, dst_mode);

				// Pixel colors copied from another source.
				// (OpenBSD draws characters using this mechanism.)
				if (drawmode & DE_DRAWMODE_XFER_EN)
					horrible_getputpixel(false, cpu, d,
						src_x, src_y, &color, src_mode);

				bool draw = true;
				if (drawmode & DE_DRAWMODE_POLY_STIP) {
					if (drawmode & DE_DRAWMODE_OPAQUE_STIP)
						color = (pattern & 0x80000000UL) ? fg : bg;
					else
						draw = (pattern & 0x80000000UL)? true : false;
				}

				// Raster-OP.
				// TODO: Other ops.
				// TODO: Should this be before or after other things?
				if (drawmode & DE_DRAWMODE_ROP) {
					switch (rop) {
					case OPENGL_LOGIC_OP_COPY:
						// color = color;
						break;
					case OPENGL_LOGIC_OP_XOR:
						color = oldcolor ^ color;
						break;
					case OPENGL_LOGIC_OP_COPY_INVERTED:
						color = 0xffffffff - oldcolor;
						break;
					default:{
							static char rop_used[256];
							static bool first = true;

							if (first) {
							memset(rop_used, 0, sizeof(rop_used));
							first = false;
							}
							if (!rop_used[rop & 255]) {
								rop_used[rop & 255] = 1;
								fatal("[ sgi_de: RECT: rop[0x%02x] used! ]\n", rop & 255);
							}

							if (rop >> 8) {
								fatal("[ sgi_de: RECT: rop > 255: 0x%08x ]\n", rop);
							}
						}
					}
				}

				if (draw)
					horrible_getputpixel(true, cpu, d,
						x, y, &color, dst_mode);

				// Rotate the stipple pattern:
				pattern = (pattern << 1) | (pattern >> (nr_of_bits_in_the_middle-1));

				if (src_is_linear)
					src_x += step_x;
				else
					src_x = (src_x + dx) & 0x7ff;
			}
			
			src_y = (src_y + dy) & 0x7ff;
		}
		break;

	default:fatal("[ sgi_de: UNIMPLEMENTED drawing op = 0x%08x,"
		    " x1=%i y1=%i x2=%i y2=%i fg=0x%x bg=0x%x pattern=0x%08x ]\n",
		    op, x1, y1, x2, y2, fg, bg, pattern);
		exit(1);
	}
}

DEVICE_ACCESS(sgi_de)
{
	struct sgi_re_data *d = (struct sgi_re_data *) extra;
	uint64_t idata = 0, odata = 0;
	int regnr;
	bool startFlag = relative_addr & CRIME_DE_START ? true : false;

	relative_addr &= ~CRIME_DE_START;

	idata = memory_readmax64(cpu, data, len);
	regnr = relative_addr / sizeof(uint32_t);

	relative_addr += 0x2000;

	/*
	 *  Treat all registers as read/write, by default.  Sometimes these
	 *  are accessed as 32-bit words, sometimes as 64-bit words to access
	 *  two adjacent registers in one operation.
	 */
	if (len == 8) {
		if (writeflag == MEM_WRITE) {
			d->de_reg[regnr] = idata >> 32ULL;
			d->de_reg[regnr+1] = idata;
		} else
			odata = ((uint64_t)d->de_reg[regnr] << 32ULL) +
			    d->de_reg[regnr+1];
	} else if (len == 4) {
		if (writeflag == MEM_WRITE)
			d->de_reg[regnr] = idata;
		else
			odata = d->de_reg[regnr];
	} else {
		fatal("sgi_de: len = %i not implemented\n", len);
		exit(1);
	}

	switch (relative_addr) {

	case CRIME_DE_MODE_SRC:
		debug("[ sgi_de: %s CRIME_DE_MODE_SRC: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_DE_MODE_DST:
		debug("[ sgi_de: %s CRIME_DE_MODE_DST: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_DE_CLIPMODE:
		debug("[ sgi_de: %s CRIME_DE_CLIPMODE: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		if (writeflag == MEM_WRITE && idata != 0)
			fatal("[ sgi_de: TODO: non-zero CRIME_DE_CLIPMODE: 0x%016llx ]\n", idata);
		break;

	case CRIME_DE_DRAWMODE:
		debug("[ sgi_de: %s CRIME_DE_DRAWMODE: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_DE_SCRMASK0:
		debug("[ sgi_de: %s CRIME_DE_SCRMASK0: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		if (writeflag == MEM_WRITE && idata != 0)
			fatal("[ sgi_de: TODO: non-zero CRIME_DE_SCRMASK0: 0x%016llx ]\n", idata);
		break;

	case CRIME_DE_SCRMASK1:
		debug("[ sgi_de: %s CRIME_DE_SCRMASK1: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		if (writeflag == MEM_WRITE && idata != 0)
			fatal("[ sgi_de: TODO: non-zero CRIME_DE_SCRMASK1: 0x%016llx ]\n", idata);
		break;

	case CRIME_DE_SCRMASK2:
		debug("[ sgi_de: %s CRIME_DE_SCRMASK2: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		if (writeflag == MEM_WRITE && idata != 0)
			fatal("[ sgi_de: TODO: non-zero CRIME_DE_SCRMASK2: 0x%016llx ]\n", idata);
		break;

	case CRIME_DE_SCRMASK3:
		debug("[ sgi_de: %s CRIME_DE_SCRMASK3: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		if (writeflag == MEM_WRITE && idata != 0)
			fatal("[ sgi_de: TODO: non-zero CRIME_DE_SCRMASK3: 0x%016llx ]\n", idata);
		break;

	case CRIME_DE_SCRMASK4:
		debug("[ sgi_de: %s CRIME_DE_SCRMASK4: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		if (writeflag == MEM_WRITE && idata != 0)
			fatal("[ sgi_de: TODO: non-zero CRIME_DE_SCRMASK4: 0x%016llx ]\n", idata);
		break;

	case CRIME_DE_SCISSOR:
		debug("[ sgi_de: %s CRIME_DE_SCISSOR: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		if (writeflag == MEM_WRITE && idata != 0)
			fatal("[ sgi_de: TODO: non-zero CRIME_DE_SCISSOR: 0x%016llx ]\n", idata);
		break;

	case CRIME_DE_SCISSOR + 4:
		// NetBSD writes 0x3fff3fff here. "High" part of SCISSOR register?
		debug("[ sgi_de: %s CRIME_DE_SCISSOR+4: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		if (writeflag == MEM_WRITE && idata != 0x3fff3fff)
			fatal("[ sgi_de: TODO: CRIME_DE_SCISSOR+4: 0x%016llx ]\n", idata);
		break;

	case CRIME_DE_PRIMITIVE:
		debug("[ sgi_de: %s CRIME_DE_PRIMITIVE: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_DE_WINOFFSET_SRC:
		debug("[ sgi_de: %s CRIME_DE_WINOFFSET_SRC: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		if (writeflag == MEM_WRITE && idata != 0)
			fatal("[ sgi_de: TODO: non-zero CRIME_DE_WINOFFSET_SRC: 0x%016llx ]\n", idata);
		break;

	case CRIME_DE_WINOFFSET_DST:
		debug("[ sgi_de: %s CRIME_DE_WINOFFSET_DST: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		if (writeflag == MEM_WRITE && idata != 0)
			fatal("[ sgi_de: TODO: non-zero CRIME_DE_WINOFFSET_DST: 0x%016llx ]\n", idata);
		break;

	case CRIME_DE_X_VERTEX_0:
		debug("[ sgi_de: %s CRIME_DE_X_VERTEX_0: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_DE_X_VERTEX_1:
		debug("[ sgi_de: %s CRIME_DE_X_VERTEX_1: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_DE_XFER_ADDR_SRC:
		debug("[ sgi_de: %s CRIME_DE_XFER_ADDR_SRC: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_DE_XFER_STRD_SRC:
		debug("[ sgi_de: %s CRIME_DE_XFER_STRD_SRC: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_DE_XFER_STEP_X:
		debug("[ sgi_de: %s CRIME_DE_XFER_STEP_X: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_DE_XFER_STEP_Y:
		debug("[ sgi_de: %s CRIME_DE_XFER_STEP_Y: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_DE_XFER_ADDR_DST:
		debug("[ sgi_de: %s CRIME_DE_XFER_ADDR_DST: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_DE_XFER_STRD_DST:
		debug("[ sgi_de: %s CRIME_DE_XFER_STRD_DST: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_DE_STIPPLE_MODE:
		debug("[ sgi_de: %s CRIME_DE_STIPPLE_MODE: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_DE_STIPPLE_PAT:
		debug("[ sgi_de: %s CRIME_DE_STIPPLE_PAT: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_DE_FG:
		debug("[ sgi_de: %s CRIME_DE_FG: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_DE_BG:
		debug("[ sgi_de: %s CRIME_DE_BG: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_DE_ROP:
		debug("[ sgi_de: %s CRIME_DE_ROP: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_DE_PLANEMASK:
		debug("[ sgi_de: %s CRIME_DE_PLANEMASK: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_DE_NULL:
		debug("[ sgi_de: %s CRIME_DE_NULL: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_DE_FLUSH:
		debug("[ sgi_de: %s CRIME_DE_FLUSH: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	default:
		if (writeflag == MEM_WRITE)
			fatal("[ sgi_de: unimplemented write to "
			    "address 0x%llx, data=0x%016llx ]\n",
			    (long long)relative_addr, (long long)idata);
		else
			fatal("[ sgi_de: unimplemented read from address"
			    " 0x%llx ]\n", (long long)relative_addr);
	}

	if (startFlag)
		draw_primitive(cpu, d);

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_sgi_de_init():
 */
void dev_sgi_de_init(struct memory *mem, uint64_t baseaddr, struct sgi_re_data *d)
{
	memory_device_register(mem, "sgi_de", baseaddr, DEV_SGI_DE_LENGTH,
	    dev_sgi_de_access, (void *)d, DM_DEFAULT, NULL);
}


/****************************************************************************/

/*
 *  SGI "mte", NetBSD sources describes it as a "memory transfer engine".
 *
 *  If the relative address has the 0x0800 (CRIME_DE_START) flag set, it means
 *  "go ahead with the transfer". Otherwise, it is just reads and writes of the
 *  registers.
 */

void do_mte_transfer(struct cpu* cpu, struct sgi_re_data *d)
{
	uint32_t mode = d->mte_reg[(CRIME_MTE_MODE - 0x3000) / sizeof(uint32_t)];
	uint32_t src0 = d->mte_reg[(CRIME_MTE_SRC0 - 0x3000) / sizeof(uint32_t)];
	uint32_t src1 = d->mte_reg[(CRIME_MTE_SRC1 - 0x3000) / sizeof(uint32_t)];
	uint32_t dst0 = d->mte_reg[(CRIME_MTE_DST0 - 0x3000) / sizeof(uint32_t)];
	uint32_t dst1 = d->mte_reg[(CRIME_MTE_DST1 - 0x3000) / sizeof(uint32_t)];
	int32_t src_y_step = d->mte_reg[(CRIME_MTE_SRC_Y_STEP - 0x3000) / sizeof(uint32_t)];
	int32_t dst_y_step = d->mte_reg[(CRIME_MTE_DST_Y_STEP - 0x3000) / sizeof(uint32_t)];
	uint32_t dstlen = dst1 - dst0 + 1, fill_addr;
	unsigned char zerobuf[4096];
	int depth = 8 << ((mode & MTE_MODE_DEPTH_MASK) >> MTE_DEPTH_SHIFT);
	int src = (mode & MTE_MODE_SRC_BUF_MASK) >> MTE_SRC_TLB_SHIFT;
	uint32_t bytemask = d->mte_reg[(CRIME_MTE_BYTEMASK - 0x3000) / sizeof(uint32_t)];
	uint32_t bg = d->mte_reg[(CRIME_MTE_BG - 0x3000) / sizeof(uint32_t)];

	debug("[ sgi_mte: STARTING: mode=0x%08x src0=0x%08x src1=0x%08x src_y_step=%i dst0=0x%08x,"
	    " dst1=0x%08x dst_y_step=%i bg=0x%x bytemask=0x%x ]\n",
	    mode,
	    src0, src1, src_y_step,
	    dst0, dst1, dst_y_step,
	    bg, bytemask);

	if (dst_y_step != 0 && dst_y_step != 1 && dst_y_step != -1) {
		fatal("[ sgi_mte: TODO! unimplemented dst_y_step %i ]", dst_y_step);
		// exit(1);
	}

	if (mode & MTE_MODE_STIPPLE) {
		fatal("[ sgi_mte: unimplemented MTE_MODE_STIPPLE ]");
		exit(1);
	}

	int src_tlb = (mode & MTE_MODE_SRC_BUF_MASK) >> MTE_SRC_TLB_SHIFT;
	int dst_tlb = (mode & MTE_MODE_DST_BUF_MASK) >> MTE_DST_TLB_SHIFT;
	
	if (src > MTE_TLB_C) {
		fatal("[ sgi_mte: unimplemented SRC ]");
		exit(1);
	}

	switch (dst_tlb) {
	case MTE_TLB_A:
	case MTE_TLB_B:
	case MTE_TLB_C:
		// Used by NetBSD's crmfb_fill_rect. It puts graphical
		// coordinates in dst0 and dst1.
		{
			int x1 = (dst0 >> 16) & 0xfff;
			int y1 = dst0 & 0xfff;
			int x2 = (dst1 >> 16) & 0xfff;
			int y2 = dst1 & 0xfff;
			x1 /= (depth / 8);
			x2 /= (depth / 8);

			int src_x1 = (src0 >> 16) & 0xfff;
			int src_y1 = src0 & 0xfff;
			// int src_x2 = (src1 >> 16) & 0xfff;
			// int src_y2 = src1 & 0xfff;
			src_x1 /= (depth / 8);
			// src_x2 /= (depth / 8);

			int dx = x1 > x2 ? -1 : 1;
			int dy = y1 > y2 ? -1 : 1;
			
			uint32_t src_mode = (src_tlb << 10) + (((mode & MTE_MODE_DEPTH_MASK) >> MTE_DEPTH_SHIFT) << 8);
			uint32_t dst_mode = (dst_tlb << 10) + (((mode & MTE_MODE_DEPTH_MASK) >> MTE_DEPTH_SHIFT) << 8);

			// Hack. The MTE perhaps doesn't deal with colors per se,
			// but this makes sure that we copy 32 bits when doing 32-bit
			// transfers.
			if (depth == 4) {
				src_mode |= DE_MODE_TYPE_RGBA;
				dst_mode |= DE_MODE_TYPE_RGBA;
			}

			int src_y = src_y1;
			for (int y = y1; y != y2+dy; y += dy) {
				int src_x = src_x1;
				
				for  (int x = x1; x != x2+dx; x += dx) {
					if (mode & MTE_MODE_COPY) {
						horrible_getputpixel(false, cpu, d,
							src_x, src_y, &bg, src_mode);
						src_x += dx;
					}

					horrible_getputpixel(true, cpu, d, x, y, &bg, dst_mode);
				}
				
				src_y += dy;
			}
		}
		break;
	case MTE_TLB_LIN_A:
	case MTE_TLB_LIN_B:
		// Used by the PROM to zero-fill memory (?).
		if (mode & MTE_MODE_COPY) {
			fatal("[ sgi_mte: unimplemented MTE_MODE_COPY ]");
			exit(1);
		}

		if (depth != 8) {
			fatal("[ sgi_mte: unimplemented MTE_DEPTH_x ]");
			exit(1);
		}

		debug("[ sgi_mte: LINEAR TRANSFER: mode=0x%08x dst0=0x%016llx,"
		    " dst1=0x%016llx (length 0x%llx), dst_y_step=%i bg=0x%x, bytemask=0x%x ]\n",
		    mode,
		    (long long)dst0, (long long)dst1,
		    (long long)dstlen, dst_y_step, (int)bg, (int)bytemask);

		if (bytemask != 0xffffffff) {
			fatal("unimplemented MTE bytemask 0x%08x\n", (int)bytemask);
			exit(1);
		}

		/*
		 *  Horrible hack:
		 *
		 *  During bootup, the PROM fills memory at 0x40000000 and
		 *  forward. This corresponds to the lowest possible RAM address.
		 *  However, these fills are not going via the CPU's cache,
		 *  which contains things such as the return address on the
		 *  stack. If we _really_ write this data in the emulator (which
		 *  doesn't emulate the cache), it would overwrite the stack.
		 *
		 *  So let's not.
		 *
		 *  (If some guest OS or firmware variant actually depends on
		 *  the ability to write to the start of memory this way,
		 *  it would not work in the emulator.)
		 */
		// if (dst0 >= 0x40000000 && dst0 < 0x40004000 && dst1 > 0x40004000) {
		//	dst0 += 0x4000;
		//	dstlen -= 0x4000;
		// }

		/*
		 *  HUH?
		 *
		 *  Note that due to a bug (?) in the PROM firmware when it
		 *  is setting up the TLB entries, only every _second_ page is
		 *  actually put correctly in the TLB. This means that even
		 *  though it then tries to fill 0x40000000 .. 0x40007fff with
		 *  zeroes, it only fills 0x40001000 .. 0x40001fff,
		 *  0x40003000 .. 0x40003fff and so on!
		 *
		 *  So the Horrible hack above is not needed.
		 *
		 *  Ironic.
		 */

		memset(zerobuf, bg, dstlen < sizeof(zerobuf) ? dstlen : sizeof(zerobuf));
		fill_addr = dst0;
		while (dstlen != 0) {
			uint64_t fill_len;
			if (dstlen > sizeof(zerobuf))
				fill_len = sizeof(zerobuf);
			else
				fill_len = dstlen;

			uint64_t starting_page = fill_addr & ~0xfff;
			uint64_t ending_page = (fill_addr + fill_len - 1) & ~0xfff;
			if (starting_page != ending_page) {
				fill_len = starting_page + 4096 - fill_addr;
			}

			// Find starting_page in the TLB in question.
			starting_page >>= 12;
			uint32_t *tlb = dst_tlb == MTE_TLB_LIN_A ? d->re_linear_a : d->re_linear_b;
			bool match = false;
			for (int i = 0; i < 32; ++i) {
				uint32_t entry = tlb[i];
				if (entry & 0x80000000) {
					entry &= ~0x80000000;
					if (entry == starting_page) {
						match = true;
						break;
					}
				}
			}
			
			if (match) {
				cpu->memory_rw(cpu, cpu->mem, fill_addr, zerobuf, fill_len,
					MEM_WRITE, NO_EXCEPTIONS | PHYSICAL);
			} else {
				debug("[ sgi_mte: WARNING: address 0x%x not found in TLB? Ignoring fill. ]\n",
					(long long)fill_addr);
			}

			fill_addr += fill_len;
			dstlen -= sizeof(zerobuf);
		}
		break;
	default:
		fatal("[ sgi_mte: TODO! unimplemented dst_tlb 0x%x ]", dst_tlb);
	}
}

DEVICE_ACCESS(sgi_mte)
{
	struct sgi_re_data *d = (struct sgi_re_data *) extra;
	uint64_t idata = 0, odata = 0;
	int regnr;
	bool startFlag = relative_addr & CRIME_DE_START ? true : false;

	relative_addr &= ~CRIME_DE_START;

	idata = memory_readmax64(cpu, data, len);
	regnr = relative_addr / sizeof(uint32_t);

	relative_addr += 0x3000;

	/*
	 *  Treat all registers as read/write, by default.  Sometimes these
	 *  are accessed as 32-bit words, sometimes as 64-bit words.
	 *
	 *  NOTE: The lowest bits are internally stored in the "low" (+0)
	 *  register, and the higher bits are stored in the "+1" word.
	 */
	if (len == 4) {
		if (writeflag == MEM_WRITE)
			d->mte_reg[regnr] = idata;
		else
			odata = d->mte_reg[regnr];
	} else if (len != 4) {
		if (writeflag == MEM_WRITE) {
			d->mte_reg[regnr+1] = idata >> 32;
			d->mte_reg[regnr] = idata;
		} else {
			odata = ((uint64_t)d->mte_reg[regnr+1] << 32) +
			    d->mte_reg[regnr];
		}
	} else {
		fatal("[ sgi_mte: UNIMPLEMENTED read/write len %i ]\n", len);
		exit(1);
	}

	switch (relative_addr) {

	case CRIME_MTE_MODE:
		debug("[ sgi_mte: %s CRIME_MTE_MODE: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_MTE_BYTEMASK:
		debug("[ sgi_mte: %s CRIME_MTE_BYTEMASK: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_MTE_STIPPLEMASK:
		fatal("[ sgi_mte: %s CRIME_MTE_STIPPLEMASK: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_MTE_BG:
		debug("[ sgi_mte: %s CRIME_MTE_BG: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_MTE_SRC0:
		debug("[ sgi_mte: %s CRIME_MTE_SRC0: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_MTE_SRC1:
		debug("[ sgi_mte: %s CRIME_MTE_SRC1: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_MTE_DST0:
		debug("[ sgi_mte: %s CRIME_MTE_DST0: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_MTE_DST1:
		debug("[ sgi_mte: %s CRIME_MTE_DST1: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_MTE_SRC_Y_STEP:
		debug("[ sgi_mte: %s CRIME_MTE_SRC_Y_STEP: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_MTE_DST_Y_STEP:
		debug("[ sgi_mte: %s CRIME_MTE_DST_Y_STEP: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_MTE_NULL:
		fatal("[ sgi_mte: %s CRIME_MTE_NULL: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_MTE_FLUSH:
		fatal("[ sgi_mte: %s CRIME_MTE_FLUSH: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	default:
		if (writeflag == MEM_WRITE)
			fatal("[ sgi_mte: unimplemented write to "
			    "address 0x%llx, data=0x%016llx ]\n",
			    (long long)relative_addr, (long long)idata);
		else
			fatal("[ sgi_mte: unimplemented read from address"
			    " 0x%llx ]\n", (long long)relative_addr);
	}

	if (startFlag)
		do_mte_transfer(cpu, d);

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_sgi_mte_init():
 */
void dev_sgi_mte_init(struct memory *mem, uint64_t baseaddr, struct sgi_re_data *d)
{
	memory_device_register(mem, "sgi_mte", baseaddr, DEV_SGI_MTE_LENGTH,
	    dev_sgi_mte_access, (void *)d, DM_DEFAULT, NULL);
}


/****************************************************************************/

/*
 *  SGI "de_status".
 */

DEVICE_ACCESS(sgi_de_status)
{
	// struct sgi_re_data *d = (struct sgi_re_data *) extra;
	uint64_t idata = 0, odata = 0;

	idata = memory_readmax64(cpu, data, len);

	relative_addr += 0x4000;

	switch (relative_addr) {

	case CRIME_DE_STATUS:	// 0x4000
		odata = CRIME_DE_IDLE |
			CRIME_DE_SETUP_IDLE |
			CRIME_DE_PIXPIPE_IDLE |
			CRIME_DE_MTE_IDLE;

		/*
		 *  TODO: Actually simulate pipeline of a number of commands?
		 */
		break;

	case 0x4008:
		/*  Unknown. Ignore for now.  */
		break;

	default:
		if (writeflag == MEM_WRITE)
			debug("[ sgi_de_status: unimplemented write to "
			    "address 0x%llx, data=0x%016llx ]\n",
			    (long long)relative_addr, (long long)idata);
		else
			debug("[ sgi_de_status: unimplemented read from address"
			    " 0x%llx ]\n", (long long)relative_addr);
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_sgi_de_status_init():
 */
void dev_sgi_de_status_init(struct memory *mem, uint64_t baseaddr, struct sgi_re_data *d)
{
	memory_device_register(mem, "sgi_de_status", baseaddr, DEV_SGI_DE_STATUS_LENGTH,
	    dev_sgi_de_status_access, (void *)d, DM_DEFAULT, NULL);
}


