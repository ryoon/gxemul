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
 *  $Id: dev_m700_fb.c,v 1.4 2004-10-25 02:51:18 debug Exp $
 *  
 *  Olivetti M700 framebuffer.
 *
 *  TODO: This is enough to show the penguin and some text, with Linux,
 *  but that's about it.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "misc.h"
#include "devices.h"


#define	DEFAULT_XSIZE		800
#define	DEFAULT_YSIZE		600


struct m700_fb_data {
	struct vfb_data		*fb;
	int			xsize;
	int			ysize;
};


/*
 *  schedule_redraw_of_whole_screen():
 */
static void schedule_redraw_of_whole_screen(struct m700_fb_data *d)
{
	d->fb->update_x1 = 0;
	d->fb->update_x2 = d->fb->xsize - 1;
	d->fb->update_y1 = 0;
	d->fb->update_y2 = d->fb->ysize - 1;
}


/*
 *  dev_m700_fb_access():
 */
int dev_m700_fb_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	struct m700_fb_data *d = (struct m700_fb_data *) extra;
	uint64_t idata = 0, odata = 0;

	idata = memory_readmax64(cpu, data, len);

	/*  Palette:  */
	if (relative_addr >= 0x800 && relative_addr <= 0xff8) {
		int index = (relative_addr - 0x800) / 8;

		if (writeflag == MEM_WRITE) {
			d->fb->rgb_palette[index*3 + 0] = (idata >> 16) & 255;
			d->fb->rgb_palette[index*3 + 1] = (idata >> 8) & 255;
			d->fb->rgb_palette[index*3 + 2] = idata & 255;
			schedule_redraw_of_whole_screen(d);
		} else {
			odata = (d->fb->rgb_palette[index*3 + 0] << 16) +
			    (d->fb->rgb_palette[index*3 + 1] << 8) +
			    d->fb->rgb_palette[index*3 + 2];
		}
		goto nice_return;
	}

	switch (relative_addr) {
	case 0x0118:
		odata = d->xsize / 4;
		break;
	case 0x0150:
		/*  TODO: This has to do with ysize, but I haven't
		    figured out exactly how yet.  */
		odata = d->ysize + 256;
		break;
	default:
		if (writeflag == MEM_WRITE) {
			debug("[ m700_fb: unimplemented write to address 0x%x, data=0x%02x ]\n",
			    (int)relative_addr, (int)idata);
		} else {
			debug("[ m700_fb: unimplemented read from address 0x%x ]\n", (int)relative_addr);
		}
	}

nice_return:
	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_m700_fb_init():
 */
void dev_m700_fb_init(struct cpu *cpu, struct memory *mem,
	uint64_t baseaddr, uint64_t baseaddr2)
{
	struct m700_fb_data *d = malloc(sizeof(struct m700_fb_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct m700_fb_data));
	d->xsize = DEFAULT_XSIZE;
	d->ysize = DEFAULT_YSIZE;

	d->fb = dev_fb_init(cpu, mem, baseaddr2, VFB_GENERIC,
	    d->xsize, d->ysize, d->xsize, d->ysize, 8, "M700 G364");
	if (d->fb == NULL) {
		fprintf(stderr, "dev_m700_fb_init(): out of memory\n");
		exit(1);
	}

	memory_device_register(mem, "m700_fb", baseaddr, DEV_M700_FB_LENGTH,
	    dev_m700_fb_access, (void *)d);
}

