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
 *  $Id: dev_cons.c,v 1.17 2005-01-19 14:24:20 debug Exp $
 *  
 *  A console device.  (Fake, only useful for simple tests.)
 *
 *  This device provides memory mapped I/O for a simple console supporting
 *  putchar (writing to memory) and getchar (reading from memory), and
 *  support for halting the emulator.  (This is useful for regression tests.)
 */

#include <stdio.h>

#include "console.h"
#include "devices.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"


/*
 *  dev_cons_access():
 */
int dev_cons_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *extra)
{
	int i;

	/*  Exit the emulator:  */
	if (relative_addr == DEV_CONS_HALT) {
		cpu->running = 0;
		cpu->machine->exit_without_entering_debugger = 1;
		return 1;
	}

	if (writeflag == MEM_WRITE) {
		for (i=0; i<len; i++) {
			if (data[i] != 0) {
				if (cpu->machine->register_dump ||
				    cpu->machine->instruction_trace)
					debug("putchar '");

				console_putchar(data[i]);

				if (cpu->machine->register_dump ||
				    cpu->machine->instruction_trace)
					debug("'\n");
				fflush(stdout);
			}
		}
        } else {
		int ch = console_readchar();
		if (ch < 0)
			ch = 0;
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

	memory_device_register(mem, "cons", DEV_CONS_ADDRESS,
	    DEV_CONS_LENGTH, dev_cons_access, NULL, MEM_DEFAULT, NULL);
}

