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
 *  $Id: x11.c,v 1.36 2004-12-10 03:53:50 debug Exp $
 *
 *  X11-related functions.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "misc.h"

#include "console.h"
#include "emul.h"


#ifndef	WITH_X11

/*  Dummy functions:  */
void x11_redraw_cursor(int i) { }
void x11_redraw(int x) { }
void x11_putpixel_fb(int fb_number, int x, int y, int color) { }
void x11_init(struct emul *emul) { }
struct fb_window *x11_fb_init(int xsize, int ysize, char *name,
	int scaledown, struct emul *emul)
    { return NULL; }
void x11_check_event(void) { }
/* int x11_fb_winxsize = 0, x11_fb_winysize = 0; */


#else	/*  WITH_X11  */


#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>


/*  Framebuffer windows:  */
#define	MAX_FRAMEBUFFER_WINDOWS		8
struct fb_window fb_windows[MAX_FRAMEBUFFER_WINDOWS];
int n_framebuffer_windows = 0;


/*
 *  x11_redraw_cursor():
 *
 *  Redraw a framebuffer's X11 cursor.
 */
void x11_redraw_cursor(int i)
{
	int last_color_used = 0;
	int n_colors_used = 0;

	/*  Remove old cursor, if any:  */
	if (fb_windows[i].x11_display != NULL && fb_windows[i].OLD_cursor_on) {
		XPutImage(fb_windows[i].x11_display,
		    fb_windows[i].x11_fb_window,
		    fb_windows[i].x11_fb_gc, fb_windows[i].fb_ximage,
		    fb_windows[i].OLD_cursor_x/fb_windows[i].scaledown,
		    fb_windows[i].OLD_cursor_y/fb_windows[i].scaledown,
		    fb_windows[i].OLD_cursor_x/fb_windows[i].scaledown,
		    fb_windows[i].OLD_cursor_y/fb_windows[i].scaledown,
		    fb_windows[i].OLD_cursor_xsize/fb_windows[i].scaledown + 1,
		    fb_windows[i].OLD_cursor_ysize/fb_windows[i].scaledown + 1);
	}

	if (fb_windows[i].x11_display != NULL && fb_windows[i].cursor_on) {
		int x, y, subx, suby;
		XImage *xtmp;

		xtmp = XSubImage(fb_windows[i].fb_ximage,
		    fb_windows[i].cursor_x/fb_windows[i].scaledown,
		    fb_windows[i].cursor_y/fb_windows[i].scaledown,
		    fb_windows[i].cursor_xsize/fb_windows[i].scaledown + 1,
		    fb_windows[i].cursor_ysize/fb_windows[i].scaledown + 1);
		if (xtmp == NULL) {
			fatal("out of memory in x11_redraw_cursor()\n");
			return;
		}

		for (y=0; y<fb_windows[i].cursor_ysize; y+=fb_windows[i].scaledown)
			for (x=0; x<fb_windows[i].cursor_xsize; x+=fb_windows[i].scaledown) {
				int px = x/fb_windows[i].scaledown;
				int py = y/fb_windows[i].scaledown;
				int p = 0, n = 0, c = 0;
				unsigned long oldcol;

				for (suby=0; suby<fb_windows[i].scaledown; suby++)
					for (subx=0; subx<fb_windows[i].scaledown; subx++) {
						c = fb_windows[i].cursor_pixels[y+suby][x+subx];
						if (c >= 0) {
							p += c;
							n++;
						}
					}
				if (n > 0)
					p /= n;
				else
					p = c;

				if (n_colors_used == 0) {
					last_color_used = p;
					n_colors_used = 1;
				} else
					if (p != last_color_used)
						n_colors_used = 2;

				switch (p) {
				case CURSOR_COLOR_TRANSPARENT:
					break;
				case CURSOR_COLOR_INVERT:
					oldcol = XGetPixel(xtmp, px, py);
					if (oldcol != fb_windows[i].
					    x11_graycolor[N_GRAYCOLORS-1].pixel)
						oldcol = fb_windows[i].
						    x11_graycolor[N_GRAYCOLORS-1].pixel;
					else
						oldcol = fb_windows[i].
						    x11_graycolor[0].pixel;
					XPutPixel(xtmp, px, py, oldcol);
					break;
				default:	/*  Normal grayscale:  */
					XPutPixel(xtmp, px, py, fb_windows[i].
					    x11_graycolor[p].pixel);
				}
			}

		XPutImage(fb_windows[i].x11_display,
		    fb_windows[i].x11_fb_window,
		    fb_windows[i].x11_fb_gc,
		    xtmp,
		    0, 0,
		    fb_windows[i].cursor_x/fb_windows[i].scaledown,
		    fb_windows[i].cursor_y/fb_windows[i].scaledown,
		    fb_windows[i].cursor_xsize/fb_windows[i].scaledown,
		    fb_windows[i].cursor_ysize/fb_windows[i].scaledown);

		XDestroyImage(xtmp);

		fb_windows[i].OLD_cursor_on = fb_windows[i].cursor_on;
		fb_windows[i].OLD_cursor_x = fb_windows[i].cursor_x;
		fb_windows[i].OLD_cursor_y = fb_windows[i].cursor_y;
		fb_windows[i].OLD_cursor_xsize = fb_windows[i].cursor_xsize;
		fb_windows[i].OLD_cursor_ysize = fb_windows[i].cursor_ysize;
		XFlush(fb_windows[i].x11_display);
	}

	/*  printf("n_colors_used = %i\n", n_colors_used);  */

	if (fb_windows[i].host_cursor != 0 && n_colors_used < 2) {
		/*  Remove the old X11 host cursor:  */
		XUndefineCursor(fb_windows[i].x11_display, fb_windows[i].x11_fb_window);
		XFlush(fb_windows[i].x11_display);
		XFreeCursor(fb_windows[i].x11_display, fb_windows[i].host_cursor);
		fb_windows[i].host_cursor = 0;
	}

	if (n_colors_used >= 2 && fb_windows[i].host_cursor == 0) {
		GC tmpgc;

		/*  Create a new X11 host cursor:  */
		/*  cursor = XCreateFontCursor(fb_windows[i].x11_display, XC_coffee_mug);  */
		if (fb_windows[i].host_cursor_pixmap != 0) {
			XFreePixmap(fb_windows[i].x11_display, fb_windows[i].host_cursor_pixmap);
			fb_windows[i].host_cursor_pixmap = 0;
		}
		fb_windows[i].host_cursor_pixmap = XCreatePixmap(fb_windows[i].x11_display, fb_windows[i].x11_fb_window, 1, 1, 1);
		XSetForeground(fb_windows[i].x11_display, fb_windows[i].x11_fb_gc,
		    fb_windows[i].x11_graycolor[0].pixel);

		tmpgc = XCreateGC(fb_windows[i].x11_display, fb_windows[i].host_cursor_pixmap, 0,0);

		XDrawPoint(fb_windows[i].x11_display, fb_windows[i].host_cursor_pixmap,
		    tmpgc, 0, 0);

		XFreeGC(fb_windows[i].x11_display, tmpgc);

		fb_windows[i].host_cursor = XCreatePixmapCursor(fb_windows[i].x11_display,
		    fb_windows[i].host_cursor_pixmap, fb_windows[i].host_cursor_pixmap,
		    &fb_windows[i].x11_graycolor[N_GRAYCOLORS-1],
		    &fb_windows[i].x11_graycolor[N_GRAYCOLORS-1],
		    0, 0);
		if (fb_windows[i].host_cursor != 0) {
			XDefineCursor(fb_windows[i].x11_display, fb_windows[i].x11_fb_window,
			    fb_windows[i].host_cursor);
			XFlush(fb_windows[i].x11_display);
		}
	}
}


/*
 *  x11_redraw():
 *
 *  Redraw X11 windows.
 */
void x11_redraw(int i)
{
	x11_putimage_fb(i);
	x11_redraw_cursor(i);
	XFlush(fb_windows[i].x11_display);
}


/*
 *  x11_putpixel_fb():
 *
 *  Output a framebuffer pixel. i is the framebuffer number.
 */
void x11_putpixel_fb(int i, int x, int y, int color)
{
	if (n_framebuffer_windows == 0 || fb_windows[i].x11_fb_winxsize <= 0)
		return;

	if (color)
		XSetForeground(fb_windows[i].x11_display,
		    fb_windows[i].x11_fb_gc, fb_windows[i].fg_color);
	else
		XSetForeground(fb_windows[i].x11_display,
		    fb_windows[i].x11_fb_gc, fb_windows[i].bg_color);

	XDrawPoint(fb_windows[i].x11_display, fb_windows[i].x11_fb_window,
	    fb_windows[i].x11_fb_gc, x, y);

	XFlush(fb_windows[i].x11_display);
}


/*
 *  x11_putimage_fb():
 *
 *  Output an entire XImage to a framebuffer window. i is the
 *  framebuffer number.
 */
void x11_putimage_fb(int i)
{
	if (n_framebuffer_windows == 0 || fb_windows[i].x11_fb_winxsize <= 0)
		return;

	XPutImage(fb_windows[i].x11_display, fb_windows[i].x11_fb_window,
	    fb_windows[i].x11_fb_gc, fb_windows[i].fb_ximage, 0,0, 0,0,
	    fb_windows[i].x11_fb_winxsize, fb_windows[i].x11_fb_winysize);
	XFlush(fb_windows[i].x11_display);
}


/*
 *  x11_init():
 *
 *  Initialize X11 stuff (but doesn't create any windows).
 *
 *  It is then up to individual drivers, for example framebuffer devices,
 *  to initialize their own windows.
 */
void x11_init(struct emul *emul)
{
	n_framebuffer_windows = 0;
	memset(&fb_windows, 0, sizeof(fb_windows));

	if (emul->x11_n_display_names > 0) {
		int i;
		for (i=0; i<emul->x11_n_display_names; i++)
			printf("X11 display: %s\n",
			    emul->x11_display_names[i]);
	}

	emul->x11_current_display_name_nr = 0;
}


/*
 *  x11_fb_init():
 *
 *  Initialize a framebuffer window.
 */
struct fb_window *x11_fb_init(int xsize, int ysize, char *name,
	int scaledown, struct emul *emul)
{
	Display *x11_display;
	int x, y, fb_number = 0;
	size_t alloclen, alloc_depth;
	XColor tmpcolor;
	int i;
	char fg[80], bg[80];
	char *display_name;


	while (fb_number < MAX_FRAMEBUFFER_WINDOWS) {
		if (fb_windows[fb_number].x11_fb_winxsize == 0)
			break;
		fb_number ++;
	}

	if (fb_number == MAX_FRAMEBUFFER_WINDOWS) {
		fprintf(stderr, "x11_fb_init(): too many framebuffer windows\n");
		exit(1);
	}

	if (fb_number + 1 >= n_framebuffer_windows)
		n_framebuffer_windows = fb_number + 1;

	memset(&fb_windows[fb_number], 0, sizeof(struct fb_window));

	fb_windows[fb_number].x11_fb_winxsize = xsize;
	fb_windows[fb_number].x11_fb_winysize = ysize;

	/*  Which display name?  */
	display_name = NULL;
	if (emul->x11_n_display_names > 0) {
		display_name = emul->x11_display_names[
		    emul->x11_current_display_name_nr];
		emul->x11_current_display_name_nr ++;
		emul->x11_current_display_name_nr %= emul->x11_n_display_names;
	}

	debug("x11_fb_init(): framebuffer window %i, %ix%i, DISPLAY=%s\n",
	    fb_number, xsize, ysize, display_name? display_name : "(default)");

	x11_display = XOpenDisplay(display_name);

	if (x11_display == NULL) {
		fatal("x11_fb_init(\"%s\"): couldn't open display\n", name);
		exit(1);
	}

	fb_windows[fb_number].x11_screen = DefaultScreen(x11_display);
	fb_windows[fb_number].x11_screen_depth = DefaultDepth(x11_display,
	    fb_windows[fb_number].x11_screen);

	if (fb_windows[fb_number].x11_screen_depth != 8 &&
	    fb_windows[fb_number].x11_screen_depth != 15 &&
	    fb_windows[fb_number].x11_screen_depth != 16 &&
	    fb_windows[fb_number].x11_screen_depth != 24) {
		fatal("\n***\n***  WARNING! Your X server is running %i-bit color mode. This is not really\n",
		    fb_windows[fb_number].x11_screen_depth);
		fatal("***  supported yet.  8, 15, 16, and 24 bits should work.\n");
		fatal("***  24-bit server gives color.  Any other bit depth gives undefined result!\n***\n\n");
	}

	if (fb_windows[fb_number].x11_screen_depth <= 8)
		debug("WARNING! X11 screen depth is not enough for color; "
		    "using only 16 grayscales instead\n");

	strcpy(bg, "Black");
	strcpy(fg, "White");

	XParseColor(x11_display, DefaultColormap(x11_display,
	    fb_windows[fb_number].x11_screen), fg, &tmpcolor);
	XAllocColor(x11_display, DefaultColormap(x11_display,
	    fb_windows[fb_number].x11_screen), &tmpcolor);
	fb_windows[fb_number].fg_color = tmpcolor.pixel;
	XParseColor(x11_display, DefaultColormap(x11_display,
	    fb_windows[fb_number].x11_screen), bg, &tmpcolor);
	XAllocColor(x11_display, DefaultColormap(x11_display,
	    fb_windows[fb_number].x11_screen), &tmpcolor);
	fb_windows[fb_number].bg_color = tmpcolor.pixel;

	for (i=0; i<N_GRAYCOLORS; i++) {
		char cname[8];
		cname[0] = '#';
		cname[1] = cname[2] = cname[3] =
		    cname[4] = cname[5] = cname[6] =
		    "0123456789ABCDEF"[i];
		cname[7] = '\0';
		XParseColor(x11_display, DefaultColormap(x11_display,
		    fb_windows[fb_number].x11_screen), cname,
		    &fb_windows[fb_number].x11_graycolor[i]);
		XAllocColor(x11_display, DefaultColormap(x11_display,
		    fb_windows[fb_number].x11_screen),
		    &fb_windows[fb_number].x11_graycolor[i]);
	}

        XFlush(x11_display);

	alloc_depth = fb_windows[fb_number].x11_screen_depth;

	if (alloc_depth == 24)
		alloc_depth = 32;
	if (alloc_depth == 15)
		alloc_depth = 16;

	fb_windows[fb_number].x11_fb_window = XCreateWindow(
	    x11_display, DefaultRootWindow(x11_display),
	    0, 0, fb_windows[fb_number].x11_fb_winxsize,
	    fb_windows[fb_number].x11_fb_winysize,
	    0, CopyFromParent, InputOutput, CopyFromParent, 0,0);

	XSetStandardProperties(x11_display,
	    fb_windows[fb_number].x11_fb_window, name,
	    "mips64emul", None, NULL, 0, NULL);
	XSelectInput(x11_display, fb_windows[fb_number].x11_fb_window,
	    StructureNotifyMask | ExposureMask | ButtonPressMask |
	    ButtonReleaseMask | PointerMotionMask | KeyPressMask);
	fb_windows[fb_number].x11_fb_gc = XCreateGC(x11_display,
	    fb_windows[fb_number].x11_fb_window, 0,0);

	/*  Make sure the window is mapped:  */
	XMapRaised(x11_display, fb_windows[fb_number].x11_fb_window);

	XSetBackground(x11_display, fb_windows[fb_number].x11_fb_gc,
	    fb_windows[fb_number].bg_color);
	XSetForeground(x11_display, fb_windows[fb_number].x11_fb_gc,
	    fb_windows[fb_number].bg_color);
	XFillRectangle(x11_display, fb_windows[fb_number].x11_fb_window,
	    fb_windows[fb_number].x11_fb_gc, 0,0,
	    fb_windows[fb_number].x11_fb_winxsize,
	    fb_windows[fb_number].x11_fb_winysize);

	fb_windows[fb_number].x11_display = x11_display;
	fb_windows[fb_number].scaledown   = scaledown;

	fb_windows[fb_number].fb_number = fb_number;

	alloclen = xsize * ysize * alloc_depth / 8;
	fb_windows[fb_number].ximage_data = malloc(alloclen);
	if (fb_windows[fb_number].ximage_data == NULL) {
		fprintf(stderr, "out of memory allocating ximage_data\n");
		exit(1);
	}

	fb_windows[fb_number].fb_ximage = XCreateImage(
	    fb_windows[fb_number].x11_display, CopyFromParent,
	    fb_windows[fb_number].x11_screen_depth, ZPixmap, 0,
	    (char *)fb_windows[fb_number].ximage_data,
	    xsize, ysize, 8, xsize * alloc_depth / 8);
	if (fb_windows[fb_number].fb_ximage == NULL) {
		fprintf(stderr, "out of memory allocating ximage\n");
		exit(1);
	}

	/*  Fill the ximage with black pixels:  */
	if (fb_windows[fb_number].x11_screen_depth > 8)
		memset(fb_windows[fb_number].ximage_data, 0, alloclen);
	else {
		debug("x11_fb_init(): clearing the XImage\n");
		for (y=0; y<ysize; y++)
			for (x=0; x<xsize; x++)
				XPutPixel(fb_windows[fb_number].fb_ximage,
				    x, y, fb_windows[fb_number].
				    x11_graycolor[0].pixel);
	}

	x11_putimage_fb(fb_number);

	/*  Fill the 64x64 "hardware" cursor with white pixels:  */
	xsize = ysize = 64;

	/*  Fill the cursor ximage with white pixels:  */
	for (y=0; y<ysize; y++)
		for (x=0; x<xsize; x++)
			fb_windows[fb_number].cursor_pixels[y][x] =
			    N_GRAYCOLORS-1;

	return &fb_windows[fb_number];
}


/*
 *  x11_check_event():
 *
 *  Check for X11 events.
 */
void x11_check_event(void)
{
	int fb_nr;

	for (fb_nr=0; fb_nr<n_framebuffer_windows; fb_nr++) {
		XEvent event;
		int need_redraw = 0, i, found;

		while (XPending(fb_windows[fb_nr].x11_display)) {
			XNextEvent(fb_windows[fb_nr].x11_display, &event);

			if (event.type==ConfigureNotify) {
				need_redraw = 1;
			}

			if (event.type==Expose && event.xexpose.count==0) {
				/*
				 *  TODO:  the xexpose struct has x,y,width,height.
				 *  Those could be used to only redraw the part of
				 *  the framebuffer that was exposed. Note that
				 *  the (mouse) cursor must be redrawn too.
				 */
				/*  x11_winxsize = event.xexpose.width;
				    x11_winysize = event.xexpose.height;  */
				need_redraw = 1;
			}

			if (event.type == MotionNotify) {
				debug("[ X11 MotionNotify: %i,%i ]\n", event.xmotion.x, event.xmotion.y);

				/*  Which window?  */
				found = -1;
				for (i=0; i<n_framebuffer_windows; i++)
					if (fb_windows[fb_nr].x11_display == fb_windows[i].x11_display &&
					    event.xmotion.window == fb_windows[i].x11_fb_window)
						found = i;

				console_mouse_coordinates(event.xmotion.x * fb_windows[found].scaledown,
				    event.xmotion.y * fb_windows[found].scaledown, found);
			}

			if (event.type == ButtonPress) {
				debug("[ X11 ButtonPress: %i ]\n", event.xbutton.button);
				/*  button = 1,2,3 = left,middle,right  */

				console_mouse_button(event.xbutton.button, 1);
			}

			if (event.type == ButtonRelease) {
				debug("[ X11 ButtonRelease: %i ]\n", event.xbutton.button);
				/*  button = 1,2,3 = left,middle,right  */

				console_mouse_button(event.xbutton.button, 0);
			}

			if (event.type==KeyPress) {
				char text[15];
				KeySym key;

				memset(text, sizeof(text), 0);

				if (XLookupString(&event.xkey, text,
				    sizeof(text), &key, 0) == 1) {
					console_makeavail(text[0]);
				}
			}
		}

		if (need_redraw)
			x11_redraw(fb_nr);
	}
}


#endif	/*  WITH_X11  */
