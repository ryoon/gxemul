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
 *  $Id: x11.c,v 1.2 2003-11-06 13:56:08 debug Exp $
 *
 *  X11-related functions.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "misc.h"


#ifndef	WITH_X11

/*  Dummy functions:  */
void x11_redraw(void) { }
void x11_putpixel_fb(int fb_number, int x, int y, int color) { }
void x11_init(void) { }
struct fb_window *x11_fb_init(int xsize, int ysize, char *name) { return NULL; }
void x11_check_event(void) { }
int x11_fb_winxsize = 0, x11_fb_winysize = 0;

#else

#include <X11/Xlib.h>
#include <X11/Xutil.h>   


Display *x11_display = NULL;
int x11_screen;
unsigned long fg_COLOR, bg_COLOR;
/*  int x11_winx, x11_winy; int x11_winxsize, x11_winysize;  */

/*  Framebuffer windows:  */
#define	MAX_FRAMEBUFFER_WINDOWS		8
int n_framebuffer_windows = 0;
struct fb_window fb_windows[MAX_FRAMEBUFFER_WINDOWS];


/*
 *  x11_redraw():
 *
 *  Redraw X11 windows.
 */
void x11_redraw(void)
{
	int i;

	if (x11_display==NULL || n_framebuffer_windows == 0)
		return;

	for (i=0; i<n_framebuffer_windows; i++)
		x11_putimage_fb(i);

	XFlush(x11_display);
}


/*
 *  x11_putpixel_fb():
 *
 *  Output a framebuffer pixel.
 */
void x11_putpixel_fb(int fb_number, int x, int y, int color)
{
	if (x11_display==NULL || n_framebuffer_windows == 0 || fb_windows[fb_number].x11_fb_winxsize <= 0)
		return;

	if (color)
		XSetForeground(x11_display, fb_windows[fb_number].x11_fb_gc, fg_COLOR);
	else
		XSetForeground(x11_display, fb_windows[fb_number].x11_fb_gc, bg_COLOR);

	XDrawPoint(x11_display, fb_windows[fb_number].x11_fb_window, fb_windows[fb_number].x11_fb_gc, x, y);

	XFlush(x11_display);
}


/*
 *  x11_putimage_fb():
 *
 *  Output an entire XImage to a framebuffer window.
 */
void x11_putimage_fb(int fb_number)
{
	if (x11_display==NULL || n_framebuffer_windows == 0 || fb_windows[fb_number].x11_fb_winxsize <= 0)
		return;

	XPutImage(x11_display, fb_windows[fb_number].x11_fb_window, fb_windows[fb_number].x11_fb_gc,
	    fb_windows[fb_number].fb_ximage, 0,0, 0,0, fb_windows[fb_number].x11_fb_winxsize, fb_windows[fb_number].x11_fb_winysize);
	XFlush(x11_display);
}


/*
 *  x11_init():
 *
 *  Initialize X11 stuff (but doesn't create any windows).
 *
 *  It is then up to individual drivers, for example framebuffer devices,
 *  to initialize their own windows.
 */
void x11_init(void)
{
	XColor tmpcolor;
	char fg[80], bg[80];

	x11_display = XOpenDisplay(NULL);
	if (x11_display == NULL) {
		fatal("couldn't open display\n");
		exit(1);
	}

	x11_screen = DefaultScreen(x11_display);

	strcpy(bg, "Black");
	strcpy(fg, "White");

	XParseColor(x11_display, DefaultColormap(x11_display, x11_screen), fg, &tmpcolor);
	XAllocColor(x11_display, DefaultColormap(x11_display, x11_screen), &tmpcolor);
	fg_COLOR = tmpcolor.pixel;
	XParseColor(x11_display, DefaultColormap(x11_display, x11_screen), bg, &tmpcolor);
	XAllocColor(x11_display, DefaultColormap(x11_display, x11_screen), &tmpcolor);
	bg_COLOR = tmpcolor.pixel;

        XFlush(x11_display);

	n_framebuffer_windows = 0;
}


/*
 *  x11_fb_init():
 *
 *  Initialize a framebuffer window.
 */
struct fb_window *x11_fb_init(int xsize, int ysize, char *name)
{
	int fb_number = 0;
	int bytes_per_pixel = 3;

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

	debug("x11_fb_init(): framebuffer window %i, %ix%i\n", fb_number, xsize, ysize);

	memset(&fb_windows[fb_number], 0, sizeof(struct fb_window));

	fb_windows[fb_number].x11_fb_winxsize = xsize;
	fb_windows[fb_number].x11_fb_winysize = ysize;

	fb_windows[fb_number].x11_fb_window = XCreateWindow(x11_display, DefaultRootWindow(x11_display),
	    0, 0, fb_windows[fb_number].x11_fb_winxsize, fb_windows[fb_number].x11_fb_winysize,
	    0, CopyFromParent, InputOutput, CopyFromParent, 0,0);

	XSetStandardProperties(x11_display, fb_windows[fb_number].x11_fb_window, name,
	    "mips64emul", None, NULL, 0, NULL);
	XSelectInput(x11_display, fb_windows[fb_number].x11_fb_window, StructureNotifyMask | ExposureMask
	    | ButtonPressMask | KeyPressMask);
	fb_windows[fb_number].x11_fb_gc = XCreateGC(x11_display, fb_windows[fb_number].x11_fb_window, 0,0);

	XMapRaised(x11_display, fb_windows[fb_number].x11_fb_window);

	XSetBackground(x11_display, fb_windows[fb_number].x11_fb_gc, bg_COLOR);
	XSetForeground(x11_display, fb_windows[fb_number].x11_fb_gc, bg_COLOR);
	XFillRectangle(x11_display, fb_windows[fb_number].x11_fb_window,
	    fb_windows[fb_number].x11_fb_gc, 0,0, fb_windows[fb_number].x11_fb_winxsize, fb_windows[fb_number].x11_fb_winysize);

	fb_windows[fb_number].x11_display = x11_display;

	fb_windows[fb_number].ximage_data = malloc(bytes_per_pixel * xsize * ysize);
	if (fb_windows[fb_number].ximage_data == NULL) {
		fprintf(stderr, "out of memory allocating ximage_data\n");
		exit(1);
	}
	memset(fb_windows[fb_number].ximage_data, 0, bytes_per_pixel * xsize * ysize);

	fb_windows[fb_number].fb_ximage = XCreateImage(fb_windows[fb_number].x11_display, CopyFromParent,
	    8 * bytes_per_pixel, XYPixmap, 0, fb_windows[fb_number].ximage_data, xsize, ysize, 8, 0);
	if (fb_windows[fb_number].fb_ximage == NULL) {
		fprintf(stderr, "out of memory allocating ximage\n");
		exit(1);
	}

	x11_putimage_fb(fb_number);

	return &fb_windows[fb_number];
}


/*
 *  x11_check_event():
 *
 *  Check for X11 events.
 */
void x11_check_event(void)
{
	XEvent event;

	if (x11_display == NULL)
		return;

	if (!XPending(x11_display))
		return;

	XNextEvent(x11_display, &event);

	if (event.type==ConfigureNotify) {
/*		x11_winxsize = event.xconfigure.width;
		x11_winysize = event.xconfigure.height; */
		x11_redraw();
	}

	if (event.type==Expose && event.xexpose.count==0) {
/*		x11_winxsize = event.xexpose.width;
		x11_winysize = event.xexpose.height; */
		x11_redraw();
	}

/*
if (event.type==ButtonPress) my_redraw();
if (event.type==KeyPress) {
	char text[10];
	if (XLookupString(&event.xkey,text,10,&key,0)==1 &&
		text[0]=='q') close_x();
}
*/
}


#endif	/*  WITH_X11  */
