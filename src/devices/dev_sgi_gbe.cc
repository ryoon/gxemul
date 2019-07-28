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
 *  COMMENT: SGI "Graphics Back End", graphics controller + framebuffer
 *
 *  Guesswork, based on how Linux, NetBSD, and OpenBSD use the graphics on
 *  the SGI O2. Using NetBSD terminology (from crmfbreg.h):
 *
 *  dev_sgi_re.cc:
 *	0x15001000	rendering engine (TLBs)
 *	0x15002000	drawing engine
 *	0x15003000	memory transfer engine
 *	0x15004000	status registers for drawing engine
 *
 *  dev_sgi_gbe.cc:
 *	0x16000000	crm (or GBE) framebuffer control / video output
 *
 *  According to https://www.linux-mips.org/wiki/GBE, the GBE is also used in
 *  the SGI Visual Workstation.
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


/*  Let's hope nothing is there already...  */
#define	FAKE_GBE_FB_ADDRESS	0x380000000


// #define	GBE_DEBUG
// #define debug fatal

#define	GBE_DEFAULT_XRES		1280
#define	GBE_DEFAULT_YRES		1024
#define	GBE_DEFAULT_BITDEPTH		8


struct sgi_gbe_data {
	// CRM / GBE registers:
	uint32_t	ctrlstat;		/* 0x00000  */
	uint32_t	dotclock;		/* 0x00004  */
	uint32_t	i2c;			/* 0x00008  */
	uint32_t	i2cfp;			/* 0x00010  */

	uint32_t	freeze;	/* and xy */	/* 0x10000  */
	uint32_t	y_intr01;		/* 0x10020  */
	uint32_t	y_intr23;		/* 0x10024  */

	uint32_t	ovr_tilesize;		/* 0x20000  */
	uint32_t	ovr_control;		/* 0x2000c  */

	uint32_t	tilesize;		/* 0x30000  */
	uint32_t	frm_control;		/* 0x3000c  */

	uint32_t	palette[32 * 256];	/* 0x50000  */

	uint32_t	cursor_pos;		/* 0x70000  */
	uint32_t	cursor_control;		/* 0x70004  */
	uint32_t	cursor_cmap0;		/* 0x70008  */
	uint32_t	cursor_cmap1;		/* 0x7000c  */
	uint32_t	cursor_cmap2;		/* 0x70010  */
	uint32_t	cursor_bitmap[64];	/* 0x78000  */

	// Emulator's representation:
	int		xres, yres;
	int 		width_in_tiles;
	int		partial_pixels;
	int 		ovr_width_in_tiles;
	int		ovr_partial_pixels;
	int		bitdepth;
	int		color_mode;
	int		cmap_select;
	uint32_t	selected_palette[256];
	struct vfb_data *fb_data;
};


void get_rgb(struct sgi_gbe_data *d, uint32_t color, uint8_t* r, uint8_t* g, uint8_t* b)
{
	// TODO: Don't switch on color_mode. For overlays, this is always 
	// 8-bit index mode!
	switch (d->color_mode) {
	case CRMFB_MODE_TYP_I8:
		color &= 0xff;
		*r = d->selected_palette[color] >> 24;
		*g = d->selected_palette[color] >> 16;
		*b = d->selected_palette[color] >>  8;
		break;
	case CRMFB_MODE_TYP_RG3B2:	// Used by NetBSD console mode
		*r = 255 * ((color >> 5) & 7) / 7;
		*g = 255 * ((color >> 2) & 7) / 7;
		*b = (color & 3) * 85;
		break;
	case CRMFB_MODE_TYP_RGB8:	// Used by NetBSD's X11 server
		*r = color >> 24;
		*g = color >> 16;
		*b = color >>  8;
		break;
	default:fatal("sgi gbe get_rgb(): unimplemented mode %i\n", d->color_mode);
		exit(1);
	}
}


void select_palette(struct sgi_gbe_data *d, int palette_nr)
{
	memmove(&d->selected_palette[0],
		&d->palette[256 * palette_nr],
		256 * sizeof(uint32_t));
}


/*
 *  dev_sgi_gbe_tick():
 *
 *  Every now and then, copy data from the framebuffer in normal ram
 *  to the actual framebuffer (which will then redraw the window).
 *
 *  NOTE: This is very slow, even slower than the normal emulated framebuffer,
 *  which is already slow as it is.
 *
 *  frm_control contains a pointer to an array of uint16_t. These numbers
 *  (when shifted 16 bits to the left) are pointers to the tiles. Tiles are
 *  512x128 in 8-bit mode, 256x128 in 16-bit mode, and 128x128 in 32-bit mode.
 *
 *  An exception is how Linux/O2 uses the framebuffer, in a "tweaked" mode
 *  which resembles linear mode. This code attempts to support both.
 */
DEVICE_TICK(sgi_gbe)
{
	struct sgi_gbe_data *d = (struct sgi_gbe_data *) extra;
	uint64_t tiletable;
	unsigned char buf[16384];	/*  must be power of 2, at most 65536 */
	int bytes_per_pixel = d->bitdepth / 8;
	int partial_pixels, width_in_tiles;

	if (!cpu->machine->x11_md.in_use)
		return;

	// If not frozen...
	if (!(d->freeze & 0x80000000)) {
		// ... check if the guest OS wants interrupts based on Y:
		if ((d->y_intr01 & CRMFB_INTR_0_MASK) != 0xfff000 ||
		    (d->y_intr01 & CRMFB_INTR_1_MASK) != 0xfff ||
		    (d->y_intr23 & CRMFB_INTR_2_MASK) != 0xfff000 ||
		    (d->y_intr23 & CRMFB_INTR_3_MASK) != 0xfff) {
			fatal("[ sgi_gbe: WARNING: Y interrupts not yet implemented. ]\n");
		}
	}

	// printf("d->frm_control = %08x  d->ovr_control = %08x\n", d->frm_control,d->ovr_control);

	// NetBSD's crmfbreg.h documents the tileptr as having a "9 bit shift",
	// but IRIX seems to put a value ending in 0x......80 there, and the
	// last part of that address seems to matter.
	// TODO: Double-check this with the real hardware.
	
	if (d->ovr_control & CRMFB_DMA_ENABLE) {
		tiletable = (d->ovr_control & 0xffffff80);
		bytes_per_pixel = 1;
		partial_pixels = d->ovr_partial_pixels;
		width_in_tiles = d->ovr_width_in_tiles;
		select_palette(d, 17);	// TODO: is it always palette nr 17 for overlays?
	} else if (d->frm_control & CRMFB_DMA_ENABLE) {
		tiletable = (d->frm_control & 0xffffff80);
		partial_pixels = d->partial_pixels;
		width_in_tiles = d->width_in_tiles;
		select_palette(d, d->cmap_select);
	} else {
		return;
	}

#ifdef GBE_DEBUG
	fatal("[ sgi_gbe: dev_sgi_gbe_tick(): tiletable = 0x%llx, bytes_per_pixel = %i ]\n", (long long)tiletable,
		bytes_per_pixel);
#endif

	if (tiletable == 0)
		return;

	// Nr of tiles horizontally:
	int w = width_in_tiles + (partial_pixels > 0 ? 1 : 0);

	// Actually, the number of tiles vertically is usually very few,
	// but this algorithm will render "up to" 256 and abort as soon
	// as the screen is filled instead. This makes it work for both
	// Linux' "tweaked linear" mode and all the other guest OSes.
	const int max_nr_of_tiles = 256;
	
	uint32_t tile[max_nr_of_tiles];
	uint8_t alltileptrs[max_nr_of_tiles * sizeof(uint16_t)];
	
	cpu->memory_rw(cpu, cpu->mem, tiletable,
	    alltileptrs, sizeof(alltileptrs), MEM_READ,
	    NO_EXCEPTIONS | PHYSICAL);

	for (int i = 0; i < 256; ++i) {
		tile[i] = (256 * alltileptrs[i*2] + alltileptrs[i*2+1]) << 16;
#ifdef GBE_DEBUG
		if (tile[i] != 0)
			printf("tile[%i] = 0x%08x\n", i, tile[i]);
#endif
	}

	int screensize = d->xres * d->yres * 3;
	int x = 0, y = 0;

	for (int tiley = 0; tiley < max_nr_of_tiles; ++tiley) {
		for (int line = 0; line < 128; ++line) {
			for (int tilex = 0; tilex < w; ++tilex) {
				int tilenr = tilex + tiley * w;
				
				if (tilenr >= max_nr_of_tiles)
					continue;
				
				uint32_t base = tile[tilenr];
				
				if (base == 0)
					continue;
				
				// Read one line of up to 512 bytes from the tile.
				int len = tilex < width_in_tiles ? 512 : (partial_pixels * bytes_per_pixel);

				cpu->memory_rw(cpu, cpu->mem, base + 512 * line,
				    buf, len, MEM_READ, NO_EXCEPTIONS | PHYSICAL);

				int fb_offset = (x + y * d->xres) * 3;
				int fb_len = (len / bytes_per_pixel) * 3;

				if (fb_offset + fb_len > screensize) {
					fb_len = screensize - fb_offset;
				}
				
				if (fb_len <= 0) {
					tiley = max_nr_of_tiles;  // to break
					tilex = w;
					line = 128;
				}

				uint8_t fb_buf[512 * 3];
				int fb_i = 0;
				for (int i = 0; i < 512; i+=bytes_per_pixel) {
					uint32_t color;
					if (bytes_per_pixel == 1)
						color = buf[i];
					else if (bytes_per_pixel == 2)
						color = (buf[i]<<8) + buf[i+1];
					else // if (bytes_per_pixel == 4)
						color = (buf[i]<<24) + (buf[i+1]<<16)
							+ (buf[i+2]<<8)+buf[i+3];
					get_rgb(d, color,
					    &fb_buf[fb_i],
					    &fb_buf[fb_i+1],
					    &fb_buf[fb_i+2]);
					fb_i += 3;
				}

				dev_fb_access(cpu, cpu->mem, fb_offset,
				    fb_buf, fb_len, MEM_WRITE, d->fb_data);

				x += len / bytes_per_pixel;
				if (x >= d->xres) {
					x -= d->xres;
					++y;
					if (y >= d->yres) {
						tiley = max_nr_of_tiles; // to break
						tilex = w;
						line = 128;
					}
				}
			}
		}
	}

	if (d->cursor_control & CRMFB_CURSOR_ON) {
		int16_t cx = d->cursor_pos & 0xffff;
		int16_t cy = d->cursor_pos >> 16;

		if (d->cursor_control & CRMFB_CURSOR_CROSSHAIR) {
			uint8_t pixel[3];
			pixel[0] = d->cursor_cmap0 >> 24;
			pixel[1] = d->cursor_cmap0 >> 16;
			pixel[2] = d->cursor_cmap0 >> 8;

			if (cx >= 0 && cx < d->xres) {
				for (y = 0; y < d->yres; ++y)
					dev_fb_access(cpu, cpu->mem, (cx + y * d->xres) * 3,
					    pixel, 3, MEM_WRITE, d->fb_data);
			}

			// TODO: Rewrite as a single framebuffer block write?
			if (cy >= 0 && cy < d->yres) {
				for (x = 0; x < d->xres; ++x)
					dev_fb_access(cpu, cpu->mem, (x + cy * d->xres) * 3,
					    pixel, 3, MEM_WRITE, d->fb_data);
			}
		} else {
			uint8_t pixel[3];
			int sx, sy;

			for (int dy = 0; dy < 32; ++dy) {
				for (int dx = 0; dx < 32; ++dx) {
					sx = cx + dx;
					sy = cy + dy;
					
					if (sx < 0 || sx >= d->xres ||
					    sy < 0 || sy >= d->yres)
						continue;
					
					int wordindex = dy*2 + (dx>>4);
					uint32_t word = d->cursor_bitmap[wordindex];
					
					int color = (word >> ((15 - (dx&15))*2)) & 3;
					
					if (!color)
						continue;

					if (color == 1) {
						pixel[0] = d->cursor_cmap0 >> 24;
						pixel[1] = d->cursor_cmap0 >> 16;
						pixel[2] = d->cursor_cmap0 >> 8;
					} else if (color == 2) {
						pixel[0] = d->cursor_cmap1 >> 24;
						pixel[1] = d->cursor_cmap1 >> 16;
						pixel[2] = d->cursor_cmap1 >> 8;
					} else {
						pixel[0] = d->cursor_cmap2 >> 24;
						pixel[1] = d->cursor_cmap2 >> 16;
						pixel[2] = d->cursor_cmap2 >> 8;
					}

					dev_fb_access(cpu, cpu->mem, (sx + sy * d->xres) * 3,
					    pixel, 3, MEM_WRITE, d->fb_data);
				}
			}
		}
	}
}


DEVICE_ACCESS(sgi_gbe)
{
	struct sgi_gbe_data *d = (struct sgi_gbe_data *) extra;
	uint64_t idata = 0, odata = 0;

	if (writeflag == MEM_WRITE) {
		idata = memory_readmax64(cpu, data, len);

#ifdef GBE_DEBUG
		fatal("[ sgi_gbe: DEBUG: write to address 0x%llx, data"
		    "=0x%llx ]\n", (long long)relative_addr, (long long)idata);
#endif
	}

	switch (relative_addr) {

	case CRMFB_CTRLSTAT:	// 0x0
		if (writeflag == MEM_WRITE) {
			debug("[ sgi_gbe: write to ctrlstat: 0x%08x ]\n", (int)idata);
			d->ctrlstat = (idata & ~CRMFB_CTRLSTAT_CHIPID_MASK)
				| (d->ctrlstat & CRMFB_CTRLSTAT_CHIPID_MASK);
		} else
			odata = d->ctrlstat;
		break;

	case CRMFB_DOTCLOCK:	// 0x4
		if (writeflag == MEM_WRITE)
			d->dotclock = idata;
		else
			odata = d->dotclock;
		break;

	case CRMFB_I2C_VGA:	// 0x8
		/*
		 *  "CRT I2C control".
		 *
		 *  I'm not sure what this does. It isn't really commented
		 *  in the Linux sources.  The IP32 PROM writes the values
		 *  0x03, 0x01, and then 0x00 to this address, and then
		 *  reads back a value.
		 */
		if (writeflag == MEM_WRITE) {
			//if (!(d->i2c & CRMFB_I2C_SCL) &&
			//    (idata & CRMFB_I2C_SCL)) {
			//	fatal("vga i2c data: %i\n", idata & CRMFB_I2C_SDA);
			//}

			d->i2c = idata;
		} else {
			odata = d->i2c;
			odata |= 1;	/*  ?  The IP32 prom wants this?  */
		}
		break;

	case CRMFB_I2C_FP:	// 0x10, i2cfp, flat panel control
		if (writeflag == MEM_WRITE) {
			//if (d->i2c & CRMFB_I2C_SCL &&
			//    !(idata & CRMFB_I2C_SCL)) {
			//	fatal("fp i2c data: %i\n", idata & CRMFB_I2C_SDA);
			//}

			d->i2cfp = idata;
		} else {
			odata = d->i2cfp;
			odata |= 1;	/*  ?  The IP32 prom wants this?  */
		}
		break;

	case CRMFB_DEVICE_ID:	// 0x14
		odata = CRMFB_DEVICE_ID_DEF;
		break;

	case CRMFB_VT_XY:	// 0x10000
		if (writeflag == MEM_WRITE)
			d->freeze = idata & 0x80000000;
		else {
			/*
			 *  vt_xy, according to Linux:
			 *
			 * bit 31 = freeze, 23..12 = cury, 11.0 = curx
			 */
			/*  odata = ((random() % (d->yres + 10)) << 12)
			    + (random() % (d->xres + 10)) +
			    d->freeze;  */

			/*
			 *  Hack for IRIX/IP32. During startup, it waits for
			 *  the value to be over 0x400 (in "gbeRun").
			 *
			 *  Hack for the IP32 PROM: During startup, it waits
			 *  for the value to be above 0x500 (I think).
			 */
			odata = d->freeze | (random() & 1 ? 0x3ff : 0x501);
		}
		break;

	case CRMFB_VT_XYMAX:	// 0x10004, vt_xymax, according to Linux & NetBSD
		odata = ((d->yres-1) << 12) + d->xres-1;
		/*  ... 12 bits maxy, 12 bits maxx.  */
		break;

	case CRMFB_VT_VSYNC:	// 0x10008
	case CRMFB_VT_HSYNC:	// 0x1000c
	case CRMFB_VT_VBLANK:	// 0x10010
	case CRMFB_VT_HBLANK:	// 0x10014
		// TODO
		break;

	case CRMFB_VT_FLAGS:	// 0x10018
		// OpenBSD/sgi writes to this register.
		break;

	case CRMFB_VT_FRAMELOCK:	// 0x1001c
		// TODO.
		break;

	case CRMFB_VT_INTR01:	// 0x10020
		if (writeflag == MEM_WRITE)
			d->y_intr01 = idata;
		break;

	case CRMFB_VT_INTR23:	// 0x10024
		if (writeflag == MEM_WRITE)
			d->y_intr23 = idata;
		break;

	case 0x10028:	// 0x10028
	case 0x1002c:	// 0x1002c
	case 0x10030:	// 0x10030
		// TODO: Unknown, written to by the PROM?
		break;

	case CRMFB_VT_HPIX_EN:	// 0x10034, vt_hpixen, according to Linux
		odata = (0 << 12) + d->xres-1;
		/*  ... 12 bits on, 12 bits off.  */
		break;

	case CRMFB_VT_VPIX_EN:	// 0x10038, vt_vpixen, according to Linux
		odata = (0 << 12) + d->yres-1;
		/*  ... 12 bits on, 12 bits off.  */
		break;

	case CRMFB_VT_HCMAP:	// 0x1003c
		if (writeflag == MEM_WRITE) {
			d->xres = (idata & CRMFB_HCMAP_ON_MASK) >> CRMFB_VT_HCMAP_ON_SHIFT;
			dev_fb_resize(d->fb_data, d->xres, d->yres);
		}
		
		odata = (d->xres << CRMFB_VT_HCMAP_ON_SHIFT) + d->xres + 100;
		break;

	case CRMFB_VT_VCMAP:	// 0x10040
		if (writeflag == MEM_WRITE) {
			d->yres = (idata & CRMFB_VCMAP_ON_MASK) >> CRMFB_VT_VCMAP_ON_SHIFT;
			dev_fb_resize(d->fb_data, d->xres, d->yres);
		}

		odata = (d->yres << CRMFB_VT_VCMAP_ON_SHIFT) + d->yres + 100;
		break;

	case CRMFB_VT_DID_STARTXY:	// 0x10044
	case CRMFB_VT_CRS_STARTXY:	// 0x10048
	case CRMFB_VT_VC_STARTXY:	// 0x1004c
		// TODO
		break;
		
	case CRMFB_OVR_WIDTH_TILE:	// 0x20000
		if (writeflag == MEM_WRITE) {
			d->ovr_tilesize = idata;

			d->ovr_width_in_tiles = (idata >> CRMFB_FRM_TILESIZE_WIDTH_SHIFT) & 0xff;
			d->ovr_partial_pixels = ((idata >> CRMFB_FRM_TILESIZE_RHS_SHIFT) & 0x1f) * 32;

			debug("[ sgi_gbe: OVR setting width in tiles = %i, partial pixels = %i ]\n",
			    d->ovr_width_in_tiles, d->ovr_partial_pixels);
		} else
			odata = d->ovr_tilesize;
		break;

	case CRMFB_OVR_TILE_PTR:	// 0x20004
		odata = d->ovr_control ^ (random() & 1);
		break;

	case CRMFB_OVR_CONTROL:		// 0x20008
		if (writeflag == MEM_WRITE)
			d->ovr_control = idata;
		else
			odata = d->ovr_control;
		break;

	case CRMFB_FRM_TILESIZE:	// 0x30000:
		if (writeflag == MEM_WRITE) {
			d->tilesize = idata;

			d->bitdepth = 8 << ((d->tilesize >> CRMFB_FRM_TILESIZE_DEPTH_SHIFT) & 3);
			d->width_in_tiles = (idata >> CRMFB_FRM_TILESIZE_WIDTH_SHIFT) & 0xff;
			d->partial_pixels = ((idata >> CRMFB_FRM_TILESIZE_RHS_SHIFT) & 0x1f) * 32 * 8 / d->bitdepth;

			debug("[ sgi_gbe: setting color depth to %i bits, width in tiles = %i, partial pixels = %i ]\n",
			    d->bitdepth, d->width_in_tiles, d->partial_pixels);
		} else
			odata = d->tilesize;
		break;

	case CRMFB_FRM_PIXSIZE:	// 0x30004
		if (writeflag == MEM_WRITE) {
			debug("[ sgi_gbe: setting PIXSIZE to 0x%08x ]\n", (int)idata);
		}
		break;
		
	case 0x30008:
		// TODO: Figure out exactly what the low bits do.
		// Irix seems to want 0x20 to "sometimes" be on or off here.
		odata = d->frm_control ^ (random() & 0x20);
		break;

	case CRMFB_FRM_CONTROL:	// 0x3000c
		/*
		 *  Writes to 3000c should be readable back at 30008?
		 *  At least bit 0 (dma) ctrl 3.
		 */
		if (writeflag == MEM_WRITE) {
			d->frm_control = idata;
			debug("[ sgi_gbe: frm_control = 0x%08x ]\n", d->frm_control);
		} else
			odata = d->frm_control;
		break;

	case CRMFB_DID_PTR:	// 0x40000
		odata = random();	/*  IP32 prom test hack. TODO  */
		/*  IRIX wants 0x20, it seems.  */
		if (random() & 1)
			odata = 0x20;
		break;

	case CRMFB_DID_CONTROL:	// 0x40004
		// TODO
		break;

	case CRMFB_CMAP_FIFO:		// 0x58000
		break;

	case CRMFB_CURSOR_POS:		// 0x70000
		if (writeflag == MEM_WRITE)
			d->cursor_pos = idata;
		else
			odata = d->cursor_pos;
		break;
		
	case CRMFB_CURSOR_CONTROL:	// 0x70004
		if (writeflag == MEM_WRITE)
			d->cursor_control = idata;
		else
			odata = d->cursor_control;
		break;
		
	case CRMFB_CURSOR_CMAP0:	// 0x70008
		if (writeflag == MEM_WRITE)
			d->cursor_cmap0 = idata;
		else
			odata = d->cursor_cmap0;
		break;
		
	case CRMFB_CURSOR_CMAP1:	// 0x7000c
		if (writeflag == MEM_WRITE)
			d->cursor_cmap1 = idata;
		else
			odata = d->cursor_cmap1;
		break;
		
	case CRMFB_CURSOR_CMAP2:	// 0x70010
		if (writeflag == MEM_WRITE)
			d->cursor_cmap2 = idata;
		else
			odata = d->cursor_cmap2;
		break;

	/*
	 *  Linux/sgimips seems to write color palette data to offset 0x50000
	 *  to 0x503xx, and gamma correction data to 0x60000 - 0x603ff, as
	 *  32-bit values at addresses divisible by 4 (formated as 0xrrggbb00).
	 *
	 *  "sgio2fb: initializing
	 *   sgio2fb: I/O at 0xffffffffb6000000
	 *   sgio2fb: tiles at ffffffffa2ef5000
	 *   sgio2fb: framebuffer at ffffffffa1000000
	 *   sgio2fb: 8192kB memory
	 *   Console: switching to colour frame buffer device 80x30"
	 *
	 *  NetBSD's crmfb_set_palette, however, uses values in reverse, like this:
	 *	val = (r << 8) | (g << 16) | (b << 24);
	 */

	default:
		/*  WID at 0x48000 .. 0x48000 + 4*31:  */
		if (relative_addr >= CRMFB_WID && relative_addr <= CRMFB_WID + 4 * 31) {
			// TODO: Figure out how this really works. Why are
			// there 32 such registers?
			if (writeflag == MEM_WRITE) {
				d->color_mode = (idata >> CRMFB_MODE_TYP_SHIFT) & 7;
				d->cmap_select = (idata >> CRMFB_MODE_CMAP_SELECT_SHIFT) & 0x1f;
			}

			break;
		}

		/*  RGB Palette at 0x50000 .. 0x57fff:  */
		if (relative_addr >= CRMFB_CMAP && relative_addr < CRMFB_CMAP + 256 * 32 * sizeof(uint32_t)) {
			int color_index = (relative_addr - CRMFB_CMAP) >> 2;
			if (writeflag == MEM_WRITE) {
				int cmap = color_index >> 8;
				d->palette[color_index] = idata;
				if (cmap == d->cmap_select)
					d->selected_palette[color_index] = idata;
			} else {
				odata = d->palette[color_index];
			}
			break;
		}

		/*  Gamma correction at 0x60000 .. 0x603ff:  */
		if (relative_addr >= CRMFB_GMAP && relative_addr <= CRMFB_GMAP + 0x3ff) {
			/*  ignore gamma correction for now  */
			break;
		}

		/*  Cursor bitmap at 0x78000 ..:  */
		if (relative_addr >= CRMFB_CURSOR_BITMAP && relative_addr <= CRMFB_CURSOR_BITMAP + 0xff) {
			if (len != 4) {
				printf("unimplemented CRMFB_CURSOR_BITMAP len %i\n", (int)len);
			}

			int index = (relative_addr & 0xff) / 4;
			if (writeflag == MEM_WRITE)
				d->cursor_bitmap[index] = idata;
			else
				odata = d->cursor_bitmap[index];
			break;
		}

		if (writeflag == MEM_WRITE)
			fatal("[ sgi_gbe: unimplemented write to address "
			    "0x%llx, data=0x%llx ]\n",
			    (long long)relative_addr, (long long)idata);
		else
			fatal("[ sgi_gbe: unimplemented read from address "
			    "0x%llx ]\n", (long long)relative_addr);
	}

	if (writeflag == MEM_READ) {
#ifdef GBE_DEBUG
		debug("[ sgi_gbe: DEBUG: read from address 0x%llx: 0x%llx ]\n",
		    (long long)relative_addr, (long long)odata);
#endif
		memory_writemax64(cpu, data, len, odata);
	}

	return 1;
}


void dev_sgi_gbe_init(struct machine *machine, struct memory *mem, uint64_t baseaddr)
{
	struct sgi_gbe_data *d;

	CHECK_ALLOCATION(d = (struct sgi_gbe_data *) malloc(sizeof(struct sgi_gbe_data)));
	memset(d, 0, sizeof(struct sgi_gbe_data));

	d->xres = GBE_DEFAULT_XRES;
	d->yres = GBE_DEFAULT_YRES;
	d->bitdepth = GBE_DEFAULT_BITDEPTH;
	
	// My O2 says 0x300ae001 here (while running).
	d->ctrlstat = CRMFB_CTRLSTAT_INTERNAL_PCLK |
			CRMFB_CTRLSTAT_GPIO6_INPUT |
			CRMFB_CTRLSTAT_GPIO5_INPUT |
			CRMFB_CTRLSTAT_GPIO4_INPUT |
			CRMFB_CTRLSTAT_GPIO4_SENSE |
			CRMFB_CTRLSTAT_GPIO3_INPUT |
			(CRMFB_CTRLSTAT_CHIPID_MASK & 1);

	// Set a value in the interrupt register that will "never happen" by default.
	d->y_intr01 = (0xfff << 12) | 0xfff;
	d->y_intr23 = (0xfff << 12) | 0xfff;

	// Grayscale palette, most likely overwritten immediately by the
	// guest operating system.
	for (int i = 0; i < 256; ++i)
		d->palette[i] = i * 0x01010100;

	d->fb_data = dev_fb_init(machine, mem, FAKE_GBE_FB_ADDRESS,
	    VFB_GENERIC, d->xres, d->yres, d->xres, d->yres, 24, "SGI GBE");

	memory_device_register(mem, "sgi_gbe", baseaddr, DEV_SGI_GBE_LENGTH,
	    dev_sgi_gbe_access, d, DM_DEFAULT, NULL);
	machine_add_tickfunction(machine, dev_sgi_gbe_tick, d, 19);
}


