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
 *  $Id: of.c,v 1.4 2005-03-09 18:30:30 debug Exp $
 *
 *  OpenFirmware emulation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "console.h"
#include "cpu.h"
#include "cpu_ppc.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"


#define	N_MAX_ARGS	10
#define	ARG_MAX_LEN	4096

extern int quiet_mode;

/*  TODO: IMPORTANT! Change this into something else, to allow multiple
	opens of the same device:  */
#define	HANDLE_STDIN	0
#define	HANDLE_STDOUT	1
#define	HANDLE_STDERR	2
#define	HANDLE_MMU	3
#define	HANDLE_MEMORY	4
#define	HANDLE_CHOSEN	5


/*
 *  readstr():
 *
 *  Helper function to read a string from emulated memory.
 */
static void readstr(struct cpu *cpu, uint64_t addr, char *strbuf,
	int bufsize)
{
	int i;
	for (i=0; i<bufsize; i++) {
		unsigned char ch;
		cpu->memory_rw(cpu, cpu->mem, addr + i,
		    &ch, sizeof(ch), MEM_READ, CACHE_DATA | NO_EXCEPTIONS);
		strbuf[i] = '\0';
		if (ch >= 1 && ch < 32)
			ch = 0;
		strbuf[i] = ch;
		if (strbuf[i] == '\0')
			break;
	}

	strbuf[bufsize - 1] = '\0';
}


/*
 *  of_emul():
 *
 *  OpenFirmware call emulation.
 */
int of_emul(struct cpu *cpu)
{
	int i, nargs, nret, ofs, handle;
	char service[50];
	char arg[N_MAX_ARGS][ARG_MAX_LEN];
	char tmpstr[ARG_MAX_LEN];
	uint64_t base, ptr;
	uint64_t buf, buflen;

	/*
	 *  r3 points to "prom_args":
	 *
	 *	char *service;		(probably 32 bit)
	 *	int nargs;
	 *	int nret;
	 *	char *args[10];
	 */

	base = cpu->cd.ppc.gpr[3];

	/*  TODO: how about 64-bit OpenFirmware?  */
	ptr   = load_32bit_word(cpu, base);
	nargs = load_32bit_word(cpu, base + 4);
	nret  = load_32bit_word(cpu, base + 8);

	readstr(cpu, ptr, service, sizeof(service));

	debug("[ of: %s(", service);
	ofs = 12;
	for (i=0; i<nargs; i++) {
		if (i > 0)
			debug(", ");
		if (i >= N_MAX_ARGS) {
			fatal("TOO MANY ARGS!");
			continue;
		}
		ptr = load_32bit_word(cpu, base + ofs);
		readstr(cpu, ptr, arg[i], ARG_MAX_LEN);
		if (arg[i][0])
			debug("\"%s\"", arg[i]);
		else {
			int x = ptr;
			if (x > -256 && x < 256)
				debug("%i", x);
			else
				debug("0x%x", x);
		}
		ofs += sizeof(uint32_t);
	}
	debug(") ]\n");

	/*  Return value:  */
	cpu->cd.ppc.gpr[3] = 0;

	/*  Note: base + ofs points to the first return slot.  */

	if (strcmp(service, "exit") == 0) {
		cpu->running = 0;
	} else if (strcmp(service, "finddevice") == 0) {
		/*  Return a handle in ret[0]:  */
		if (nret < 1) {
			fatal("[ of: finddevice(\"%s\"): nret < 1! ]\n",
			    arg[0]);
		} else if (strcmp(arg[0], "/memory") == 0) {	
			store_32bit_word(cpu, base + ofs, HANDLE_MEMORY);
		} else if (strcmp(arg[0], "/chosen") == 0) {	
			store_32bit_word(cpu, base + ofs, HANDLE_CHOSEN);
		} else {
			/*  Device not found.  */
			fatal("[ of: finddevice(\"%s\"): not yet"
			    " implemented ]\n", arg[0]);
			cpu->cd.ppc.gpr[3] = -1;
		}
	} else if (strcmp(service, "getprop") == 0) {
		handle = load_32bit_word(cpu, base + 12 + 4*0);
		ptr    = load_32bit_word(cpu, base + 12 + 4*1);
		buf    = load_32bit_word(cpu, base + 12 + 4*2);
		buflen = load_32bit_word(cpu, base + 12 + 4*3);
		readstr(cpu, ptr, tmpstr, sizeof(tmpstr));

		/*  TODO: rewrite this  */
		switch (handle) {
		case HANDLE_MEMORY:
			if (strcmp(tmpstr, "available") == 0) {
				store_32bit_word(cpu, base + ofs, 2*8);
				/*  TODO.  {start, size}  */
				store_32bit_word(cpu, buf, 0);
				store_32bit_word(cpu, buf+4,
				    cpu->machine->physical_ram_in_mb * 1048576
				    - 65536);
				store_32bit_word(cpu, buf+8, 0);
				store_32bit_word(cpu, buf+12, 0);
			} else if (strcmp(tmpstr, "reg") == 0) {
				/*  TODO  */
				store_32bit_word(cpu, base + ofs, 33*8);
				store_32bit_word(cpu, buf, 0);
				store_32bit_word(cpu, buf+4,
				    cpu->machine->physical_ram_in_mb * 1048576);
				store_32bit_word(cpu, buf+8, 0);
				store_32bit_word(cpu, buf+12, 0);
			} else {
				fatal("[ of: getprop(%i,\"%s\"): not yet"
				    " implemented ]\n", (int)handle, arg[1]);
				cpu->cd.ppc.gpr[3] = -1;
			}
			break;
		case HANDLE_CHOSEN:
			if (strcmp(tmpstr, "stdin") == 0) {
				if (buflen >= 4)
					store_32bit_word(cpu, buf,
					    HANDLE_STDIN);
				store_32bit_word(cpu, base + ofs, 4);
			} else if (strcmp(tmpstr, "stdout") == 0) {
				if (buflen >= 4)
					store_32bit_word(cpu, buf,
					    HANDLE_STDOUT);
				store_32bit_word(cpu, base + ofs, 4);
			} else if (strcmp(tmpstr, "mmu") == 0) {
				if (buflen >= 4)
					store_32bit_word(cpu, buf,
					    HANDLE_MMU);
				store_32bit_word(cpu, base + ofs, 4);
			} else {
				fatal("[ of: getprop(%i,\"%s\"): not yet"
				    " implemented ]\n", (int)handle, arg[1]);
				cpu->cd.ppc.gpr[3] = -1;
			}
			break;
		default:
			fatal("[ of: getprop(%i,\"%s\"): not yet"
			    " implemented ]\n", (int)handle, arg[1]);
			cpu->cd.ppc.gpr[3] = -1;
		}
	} else if (strcmp(service, "instance-to-package") == 0) {
		/*  TODO: a package handle  */
		store_32bit_word(cpu, base + ofs, 1000);
	} else {
		quiet_mode = 0;
		cpu_register_dump(cpu->machine, cpu, 1, 0);
		printf("\n");
		fatal("[ of_emul(): unimplemented service \"%s\" ]\n", service);
		cpu->running = 0;
		cpu->dead = 1;
	}

	return 1;
}

