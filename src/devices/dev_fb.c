/*
 *  Copyright (C) 2003 by Anders Gavare.  All rights reserved.
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
 *  $Id: dev_fb.c,v 1.4 2003-11-07 02:16:54 debug Exp $
 *  
 *  Generic framebuffer device.
 *
 *	HPCmips framebuffer
 *	DECstation VFB01 monochrome framebuffer, 1024x864
 *	DECstation VFB02 8-bit color framebuffer, 1024x864
 *	DECstation Maxine, 1024x768 8-bit color
 *	Playstation 2 (24-bit color)
 *	generic (any resolution, several bit depths possible)
 *
 *  VFB01/02 is called 'pm' in NetBSD/pmax, 'fb' in Ultrix.
 *
 *  TODO:  This should actually be independant of X11, but that
 *  might be too hard to do right now.
 *
 *  TODO:  playstation 2 pixels are stored in another format, actually
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef WITH_X11
#include <X11/Xlib.h>
#include <X11/Xos.h>
#include <X11/Xutil.h>
#endif

#include "misc.h"


extern int x11_scaledown;
extern int use_x11;


/*
 *  set_grayscale_palette():
 *
 *  Fill d->rgb_palette with grayscale values. ncolors should
 *  be something like 2, 4, 16, or 256.
 */
void set_grayscale_palette(struct vfb_data *d, int ncolors)
{
	int i, gray;

	for (i=0; i<256; i++) {
		gray = 255*i/(ncolors-1);
		d->rgb_palette[i*3 + 0] = gray;
		d->rgb_palette[i*3 + 1] = gray;
		d->rgb_palette[i*3 + 2] = gray;
	}
}


/*
 *  set_blackwhite_palette():
 *
 *  Set color 0 = black, all others to white.
 */
void set_blackwhite_palette(struct vfb_data *d, int ncolors)
{
	int i, gray;

	for (i=0; i<256; i++) {
		gray = i==0? 0 : 255;
		d->rgb_palette[i*3 + 0] = gray;
		d->rgb_palette[i*3 + 1] = gray;
		d->rgb_palette[i*3 + 2] = gray;
	}
}


/*
 *  update_framebuffer():
 *
 *  The framebuffer memory has been updated. This function tries to make
 *  sure that the XImage is also updated (1 or more pixels).
 */
void update_framebuffer(struct vfb_data *d, int addr, int len)
{
	int x, y, pixel, npixels;
	long color_r, color_g, color_b, color;
	int scaledown = d->vfb_scaledown;
	int scaledownXscaledown = 1;

	if (scaledown == 1) {
		/*  Which framebuffer pixel does addr correspond to?  */
		pixel = addr * 8 / d->bit_depth;
		y = pixel / d->xsize;
		x = pixel % d->xsize;

		/*  How many framebuffer pixels?  */
		npixels = len * 8 / d->bit_depth;
		if (npixels == 0)
			npixels = 1;

		for (pixel=0; pixel<npixels; pixel++) {
			int fb_addr, c, r, g, b;
			color_r = color_g = color_b = 0;

			fb_addr = (y * d->xsize + x) * d->bit_depth;
			/*  fb_addr is now which _bit_ in framebuffer  */

			if (d->bit_depth <= 8) {
				c = d->framebuffer[fb_addr >> 3];
				fb_addr &= 7;

				/*  HPCmips is reverse:  */
				if (d->vfb_type == VFB_HPCMIPS)
					fb_addr = 8 - d->bit_depth - fb_addr;

				c = (c >> fb_addr) & ((1<<d->bit_depth) - 1);
				/*  c <<= (8 - d->bit_depth);  */

				r = d->rgb_palette[c*3 + 0];
				g = d->rgb_palette[c*3 + 1];
				b = d->rgb_palette[c*3 + 2];
			} else {
				/*  > 8 bits color.  */
				fb_addr >>= 3;
				switch (d->bit_depth) {
				case 24:
					r = d->framebuffer[fb_addr];
					g = d->framebuffer[fb_addr + 1];
					b = d->framebuffer[fb_addr + 2];
					break;
				default:
					r = g = b = random() & 255;
				}
			}

			/*  Combine the color into an RGB long:  */
			/*  TODO:  construct color in a more portable way:  */
			color = (r << 16) + (g << 8) + b;
#ifdef WITH_X11
			if (x>=0 && x<d->x11_xsize && y>=0 && y<d->x11_ysize)
				XPutPixel(d->fb_window->fb_ximage, x, y, color);
#endif
			x++;
		}
		return;
	}

	/*  scaledown != 1:  */

	scaledown = d->vfb_scaledown;
	scaledownXscaledown = scaledown * scaledown;

	/*  Which framebuffer pixel does addr correspond to?  */
	pixel = addr * 8 / d->bit_depth;
	y = pixel / d->xsize;
	x = pixel % d->xsize;

	/*  How many framebuffer pixels?  */
	npixels = len * 8 / d->bit_depth;

	/*  Which x11 pixel?  */
	x /= scaledown;
	y /= scaledown;

	/*  How many x11 pixels:  */
	npixels /= scaledown;
	if (npixels == 0)
		npixels = 1;

	for (pixel=0; pixel<npixels; pixel++) {
		int subx, suby;
		color_r = color_g = color_b = 0;
		for (suby=0; suby<scaledown; suby++)
		    for (subx=0; subx<scaledown; subx++) {
			int fb_x, fb_y, fb_addr, c, r, g, b;

			fb_x = x * scaledown + subx;
			fb_y = y * scaledown + suby;

			fb_addr = fb_y * d->xsize + fb_x;
			fb_addr = fb_addr * d->bit_depth;
			/*  fb_addr is now which _bit_ in framebuffer  */

			if (d->bit_depth <= 8) {
				c = d->framebuffer[fb_addr >> 3];
				fb_addr &= 7;

				/*  HPCmips is reverse:  */
				if (d->vfb_type == VFB_HPCMIPS)
					fb_addr = 8 - d->bit_depth - fb_addr;

				c = (c >> fb_addr) & ((1<<d->bit_depth) - 1);
				/*  c <<= (8 - d->bit_depth);  */

				r = d->rgb_palette[c*3 + 0];
				g = d->rgb_palette[c*3 + 1];
				b = d->rgb_palette[c*3 + 2];
			} else {
				/*  > 8 bits color.  */
				fb_addr >>= 3;
				switch (d->bit_depth) {
				case 24:
					r = d->framebuffer[fb_addr];
					g = d->framebuffer[fb_addr + 1];
					b = d->framebuffer[fb_addr + 2];
					break;
				default:
					r = g = b = random() & 255;
				}
			}

			color_r += r;
			color_g += g;
			color_b += b;
		    }

		/*  Average out the pixel color, and combine it to a RGB long:  */
		/*  TODO:  construct color in a more portable way:  */
		color = ((color_r / scaledownXscaledown) << 16) +
			((color_g / scaledownXscaledown) << 8) +
			(color_b / scaledownXscaledown);

#ifdef WITH_X11
		if (x>=0 && x<d->x11_xsize && y>=0 && y<d->x11_ysize)
			XPutPixel(d->fb_window->fb_ximage, x, y, color);
#endif

		x++;
	}
}


/*
 *  dev_fb_tick():
 *
 */
void dev_fb_tick(struct cpu *cpu, void *extra)
{
	struct vfb_data *d = extra;

	if (d->update_x1 != -1) {
		int y, addr, addr2, len;

		if (d->update_x1 >= d->visible_xsize)	d->update_x1 = d->visible_xsize - 1;
		if (d->update_x2 >= d->visible_xsize)	d->update_x2 = d->visible_xsize - 1;
		if (d->update_y1 >= d->visible_ysize)	d->update_y1 = d->visible_ysize - 1;
		if (d->update_y2 >= d->visible_ysize)	d->update_y2 = d->visible_ysize - 1;

		addr  = d->update_y1 * d->bytes_per_line + d->update_x1 * d->bit_depth / 8;
		addr2 = d->update_y1 * d->bytes_per_line + d->update_x2 * d->bit_depth / 8;
		len = addr2 - addr + (d->bit_depth + 7) / 8;

		for (y=d->update_y1; y<=d->update_y2; y+=d->vfb_scaledown) {
			update_framebuffer(d, addr, len);
			addr  += d->bytes_per_line * d->vfb_scaledown;
			addr2 += d->bytes_per_line * d->vfb_scaledown;
		}

#ifdef WITH_X11
		XPutImage(d->fb_window->x11_display, d->fb_window->x11_fb_window, d->fb_window->x11_fb_gc, d->fb_window->fb_ximage,
		    d->update_x1/d->vfb_scaledown, d->update_y1/d->vfb_scaledown,
		    d->update_x1/d->vfb_scaledown, d->update_y1/d->vfb_scaledown,
		    (d->update_x2 - d->update_x1)/d->vfb_scaledown + 1,
		    (d->update_y2 - d->update_y1)/d->vfb_scaledown + 1);
		XFlush(d->fb_window->x11_display);
#endif
		d->update_x1 = d->update_y1 = d->update_x2 = d->update_y2 = -1;
	}
}


/*
 *  dev_fb_access():
 *
 *  Returns 1 if ok, 0 on error.
 */
int dev_fb_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *extra)
{
	struct vfb_data *d = extra;
	int i;

/*
	if (writeflag == MEM_WRITE) {
		debug("[ dev_fb: write  to addr=%08lx, data = ", (long)relative_addr);
		for (i=0; i<len; i++)
			debug("%02x ", data[i]);
		debug("]\n");
	} else {
		debug("[ dev_fb: read from addr=%08lx, data = ", (long)relative_addr);
		for (i=0; i<len; i++)
			debug("%02x ", d->framebuffer[relative_addr + i]);
		debug("]\n");
	}
*/

	if (writeflag == MEM_WRITE && use_x11) {
		int x, y;

		x = (relative_addr % d->bytes_per_line) * 8 / d->bit_depth;
		y = relative_addr / d->bytes_per_line;

		/*  Is this far away from the previous updates? Then update:  */
		if (d->update_y1 != -1) {
			int diff1, diff2;
			diff1 = y - d->update_y1;
			diff2 = y - d->update_y2;
			if (diff1 < -30 || diff1 > 30 || diff2 < -30 || diff2 > 30)
				dev_fb_tick(cpu, d);
		}

		if (x < d->update_x1 || d->update_x1 == -1)	d->update_x1 = x;
		if (y < d->update_y1 || d->update_y1 == -1)	d->update_y1 = y;

		x += len * 8 / d->bit_depth - 1;	/*  TODO: 24 bit stuff?  */
		if (x > d->update_x2 || d->update_x2 == -1)	d->update_x2 = x;
		if (y > d->update_y2 || d->update_y2 == -1)	d->update_y2 = y;
	}

	/*
	 *  Read from/write to the framebuffer:
	 *  (TODO: take the color_plane_mask into account)
	 *
	 *  Calling memcpy() is probably overkill, as it usually is just one
	 *  or a few bytes that are read/written at a time.
	 */
	if (writeflag == MEM_WRITE)
		for (i=0; i<len; i++)
			d->framebuffer[relative_addr + i] = data[i];
	else
		for (i=0; i<len; i++)
			data[i] = d->framebuffer[relative_addr + i];

	return 1;
}


/*
 *  dev_fb_init():
 *
 *  xsize and ysize are ignored if vfb_type is VFB_DEC_VFB01 or 02.
 */
struct vfb_data *dev_fb_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr, int vfb_type,
	int visible_xsize, int visible_ysize, int xsize, int ysize, int bit_depth, char *name)
{
	struct vfb_data *d;
	struct fb_window *fb_window;
	size_t size;
	char title[400];
	int bytes_per_pixel = 3;

	d = malloc(sizeof(struct vfb_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct vfb_data));

	d->vfb_type = vfb_type;

	/*  Defaults:  */
	d->xsize = xsize;  d->visible_xsize = visible_xsize;
	d->ysize = ysize;  d->visible_ysize = visible_ysize;
	d->bit_depth = bit_depth;

	/*  Specific types:  */
	switch (vfb_type) {
	case VFB_DEC_VFB01:
		/*  DECstation VFB01 (monochrome)  */
		d->xsize = 2048;  d->visible_xsize = 1024;
		d->ysize = 1024;  d->visible_ysize = 864;
		d->bit_depth = 1;
		break;
	case VFB_DEC_VFB02:
		/*  DECstation VFB02 (color)  */
		d->xsize = 1024;  d->visible_xsize = 1024;
		d->ysize = 1024;  d->visible_ysize = 864;
		d->bit_depth = 8;
		break;
	case VFB_DEC_MAXINE:
		/*  DECstation Maxine (1024x768x8)  */
		d->xsize = 1024; d->visible_xsize = d->xsize;
		d->ysize = 768;  d->visible_ysize = d->ysize;
		d->bit_depth = 8;
		break;
	case VFB_PLAYSTATION2:
		/*  Playstation 2  */
		d->xsize = xsize;  d->visible_xsize = d->xsize;
		d->ysize = ysize;  d->visible_ysize = d->ysize;
		d->bit_depth = 24;
		break;
	default:
		;
	}

	if (d->bit_depth == 4)
		set_grayscale_palette(d, 1 << d->bit_depth);
	else if (d->bit_depth == 8 || d->bit_depth == 1)
		set_blackwhite_palette(d, 1 << d->bit_depth);

	d->vfb_scaledown = x11_scaledown;

	d->bytes_per_line = d->xsize * d->bit_depth / 8;
	size = d->ysize * d->bytes_per_line;

	d->framebuffer = malloc(size);
	if (d->framebuffer == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}

	/*  Clear the framebuffer (all black pixels):  */
	d->framebuffer_size = size;
	memset(d->framebuffer, 0, size);

	/*  {  int i; for (i=0; i<size; i++)
		d->framebuffer[i] = random(); }  */

	d->x11_xsize = d->visible_xsize / d->vfb_scaledown;
	d->x11_ysize = d->visible_ysize / d->vfb_scaledown;

	d->update_x1 = d->update_y1 = d->update_x2 = d->update_y2 = -1;

	snprintf(title, sizeof(title), "mips64emul: %ix%ix%i %s framebuffer",
	    d->visible_xsize, d->visible_ysize, d->bit_depth, name);
	title[sizeof(title)-1] = '\0';

#ifdef WITH_X11
	if (use_x11)
		d->fb_window = x11_fb_init(d->x11_xsize, d->x11_ysize, title);
	else
#endif
		d->fb_window = NULL;

	memory_device_register(mem, name, baseaddr, size, dev_fb_access, d);

	cpu_add_tickfunction(cpu, dev_fb_tick, d, 18);
	return d;
}

