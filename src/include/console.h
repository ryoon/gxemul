#ifndef	CONSOLE_H
#define	CONSOLE_H

/*
 *  Copyright (C) 2003  Anders Gavare.  All rights reserved.
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
 *  $Id: console.h,v 1.8 2004-11-01 12:23:27 debug Exp $
 *
 *  Generic console functions.  (See console.c for more info.)
 */

#include "misc.h"

void console_init(struct emul *);
void console_deinit(void);
void console_sigcont(int x);
void console_makeavail(char ch);
int console_charavail(void);
int console_readchar(void);
void console_putchar(int ch);
void console_flush(void);

void console_mouse_coordinates(int x, int y, int fb_nr);
void console_mouse_button(int, int);
void console_get_framebuffer_mouse(int *x, int *y, int *fb_nr);
void console_set_framebuffer_mouse(int x, int y, int fb_nr);
void console_getmouse(int *x, int *y, int *buttons, int *fb_nr);

#endif	/*  CONSOLE_H  */

