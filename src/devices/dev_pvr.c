/*
 *  Copyright (C) 2006  Anders Gavare.  All rights reserved.
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
 *  $Id: dev_pvr.c,v 1.6 2006-10-21 09:25:24 debug Exp $
 *  
 *  PowerVR CLX2 (Graphics controller used in the Dreamcast)
 *
 *  http://mc.pp.se/dc/pvr.html has a good overview of how the PVR/TA works.
 *
 *  TODO: Almost everything
 *	x)  Change resolution during runtime? (PAL/NTSC/???)
 *	x)  Change framebuffer layout in memory during runtime
 *		(bits per pixel, location, etc)
 *	x)  3D "Tile Accelerator" engine
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"

#include "dreamcast_pvr.h"


#define debug fatal

#define	INTERNAL_FB_ADDR	0x300000000ULL

struct pvr_data {
	struct vfb_data		*fb;

	/*  PVR registers:  */
	uint32_t		reg[PVRREG_REGSIZE / sizeof(uint32_t)];

	/*  Calculated by pvr_geometry_updated():  */
	int			xsize, ysize;

	/*  Cached values (from registers):  */
	/*  DIWMODE:  */
	int			clock_double;
	int			strip_buffer_enabled;
	int			strip_length;
	int			argb8888_threshold;
	int			extend;
	int			pixelmode;
	int			line_double;
	int			display_enabled;
	/*  SYNCCONF:  */
	int			video_enabled;
	int			broadcast_standard;
	int			interlaced;
	int			h_sync_positive;
	int			v_sync_positive;
	/*  TILEBUF_SIZE:  */
	int			tilebuf_xsize;
	int			tilebuf_ysize;

	/*  Tile Accelerator Command Compiler List:  */
	uint32_t		ta[64 / sizeof(uint32_t)];

	uint8_t			*vram;
};


#define	REG(x)		(d->reg[(x)/sizeof(uint32_t)])
#define	DEFAULT_WRITE	REG(relative_addr) = idata;


/*
 *  pvr_fb_invalidate():
 */
static void pvr_fb_invalidate(struct pvr_data *d, int start, int stop)
{
}


/*
 *  pvr_geometry_updated():
 *
 *  This function should be called every time a register is written to which
 *  affects the framebuffer geometry (size, bit-depth, starting position, etc).
 */
static void pvr_geometry_updated(struct pvr_data *d)
{
	d->xsize = (REG(PVRREG_DIWSIZE) >> DIWSIZE_DPL_SHIFT) & DIWSIZE_MASK;
	d->ysize = (REG(PVRREG_DIWSIZE) >> DIWSIZE_LPF_SHIFT) & DIWSIZE_MASK;

	/*  E.g. 319x479  =>  320x480  */
	d->xsize ++; d->ysize ++;

	if (d->clock_double)
		d->xsize *= 2;

	if (d->line_double)
		d->ysize /= 2;

	/*  Only show geometry debug message if output is enabled:  */
	if (!d->video_enabled || !d->display_enabled)
		return;

	debug("[ pvr_geometry_updated: %i x %i, ", d->xsize, d->ysize);

	switch (d->pixelmode) {
	case 0: debug("RGB0555 (16-bit)"); break;
	case 1: debug("RGB565 (16-bit)"); break;
	case 2: debug("RGB888 (24-bit)"); break;
	case 3: debug("RGB0888 (32-bit)"); break;
	}

	debug(" ]\n");
}


/*
 *  pvr_render():
 *
 *  Render from the Object Buffer to the framebuffer.
 */
static void pvr_render(struct pvr_data *d)
{
	int ob_ofs = REG(PVRREG_OB_ADDR);
	int fb_base = REG(PVRREG_FB_RENDER_ADDR1);

	fatal("[ pvr_render: rendering to FB offset 0x%x ]\n", fb_base);

	for (;;) {
		uint8_t cmd = d->vram[ob_ofs];

		if (cmd == 0)
			break;
		else if (cmd == 1) {
			int x = d->vram[ob_ofs+1], y=d->vram[ob_ofs+2];
			fatal("rendering pixel at %i,%i\n", x, y);
			d->vram[fb_base + x + y * d->xsize * 2];
		} else
			fatal("pvr_render: internal error, unknown cmd\n");

		ob_ofs += sizeof(uint64_t);
	}
}


/*
 *  pvr_reset_ta():
 *
 *  Reset the Tile Accelerator.
 */
static void pvr_reset_ta(struct pvr_data *d)
{
	/*  TODO  */
}


/*
 *  pvr_ta_init():
 *
 *  Initialize the Tile Accelerator. This makes the TA ready to receive
 *  commands (via address 0x10000000).
 */
static void pvr_ta_init(struct pvr_data *d)
{
	REG(PVRREG_TA_OPB_POS) = REG(PVRREG_TA_OPB_START);
	REG(PVRREG_TA_OB_POS) = REG(PVRREG_TA_OB_START);
}


/*
 *  pvr_ta_command():
 *
 *  Read a command (e.g. parts of a polygon primitive) from d->ta[], and output
 *  "compiled commands" into the Object list and Object Pointer list.
 */
static void pvr_ta_command(struct pvr_data *d)
{
	int ob_ofs;
	float x, y, z;

#if 1
	/*  Dump the Tile Accelerator command for debugging:  */
	{
		int i;
		fatal("TA cmd:");
		for (i=0; i<8; i++)
			fatal(" %08x", (int) d->ta[i]);
		fatal("\n");
	}
#endif

	/*
	 *  TODO: REWRITE!!!
	 *
	 *  THis is just a quick hack to see if I can get out at least
	 *  the pixel coordinates.
	 */

	x = * ((float *) &d->ta[1]);
	y = * ((float *) &d->ta[2]);
	z = * ((float *) &d->ta[3]);

	ob_ofs = REG(PVRREG_TA_OB_POS);

	switch (d->ta[0] >> 28) {
	case 0x8:
		break;
	case 0xe:
	case 0xf:
		/*  Point.  */
		d->vram[ob_ofs + 0] = 1;
		d->vram[ob_ofs + 1] = x / 8;
		d->vram[ob_ofs + 2] = y / 8;
		d->vram[ob_ofs + 3] = z / 8;
		REG(PVRREG_TA_OB_POS) = ob_ofs + sizeof(uint64_t);
		break;
	case 0x0:
	default:
		/*  End of list.  */
		d->vram[ob_ofs + 0] = 0;
		REG(PVRREG_TA_OB_POS) = ob_ofs + sizeof(uint64_t);
		break;
	}
}


DEVICE_ACCESS(pvr_ta)
{
	struct pvr_data *d = (struct pvr_data *) extra;
	uint64_t idata = 0, odata = 0;

	if (writeflag == MEM_WRITE) {
		idata = memory_readmax64(cpu, data, len);
		d->ta[relative_addr / sizeof(uint32_t)] = idata;

		if (relative_addr == 0x3c) {
			fatal("TODO: 64-bit Tile Accelerator command\n");
			exit(1);
		}

		if (relative_addr == 0x1c)
			pvr_ta_command(d);
	} else {
		odata = d->ta[relative_addr / sizeof(uint32_t)];
		memory_writemax64(cpu, data, len, odata);
	}

	return 1;
}


DEVICE_ACCESS(pvr)
{
	struct pvr_data *d = (struct pvr_data *) extra;
	uint64_t idata = 0, odata = 0;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);

	/*  Default read action: Read from reg[]:  */
	if (writeflag == MEM_READ)
		odata = d->reg[relative_addr / sizeof(uint32_t)];

	/*  Fog table access:  */
	if (relative_addr >= PVRREG_FOG_TABLE &&
	    relative_addr < PVRREG_FOG_TABLE + PVR_FOG_TABLE_SIZE) {
		if (writeflag == MEM_WRITE)
			DEFAULT_WRITE;
		goto return_ok;
	}

	switch (relative_addr) {

	case PVRREG_ID:
		/*  ID for Set 5.xx versions of the Dreamcast, according
		    to http://www.ludd.luth.se/~jlo/dc/powervr-reg.txt:  */
		odata = 0x17fd11db;
		break;

	case PVRREG_REVISION:
		/*  Revision 1.1, for Dreamcast Set 5.2x.  */
		odata = 0x00000011;
		break;

	case PVRREG_RESET:
		if (writeflag == MEM_WRITE) {
			debug("[ pvr: RESET ");
			if (idata & PVR_RESET_BUS)
				fatal("{ PVR_RESET_BUS: TODO } ");
			if (idata & PVR_RESET_PVR)
				fatal("{ PVR_RESET_PVR: TODO } ");
			if (idata & PVR_RESET_TA)
				pvr_reset_ta(d);
			debug("]\n");
			idata = 0;
			DEFAULT_WRITE;
		}
		break;

	case PVRREG_STARTRENDER:
		if (writeflag == MEM_WRITE) {
			debug("[ pvr: STARTRENDER ]\n");
			pvr_render(d);
		} else {
			fatal("[ pvr: huh? read from STARTRENDER ]\n");
		}
		break;

	case PVRREG_OB_ADDR:
		if (writeflag == MEM_WRITE) {
			debug("[ pvr: OB_ADDR set to 0x%08"PRIx32" ]\n",
			    (uint32_t)(idata & PVR_OB_ADDR_MASK));
			if (idata & ~PVR_OB_ADDR_MASK)
				fatal("[ pvr: OB_ADDR: WARNING: Unknown"
				    " bits set: 0x%08"PRIx32" ]\n",
				    (uint32_t)(idata & ~PVR_OB_ADDR_MASK));
			idata &= PVR_OB_ADDR_MASK;
			DEFAULT_WRITE;
		}
		break;

	case PVRREG_TILEBUF_ADDR:
		if (writeflag == MEM_WRITE) {
			debug("[ pvr: TILEBUF_ADDR set to 0x%08"PRIx32" ]\n",
			    (uint32_t)(idata & PVR_TILEBUF_ADDR_MASK));
			if (idata & ~PVR_TILEBUF_ADDR_MASK)
				fatal("[ pvr: TILEBUF_ADDR: WARNING: Unknown"
				    " bits set: 0x%08"PRIx32" ]\n",
				    (uint32_t)(idata & ~PVR_TILEBUF_ADDR_MASK));
			idata &= PVR_TILEBUF_ADDR_MASK;
			DEFAULT_WRITE;
		}
		break;

	case PVRREG_BRDCOLR:
		if (writeflag == MEM_WRITE) {
			debug("[ pvr: BRDCOLR set to 0x%06"PRIx32" ]\n",
			    (int)idata);
			DEFAULT_WRITE;
		}
		break;

	case PVRREG_DIWMODE:
		if (writeflag == MEM_WRITE) {
			d->clock_double = idata & DIWMODE_C_MASK? 1:0;
			d->strip_buffer_enabled = idata & DIWMODE_SE_MASK? 1:0;
			d->strip_length = (idata & DIWMODE_SL_MASK)
			    >> DIWMODE_SL_SHIFT;
			d->argb8888_threshold = (idata & DIWMODE_TH_MASK)
			    >> DIWMODE_TH_SHIFT;
			d->extend = (idata & DIWMODE_EX_MASK)
			    >> DIWMODE_EX_SHIFT;
			d->pixelmode = (idata & DIWMODE_COL_MASK)
			    >> DIWMODE_COL_SHIFT;
			d->line_double = idata & DIWMODE_SD_MASK? 1:0;
			d->display_enabled = idata & DIWMODE_DE_MASK? 1:0;

			debug("[ pvr: DIWMODE set to: ");
			debug("clock_double=%i, ", d->clock_double);
			debug("strip_buffer_enabled=%i, ",
			    d->strip_buffer_enabled);
			debug("strip_length=%i, ", d->strip_length);
			debug("argb8888_threshold=%i, ", d->argb8888_threshold);
			debug("extend=0x%x, ", d->extend);
			debug("pixelmode=");
			switch (d->pixelmode) {
			case 0: debug("RGB0555 (16-bit)"); break;
			case 1: debug("RGB565 (16-bit)"); break;
			case 2: debug("RGB888 (24-bit)"); break;
			case 3: debug("RGB0888 (32-bit)"); break;
			}
			debug(", line_double=%i, ", d->line_double);
			debug("display_enabled=%i", d->display_enabled);
			debug(" ]\n");

			DEFAULT_WRITE;
			pvr_geometry_updated(d);
			pvr_fb_invalidate(d, -1, -1);
		}
		break;

	case PVRREG_DIWSIZE:
		if (writeflag == MEM_WRITE) {
			debug("[ pvr: DIWSIZE set to modulo=%i, "
			    "width=%i, height=%i ]\n", (int)
			    ((idata >> DIWSIZE_MODULO_SHIFT) & DIWSIZE_MASK),
			    (int)((idata >> DIWSIZE_DPL_SHIFT) & DIWSIZE_MASK),
			    (int)((idata >> DIWSIZE_LPF_SHIFT) & DIWSIZE_MASK));
			DEFAULT_WRITE;
			pvr_geometry_updated(d);
			pvr_fb_invalidate(d, -1, -1);
		}
		break;

	case PVRREG_FB_RENDER_ADDR1:
		if (writeflag == MEM_WRITE) {
			debug("[ pvr: FB_RENDER_ADDR1 set to 0x%08"PRIx32
			    " ]\n", (int) idata);
			DEFAULT_WRITE;
		}
		break;

	case PVRREG_FB_RENDER_ADDR2:
		if (writeflag == MEM_WRITE) {
			debug("[ pvr: FB_RENDER_ADDR2 set to 0x%08"PRIx32
			    " ]\n", (int) idata);
			DEFAULT_WRITE;
		}
		break;

	case PVRREG_VRAM_CFG1:
		if (writeflag == MEM_WRITE) {
			debug("[ pvr: VRAM_CFG1 set to 0x%08"PRIx32,
			    (int) idata);
			if (idata != VRAM_CFG1_GOOD_REFRESH_VALUE)
				fatal("{ VRAM_CFG1 = 0x%08"PRIx32" is not "
				    "yet implemented! }", (int) idata);
			debug(" ]\n");
			DEFAULT_WRITE;
		}
		break;

	case PVRREG_VRAM_CFG2:
		if (writeflag == MEM_WRITE) {
			debug("[ pvr: VRAM_CFG2 set to 0x%08"PRIx32,
			    (int) idata);
			if (idata != VRAM_CFG2_UNKNOWN_MAGIC)
				fatal("{ VRAM_CFG2 = 0x%08"PRIx32" is not "
				    "yet implemented! }", (int) idata);
			debug(" ]\n");
			DEFAULT_WRITE;
		}
		break;

	case PVRREG_VRAM_CFG3:
		if (writeflag == MEM_WRITE) {
			debug("[ pvr: VRAM_CFG3 set to 0x%08"PRIx32,
			    (int) idata);
			if (idata != VRAM_CFG3_UNKNOWN_MAGIC)
				fatal("{ VRAM_CFG3 = 0x%08"PRIx32" is not "
				    "yet implemented! }", (int) idata);
			debug(" ]\n");
			DEFAULT_WRITE;
		}
		break;

	case PVRREG_FOG_TABLE_COL:
		if (writeflag == MEM_WRITE) {
			debug("[ pvr: FOG_TABLE_COL set to 0x%08"PRIx32" ]\n",
			    (int) idata);
			DEFAULT_WRITE;
		}
		break;

	case PVRREG_FOG_VERTEX_COL:
		if (writeflag == MEM_WRITE) {
			debug("[ pvr: FOG_VERTEX_COL set to 0x%08"PRIx32" ]\n",
			    (int) idata);
			DEFAULT_WRITE;
		}
		break;

	case PVRREG_DIWADDRL:
		if (writeflag == MEM_WRITE) {
			debug("[ pvr: DIWADDRL set to 0x%08"PRIx32" ]\n",
			    (int) idata);
			pvr_fb_invalidate(d, -1, -1);
			DEFAULT_WRITE;
		}
		break;

	case PVRREG_DIWADDRS:
		if (writeflag == MEM_WRITE) {
			debug("[ pvr: DIWADDRS set to 0x%08"PRIx32" ]\n",
			    (int) idata);
			pvr_fb_invalidate(d, -1, -1);
			DEFAULT_WRITE;
		}
		break;

	case PVRREG_SYNCCONF:
		if (writeflag == MEM_WRITE) {
			d->video_enabled = idata & SYNCCONF_VO_MASK? 1:0;
			d->broadcast_standard = (idata & SYNCCONF_BC_MASK)
			    >> SYNCCONF_BC_SHIFT;
			d->interlaced = idata & SYNCCONF_I_MASK? 1:0;
			d->h_sync_positive = idata & SYNCCONF_HP_MASK? 1:0;
			d->v_sync_positive = idata & SYNCCONF_VP_MASK? 1:0;

			debug("[ pvr: SYNCCONF set to: ");
			debug("video_enabled=%i, ", d->video_enabled);
			switch (d->broadcast_standard) {
			case SYNCCONF_BC_VGA: debug("VGA"); break;
			case SYNCCONF_BC_NTSC: debug("NTSC"); break;
			case SYNCCONF_BC_PAL: debug("PAL"); break;
			default: debug("*UNKNOWN*"); break;
			}
			debug(", interlaced=%i, ", d->interlaced);
			debug("hsync=%i, ", d->h_sync_positive);
			debug("vsync=%i ]\n", d->v_sync_positive);

			DEFAULT_WRITE;
			pvr_geometry_updated(d);
			pvr_fb_invalidate(d, -1, -1);
		}
		break;

	case PVRREG_SYNC_STAT:
		/*  Ugly hack, but it works:  */
		odata = random();
		break;

	case PVRREG_TA_OPB_START:
		if (writeflag == MEM_WRITE) {
			idata &= TA_OPB_START_MASK;
			debug("[ pvr: TA_OPB_START set to 0x%x ]\n",
			    (int) idata);
			DEFAULT_WRITE;
		}
		break;

	case PVRREG_TA_OB_START:
		if (writeflag == MEM_WRITE) {
			idata &= TA_OB_START_MASK;
			debug("[ pvr: TA_OB_START set to 0x%x ]\n",
			    (int) idata);
			DEFAULT_WRITE;
		}
		break;

	case PVRREG_TA_OPB_END:
		if (writeflag == MEM_WRITE) {
			idata &= TA_OPB_END_MASK;
			debug("[ pvr: TA_OPB_END set to 0x%x ]\n",
			    (int) idata);
			DEFAULT_WRITE;
		}
		break;

	case PVRREG_TA_OB_END:
		if (writeflag == MEM_WRITE) {
			idata &= TA_OB_END_MASK;
			debug("[ pvr: TA_OB_END set to 0x%x ]\n",
			    (int) idata);
			DEFAULT_WRITE;
		}
		break;

	case PVRREG_TA_OPB_POS:
		if (writeflag == MEM_WRITE) {
			idata &= TA_OPB_POS_MASK;
			debug("[ pvr: TA_OPB_POS set to 0x%x ]\n",
			    (int) idata);
			DEFAULT_WRITE;
		}
		break;

	case PVRREG_TA_OB_POS:
		if (writeflag == MEM_WRITE) {
			idata &= TA_OB_POS_MASK;
			debug("[ pvr: TA_OB_POS set to 0x%x ]\n",
			    (int) idata);
			DEFAULT_WRITE;
		}
		break;

	case PVRREG_TILEBUF_SIZE:
		if (writeflag == MEM_WRITE) {
			d->tilebuf_ysize = (idata & TILEBUF_SIZE_HEIGHT_MASK)
			    >> TILEBUF_SIZE_HEIGHT_SHIFT;
			d->tilebuf_xsize = idata & TILEBUF_SIZE_WIDTH_MASK;
			d->tilebuf_xsize ++; d->tilebuf_ysize ++;
			debug("[ pvr: TILEBUF_SIZE set to %i x %i ]\n",
			    d->tilebuf_xsize, d->tilebuf_ysize);
			DEFAULT_WRITE;
		}
		break;

	case PVRREG_TA_INIT:
		if (writeflag == MEM_WRITE) {
			debug("[ pvr: TA_INIT ]\n");

			if (idata & PVR_TA_INIT)
				pvr_ta_init(d);

			if (idata != PVR_TA_INIT && idata != 0)
				fatal("{ TA_INIT = 0x%08"PRIx32" is not "
				    "yet implemented! }", (int) idata);

			/*  Always reset to 0.  */
			idata = 0;
			DEFAULT_WRITE;
		}
		break;

	default:if (writeflag == MEM_READ) {
			fatal("[ pvr: read from UNIMPLEMENTED addr 0x%x ]\n",
			    (int)relative_addr);
		} else {
			fatal("[ pvr: write to UNIMPLEMENTED addr 0x%x: 0x%x"
			    " ]\n", (int)relative_addr, (int)idata);
		}
	}

return_ok:
	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


DEVICE_ACCESS(pvr_vram)
{
	struct pvr_data *d = extra;

	if (writeflag == MEM_READ) {
		memcpy(data, d->vram + relative_addr, len);
	} else {
		memcpy(d->vram + relative_addr, data, len);
	}

	return 1;
}


DEVINIT(pvr)
{
	struct machine *machine = devinit->machine;
	struct pvr_data *d = malloc(sizeof(struct pvr_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct pvr_data));

	memory_device_register(machine->memory, devinit->name,
	    PVRREG_REGSTART, PVRREG_REGSIZE, dev_pvr_access, d,
	    DM_DEFAULT, NULL);

	/*  8 MB video RAM:  */
	d->vram = zeroed_alloc(8 * 1048576);
	memory_device_register(machine->memory, "pvr_vram", 0x05000000,
	    8 * 1048576, dev_pvr_vram_access, (void *)d,
	    DM_DYNTRANS_OK | DM_DYNTRANS_WRITE_OK
	    | DM_READS_HAVE_NO_SIDE_EFFECTS, d->vram);

	memory_device_register(machine->memory, "pvr_ta",
	    0x10000000, sizeof(d->ta), dev_pvr_ta_access, d, DM_DEFAULT, NULL);

	d->fb = dev_fb_init(machine, machine->memory, INTERNAL_FB_ADDR,
	    VFB_GENERIC, 640,480, 640,480, 24, "Dreamcast PVR");

	return 1;
}

