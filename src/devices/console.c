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
 *  $Id: console.c,v 1.7 2004-01-06 01:59:51 debug Exp $
 *
 *  Generic console support functions.
 *
 *  This is used by individual device drivers, for example serial controllers,
 *  to attach stdin/stdout of the host system to a specific device.
 */

#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <sys/time.h>

#include "memory.h"
#include "misc.h"
#include "console.h"


extern int register_dump;
extern int instruction_trace;
extern int ncpus;


struct termios console_oldtermios;
struct termios console_curtermios;

static int console_initialized = 0;
static int console_stdout_pending;

#define	CONSOLE_FIFO_LEN	2048

static unsigned char console_fifo[CONSOLE_FIFO_LEN];
static int console_fifo_head;
static int console_fifo_tail;

static int console_mouse_x;		/*  absolute x, 0-based  */
static int console_mouse_y;		/*  absolute y, 0-based  */
static int console_mouse_buttons;	/*  left=4, middle=2, right=1  */


/*
 *  console_init():
 *
 *  Put host's console into single-character (non-canonical) mode.
 */
void console_init(void)
{
	if (console_initialized)
		return;

	tcgetattr(STDIN_FILENO, &console_oldtermios);
	memcpy(&console_curtermios, &console_oldtermios, sizeof (struct termios));

	console_curtermios.c_lflag &= (~ICANON);
	console_curtermios.c_cc[VTIME] = 0;
	console_curtermios.c_cc[VMIN] = 1;

	console_curtermios.c_lflag &= (~ECHO);

	tcsetattr(STDIN_FILENO, TCSANOW, &console_curtermios);

	console_stdout_pending = 1;
	console_fifo_head = console_fifo_tail = 0;

	console_mouse_x = 0;
	console_mouse_y = 0;
	console_mouse_buttons = 0;

	console_initialized = 1;
}


/*
 *  console_deinit():
 *
 *  Restore host's console settings.
 */
void console_deinit(void)
{
	if (!console_initialized)
		return;

	tcsetattr(STDIN_FILENO, TCSANOW, &console_oldtermios);

	console_initialized = 0;
}


/*
 *  console_makeavail():
 *
 *  Put a character in the queue, so that it will be avaiable,
 *  by inserting it into the char fifo.
 */
void console_makeavail(char ch)
{
	console_fifo[console_fifo_head] = ch;
	console_fifo_head = (console_fifo_head + 1) % CONSOLE_FIFO_LEN;

	if (console_fifo_head == console_fifo_tail)
		fatal("WARNING: console fifo overrun\n");
}


/*
 *  console_stdin_avail():
 *
 *  Returns 1 if a char is available from stdin, 0 otherwise.
 */
int console_stdin_avail(void)
{
	fd_set rfds;
	struct timeval tv;
	int result;
	int s = STDIN_FILENO;

	FD_ZERO(&rfds);
	FD_SET(s, &rfds);
	tv.tv_sec = 0;
	tv.tv_usec = 0;
	result = select(s+1, &rfds, NULL, NULL, &tv);
	return result;
}


/*
 *  console_charavail():
 *
 *  Returns 1 if a char is available in the fifo, 0 otherwise.
 */
int console_charavail(void)
{
	while (console_stdin_avail())
		console_makeavail(getchar());

	if (console_fifo_head == console_fifo_tail)
		return 0;

	return 1;
}


/*
 *  console_readchar():
 *
 *  Returns 0..255 if a char was available, -1 otherwise.
 */
int console_readchar(void)
{
	int ch;

	if (!console_charavail())
		return -1;

	ch = console_fifo[console_fifo_tail];
	console_fifo_tail = (console_fifo_tail + 1) % CONSOLE_FIFO_LEN;

	return ch;
}


/*
 *  console_putchar():
 *
 *  Prints a char to stdout, and sets the console_stdout_pending flag.
 */
void console_putchar(int ch)
{
	putchar(ch);

	/*  Assume flushes by OS or libc on newlines:  */
	if (ch == '\n')
		console_stdout_pending = 0;
	else
		console_stdout_pending = 1;
}


/*
 *  console_flush():
 *
 *  Flushes stdout, if neccessary, and resets console_stdout_pending to zero.
 */
void console_flush(void)
{
	if (console_stdout_pending)
		fflush(stdout);

	console_stdout_pending = 0;
}


/*
 *  console_mouse_coordinates():
 *
 *  Sets mouse coordinates. Called by for example an X11 event handler.
 *  x and y are absolute coordinates.
 */
void console_mouse_coordinates(int x, int y)
{
	console_mouse_x = x;
	console_mouse_y = y;
}


/*
 *  console_mouse_button():
 *
 *  Sets a mouse button to be pressed or released. Called by for example an
 *  X11 event handler.  button is 1 (left), 2 (middle), or 3 (right), and
 *  pressed = 1 for pressed, 0 for not pressed.
 */
void console_mouse_button(int button, int pressed)
{
	int mask = 1 << (3-button);

	if (pressed)
		console_mouse_buttons |= mask;
	else
		console_mouse_buttons &= ~mask;
}


/*
 *  console_getmouse():
 *
 *  Puts current mouse data into the variables pointed to by
 *  the arguments.
 */
void console_getmouse(int *x, int *y, int *buttons)
{
	*x = console_mouse_x;
	*y = console_mouse_y;
	*buttons = console_mouse_buttons;
}

