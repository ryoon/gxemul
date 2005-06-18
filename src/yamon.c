/*
 *  Copyright (C) 2005  Anders Gavare.  All rights reserved.
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
 *  $Id: yamon.c,v 1.1 2005-06-18 21:07:39 debug Exp $
 *
 *  YAMON emulation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "console.h"
#include "cpu.h"
#include "cpu_mips.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"


/*
 *  mem_readchar():
 *  
 *  Reads a byte from emulated RAM, using a MIPS register as a base address.
 *  (Helper function.)
 */
static unsigned char mem_readchar(struct cpu *cpu, int regbase, int offset)
{
	unsigned char ch;
	cpu->memory_rw(cpu, cpu->mem, cpu->cd.mips.gpr[regbase] + offset,
	    &ch, sizeof(ch), MEM_READ, CACHE_DATA | NO_EXCEPTIONS);
	return ch;
}


/*
 *  yamon_emul():
 *
 *  YAMON emulation (for evbmips).
 */
int yamon_emul(struct cpu *cpu)
{
	int ofs = cpu->pc & 0xfff;
	int n;

	switch (ofs) {
	case 0x804:	/*  "print count": string at a1, count at a2  */
		n = 0;
		while (n < cpu->cd.mips.gpr[MIPS_GPR_A2]) {
			char ch = mem_readchar(cpu, MIPS_GPR_A1, n);
			console_putchar(cpu->machine->main_console_handle,
			    ch);
			n++;
		}
		break;
	default:cpu_register_dump(cpu->machine, cpu, 1, 0);
		printf("\n");
		fatal("[ yamon_emul(): unimplemented ofs 0x%x ]\n", ofs);
		cpu->running = 0;
		cpu->dead = 1;
	}

	return 1;
}

