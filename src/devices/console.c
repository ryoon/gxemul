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
 *  $Id: console.c,v 1.3 2003-11-07 08:48:15 debug Exp $
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

#include "misc.h"
#include "console.h"


extern int register_dump;
extern int instruction_trace;
extern int ncpus;


struct termios console_oldtermios;
struct termios console_curtermios;


static int console_initialized = 0;
static int console_stdout_pending;


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
 *  console_charavail():
 *
 *  Returns 1 if a char is available, 0 otherwise.
 */
int console_charavail(void)
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
 *  console_readchar():
 *
 *  Returns 0..255 if a char was available, -1 otherwise.
 */
int console_readchar(void)
{
	if (!console_charavail())
		return -1;

	return getchar();
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


