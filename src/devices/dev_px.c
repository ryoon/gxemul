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
 *  $Id: dev_px.c,v 1.2 2004-03-07 03:55:40 debug Exp $
 *  
 *  TURBOchannel Pixelstamp ("PX", "PXG") graphics device.
 *
 *  See include/pxreg.h (and NetBSD's arch/pmax/dev/px.c) for more information.
 *
 *  NetBSD recognizes this device as px0, Ultrix as ga0.
 *
 *  TODO:  A lot of stuff:
 *	Scroll/block copy.
 *	Cursor.
 *	Color.
 *	24-bit vs 8-bit.
 *	Make sure that everything works with both NetBSD and Ultrix.
 *	3D?
 *	Don't use so many hardcoded values.
 *	Interrupts?
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "misc.h"
#include "devices.h"

#include "pxreg.h"

#define	PX_DEBUG


/*
 *  dev_px_tick():
 */
void dev_px_tick(struct cpu *cpu, void *extra)
{
	struct px_data *d = extra;

#if 0
	if (d->intr & STIC_INT_P_EN)		/*  or _WE ?  */
		cpu_interrupt(cpu, d->irq_nr);
#endif
}


/*
 *  dev_px_dma():
 */
void dev_px_dma(struct cpu *cpu, uint32_t sys_addr, struct px_data *d)
{
	unsigned char dma_buf[32768];
	int dma_len = sizeof(dma_buf);
	int i;
	uint32_t cmdword;

	dma_len = 56 * 4;	/*  TODO: this is just enough for NetBSD's putchar  */
	memory_rw(cpu, cpu->mem, sys_addr, dma_buf, dma_len, MEM_READ, NO_EXCEPTIONS | PHYSICAL);

	if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
		cmdword = dma_buf[0] + (dma_buf[1] << 8) + (dma_buf[2] << 16) + (dma_buf[3] << 24);
	else
		cmdword = dma_buf[3] + (dma_buf[2] << 8) + (dma_buf[1] << 16) + (dma_buf[0] << 24);

#ifdef PX_DEBUG
	debug("[ px: dma from 0x%08x: ", (int)sys_addr);

	debug("cmd=");
	switch (cmdword & 0xf) {
	case STAMP_CMD_POINTS:		debug("points");	break;
	case STAMP_CMD_LINES:		debug("lines");		break;
	case STAMP_CMD_TRIANGLES:	debug("triangles");	break;
	case STAMP_CMD_COPYSPANS:	debug("copyspans");	break;
	case STAMP_CMD_READSPANS:	debug("readspans");	break;
	case STAMP_CMD_WRITESPANS:	debug("writespans");	break;
	case STAMP_CMD_VIDEO:		debug("video");		break;
	default:
		debug("0x%x (?)", cmdword & 0xf);
	}

	debug(",rgb=");
	switch (cmdword & 0x30) {
	case STAMP_RGB_NONE:	debug("none");		break;
	case STAMP_RGB_CONST:	debug("const");		break;
	case STAMP_RGB_FLAT:	debug("flat");		break;
	case STAMP_RGB_SMOOTH:	debug("smooth");	break;
	default:
		debug("0x%x (?)", cmdword & 0x30);
	}

	debug(",z=");
	switch (cmdword & 0xc0) {
	case STAMP_Z_NONE:	debug("none");		break;
	case STAMP_Z_CONST:	debug("const");		break;
	case STAMP_Z_FLAT:	debug("flat");		break;
	case STAMP_Z_SMOOTH:	debug("smooth");	break;
	default:
		debug("0x%x (?)", cmdword & 0xc0);
	}

	debug(",xy=");
	switch (cmdword & 0x300) {
	case STAMP_XY_NONE:		debug("none");		break;
	case STAMP_XY_PERPACKET:	debug("perpacket");	break;
	case STAMP_XY_PERPRIMATIVE:	debug("perprimative");	break;
	default:
		debug("0x%x (?)", cmdword & 0x300);
	}

	debug(",lw=");
	switch (cmdword & 0xc00) {
	case STAMP_LW_NONE:		debug("none");		break;
	case STAMP_LW_PERPACKET:	debug("perpacket");	break;
	case STAMP_LW_PERPRIMATIVE:	debug("perprimative");	break;
	default:
		debug("0x%x (?)", cmdword & 0xc00);
	}

	if (cmdword & STAMP_CLIPRECT)
		debug(",CLIPRECT");
	if (cmdword & STAMP_MESH)
		debug(",MESH");
	if (cmdword & STAMP_AALINE)
		debug(",AALINE");
	if (cmdword & STAMP_HS_EQUALS)
		debug(",HS_EQUALS");

	for (i=0; i<dma_len; i++)
		debug(" %02x", dma_buf[i]);
	debug(" ]\n");
#endif	/*  PX_DEBUG  */

	/*  NetBSD and Ultrix copyspans  */
	if (cmdword == 0x405) {
		uint32_t nspans, lw;
		int spannr, ofs;
		uint32_t span_len, span_src, span_dst;
		unsigned char pixels[1280];

		if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
			nspans = dma_buf[4] + (dma_buf[5] << 8) + (dma_buf[6] << 16) + (dma_buf[7] << 24);
		else
			nspans = dma_buf[7] + (dma_buf[6] << 8) + (dma_buf[5] << 16) + (dma_buf[4] << 24);

		if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
			lw = dma_buf[16] + (dma_buf[17] << 8) + (dma_buf[18] << 16) + (dma_buf[19] << 24);
		else
			lw = dma_buf[19] + (dma_buf[18] << 8) + (dma_buf[17] << 16) + (dma_buf[16] << 24);

		nspans >>= 24;
		/*  Why not this?  lw = (lw + 1) >> 2;  */
		debug("px: copyspans:  nspans = %i, lw = %i\n", nspans, lw);

		/*  Reread copyspans command if it wasn't completely read:  */
		if (dma_len < 4*(5 + nspans*3)) {
			dma_len = 4 * (5+nspans*3);
			memory_rw(cpu, cpu->mem, sys_addr, dma_buf, dma_len, MEM_READ, NO_EXCEPTIONS | PHYSICAL);
		}

		ofs = 4*5;
		for (spannr=0; spannr<nspans; spannr++) {
			if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
				span_len = dma_buf[ofs+0] + (dma_buf[ofs+1] << 8) + (dma_buf[ofs+2] << 16) + (dma_buf[ofs+3] << 24);
			else
				span_len = dma_buf[ofs+3] + (dma_buf[ofs+2] << 8) + (dma_buf[ofs+1] << 16) + (dma_buf[ofs+0] << 24);
			ofs += 4;

			if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
				span_src = dma_buf[ofs+0] + (dma_buf[ofs+1] << 8) + (dma_buf[ofs+2] << 16) + (dma_buf[ofs+3] << 24);
			else
				span_src = dma_buf[ofs+3] + (dma_buf[ofs+2] << 8) + (dma_buf[ofs+1] << 16) + (dma_buf[ofs+0] << 24);
			ofs += 4;

			if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
				span_dst = dma_buf[ofs+0] + (dma_buf[ofs+1] << 8) + (dma_buf[ofs+2] << 16) + (dma_buf[ofs+3] << 24);
			else
				span_dst = dma_buf[ofs+3] + (dma_buf[ofs+2] << 8) + (dma_buf[ofs+1] << 16) + (dma_buf[ofs+0] << 24);
			ofs += 4;

			span_len >>= 3;
			span_dst >>= 3;
			span_src >>= 3;

			if (span_len > 1280)
				span_len = 1280;

			/*  debug("    span %i: len=%i src=%i dst=%i\n", spannr, span_len, span_src, span_dst);  */

			dev_fb_access(cpu, cpu->mem, span_src * 1280, pixels, span_len, MEM_READ, d->vfb_data);
			dev_fb_access(cpu, cpu->mem, span_dst * 1280, pixels, span_len, MEM_WRITE, d->vfb_data);
		}
	}

	/*  NetBSD and Ultrix erasecols/eraserows  */
	if (cmdword == 0x411) {
		uint32_t v1, v2, lw;
		int x,y,x2,y2;
		int fb_y;
		unsigned char pixels[1280];
		memset(pixels, 0, sizeof(pixels));	/*  TODO: other colors  */

		if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
			lw = dma_buf[16] + (dma_buf[17] << 8) + (dma_buf[18] << 16) + (dma_buf[19] << 24);
		else
			lw = dma_buf[19] + (dma_buf[18] << 8) + (dma_buf[17] << 16) + (dma_buf[16] << 24);

		if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
			v1 = dma_buf[24] + (dma_buf[25] << 8) + (dma_buf[26] << 16) + (dma_buf[27] << 24);
		else
			v1 = dma_buf[27] + (dma_buf[26] << 8) + (dma_buf[25] << 16) + (dma_buf[24] << 24);

		if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
			v2 = dma_buf[28] + (dma_buf[29] << 8) + (dma_buf[30] << 16) + (dma_buf[31] << 24);
		else
			v2 = dma_buf[31] + (dma_buf[30] << 8) + (dma_buf[29] << 16) + (dma_buf[28] << 24);

		v1 -= lw;
		v2 -= lw;

		x = (v1 >> 19) & 2047;
		y = (v1 >> 3) & 1023;
		x2 = (v2 >> 19) & 2047;
		y2 = (v2 >> 3) & 1023;

		lw = (lw + 1) >> 2;

		debug("px: clear/fill: v1 = 0x%08x  v2 = 0x%08x lw=%i x=%i y=%i x2=%i y2=%i\n", (int)v1, (int)v2, lw, x,y, x2,y2);

		for (fb_y=y; fb_y < y2 + lw; fb_y ++) {
			dev_fb_access(cpu, cpu->mem, fb_y * 1280 + x, pixels, x2-x, MEM_WRITE, d->vfb_data);
		}
	}

	/*  NetBSD and Ultrix putchar  */
	if (cmdword == 0xa21) {
		/*  Ugly test code:  */
		unsigned char pixels[16];
		int pixels_len = 16;
		uint32_t v1, v2;
		int x, y, x2,y2, i, maxi;
		int xbit;
		int suby;

		if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
			v1 = dma_buf[52] + (dma_buf[53] << 8) + (dma_buf[54] << 16) + (dma_buf[55] << 24);
		else
			v1 = dma_buf[55] + (dma_buf[54] << 8) + (dma_buf[53] << 16) + (dma_buf[52] << 24);

		if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
			v2 = dma_buf[56] + (dma_buf[57] << 8) + (dma_buf[58] << 16) + (dma_buf[59] << 24);
		else
			v2 = dma_buf[59] + (dma_buf[58] << 8) + (dma_buf[57] << 16) + (dma_buf[56] << 24);

		x = (v1 >> 19) & 2047;
		y = ((v1 - 63) >> 3) & 1023;
		x2 = (v2 >> 19) & 2047;
		y2 = ((v2 - 63) >> 3) & 1023;

		debug("px putchar: v1 = 0x%08x  v2 = 0x%08x x=%i y=%i\n", (int)v1, (int)v2, x,y, x2,y2);

		x %= 1280;
		y %= 1024;
		x2 %= 1280;
		y2 %= 1024;

		pixels_len = x2 - x;

		suby = 0;
		maxi = 12;
		maxi = 33;

		for (i=4; i<maxi; i++) {
			if (i == 12)
				i = 30;

			for (xbit = 0; xbit < 8; xbit ++) {
				pixels[xbit]     = (dma_buf[i*4 + 0] & (1 << xbit))? 255 : 0;
				pixels[xbit + 8] = (dma_buf[i*4 + 1] & (1 << xbit))? 255 : 0;
			}
			dev_fb_access(cpu, cpu->mem, (y+suby) * 1280 + x, pixels, pixels_len, MEM_WRITE, d->vfb_data);
			for (xbit = 0; xbit < 8; xbit ++) {
				pixels[xbit]     = (dma_buf[i*4 + 2] & (1 << xbit))? 255 : 0;
				pixels[xbit + 8] = (dma_buf[i*4 + 3] & (1 << xbit))? 255 : 0;
			}
			dev_fb_access(cpu, cpu->mem, (y+suby+1) * 1280 + x, pixels, pixels_len, MEM_WRITE, d->vfb_data);
			suby += 2;
		}
	}
}


/*
 *  dev_px_access():
 *
 *  Returns 1 if ok, 0 on error.
 */
int dev_px_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *extra)
{
	uint64_t idata = 0, odata = 0;
	struct px_data *d = extra;

	idata = memory_readmax64(cpu, data, len);

	if (relative_addr < 0x0c0000) {
		/*
		 *  DMA poll:  a read from this address should start a DMA
		 *  transfer, and return 1 in odata while the DMA is in progress (STAMP_BUSY),
		 *  and then 0 (STAMP_OK) once we're done.
		 *
		 *  According to NetBSD's pxreg.h, the following formula gets us from
		 *  system address to DMA address:  (v is the system address)
		 *
		 *	dma_addr = ( ( ((v & ~0x7fff) << 3) | (v & 0x7fff) ) & 0x1ffff800) >> 9;
		 *
		 *  Hopefully, this is a good enough reversal of that formula:
		 *
		 *	sys_addr = ((dma_addr << 9) & 0x7800) +
		 *		   ((dma_addr << 6) & 0xffff8000);
		 */
		uint32_t sys_addr;	/*  system address for DMA transfers  */
		sys_addr = ((relative_addr << 9) & 0x7800) + ((relative_addr << 6) & 0xffff8000);

		/*  If the system address is sane enough, then start a DMA transfer:  */
		if (sys_addr >= 0x4000)
			dev_px_dma(cpu, sys_addr, d);

		/*  Pretend that it was always OK:  */
		odata = STAMP_OK;
	}

	switch (relative_addr) {
	case 0x180008:		/*  hsync  */
		if (writeflag==MEM_READ) {
			debug("[ px: read from hsync: 0x%08llx ]\n", (long long)odata);
		} else {
			debug("[ px: write to hsync: 0x%08llx ]\n", (long long)idata);
		}
		break;
	case 0x18000c:		/*  hsync2  */
		if (writeflag==MEM_READ) {
			debug("[ px: read from hsync2: 0x%08llx ]\n", (long long)odata);
		} else {
			debug("[ px: write to hsync2: 0x%08llx ]\n", (long long)idata);
		}
		break;
	case 0x180010:		/*  hblank  */
		if (writeflag==MEM_READ) {
			debug("[ px: read from hblank: 0x%08llx ]\n", (long long)odata);
		} else {
			debug("[ px: write to hblank: 0x%08llx ]\n", (long long)idata);
		}
		break;
	case 0x180014:		/*  vsync  */
		if (writeflag==MEM_READ) {
			debug("[ px: read from vsync: 0x%08llx ]\n", (long long)odata);
		} else {
			debug("[ px: write to vsync: 0x%08llx ]\n", (long long)idata);
		}
		break;
	case 0x180018:		/*  vblank  */
		if (writeflag==MEM_READ) {
			debug("[ px: read from vblank: 0x%08llx ]\n", (long long)odata);
		} else {
			debug("[ px: write to vblank: 0x%08llx ]\n", (long long)idata);
		}
		break;
	case 0x180020:		/*  ipdvint  */
		if (writeflag==MEM_READ) {
			odata = d->intr;
odata = random();
			debug("[ px: read from ipdvint: 0x%08llx ]\n", (long long)odata);
		} else {
			d->intr = idata;
			if (idata & STIC_INT_E_WE)
				d->intr &= ~STIC_INT_E;
			if (idata & STIC_INT_V_WE)
				d->intr &= ~STIC_INT_V;
			if (idata & STIC_INT_P_WE)
				d->intr &= ~STIC_INT_P;
			debug("[ px: write to ipdvint: 0x%08llx ]\n", (long long)idata);
		}
		break;
	case 0x180028:		/*  sticsr  */
		if (writeflag==MEM_READ) {
			debug("[ px: read from sticsr: 0x%08llx ]\n", (long long)odata);
		} else {
			debug("[ px: write to sticsr: 0x%08llx ]\n", (long long)idata);
		}
		break;
	case 0x180038:		/*  buscsr  */
		if (writeflag==MEM_READ) {
			debug("[ px: read from buscsr: 0x%08llx ]\n", (long long)odata);
		} else {
			debug("[ px: write to buscsr: 0x%08llx ]\n", (long long)idata);
		}
		break;
	case 0x18003c:		/*  modcl  */
		if (writeflag==MEM_READ) {
			odata = d->type << 12;
			debug("[ px: read from modcl: 0x%llx ]\n", (long long)odata);
		} else {
			debug("[ px: write to modcl: 0x%llx ]\n", (long long)idata);
		}
		break;
	default:
		if (writeflag==MEM_READ) {
			debug("[ px: read from addr 0x%x: 0x%llx ]\n", (int)relative_addr, (long long)odata);
		} else {
			debug("[ px: write to addr 0x%x: 0x%llx ]\n", (int)relative_addr, (long long)idata);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_px_init():
 */
void dev_px_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr, int px_type, int irq_nr)
{
	struct px_data *d;

	d = malloc(sizeof(struct px_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct px_data));

	d->type = px_type;
	d->irq_nr = irq_nr;

	d->bitdepth = 24;
	if (d->type == DEV_PX_TYPE_PX || d->type == DEV_PX_TYPE_PXG)
		d->bitdepth = 8;

	d->fb_mem = memory_new(DEFAULT_BITS_PER_PAGETABLE, DEFAULT_BITS_PER_MEMBLOCK, 1280 * 1024 * d->bitdepth / 8, DEFAULT_MAX_BITS);
	if (d->fb_mem == NULL) {
		fprintf(stderr, "dev_px_init(): out of memory (1)\n");
		exit(1);
	}
	d->vfb_data = dev_fb_init(cpu, d->fb_mem, 0, VFB_GENERIC, 1280, 1024, 1280, 1024, d->bitdepth, "PX");
	if (d->vfb_data == NULL) {
		fprintf(stderr, "dev_px_init(): out of memory (2)\n");
		exit(2);
	}

	switch (d->type) {
	case DEV_PX_TYPE_PX:
		dev_bt459_init(mem, baseaddr + 0x200000, d->vfb_data->rgb_palette, 8);
		break;
	case DEV_PX_TYPE_PXG:
		dev_bt459_init(mem, baseaddr + 0x300000, d->vfb_data->rgb_palette, 8);
		fatal("PXG: TODO\n");
		break;
	default:
		fatal("dev_px_init(): unimplemented px_type\n");
	}

	memory_device_register(mem, "px", baseaddr, DEV_PX_LENGTH, dev_px_access, d);
	cpu_add_tickfunction(cpu, dev_px_tick, d, 12);
}

