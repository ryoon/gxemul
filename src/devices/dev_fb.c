/*
 *  Copyright (C) 2003-2004  Anders Gavare.  All rights reserved.
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
 *  $Id: dev_fb.c,v 1.68 2004-12-04 12:48:41 debug Exp $
 *  
 *  Generic framebuffer device.
 *
 *	DECstation VFB01 monochrome framebuffer, 1024x864
 *	DECstation VFB02 8-bit color framebuffer, 1024x864
 *	DECstation Maxine, 1024x768 8-bit color
 *	HPCmips framebuffer
 *	Playstation 2 (24-bit color)
 *	generic (any resolution, several bit depths possible)
 *
 *
 *  TODO:  There is still a bug when redrawing the cursor. The underlying
 *         image is moved 1 pixel (?), or something like that.
 *
 *  TODO:  This should actually be independant of X11, but that
 *         might be too hard to do right now.
 *
 *  TODO:  playstation 2 pixels are stored in another format, actually
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "console.h"
#include "devices.h"
#include "memory.h"
#include "misc.h"

#ifdef WITH_X11
#include <X11/Xlib.h>
#include <X11/Xos.h>
#include <X11/Xutil.h>
#endif


#define	FB_TICK_SHIFT		18

#define	LOGO_XSIZE		256
#define	LOGO_YSIZE		256
#define	LOGO_BOTTOM_MARGIN	60
/*  This must be a 256*256 pixels P4 ppm:  */
#include "fb_logo.c"
unsigned char *fb_logo = fb_logo_ppm + 11;


/*  #define FB_DEBUG  */

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
 *  dev_fb_setcursor():
 */
void dev_fb_setcursor(struct vfb_data *d, int cursor_x, int cursor_y, int on,
	int cursor_xsize, int cursor_ysize)
{
	if (cursor_x < 0)
		cursor_x = 0;
	if (cursor_y < 0)
		cursor_y = 0;
	if (cursor_x + cursor_xsize >= d->xsize)
		cursor_x = d->xsize - cursor_xsize;
	if (cursor_y + cursor_ysize >= d->ysize)
		cursor_y = d->ysize - cursor_ysize;

#ifdef WITH_X11
	if (d->fb_window != NULL) {
		d->fb_window->cursor_x      = cursor_x;
		d->fb_window->cursor_y      = cursor_y;
		d->fb_window->cursor_on     = on;
		d->fb_window->cursor_xsize  = cursor_xsize;
		d->fb_window->cursor_ysize  = cursor_ysize;
	}
#endif

	if (d->fb_window != NULL)
		console_set_framebuffer_mouse(cursor_x, cursor_y,
		    d->fb_window->fb_number);

	/*  debug("dev_fb_setcursor(%i,%i, size %i,%i, on=%i)\n",
	    cursor_x, cursor_y, cursor_xsize, cursor_ysize, on);  */
}


/*
 *  framebuffer_blockcopyfill():
 *
 *  This function should be used by devices that are capable of doing
 *  block copy/fill.
 *
 *  If fillflag is non-zero, then fill_[rgb] should contain the color
 *  with which to fill.
 *
 *  If fillflag is zero, copy mode is used, and from_[xy] should contain
 *  the offset on the framebuffer where we should copy from.
 *
 *  NOTE:  Overlapping copies are undefined!
 */
void framebuffer_blockcopyfill(struct vfb_data *d, int fillflag, int fill_r,
	int fill_g, int fill_b, int x1, int y1, int x2, int y2,
	int from_x, int from_y)
{
	int y;
	long from_ofs, dest_ofs, linelen;

	if (fillflag)
		debug("framebuffer_blockcopyfill(FILL, %i,%i, %i,%i, color %i,%i,%i)\n",
		    x1,y1, x2,y2, fill_r, fill_g, fill_b);
	else
		debug("framebuffer_blockcopyfill(COPY, %i,%i, %i,%i, from %i,%i)\n",
		    x1,y1, x2,y2, from_x,from_y);

	/*  Clip x:  */
	if (x1 < 0)		x1 = 0;
	if (x1 >= d->xsize)	x1 = d->xsize-1;
	if (x2 < 0)		x2 = 0;
	if (x2 >= d->xsize)	x2 = d->xsize-1;

	dest_ofs = d->bytes_per_line * y1 + (d->bit_depth/8) * x1;
	linelen = (x2-x1 + 1) * (d->bit_depth/8);	/*  NOTE: nr of bytes, not pixels  */

	if (fillflag) {
		for (y=y1; y<=y2; y++) {
			if (y>=0 && y<d->ysize) {
				int x;
				char buf[8192 * 3];
				if (d->bit_depth == 24)
					for (x=0; x<linelen; x+=3) {
						buf[x] = fill_r;
						buf[x+1] = fill_g;
						buf[x+2] = fill_b;
					}
				else
					printf("TODO: fill for non-24-bit modes\n");

				memmove(d->framebuffer + dest_ofs, buf, linelen);
			}

			dest_ofs += d->bytes_per_line;
		}
	} else {
		from_ofs = d->bytes_per_line * from_y + (d->bit_depth/8) * from_x;

		for (y=y1; y<=y2; y++) {
			if (y>=0 && y<d->ysize)
				memmove(d->framebuffer + dest_ofs, d->framebuffer + from_ofs, linelen);

			from_ofs += d->bytes_per_line;
			dest_ofs += d->bytes_per_line;
		}
	}

	if (x1 < d->update_x1 || d->update_x1 == -1)	d->update_x1 = x1;
	if (x1 > d->update_x2 || d->update_x2 == -1)	d->update_x2 = x1;
	if (x2 < d->update_x1 || d->update_x1 == -1)	d->update_x1 = x2;
	if (x2 > d->update_x2 || d->update_x2 == -1)	d->update_x2 = x2;

	if (y1 < d->update_y1 || d->update_y1 == -1)	d->update_y1 = y1;
	if (y1 > d->update_y2 || d->update_y2 == -1)	d->update_y2 = y1;
	if (y2 < d->update_y1 || d->update_y1 == -1)	d->update_y1 = y2;
	if (y2 > d->update_y2 || d->update_y2 == -1)	d->update_y2 = y2;
}


#ifdef WITH_X11
#define macro_put_pixel() {	\
			/*  Combine the color into an X11 long and display it:  */	\
			/*  TODO:  construct color in a more portable way:  */		\
			switch (d->fb_window->x11_screen_depth) {					\
			case 24:							\
				if (d->fb_window->fb_ximage->byte_order)		\
					color = (b << 16) + (g << 8) + r;		\
				else							\
					color = (r << 16) + (g << 8) + b;		\
				break;							\
			case 16:							\
				r >>= 3; g >>= 2; b >>= 3;				\
				if (d->fb_window->fb_ximage->byte_order) {		\
					/*  Big endian 16-bit X server:  */		\
					static int first = 1;				\
					if (first) {					\
						fprintf(stderr, "\n*** Please report to the author whether 16-bit X11 colors are rendered correctly or not!\n\n"); \
						first = 0;				\
					}						\
					color = (b << 11) + (g << 5) + r;		\
				} else {						\
					/*  Little endian (eg PC) X servers:  */	\
					color = (r << 11) + (g << 5) + b;		\
				}							\
				break;							\
			case 15:							\
				r >>= 3; g >>= 3; b >>= 3;				\
				if (d->fb_window->fb_ximage->byte_order) {		\
					/*  Big endian 15-bit X server:  */		\
					static int first = 1;				\
					if (first) {					\
						fprintf(stderr, "\n*** Please report to the author whether 15-bit X11 colors are rendered correctly or not!\n\n"); \
						first = 0;				\
					}						\
					color = (b << 10) + (g << 5) + r;		\
				} else {						\
					/*  Little endian (eg PC) X servers:  */	\
					color = (r << 10) + (g << 5) + b;		\
				}							\
				break;							\
			default:							\
				color = d->fb_window->x11_graycolor[15 * (r + g + b) / (255 * 3)].pixel; \
			}								\
			if (x>=0 && x<d->x11_xsize && y>=0 && y<d->x11_ysize)		\
				XPutPixel(d->fb_window->fb_ximage, x, y, color);	\
		}
#else
/*  If not WITH_X11:  */
#define macro_put_pixel() { }
#endif


/*
 *  update_framebuffer():
 *
 *  The framebuffer memory has been updated. This function tries to make
 *  sure that the XImage is also updated (1 or more pixels).
 */
void update_framebuffer(struct vfb_data *d, int addr, int len)
{
	int x, y, pixel, npixels;
	long color_r, color_g, color_b;
#ifdef WITH_X11
	long color;
#endif
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

		if (d->bit_depth < 8) {
			for (pixel=0; pixel<npixels; pixel++) {
				int fb_addr, c, r, g, b;
				color_r = color_g = color_b = 0;

				fb_addr = (y * d->xsize + x) * d->bit_depth;
				/*  fb_addr is now which _bit_ in framebuffer  */

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

				macro_put_pixel();
				x++;
			}
		} else if (d->bit_depth == 8) {
			for (pixel=0; pixel<npixels; pixel++) {
				int fb_addr, c, r, g, b;
				color_r = color_g = color_b = 0;

				fb_addr = y * d->xsize + x;
				/*  fb_addr is now which byte in framebuffer  */
				c = d->framebuffer[fb_addr];
				r = d->rgb_palette[c*3 + 0];
				g = d->rgb_palette[c*3 + 1];
				b = d->rgb_palette[c*3 + 2];

				macro_put_pixel();
				x++;
			}
		} else {	/*  d->bit_depth > 8  */
			for (pixel=0; pixel<npixels; pixel++) {
				int fb_addr, r, g, b;
				color_r = color_g = color_b = 0;

				fb_addr = (y * d->xsize + x) * d->bit_depth;
				/*  fb_addr is now which byte in framebuffer  */

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

				macro_put_pixel();
				x++;
			}
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
		int subx, suby, r, g, b;
		color_r = color_g = color_b = 0;
		for (suby=0; suby<scaledown; suby++)
		    for (subx=0; subx<scaledown; subx++) {
			int fb_x, fb_y, fb_addr, c;

			fb_x = x * scaledown + subx;
			fb_y = y * scaledown + suby;

			fb_addr = fb_y * d->xsize + fb_x;

			if (d->bit_depth < 8) {
				fb_addr = fb_addr * d->bit_depth;
				/*  fb_addr is now which _bit_ in framebuffer  */

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
			} else if (d->bit_depth == 8) {
				/*  fb_addr is which _byte_ in framebuffer  */

				c = d->framebuffer[fb_addr] * 3;
				r = d->rgb_palette[c + 0];
				g = d->rgb_palette[c + 1];
				b = d->rgb_palette[c + 2];
			} else {
				fb_addr = (fb_addr * d->bit_depth) >> 3;
				/*  fb_addr is which _byte_ in framebuffer  */

				/*  > 8 bits color.  */
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

		r = color_r / scaledownXscaledown;
		g = color_g / scaledownXscaledown;
		b = color_b / scaledownXscaledown;

		macro_put_pixel();

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
#ifdef WITH_X11
	int need_to_flush_x11 = 0;
	int need_to_redraw_cursor = 0;
#endif

	if (!cpu->emul->use_x11)
		return;

#ifdef BINTRANS
	do {
		uint64_t low, high;
		int x, y;

		memory_device_bintrans_access(cpu, cpu->mem, extra, &low, &high);
		if ((int64_t)low == -1)
			break;

		/*  printf("low=%016llx high=%016llx\n",
		    (long long)low, (long long)high);  */

		x = (low % d->bytes_per_line) * 8 / d->bit_depth;
		y = low / d->bytes_per_line;
		if (x < d->update_x1 || d->update_x1 == -1)	d->update_x1 = x;
		if (x > d->update_x2 || d->update_x2 == -1)	d->update_x2 = x;
		if (y < d->update_y1 || d->update_y1 == -1)	d->update_y1 = y;
		if (y > d->update_y2 || d->update_y2 == -1)	d->update_y2 = y;

		x = ((low+7) % d->bytes_per_line) * 8 / d->bit_depth;
		y = (low+7) / d->bytes_per_line;
		if (x < d->update_x1 || d->update_x1 == -1)	d->update_x1 = x;
		if (x > d->update_x2 || d->update_x2 == -1)	d->update_x2 = x;
		if (y < d->update_y1 || d->update_y1 == -1)	d->update_y1 = y;
		if (y > d->update_y2 || d->update_y2 == -1)	d->update_y2 = y;

		x = (high % d->bytes_per_line) * 8 / d->bit_depth;
		y = high / d->bytes_per_line;
		if (x < d->update_x1 || d->update_x1 == -1)	d->update_x1 = x;
		if (x > d->update_x2 || d->update_x2 == -1)	d->update_x2 = x;
		if (y < d->update_y1 || d->update_y1 == -1)	d->update_y1 = y;
		if (y > d->update_y2 || d->update_y2 == -1)	d->update_y2 = y;

		x = ((high+7) % d->bytes_per_line) * 8 / d->bit_depth;
		y = (high+7) / d->bytes_per_line;
		if (x < d->update_x1 || d->update_x1 == -1)	d->update_x1 = x;
		if (x > d->update_x2 || d->update_x2 == -1)	d->update_x2 = x;
		if (y < d->update_y1 || d->update_y1 == -1)	d->update_y1 = y;
		if (y > d->update_y2 || d->update_y2 == -1)	d->update_y2 = y;

		/*
		 *  An update covering more than one line will automatically
		 *  force an update of all the affected lines:
		 */
		if (d->update_y1 != d->update_y2) {
			d->update_x1 = 0;
			d->update_x2 = d->xsize-1;
		}
	} while (0);
#endif

#ifdef WITH_X11
	/*  Do we need to redraw the cursor?  */
	if (d->fb_window->cursor_on != d->fb_window->OLD_cursor_on ||
	    d->fb_window->cursor_x != d->fb_window->OLD_cursor_x ||
	    d->fb_window->cursor_y != d->fb_window->OLD_cursor_y ||
	    d->fb_window->cursor_xsize != d->fb_window->OLD_cursor_xsize ||
	    d->fb_window->cursor_ysize != d->fb_window->OLD_cursor_ysize)
		need_to_redraw_cursor = 1;

	if (d->update_x2 != -1) {
		if ( (d->update_x1 >= d->fb_window->OLD_cursor_x &&
		      d->update_x1 < (d->fb_window->OLD_cursor_x + d->fb_window->OLD_cursor_xsize)) ||
		     (d->update_x2 >= d->fb_window->OLD_cursor_x &&
		      d->update_x2 < (d->fb_window->OLD_cursor_x + d->fb_window->OLD_cursor_xsize)) ||
		     (d->update_x1 <  d->fb_window->OLD_cursor_x &&
		      d->update_x2 >= (d->fb_window->OLD_cursor_x + d->fb_window->OLD_cursor_xsize)) ) {
			if ( (d->update_y1 >= d->fb_window->OLD_cursor_y &&
			      d->update_y1 < (d->fb_window->OLD_cursor_y + d->fb_window->OLD_cursor_ysize)) ||
			     (d->update_y2 >= d->fb_window->OLD_cursor_y &&
			      d->update_y2 < (d->fb_window->OLD_cursor_y + d->fb_window->OLD_cursor_ysize)) ||
			     (d->update_y1 <  d->fb_window->OLD_cursor_y &&
			      d->update_y2 >= (d->fb_window->OLD_cursor_y + d->fb_window->OLD_cursor_ysize)) )
				need_to_redraw_cursor = 1;
		}
	}

	if (need_to_redraw_cursor) {
		/*  Remove old cursor, if any:  */
		if (d->fb_window->OLD_cursor_on) {
			XPutImage(d->fb_window->x11_display,
			    d->fb_window->x11_fb_window,
			    d->fb_window->x11_fb_gc, d->fb_window->fb_ximage,
			    d->fb_window->OLD_cursor_x/d->vfb_scaledown,
			    d->fb_window->OLD_cursor_y/d->vfb_scaledown,
			    d->fb_window->OLD_cursor_x/d->vfb_scaledown,
			    d->fb_window->OLD_cursor_y/d->vfb_scaledown,
			    d->fb_window->OLD_cursor_xsize/d->vfb_scaledown + 1,
			    d->fb_window->OLD_cursor_ysize/d->vfb_scaledown + 1);
		}
	}
#endif

	if (d->update_x2 != -1) {
		int y, addr, addr2, q = d->vfb_scaledown;

		if (d->update_x1 >= d->visible_xsize)	d->update_x1 = d->visible_xsize - 1;
		if (d->update_x2 >= d->visible_xsize)	d->update_x2 = d->visible_xsize - 1;
		if (d->update_y1 >= d->visible_ysize)	d->update_y1 = d->visible_ysize - 1;
		if (d->update_y2 >= d->visible_ysize)	d->update_y2 = d->visible_ysize - 1;

		/*  Without these, we might miss the right most / bottom pixel:  */
		d->update_x2 += (q - 1);
		d->update_y2 += (q - 1);

		d->update_x1 = d->update_x1 / q * q;
		d->update_x2 = d->update_x2 / q * q;
		d->update_y1 = d->update_y1 / q * q;
		d->update_y2 = d->update_y2 / q * q;

		addr  = d->update_y1 * d->bytes_per_line + d->update_x1 * d->bit_depth / 8;
		addr2 = d->update_y1 * d->bytes_per_line + d->update_x2 * d->bit_depth / 8;

		for (y=d->update_y1; y<=d->update_y2; y+=q) {
			update_framebuffer(d, addr, addr2 - addr);
			addr  += d->bytes_per_line * q;
			addr2 += d->bytes_per_line * q;
		}

#ifdef WITH_X11
		XPutImage(d->fb_window->x11_display, d->fb_window->x11_fb_window, d->fb_window->x11_fb_gc, d->fb_window->fb_ximage,
		    d->update_x1/d->vfb_scaledown, d->update_y1/d->vfb_scaledown,
		    d->update_x1/d->vfb_scaledown, d->update_y1/d->vfb_scaledown,
		    (d->update_x2 - d->update_x1)/d->vfb_scaledown + 1,
		    (d->update_y2 - d->update_y1)/d->vfb_scaledown + 1);

		need_to_flush_x11 = 1;
#endif

		d->update_x1 = d->update_y1 = 99999;
		d->update_x2 = d->update_y2 = -1;
	}

#ifdef WITH_X11
	if (need_to_redraw_cursor) {
		/*  Paint new cursor:  */
		if (d->fb_window->cursor_on) {
			x11_redraw_cursor(d->fb_window->fb_number);
			d->fb_window->OLD_cursor_on = d->fb_window->cursor_on;
			d->fb_window->OLD_cursor_x = d->fb_window->cursor_x;
			d->fb_window->OLD_cursor_y = d->fb_window->cursor_y;
			d->fb_window->OLD_cursor_xsize = d->fb_window->cursor_xsize;
			d->fb_window->OLD_cursor_ysize = d->fb_window->cursor_ysize;
		}
	}
#endif

#ifdef WITH_X11
	if (need_to_flush_x11)
		XFlush(d->fb_window->x11_display);
#endif
}


/*
 *  dev_fb_access():
 */
int dev_fb_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	struct vfb_data *d = extra;
	int i;

#ifdef FB_DEBUG
	if (writeflag == MEM_WRITE) { if (data[0]) {
		fatal("[ dev_fb: write  to addr=%08lx, data = ", (long)relative_addr);
		for (i=0; i<len; i++)
			fatal("%02x ", data[i]);
		fatal("]\n");
	} else {
		fatal("[ dev_fb: read from addr=%08lx, data = ", (long)relative_addr);
		for (i=0; i<len; i++)
			fatal("%02x ", d->framebuffer[relative_addr + i]);
		fatal("]\n");
	}
#endif

	/*  See if a write actually modifies the framebuffer contents:  */
	if (writeflag == MEM_WRITE) {
		for (i=0; i<len; i++) {
			if (data[i] != d->framebuffer[relative_addr + i])
				break;

			/*  If all bytes are equal to what is already stored in the
				framebuffer, then simply return:  */
			if (i==len-1)
				return 1;
		}
	}

	/*
	 *  If the framebuffer is modified, then we should keep a track
	 *  of which area(s) we modify, so that the display isn't updated
	 *  unneccessarily.
	 */
	if (writeflag == MEM_WRITE && cpu->emul->use_x11) {
		int x, y, x2,y2;

		x = (relative_addr % d->bytes_per_line) * 8 / d->bit_depth;
		y = relative_addr / d->bytes_per_line;
		x2 = ((relative_addr + len) % d->bytes_per_line) * 8 / d->bit_depth;
		y2 = (relative_addr + len) / d->bytes_per_line;

		/*  Is this far away from the previous updates? Then update:  */
		if (d->update_y1 != -1) {
			int diffx1, diffx2, diffy1, diffy2;
			/*  fhmult may be an integer multiple of the (assumed)
			    font height, or any other number:  */
			int short_distance = 8 * 22;

			diffx1 = abs(x - d->update_x1);
			diffx2 = abs(x - d->update_x2);
			diffy1 = abs(y - d->update_y1);
			diffy2 = abs(y - d->update_y2);

			if (diffx2 > diffx1)
				diffx1 = diffx2;
			if (diffy2 > diffy1)
				diffy1 = diffy2;

			/*  use diffx1 and diffy1 from here on  */

#if 0
			if (diffx1 >= short_distance &&
			    diffy1 >= short_distance)
				dev_fb_tick(cpu, d);
#endif
		}

		if (x < d->update_x1 || d->update_x1 == -1)	d->update_x1 = x;
		if (x > d->update_x2 || d->update_x2 == -1)	d->update_x2 = x;

		if (y < d->update_y1 || d->update_y1 == -1)	d->update_y1 = y;
		if (y > d->update_y2 || d->update_y2 == -1)	d->update_y2 = y;

		if (x2 < d->update_x1 || d->update_x1 == -1)	d->update_x1 = x2;
		if (x2 > d->update_x2 || d->update_x2 == -1)	d->update_x2 = x2;

		if (y2 < d->update_y1 || d->update_y1 == -1)	d->update_y1 = y2;
		if (y2 > d->update_y2 || d->update_y2 == -1)	d->update_y2 = y2;

		/*
		 *  An update covering more than one line will automatically
		 *  force an update of all the affected lines:
		 */
		if (y != y2) {
			d->update_x1 = 0;
			d->update_x2 = d->xsize-1;
		}
	}

	/*
	 *  Read from/write to the framebuffer:
	 *  (TODO: take the color_plane_mask into account)
	 *
	 *  Calling memcpy() is probably overkill, as it usually is just one
	 *  or a few bytes that are read/written at a time.
	 */
	if (writeflag == MEM_WRITE) {
		if (len > 8)
			memcpy(d->framebuffer + relative_addr, data, len);
		else
			for (i=0; i<len; i++)
				d->framebuffer[relative_addr + i] = data[i];
	} else {
		if (len > 8)
			memcpy(data, d->framebuffer + relative_addr, len);
		else
			for (i=0; i<len; i++)
				data[i] = d->framebuffer[relative_addr + i];
	}

	return 1;
}


/*
 *  dev_fb_init():
 *
 *  xsize and ysize are ignored if vfb_type is VFB_DEC_VFB01 or 02.
 */
struct vfb_data *dev_fb_init(struct cpu *cpu, struct memory *mem,
	uint64_t baseaddr, int vfb_type, int visible_xsize, int visible_ysize,
	int xsize, int ysize, int bit_depth, char *name, int logo)
{
	struct vfb_data *d;
	size_t size;
	int x, y;
	char title[400];

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

	if (d->bit_depth == 2 || d->bit_depth == 4)
		set_grayscale_palette(d, 1 << d->bit_depth);
	else if (d->bit_depth == 8 || d->bit_depth == 1)
		set_blackwhite_palette(d, 1 << d->bit_depth);

	d->vfb_scaledown = cpu->emul->x11_scaledown;

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

	d->x11_xsize = d->visible_xsize / d->vfb_scaledown;
	d->x11_ysize = d->visible_ysize / d->vfb_scaledown;

	d->update_x1 = d->update_y1 = 99999;
	d->update_x2 = d->update_y2 = -1;


	/*  A nice bootup logo:  */
	if (logo) {
		int logo_bottom_margin = LOGO_BOTTOM_MARGIN;
		if (logo == 2) 	/*  Ugly, hardcoded for dev_vga :-)  */
			logo_bottom_margin = 200;

		d->update_x1 = 0;
		d->update_x2 = LOGO_XSIZE-1;
		d->update_y1 = d->visible_ysize-LOGO_YSIZE-logo_bottom_margin;
		d->update_y2 = d->visible_ysize-logo_bottom_margin;
		for (y=0; y<LOGO_YSIZE; y++)
			for (x=0; x<LOGO_XSIZE; x++) {
				int s, a = ((y+d->visible_ysize-LOGO_YSIZE-logo_bottom_margin)*d->xsize
				    + x) * d->bit_depth / 8;
				int b = fb_logo[(y*LOGO_XSIZE+x) / 8] & (128 >> (x&7));
				for (s=0; s<d->bit_depth / 8; s++)
					d->framebuffer[a+s] = b? 0 : 255;
			}
	}

	snprintf(title, sizeof(title), "mips64emul: %ix%ix%i %s framebuffer",
	    d->visible_xsize, d->visible_ysize, d->bit_depth, name);
	title[sizeof(title)-1] = '\0';

#ifdef WITH_X11
	if (cpu->emul->use_x11)
		d->fb_window = x11_fb_init(d->x11_xsize, d->x11_ysize,
		    title, cpu->emul->x11_scaledown, cpu->emul);
	else
#endif
		d->fb_window = NULL;

	memory_device_register(mem, name, baseaddr, size, dev_fb_access,
	    d, /* MEM_DEFAULT */
	    MEM_BINTRANS_OK | MEM_BINTRANS_WRITE_OK,
	    d->framebuffer);

	cpu_add_tickfunction(cpu, dev_fb_tick, d, FB_TICK_SHIFT);
	return d;
}

