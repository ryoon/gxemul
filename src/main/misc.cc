/*
 *  Copyright (C) 2005-2007  Anders Gavare.  All rights reserved.
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
 *  $Id: misc.cc,v 1.1 2007-11-16 09:23:33 debug Exp $
 *
 *  This file contains things that don't fit anywhere else, and fake/dummy
 *  implementations of libc functions that are missing on some systems.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#include "cpu.h"
#include "misc.h"


extern "C"
{

/*
 *  mystrtoull():
 *
 *  This function is used on OSes that don't have strtoull() in libc.
 */
unsigned long long mystrtoull(const char *s, char **endp, int base)
{
	unsigned long long res = 0;
	int minus_sign = 0;

	if (s == NULL)
		return 0;

	/*  TODO: Implement endp?  */
	if (endp != NULL) {
		fprintf(stderr, "mystrtoull(): endp isn't implemented\n");
		exit(1);
	}

	if (s[0] == '-') {
		minus_sign = 1;
		s++;
	}

	/*  Guess base:  */
	if (base == 0) {
		if (s[0] == '0') {
			/*  Just "0"? :-)  */
			if (!s[1])
				return 0;
			if (s[1] == 'x' || s[1] == 'X') {
				base = 16;
				s += 2;
			} else {
				base = 8;
				s ++;
			}
		} else if (s[0] >= '1' && s[0] <= '9')
			base = 10;
	}

	while (s[0]) {
		int c = s[0];
		if (c >= '0' && c <= '9')
			c -= '0';
		else if (c >= 'a' && c <= 'f')
			c = c - 'a' + 10;
		else if (c >= 'A' && c <= 'F')
			c = c - 'A' + 10;
		else
			break;
		switch (base) {
		case 8:	res = (res << 3) | c;
			break;
		case 16:res = (res << 4) | c;
			break;
		default:res = (res * base) + c;
		}
		s++;
	}

	if (minus_sign)
		res = (uint64_t) -(int64_t)res;
	return res;
}


/*
 *  mymkstemp():
 *
 *  mkstemp() replacement for systems that lack that function. This is NOT
 *  really safe, but should at least allow the emulator to build and run.
 */
int mymkstemp(char *templ)
{
	int h = 0;
	char *p = templ;

	while (*p) {
		if (*p == 'X')
			*p = 48 + random() % 10;
		p++;
	}

	h = open(templ, O_RDWR, 0600);
	return h;
}


#ifdef USE_STRLCPY_REPLACEMENTS
/*
 *  mystrlcpy():
 *
 *  Quick hack strlcpy() replacement for systems that lack that function.
 *  NOTE: No length checking is done.
 */
size_t mystrlcpy(char *dst, const char *src, size_t size)
{
	strcpy(dst, src);
	return strlen(src);
}


/*
 *  mystrlcat():
 *
 *  Quick hack strlcat() replacement for systems that lack that function.
 *  NOTE: No length checking is done.
 */
size_t mystrlcat(char *dst, const char *src, size_t size)
{
	size_t orig_dst_len = strlen(dst);
	strcat(dst, src);
	return strlen(src) + orig_dst_len;
}
#endif


/*
 *  print_separator_line():
 *
 *  Prints a line of "----------".
 */
void print_separator_line(void)
{
        int i = 79; 
        while (i-- > 0)
                debug("-");
        debug("\n");
}


/*****************************************************************************
 *
 *  NOTE:  debug(), fatal(), and debug_indentation() are not re-entrant.
 *         The global variable quiet_mode can be used to suppress the output
 *         of debug(), but not the output of fatal().
 *
 *****************************************************************************/

int verbose = 0;
int quiet_mode = 0;

static int debug_indent = 0;
static int debug_currently_at_start_of_line = 1;


/*
 *  va_debug():
 *
 *  Used internally by debug() and fatal().
 */
static void va_debug(va_list argp, const char *fmt)
{
	char buf[DEBUG_BUFSIZE + 1];
	char *s;
	int i;

	buf[0] = buf[DEBUG_BUFSIZE] = 0;
	vsnprintf(buf, DEBUG_BUFSIZE, fmt, argp);

	s = buf;
	while (*s) {
		if (debug_currently_at_start_of_line) {
			for (i=0; i<debug_indent; i++)
				printf(" ");
		}

		printf("%c", *s);

		debug_currently_at_start_of_line = 0;
		if (*s == '\n' || *s == '\r')
			debug_currently_at_start_of_line = 1;
		s++;
	}
}


/*
 *  debug_indentation():
 *
 *  Modify the debug indentation.
 */
void debug_indentation(int diff)
{
	debug_indent += diff;
	if (debug_indent < 0)
		fprintf(stderr, "WARNING: debug_indent less than 0!\n");
}


/*
 *  debug():
 *
 *  Debug output (ignored if quiet_mode is set).
 */
void debug(const char *fmt, ...)
{
	va_list argp;

	if (quiet_mode)
		return;

	va_start(argp, fmt);
	va_debug(argp, fmt);
	va_end(argp);
}


/*
 *  fatal():
 *
 *  Fatal works like debug(), but doesn't care about the quiet_mode
 *  setting.
 */
void fatal(const char *fmt, ...)
{
	va_list argp;

	va_start(argp, fmt);
	va_debug(argp, fmt);
	va_end(argp);
}


}	// extern "C"

