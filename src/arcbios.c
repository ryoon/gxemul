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
 *  $Id: arcbios.c,v 1.2 2003-11-06 13:56:07 debug Exp $
 *
 *  ARCBIOS emulation.
 *
 *  TODO:  ARCBIOS emulation should be made more generic, so that it can
 *         be used with both SGI machines and other types of machines.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

#include "misc.h"


extern int machine;
extern int register_dump;
extern int instruction_trace;
extern int show_nr_of_instructions;
extern int quiet_mode;
extern int use_x11;


/*
 *  read_char_from_memory():
 */
static unsigned char read_char_from_memory(struct cpu *cpu, int regbase, int offset)
{
	unsigned char ch;
	memory_rw(cpu, cpu->mem, cpu->gpr[regbase] + offset, &ch, sizeof(ch), MEM_READ, CACHE_NONE | NO_EXCEPTIONS);
	return ch;
}


/*
 *  dump_mem_string():
 */
void dump_mem_string(struct cpu *cpu, uint64_t addr)
{
	int i, ch;

	for (i=0; i<40; i++) {
		char ch = '\0';
		memory_rw(cpu, cpu->mem, addr + i, &ch, sizeof(ch), MEM_READ, CACHE_NONE | NO_EXCEPTIONS);
		if (ch == '\0')
			return;
		if (ch >= ' ' && ch < 126)
			debug("%c", ch);
		else
			debug("[%02x]", ch);
	}
}


/*
 *  arcbios_emul():  ARCBIOS emulation
 *
 *	0x28	GetChild(node)
 *	0x44	GetSystemId()
 *	0x6c	Write(handle, buf, len, &returnlen)
 *	0x78	GetEnvironmentVariable(char *)
 *	0x88	FlushAllCaches()
 */
void arcbios_emul(struct cpu *cpu)
{
	int vector = cpu->pc & 0xffff;
	int i, j;
	unsigned char ch2;
	char buf[40];

	switch (vector) {
	case 0x28:		/*  GetChild(node)  */
		debug("[ ARCBIOS GetChild(node): TODO ]\n");
		cpu->gpr[GPR_V0] = 0;
		break;
	case 0x44:		/*  GetSystemId()  */
		debug("[ ARCBIOS GetSystemId() ]\n");
		cpu->gpr[GPR_V0] = SGI_SYSID_ADDR;
		break;
	case 0x6c:		/*  Write(handle, buf, len, &returnlen)  */
		if (cpu->gpr[GPR_A0] != 1)	/*  1 = stdout?  */
			debug("[ ARCBIOS Write(%i,0x%08llx,%i,0x%08llx) ]\n",
			    (int)cpu->gpr[GPR_A0], (long long)cpu->gpr[GPR_A1],
			    (int)cpu->gpr[GPR_A2], (long long)cpu->gpr[GPR_A3]);
		for (i=0; i<cpu->gpr[GPR_A2]; i++) {
			unsigned char ch = '\0';
			memory_rw(cpu, cpu->mem, cpu->gpr[GPR_A1] + i, &ch, sizeof(ch), MEM_READ, CACHE_NONE | NO_EXCEPTIONS);
			console_putchar(ch);
		}
		/*  TODO: store len in returnlen  */
		break;
	case 0x78:		/*  GetEnvironmentVariable(char *)  */
		/*  Find the environment variable given by a0:  */
		for (i=0; i<sizeof(buf); i++)
			memory_rw(cpu, cpu->mem, cpu->gpr[GPR_A0] + i, &buf[i], sizeof(char), MEM_READ, CACHE_NONE | NO_EXCEPTIONS);
		buf[sizeof(buf)-1] = '\0';
		debug("[ ARCBIOS GetEnvironmentVariable(\"%s\") ]\n", buf);
		for (i=0; i<0x1000; i++) {
			/*  Matching string at offset i?  */
			int nmatches = 0;
			for (j=0; j<strlen(buf); j++) {
				memory_rw(cpu, cpu->mem, (uint64_t)(SGI_ENV_STRINGS + i + j), &ch2, sizeof(char), MEM_READ, CACHE_NONE | NO_EXCEPTIONS);
				if (ch2 == buf[j])
					nmatches++;
			}
			memory_rw(cpu, cpu->mem, (uint64_t)(SGI_ENV_STRINGS + i + strlen(buf)), &ch2, sizeof(char), MEM_READ, CACHE_NONE | NO_EXCEPTIONS);
			if (nmatches == strlen(buf) && ch2 == '=') {
				cpu->gpr[GPR_V0] = SGI_ENV_STRINGS + i + strlen(buf) + 1;
				return;
			}
		}
		/*  Return NULL if string wasn't found.  */
		cpu->gpr[GPR_V0] = 0;
		break;
	case 0x88:		/*  FlushAllCaches()  */
		debug("[ ARCBIOS FlushAllCaches(): TODO ]\n");
		cpu->gpr[GPR_V0] = 0;
		break;
	default:
		cpu_register_dump(cpu);
		debug("a0 points to: ");
		dump_mem_string(cpu, cpu->gpr[GPR_A0]);
		debug("\n");
		fatal("ARCBIOS: unimplemented vector 0x%x\n", vector);
		exit(1);
	}
}

