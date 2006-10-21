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
 *  $Id: dev_pvr.c,v 1.4 2006-10-21 02:39:08 debug Exp $
 *  
 *  PowerVR CLX2 (Graphics controller used in the Dreamcast)
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

struct pvr_data {
	struct vfb_data		*fb;

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

	uint32_t		reg[PVRREG_REGSIZE / sizeof(uint32_t)];
};


#define	REG(x)		(d->reg[(x)/sizeof(uint32_t)])
#define	DEFAULT_WRITE	REG(relative_addr) = idata;


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


DEVICE_ACCESS(pvr)
{
	struct pvr_data *d = (struct pvr_data *) extra;
	uint64_t idata = 0, odata = 0;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);

	/*  Default read action: Read from reg[]:  */
	if (writeflag == MEM_READ)
		odata = d->reg[relative_addr / sizeof(uint32_t)];

	switch (relative_addr) {

	case PVRREG_RESET:
		if (writeflag == MEM_WRITE) {
			debug("[ pvr: RESET ");
			if (idata & PVR_RESET_BUS)
				fatal("{ PVR_RESET_BUS: TODO } ");
			if (idata & PVR_RESET_PVR)
				fatal("{ PVR_RESET_PVR: TODO } ");
			if (idata & PVR_RESET_TA)
				fatal("{ PVR_RESET_TA: TODO } ");
			debug("]\n");
			idata = 0;
			DEFAULT_WRITE;
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
		}
		break;

	case PVRREG_DIWADDRL:
		if (writeflag == MEM_WRITE) {
			debug("[ pvr: DIWADDRL set to 0x%08"PRIx32" ]\n",
			    (int) idata);
			DEFAULT_WRITE;
		}
		break;

	case PVRREG_DIWADDRS:
		if (writeflag == MEM_WRITE) {
			debug("[ pvr: DIWADDRS set to 0x%08"PRIx32" ]\n",
			    (int) idata);
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
		}
		break;

	case PVRREG_SYNC_STAT:
		/*  Ugly hack, but it works:  */
		odata = random();
		break;

	default:if (writeflag == MEM_READ) {
			fatal("[ pvr: read from UNIMPLEMENTED addr 0x%x ]\n",
			    (int)relative_addr);
		} else {
			fatal("[ pvr: write to UNIMPLEMENTED addr 0x%x: 0x%x"
			    " ]\n", (int)relative_addr, (int)idata);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

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

#if 1
	/*  640x480 16-bit framebuffer:  */
	d->fb = dev_fb_init(machine, machine->memory, PVRREG_FBSTART,
	    VFB_HPC, 640,480, 640,480, 16, "Dreamcast PVR");
#else
	/*  IP.BIN experiment.  */
	d->fb = dev_fb_init(machine, machine->memory, PVRREG_FBSTART + 0x200000,
	    VFB_GENERIC, 640,480, 640,480, 32, "Dreamcast PVR");
#endif

	return 1;
}

