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
 *  $Id: console.c,v 1.1 2005-02-06 12:36:37 debug Exp $
 *
 *  Generic console support functions.
 *
 *  This is used by individual device drivers, for example serial controllers,
 *  to attach stdin/stdout of the host system to a specific device.
 *
 *  NOTE: This stuff is non-reentrant.
 */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>

#include "console.h"
#include "emul.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"


extern char *progname;


static struct termios console_oldtermios;
static struct termios console_curtermios;

/*  For 'slave' mode:  */
static struct termios console_slave_tios;

static int console_initialized = 0;
static int console_stdout_pending;

#define	CONSOLE_FIFO_LEN	2048

static unsigned char console_fifo[CONSOLE_FIFO_LEN];
static int console_fifo_head;
static int console_fifo_tail;

/*  Mouse coordinates:  */
static int console_framebuffer_mouse_x;		/*  absolute x, 0-based  */
static int console_framebuffer_mouse_y;		/*  absolute y, 0-based  */
static int console_framebuffer_mouse_fb_nr;	/*  fb_number of last
						    framebuffer cursor update */

static int console_mouse_x;		/*  absolute x, 0-based  */
static int console_mouse_y;		/*  absolute y, 0-based  */
static int console_mouse_fb_nr;		/*  framebuffer number of
					    host movement, 0-based  */
static int console_mouse_buttons;	/*  left=4, middle=2, right=1  */


struct console_handle {
	int		in_use;
	int		using_xterm;
	char		*name;

	int		w_descriptor;
	int		r_descriptor;
};

/*  A simple array of console_handles  */
static struct console_handle *console_handles = NULL;
static int n_console_handles = 0;


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
 *  d_avail():
 *
 *  Returns 1 if anything is available on a descriptor.
 */
static int d_avail(int d)
{
	fd_set rfds;
	struct timeval tv;

	FD_ZERO(&rfds);
	FD_SET(d, &rfds);
	tv.tv_sec = 0;
	tv.tv_usec = 0;
	return select(d+1, &rfds, NULL, NULL, &tv);
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
	return d_avail(STDIN_FILENO);
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
			/*  Ugly hack: convert ctrl-b into ctrl-c.
			    (TODO: fix)  */
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


/*
 *  console_slave_sigcont():
 *
 *  See comment for console_sigcont. This is for used by console_slave().
 */
static void console_slave_sigcont(int x)
{
	/*  Make sure our 'current' termios setting is active:  */
	tcsetattr(STDIN_FILENO, TCSANOW, &console_slave_tios);

	/*  Reset the signal handler:  */
	signal(SIGCONT, console_slave_sigcont);
}


/*
 *  console_slave():
 *
 *  This function is used when running with X11, and mips64emul opens up
 *  separate xterms for each emulated terminal or serial port.
 */
void console_slave(char *arg)
{
	int inputd, outputd;
	int len;
	char *p;
	char buf[400];

	/*  arg = '3,6' or similar, input and output descriptors  */
	/*  printf("console_slave(): arg = '%s'\n", arg);  */

	inputd = atoi(arg);
	p = strchr(arg, ',');
	if (p == NULL) {
		printf("console_slave(): bad arg '%s'\n", arg);
		sleep(5);
		exit(1);
	}
	outputd = atoi(p+1);

	/*  Set the terminal to raw mode:  */
	tcgetattr(STDIN_FILENO, &console_slave_tios);

	console_slave_tios.c_lflag &= ~ICANON;
	console_slave_tios.c_cc[VTIME] = 0;
	console_slave_tios.c_cc[VMIN] = 1;
	console_slave_tios.c_lflag &= ~ECHO;
	console_slave_tios.c_iflag &= ~ICRNL;
	tcsetattr(STDIN_FILENO, TCSANOW, &console_slave_tios);

	signal(SIGINT, SIG_IGN);
	signal(SIGCONT, console_slave_sigcont);

	for (;;) {
		/*  TODO: select() on both inputd and stdin  */

		if (d_avail(inputd)) {
			len = read(inputd, buf, sizeof(buf) - 1);
			if (len < 1)
				exit(0);
			buf[len] = '\0';
			printf("%s", buf);
			fflush(stdout);
		}

		if (d_avail(STDIN_FILENO)) {
			len = read(STDIN_FILENO, buf, sizeof(buf));
			if (len < 1)
				exit(0);
			write(outputd, buf, len);
		}

		usleep(10);
	}
}


/*
 *  console_new_handle():
 *
 *  Allocates a new console_handle struct, and returns a pointer to it.
 *
 *  For internal use.
 */
static struct console_handle *console_new_handle(char *name, int *handlep)
{
	struct console_handle *chp;
	int i, n, found_free = -1;

	/*  Reuse an old slot, if possible:  */
	n = n_console_handles;
	for (i=0; i<n; i++)
		if (!console_handles[i].in_use) {
			found_free = i;
			break;
		}

	if (found_free == -1) {
		/*  Let's realloc console_handles[], to make room
		    for the new one:  */
		console_handles = realloc(console_handles, sizeof(
		    struct console_handle) * (n_console_handles + 1));
		if (console_handles == NULL) {
			printf("console_new_handle(): out of memory\n");
			exit(1);
		}
		found_free = n_console_handles;
		n_console_handles ++;
	}

	chp = &console_handles[found_free];
	memset(chp, 0, sizeof(struct console_handle));

	chp->in_use = 1;
	chp->name = strdup(name);
	if (chp->name == NULL) {
		printf("console_new_handle(): out of memory\n");
		exit(1);
	}

	*handlep = found_free;
	return chp;
}


/*
 *  console_start_slave():
 *
 *  When using X11:
 *
 *  This routine tries to start up an xterm, with another copy of mips64emul
 *  inside. The other mips64emul copy is given arguments that will cause it
 *  to run console_slave().
 *
 *  When not using X11:  Things will seem to work the same way without X11,
 *  but no xterm will actually be started.
 *
 *  consolename should be something like "serial 0".
 *
 *  On success, an integer >= 0 is returned. This can then be used as a
 *  'handle' when writing to or reading from an emulated console.
 *
 *  On failure, -1 is returned.
 */
int console_start_slave(struct machine *machine, char *consolename)
{
	int filedes[2];
	int filedesB[2];
	int res, i, handle;
	char **a;
	pid_t p;
	struct console_handle *chp;

	if (machine == NULL || consolename == NULL) {
		printf("console_start_slave(): NULL ptr\n");
		exit(1);
	}

	chp = console_new_handle(consolename, &handle);

	if (!machine->use_x11) {
		return handle;
	}

	chp->using_xterm = 1;

	res = pipe(filedes);
	if (res) {
		printf("[ console_start_slave(): pipe(): %i ]\n", errno);
		return -1;
	}

	res = pipe(filedesB);
	if (res) {
		printf("[ console_start_slave(): pipe(): %i ]\n", errno);
		return -1;
	}

	/*  printf("filedes = %i,%i\n", filedes[0], filedes[1]);  */
	/*  printf("filedesB = %i,%i\n", filedesB[0], filedesB[1]);  */

	/*  TODO: other names for xterm? For example a config file setting,
	    or read from the environment?  */

	a = malloc(sizeof(char *) * 15);
	if (a == NULL) {
		fprintf(stderr, "console_start_slave(): out of memory\n");
		exit(1);
	}

	a[0] = "xterm";
	a[1] = "-title";
	a[2] = malloc(strlen(machine->name) + strlen(consolename) + 100);
	if (a[2] == NULL) {
		fprintf(stderr, "console_start_slave(): out of memory\n");
		exit(1);
	}
	sprintf(a[2], "mips64emul: '%s' %s", machine->name, consolename);
	a[3] = "-e";
	a[4] = progname;
	a[5] = malloc(50);
	sprintf(a[5], "-WW@S%i,%i", filedes[0], filedesB[1]);
	a[6] = NULL;

	p = fork();
	if (p == -1) {
		printf("[ console_start_slave(): ERROR while trying to "
		    "fork(): %i ]\n", errno);
		return -1;
	} else if (p == 0) {
		close(filedes[1]);
		close(filedesB[0]);
		res = execvp(a[0], a);
		printf("[ console_start_slave(): ERROR while trying to "
		    "execvp(\"");
		while (a[0] != NULL) {
			printf("%s", a[0]);
			if (a[1] != NULL)
				printf(" ");
			a++;
		}
		printf("\"): %i ]\n", errno);
		exit(1);
	}

close(filedes[0]);
close(filedesB[1]);

	/*
	 *  write to filedes[1], read from filedesB[0]
	 */

	chp->w_descriptor = filedes[1];
	chp->r_descriptor = filedesB[0];

	return handle;
}

