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
 *  $Id: dev_cons.c,v 1.5 2004-01-06 01:59:51 debug Exp $
 *  
 *  A console device.  (Fake, only useful for simple tests.)
 *
 *  This device provides memory mapped I/O for a simple console supporting
 *  putchar (writing to memory) and getchar (reading from memory).
 */

#include <stdio.h>

#include "memory.h"
#include "misc.h"
#include "console.h"
#include "devices.h"


extern int register_dump;
extern int instruction_trace;


/*
 *  dev_cons_access():
 *
 *  Returns 1 if ok, 0 on error.
 */
int dev_cons_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *extra)
{
	int i;

	/*  TODO: care about the relative address, instead of just the write flag  */

	if (writeflag == MEM_WRITE) {
		for (i=0; i<len; i++) {
			if (data[i] != 0) {
				if (register_dump || instruction_trace)
					debug("putchar '");

				console_putchar(data[i]);

				if (register_dump || instruction_trace)
					debug("'\n");
				fflush(stdout);
			}
		}
        } else {
		int ch = console_readchar();
		for (i=0; i<len; i++)
			data[i] = ch;
	}

	return 1;
}


/*
 *  dev_cons_init():
 */
void dev_cons_init(struct memory *mem)
{
	/*
	 *  TODO:  stdin should be set to nonblocking mode...

	    int tmp;
	    tmp = fcntl (socket, F_GETFL);
	    if (blocking)
	        fcntl (socket, F_SETFL, tmp & (~O_NONBLOCK) );
	    else
	        fcntl (socket, F_SETFL, tmp | (O_NONBLOCK) );

	 *  or something similar
	 */

	memory_device_register(mem, "cons", DEV_CONS_ADDRESS, DEV_CONS_LENGTH, dev_cons_access, NULL);
}

