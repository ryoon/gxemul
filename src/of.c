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
 *  $Id: of.c,v 1.1 2005-03-08 22:59:00 debug Exp $
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
	int i, nargs, nret, ofs;
	char service[50];
	char arg[N_MAX_ARGS][ARG_MAX_LEN];
	uint64_t base, ptr;

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

	debug("[ of: %s ]\n", service);
	ofs = 12;
	for (i=0; i<nargs; i++) {
		if (i >= N_MAX_ARGS) {
			fatal("[ of: too many args! ]\n");
			continue;
		}
		ptr = load_32bit_word(cpu, base + ofs);
		readstr(cpu, ptr, arg[i], ARG_MAX_LEN);
		if (arg[i][0])
			debug("[ of: arg[%i] = \"%s\" ]\n", i, arg[i]);
		else
			debug("[ of: arg[%i] = 0x%08x ]\n", i, (uint32_t)ptr);
		ofs += sizeof(uint32_t);
	}
	for (i=0; i<nret; i++) {
		ptr = load_32bit_word(cpu, base + ofs);
		debug("[ of: ret[%i] = 0x%08x ]\n", i, (uint32_t)ptr);
		ofs += sizeof(uint32_t);
	}

	/*  Return value:  */
	cpu->cd.ppc.gpr[3] = 0;

	if (strcmp(service, "exit") == 0) {
		cpu->running = 0;
	} else if (strcmp(service, "finddevice") == 0) {
		if (strcmp(arg[0], "/memory") == 0) {
			/*  TODO  */
		} else {
			/*  Device not found.  */
			fatal("[ of: finddevice(\"%s\"): not yet"
			    " implemented ]\n", arg[0]);
			cpu->cd.ppc.gpr[3] = 2;		/*  Hm. TODO  */
		}
	} else if (strcmp(service, "getprop") == 0) {
		fatal("[ of: getprop(\"%s\"): not yet"
		    " implemented ]\n", arg[1]);
		cpu->cd.ppc.gpr[3] = 2;		/*  Hm. TODO  */
	} else {
		quiet_mode = 0;
		cpu_register_dump(cpu->machine, cpu, 1, 0x1);
		printf("\n");
		fatal("of_emul(): unimplemented service \"%s\"\n", service);
		cpu->running = 0;
		cpu->dead = 1;
	}

	return 1;
}

