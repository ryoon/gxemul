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
 *  $Id: dec_prom.c,v 1.2 2003-11-06 13:56:08 debug Exp $
 *
 *  DECstation PROM emulation.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

#include "misc.h"

#include "dec_5100.h"
#include "dec_kn01.h"
#include "dec_kn02.h"
#include "dec_kn03.h"


extern int machine;
extern int register_dump;
extern int instruction_trace;
extern int show_nr_of_instructions;
extern int quiet_mode;
extern int use_x11;


/*
 *  read_char_from_memory():
 */
unsigned char read_char_from_memory(struct cpu *cpu, int regbase, int offset)
{
	unsigned char ch;
	memory_rw(cpu, cpu->mem, cpu->gpr[regbase] + offset, &ch, sizeof(ch), MEM_READ, CACHE_NONE | NO_EXCEPTIONS);
	return ch;
}


/*
 *  decstation_prom_emul():
 *
 *  DECstation PROM emulation.
 *
 *	0x24	getchar()
 *	0x30	printf()
 *	0x54	bootinit()
 *	0x58	bootread()
 *	0x64	getenv()
 *	0x6c	slot_address()
 *	0x80	getsysid()
 *	0x84	getbitmap()
 *	0xac	rex()
 */
void decstation_prom_emul(struct cpu *cpu)
{
	int i, j, ch, argreg;
	int vector = cpu->pc & 0xffff;
	unsigned char buf[100];
	unsigned char ch2;
	uint64_t slot_base = 0x10000000, slot_size = 0;

	switch (vector) {
	case 0x24:		/*  getchar()  */
		/*  debug("[ DEC PROM getchar() ]\n");  */
		cpu->gpr[GPR_V0] = console_readchar();
		break;
	case 0x30:		/*  printf()  */
		if (register_dump || instruction_trace)
			debug("PROM printf(0x%08lx): \n", (long)cpu->gpr[GPR_A0]);

		i = 0; ch = -1; argreg = GPR_A1;
		while (ch != '\0') {
			ch = read_char_from_memory(cpu, GPR_A0, i++);
			switch (ch) {
			case '%':
				ch = read_char_from_memory(cpu, GPR_A0, i++);
				switch (ch) {
				case '%':
					printf("%%");
					break;
				case 'c':
				case 's':
					/*  Get argument:  */
					if (argreg > GPR_A3) {
						printf("[ decstation_prom_emul(): too many arguments ]");
						argreg = GPR_A3;	/*  This reuses the last argument,
								which is utterly incorrect. (TODO)  */
					}
					ch2 = cpu->gpr[argreg];
					if (ch == 'c')
						printf("%c", ch2);
					else {
						/*  Print a "%s" string.  */
						while (ch2) {
							ch2 = read_char_from_memory(cpu, argreg, i++);
							if (ch2)
								printf("%c", ch2);
						}
						/*  TODO:  without this newline, output looks ugly  */
						printf("\n");
					}
					argreg ++;
					break;
				default:
					printf("[ unknown printf format char '%c' ]", ch);
				}
				break;
			case '\0':
				break;
			default:
				printf("%c", ch);
			}
		}
		if (register_dump || instruction_trace)
			debug("\n");
		fflush(stdout);
		break;
	case 0x54:		/*  bootinit()  */
		debug("[ DEC PROM bootinit(0x%08x): TODO ]\n", (int)cpu->gpr[GPR_A0]);
		cpu->gpr[GPR_V0] = 0;
		break;
	case 0x58:		/*  bootread(int b, void *buffer, int n)  */
		debug("[ DEC PROM bootread(0x%x, 0x%08x, 0x%x) ]\n",
		    (int)cpu->gpr[GPR_A0], (int)cpu->gpr[GPR_A1], (int)cpu->gpr[GPR_A2]);
		cpu->gpr[GPR_V0] = 0;
		break;
	case 0x64:		/*  getenv()  */
		/*  Find the environment variable given by a0:  */
		for (i=0; i<sizeof(buf); i++)
			memory_rw(cpu, cpu->mem, cpu->gpr[GPR_A0] + i, &buf[i], sizeof(char), MEM_READ, CACHE_NONE | NO_EXCEPTIONS);
		buf[sizeof(buf)-1] = '\0';
		debug("[ DEC PROM getenv(\"%s\") ]\n", buf);
		for (i=0; i<0x1000; i++) {
			/*  Matching string at offset i?  */
			int nmatches = 0;
			for (j=0; j<strlen(buf); j++) {
				memory_rw(cpu, cpu->mem, (uint64_t)(DEC_PROM_STRINGS + i + j), &ch2, sizeof(char), MEM_READ, CACHE_NONE | NO_EXCEPTIONS);
				if (ch2 == buf[j])
					nmatches++;
			}
			memory_rw(cpu, cpu->mem, (uint64_t)(DEC_PROM_STRINGS + i + strlen(buf)), &ch2, sizeof(char), MEM_READ, CACHE_NONE | NO_EXCEPTIONS);
			if (nmatches == strlen(buf) && ch2 == '=') {
				cpu->gpr[GPR_V0] = DEC_PROM_STRINGS + i + strlen(buf) + 1;
				return;
			}
		}
		/*  Return NULL if string wasn't found.  */
		cpu->gpr[GPR_V0] = 0;
		break;
	case 0x6c:		/*  ulong slot_address(int sn)  */
		debug("[ DEC PROM slot_address(%i) ]\n", (int)cpu->gpr[GPR_A0]);
		/*  TODO:  This is too hardcoded.  */
		/*  TODO 2:  Should these be physical or virtual addresses?  */
		switch (machine) {
		case MACHINE_3MAX_5000:
			slot_base = KN02_PHYS_TC_0_START;	/*  0x1e000000  */
			slot_size = 4*1048576;		/*  4 MB  */
			break;
		case MACHINE_3MIN_5000:
			slot_base = 0x10000000;
			slot_size = 0x4000000;		/*  64 MB  */
			break;
		case MACHINE_3MAXPLUS_5000:
			slot_base = 0x1e000000;
			slot_size = 0x800000;		/*  8 MB  */
			break;
		case MACHINE_MAXINE_5000:
			slot_base = 0x10000000;
			slot_size = 0x4000000;		/*  64 MB  */
			break;
		default:
			fatal("warning: DEC PROM slot_address() unimplemented for this machine type\n");
		}
		cpu->gpr[GPR_V0] = 0x80000000 + slot_base + slot_size * cpu->gpr[GPR_A0];
		break;
	case 0x80:		/*  getsysid()  */
		debug("[ DEC PROM getsysid() ]\n");
		/*  TODO:  why did I add the 0x82 stuff???  */
		cpu->gpr[GPR_V0] = (0x82 << 24) + (machine << 16) + (0x3 << 8);
		break;
	case 0x84:		/*  getbitmap()  */
		debug("[ DEC PROM getbitmap(0x%08x) ]\n", (int)cpu->gpr[GPR_A0]);
		{
			unsigned char buf[sizeof(memmap)];
			memory_rw(cpu, cpu->mem, DEC_MEMMAP_ADDR, buf, sizeof(buf), MEM_READ, CACHE_NONE | NO_EXCEPTIONS);
			memory_rw(cpu, cpu->mem, cpu->gpr[GPR_A0], buf, sizeof(buf), MEM_WRITE, CACHE_NONE | NO_EXCEPTIONS);
		}
		cpu->gpr[GPR_V0] = sizeof((memmap.bitmap));
		break;
	case 0xac:		/*  rex()  */
		debug("[ DEC PROM rex('%c') ]\n", (int)cpu->gpr[GPR_A0]);
		switch (cpu->gpr[GPR_A0]) {
		case 'h':
			debug("DEC PROM: rex('h') ==> halt\n");
			cpu->running = 0;
			break;
		case 'b':
			fatal("DEC PROM: rex('b') ==> reboot: TODO (halting CPU instead)\n");
			cpu->running = 0;
			break;
		default:
			fatal("DEC prom emulation: unknown rex() a0=0x%llx ('%c')\n",
			    (long long)cpu->gpr[GPR_A0], (char)cpu->gpr[GPR_A0]);
			exit(1);
		}
		break;
	default:
		cpu_register_dump(cpu);
		printf("a0 points to: ");
		for (i=0; i<40; i++) {
			char ch = '\0';
			memory_rw(cpu, cpu->mem, cpu->gpr[GPR_A0] + i, &ch, sizeof(ch), MEM_READ, CACHE_NONE | NO_EXCEPTIONS);
			if (ch >= ' ' && ch < 126)
				printf("%c", ch);
			else
				printf("[%02x]", ch);
		}
		printf("\n");
		fatal("PROM emulation: unimplemented callback vector 0x%x\n", vector);
		exit(1);
	}
}

