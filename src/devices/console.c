/*
 *  Copyright (C) 2003-2005  Anders Gavare.  All rights reserved.
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
 *  $Id: console.c,v 1.23 2005-01-22 07:43:07 debug Exp $
 *
 *  Generic console support functions.
 *
 *  This is used by individual device drivers, for example serial controllers,
 *  to attach stdin/stdout of the host system to a specific device.
 */

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <sys/time.h>

#include "console.h"
#include "emul.h"
#include "memory.h"
#include "misc.h"


struct termios console_oldtermios;
struct termios console_curtermios;

static int console_initialized = 0;
static int console_stdout_pending;

#define	CONSOLE_FIFO_LEN	2048

static unsigned char console_fifo[CONSOLE_FIFO_LEN];
static int console_fifo_head;
static int console_fifo_tail;

/*  Mouse coordinates:  */
static int console_framebuffer_mouse_x;		/*  absolute x, 0-based  */
static int console_framebuffer_mouse_y;		/*  absolute y, 0-based  */
static int console_framebuffer_mouse_fb_nr;	/*  fb_number of last framebuffer cursor update  */

static int console_mouse_x;		/*  absolute x, 0-based  */
static int console_mouse_y;		/*  absolute y, 0-based  */
static int console_mouse_fb_nr;		/*  framebuffer number of host movement, 0-based  */
static int console_mouse_buttons;	/*  left=4, middle=2, right=1  */


/*
 *  console_init():
 *
 *  Put host's console into single-character (non-canonical) mode.
 */
void console_init(struct emul *emul)
{
	int i, tra;

	if (console_initialized)
		return;

	tcgetattr(STDIN_FILENO, &console_oldtermios);
	memcpy(&console_curtermios, &console_oldtermios,
	    sizeof (struct termios));

	console_curtermios.c_lflag &= ~ICANON;
	console_curtermios.c_cc[VTIME] = 0;
	console_curtermios.c_cc[VMIN] = 1;

	console_curtermios.c_lflag &= ~ECHO;

	/*
	 *  Most guest OSes seem to work ok without ~ICRNL, but Linux on
	 *  DECstation requires it to be usable.  Unfortunately, clearing
	 *  out ICRNL makes tracing with '-t ... |more' akward, as you
	 *  might need to use CTRL-J instead of the enter key.  Hence,
	 *  this bit is only cleared if we're not tracing:
	 */
	tra = 0;
	for (i=0; i<emul->n_machines; i++)
		if (emul->machines[i]->show_trace_tree ||
		    emul->machines[i]->instruction_trace ||
		    emul->machines[i]->register_dump)
			tra = 1;
	if (!tra)
		console_curtermios.c_iflag &= ~ICRNL;

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
 *  console_sigcont():
 *
 *  If the user presses CTRL-Z (to stop the emulator process) and then
 *  continues, we have to make sure that the right termios settings are
 *  active.  (This should be set as the SIGCONT signal handler in src/emul.c.)
 */
void console_sigcont(int x)
{
	if (!console_initialized)
		return;

	/*  Make sure our 'current' termios setting is active:  */
	tcsetattr(STDIN_FILENO, TCSANOW, &console_curtermios);

	/*  Reset the signal handler:  */
	signal(SIGCONT, console_sigcont);
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
	while (console_stdin_avail()) {
		unsigned char ch[1000]; /* = getchar(); */
		int i;
		ssize_t len = read(STDIN_FILENO, ch, sizeof(ch));

		for (i=0; i<len; i++) {
			/*  printf("[ %i: %i ]\n", i, ch[i]);  */
			/*  Ugly hack: convert ctrl-b into ctrl-c.  (TODO: fix)  */
			if (ch[i] == 2)
				ch[i] = 3;
			console_makeavail(ch[i]);
		}
	}

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
 *  Flushes stdout, if necessary, and resets console_stdout_pending to zero.
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
 *  x and y are absolute coordinates, fb_nr is where the mouse movement
 *  took place.
 */
void console_mouse_coordinates(int x, int y, int fb_nr)
{
	/*  TODO: fb_nr isn't used yet.  */

	console_mouse_x = x;
	console_mouse_y = y;
	console_mouse_fb_nr = fb_nr;
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
 *  console_get_framebuffer_mouse():
 *
 *  TODO: Comment
 */
void console_get_framebuffer_mouse(int *x, int *y, int *fb_nr)
{
	*x = console_framebuffer_mouse_x;
	*y = console_framebuffer_mouse_y;
	*fb_nr = console_framebuffer_mouse_fb_nr;
}


/*
 *  console_set_framebuffer_mouse():
 *
 *  A framebuffer device calls this function when it sets the
 *  position of a cursor (ie a mouse cursor).
 */
void console_set_framebuffer_mouse(int x, int y, int fb_nr)
{
	console_framebuffer_mouse_x = x;
	console_framebuffer_mouse_y = y;
	console_framebuffer_mouse_fb_nr = fb_nr;
}


/*
 *  console_getmouse():
 *
 *  Puts current mouse data into the variables pointed to by
 *  the arguments.
 */
void console_getmouse(int *x, int *y, int *buttons, int *fb_nr)
{
	*x = console_mouse_x;
	*y = console_mouse_y;
	*buttons = console_mouse_buttons;
	*fb_nr = console_mouse_fb_nr;
}


