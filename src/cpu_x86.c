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
 *  $Id: cpu_x86.c,v 1.9 2005-04-15 21:39:59 debug Exp $
 *
 *  x86 (and potentially amd64) CPU emulation.
 *
 *  TODO:
 *
 *	x)  Some more instructions should set flags.
 *
 *	x)  The entire file should be refactored!
 *
 *	x)  Better 16-bit support (for booting from disks, MS-DOS etc)
 *		x)  BIOS emulation
 *
 *	x)  AMD64 stuff.
 *
 *	x)  Many many other things. :-)
 *
 *  See http://library.n0i.net/hardware/intel80386-programmer-manual/
 *  for more into about x86.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "misc.h"


#ifndef	ENABLE_X86


#include "cpu_x86.h"


/*
 *  x86_cpu_family_init():
 *
 *  Bogus, when ENABLE_X86 isn't defined.
 */
int x86_cpu_family_init(struct cpu_family *fp)
{
	return 0;
}


#else	/*  ENABLE_X86  */


#include "cpu.h"
#include "cpu_x86.h"
#include "machine.h"
#include "memory.h"
#include "symbol.h"


extern volatile int single_step;
extern int old_show_trace_tree;   
extern int old_instruction_trace;
extern int old_quiet_mode;
extern int quiet_mode;


/*
 *  x86_cpu_new():
 *
 *  Create a new x86 cpu object.
 */
struct cpu *x86_cpu_new(struct memory *mem, struct machine *machine,
	int cpu_id, char *cpu_type_name)
{
	struct cpu *cpu;

	if (cpu_type_name == NULL || (strcasecmp(cpu_type_name, "amd64") != 0
	    && strcasecmp(cpu_type_name, "386") != 0))
		return NULL;

	cpu = malloc(sizeof(struct cpu));
	if (cpu == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}

	memset(cpu, 0, sizeof(struct cpu));
	cpu->memory_rw          = x86_memory_rw;
	cpu->name               = cpu_type_name;
	cpu->mem                = mem;
	cpu->machine            = machine;
	cpu->cpu_id             = cpu_id;
	cpu->byte_order         = EMUL_LITTLE_ENDIAN;
	cpu->bootstrap_cpu_flag = 0;
	cpu->running            = 0;

	cpu->cd.x86.mode = 32;
	cpu->cd.x86.bits = 32;

	if (strcasecmp(cpu_type_name, "amd64") == 0) {
		/*  TODO: boot in 64-bit mode?  */
		cpu->cd.x86.bits = 64;
	}

	cpu->cd.x86.esp = 0xff0;

	/*  Only show name and caches etc for CPU nr 0 (in SMP machines):  */
	if (cpu_id == 0) {
		debug("%s", cpu->name);
	}

	return cpu;
}


/*
 *  x86_cpu_dumpinfo():
 */
void x86_cpu_dumpinfo(struct cpu *cpu)
{
	debug(" (%i bits)", cpu->cd.x86.bits);
	debug(", %i-bit mode", cpu->cd.x86.mode);
	debug("\n");
}


/*
 *  x86_cpu_list_available_types():
 *
 *  Print a list of available x86 CPU types.
 */
void x86_cpu_list_available_types(void)
{
	/*  TODO  */

	debug("amd64     386\n");
}


/*
 *  x86_cpu_register_dump():
 *
 *  Dump cpu registers in a relatively readable format.
 *  (gprs and coprocs are mostly useful for the MIPS version of this function.)
 */
void x86_cpu_register_dump(struct cpu *cpu, int gprs, int coprocs)
{
	char *symbol;
	uint64_t offset;
	int x = cpu->cpu_id;

	if (cpu->cd.x86.mode == 16) {
		debug("cpu%i:  cs:ip = 0x%04x:0x%04x\n", x,
		    cpu->cd.x86.cs, (int)cpu->pc);

		debug("cpu%i:  ax = 0x%04x  bx = 0x%04x  cx = 0x%04x  dx = "
		    "0x%04x\n", x, (int)cpu->cd.x86.eax, (int)cpu->cd.x86.ebx,
		    (int)cpu->cd.x86.ecx, (int)cpu->cd.x86.edx);
		debug("cpu%i:  si = 0x%04x  di = 0x%04x  bp = 0x%04x  sp = "
		    "0x%04x\n", x, (int)cpu->cd.x86.esi, (int)cpu->cd.x86.edi,
		    (int)cpu->cd.x86.ebp, (int)cpu->cd.x86.esp);

		debug("cpu%i:  ds = 0x%04x  es = 0x%04x  ss = 0x%04x  flags "
		    "= 0x%04x\n", x, cpu->cd.x86.ds, cpu->cd.x86.es,
		    cpu->cd.x86.ss, (int)cpu->cd.x86.eflags);
	} else if (cpu->cd.x86.mode == 32) {
		symbol = get_symbol_name(&cpu->machine->symbol_context,
		    cpu->pc, &offset);

		debug("cpu%i:  eip=0x", x);
	        debug("%08x", (int)cpu->pc);
		debug("  <%s>\n", symbol != NULL? symbol : " no symbol ");

		debug("cpu%i:  eax=0x%08x  ebx=0x%08x  ecx=0x%08x  edx="
		    "0x%08x\n", x, (int)cpu->cd.x86.eax, (int)cpu->cd.x86.ebx,
		    (int)cpu->cd.x86.ecx, (int)cpu->cd.x86.edx);
		debug("cpu%i:  esi=0x%08x  edi=0x%08x  ebp=0x%08x  esp="
		    "0x%08x\n", x, (int)cpu->cd.x86.esi, (int)cpu->cd.x86.edi,
		    (int)cpu->cd.x86.ebp, (int)cpu->cd.x86.esp);

		debug("cpu%i:  cs=0x%04x ds=0x%04x es=0x%04x fs=0x%04x "
		    "gx=0x%04x ss=0x%04x\n", x, cpu->cd.x86.cs, cpu->cd.x86.ds,
		    cpu->cd.x86.es, cpu->cd.x86.fs, cpu->cd.x86.gs,
		    cpu->cd.x86.ss);

		debug("cpu%i:  cr3=0x%08x  eflags=0x%08x\n", x,
		    (int)cpu->cd.x86.cr3, (int)cpu->cd.x86.eflags);
	} else {
		/*  64-bit  */
		debug("x86_cpu_register_dump(): 64-bit: TODO\n");
	}
}


/*
 *  x86_cpu_register_match():
 */
void x86_cpu_register_match(struct machine *m, char *name,
	int writeflag, uint64_t *valuep, int *match_register)
{
	int cpunr = 0;

	/*  CPU number:  */

	/*  TODO  */

	/*  Register name:  */
	if (strcasecmp(name, "pc") == 0 || strcasecmp(name, "ip") == 0
	    || strcasecmp(name, "eip") == 0) {
		if (writeflag) {
			m->cpus[cpunr]->pc = *valuep;
		} else
			*valuep = m->cpus[cpunr]->pc;
		*match_register = 1;
	}

#define regmatch(str,field) if (strcasecmp(name, str) == 0) {		\
		if (writeflag) {					\
			m->cpus[cpunr]->cd.x86.field = *valuep;		\
		} else							\
			*valuep = m->cpus[cpunr]->cd.x86.field;		\
		*match_register = 1;					\
	}

#define regmatch16(str,field) if (strcasecmp(name, str) == 0) {		\
		if (writeflag) {					\
			m->cpus[cpunr]->cd.x86.field =			\
			    (m->cpus[cpunr]->cd.x86.field & ~0xffff)	\
			    | *valuep;					\
		} else							\
			*valuep = m->cpus[cpunr]->cd.x86.field & 0xffff;\
		*match_register = 1;					\
	}

#define regmatch8(str,field) if (strcasecmp(name, str) == 0) {		\
		if (writeflag) {					\
			m->cpus[cpunr]->cd.x86.field =			\
			    (m->cpus[cpunr]->cd.x86.field & ~0xff)	\
			    | *valuep;					\
		} else							\
			*valuep = m->cpus[cpunr]->cd.x86.field & 0xff;	\
		*match_register = 1;					\
	}

#define regmatchhi8(str,field) if (strcasecmp(name, str) == 0) {	\
		if (writeflag) {					\
			m->cpus[cpunr]->cd.x86.field =			\
			    (m->cpus[cpunr]->cd.x86.field & ~0xff00)	\
			    | (*valuep << 8);				\
		} else							\
			*valuep = (m->cpus[cpunr]->cd.x86.field >> 8)	\
			    & 0xff;					\
		*match_register = 1;					\
	}

	regmatch("eax", eax);
	regmatch("ebx", ebx);
	regmatch("ecx", ecx);
	regmatch("edx", edx);
	regmatch("esi", esi);
	regmatch("edi", edi);
	regmatch("ebp", ebp);
	regmatch("esp", esp);

	regmatch16("ax", eax);
	regmatch16("bx", ebx);
	regmatch16("cx", ecx);
	regmatch16("dx", edx);
	regmatch16("si", esi);
	regmatch16("di", edi);
	regmatch16("bp", ebp);
	regmatch16("sp", esp);

	regmatch8("al", eax);
	regmatch8("bl", ebx);
	regmatch8("cl", ecx);
	regmatch8("dl", edx);

	regmatchhi8("ah", eax);
	regmatchhi8("bh", ebx);
	regmatchhi8("ch", ecx);
	regmatchhi8("dh", edx);

	regmatch("cs", cs);
	regmatch("ds", ds);
	regmatch("es", es);
	regmatch("fs", fs);
	regmatch("gs", gs);
	regmatch("ss", ss);

	regmatch("eflags", eflags);
	regmatch16("flags", eflags);
	regmatch("cr3", cr3);
}


#define	HEXPRINT(x,n) { int j; for (j=0; j<(n); j++) debug("%02x",(x)[j]); }
#define	HEXSPACES(i) { int j; for (j=0; j<10-(i);j++) debug("  "); debug(" "); }


/*
 *  x86_cpu_disassemble_instr():
 *
 *  Convert an instruction word into human readable format, for instruction
 *  tracing.
 *
 *  If running is 1, cpu->pc should be the address of the instruction.
 *
 *  If running is 0, things that depend on the runtime environment (eg.
 *  register contents) will not be shown, and addr will be used instead of
 *  cpu->pc for relative addresses.
 */
int x86_cpu_disassemble_instr(struct cpu *cpu, unsigned char *instr,
	int running, uint64_t dumpaddr, int bintrans)
{
	int instr_len = 0, op, rep = 0;
	uint64_t offset;
	uint32_t imm=0, imm2, mode = cpu->cd.x86.mode;
	char *symbol, *tmp = "ERROR", *mnem = "ERROR";

	if (running)
		dumpaddr = cpu->pc;

	symbol = get_symbol_name(&cpu->machine->symbol_context,
	    dumpaddr, &offset);
	if (symbol != NULL && offset==0)
		debug("<%s>\n", symbol);

	if (cpu->machine->ncpus > 1 && running)
		debug("cpu%i: ", cpu->cpu_id);

	if (mode == 32)
		debug("%08x:  ", (int)dumpaddr);
	else if (mode == 64)
		debug("%016llx:  ", (long long)dumpaddr);
	else { /*  16-bit mode  */
		if (running)
			debug("%04x:%04x  ", cpu->cd.x86.cs, (int)dumpaddr);
		else
			debug("%08x:  ", (int)dumpaddr);
	}

	if (bintrans && !running) {
		debug("(bintrans)");
		goto disasm_ret;
	}

	/*
	 *  Decode the instruction:
	 */

	/*  All instructions are at least 1 byte long:  */
	HEXPRINT(instr,1);
	instr_len=1;

	/*  Any prefix?  */
	while (instr[0] == 0x66 || instr[0] == 0xf3) {
		switch (instr[0]) {
		case 0x66:
			if (mode == 32)
				mode = 16;
			else
				mode = 32;
			break;
		case 0xf3:
			rep = 1;
			break;
		}
		/*  TODO: lock, segment overrides etc  */
		instr ++;
		instr_len ++;
		HEXPRINT(instr,1);
	}

	switch ((op = instr[0])) {

	case 0x0f:
		instr++;
		instr_len++;
		HEXPRINT(instr,1);
		switch (instr[0]) {
		case 0x84:
		case 0x85:
			op = instr[0];
			instr ++;
			imm = instr[0] + (instr[1] << 8) + (instr[2] << 16)
			    + (instr[3] << 24);
			imm = dumpaddr + 6 + imm;
			HEXPRINT(instr,4);
			instr_len += 4;
			HEXSPACES(instr_len);
			switch (op) {
			case 0x84: mnem = "je"; break;
			case 0x85: mnem = "jne"; break;
			}
			debug("%s\t$0x%x", mnem, (uint32_t)imm);
			/*  TODO: symbol  */
			break;
		default:
			HEXSPACES(instr_len);
			debug("UNIMPLEMENTED");
		}
		break;

	case 0x25:
		instr ++;
		if (mode == 32) {
			imm = instr[0] + (instr[1] << 8) + (instr[2] << 16)
			    + (instr[3] << 24);
			HEXPRINT(instr,4);
			instr_len += 4;
		} else {
			imm = instr[0] + (instr[1] << 8);
			HEXPRINT(instr,2);
			instr_len += 2;
		}
		HEXSPACES(instr_len);
		switch (op) {
		case 0x25: tmp = "eax"; break;
		}
		debug("and\t$0x%x,%%%s", (uint32_t)imm, tmp);
		break;

	case 0x31:
		instr++;
		instr_len++;
		HEXPRINT(instr,1);
		switch (instr[0]) {
		case 0xc0:
			HEXSPACES(instr_len);
			if (mode == 32)
				debug("xor\t%%eax,%%eax");
			else if (mode == 16)
				debug("xor\t%%ax,%%ax");
			else
				debug("31/c0 TODO");
			break;
		default:
			HEXSPACES(instr_len);
			debug("UNIMPLEMENTED");
		}
		break;

	case 0x33:
		instr++;
		instr_len++;
		HEXPRINT(instr,1);
		switch (instr[0]) {
		case 0xc0:
			HEXSPACES(instr_len);
			if (mode == 32)
				debug("xor\t%%eax,%%eax");
			else if (mode == 16)
				debug("xor\t%%ax,%%ax");
			else
				debug("31/c0 TODO");
			break;
		default:
			HEXSPACES(instr_len);
			debug("UNIMPLEMENTED");
		}
		break;

	case 0x39:
		instr++;
		instr_len++;
		HEXPRINT(instr,1);
		switch (instr[0]) {
		case 0xd0:
			HEXSPACES(instr_len);
			debug("cmp\t%%edx,%%eax");
			break;
		default:
			HEXSPACES(instr_len);
			debug("UNIMPLEMENTED");
		}
		break;

	case 0x68:
		instr ++;
		imm = instr[0] + (instr[1] << 8) + (instr[2] << 16)
		    + (instr[3] << 24);
		HEXPRINT(instr,4);
		instr_len += 4;
		HEXSPACES(instr_len);
		debug("push\t$0x%x", (uint32_t)imm);
		break;

	case 0x70:
	case 0x71:
	case 0x72:
	case 0x73:
	case 0x74:
	case 0x75:
	case 0x76:
	case 0x77:
		instr ++;
		imm = (signed char) instr[0];
		HEXPRINT(instr,1);
		instr_len += 1;
		HEXSPACES(instr_len);
		switch (op) {
		case 0x70: mnem = "jo"; break;
		case 0x71: mnem = "jno"; break;
		case 0x72: mnem = "jb"; break;
		case 0x73: mnem = "jae"; break;
		case 0x74: mnem = "je"; break;
		case 0x75: mnem = "jne"; break;
		case 0x76: mnem = "jna"; break;
		case 0x77: mnem = "ja"; break;
		}
		debug("%s\t$0x%x", mnem, (uint32_t)(imm + dumpaddr + 2));
		/*  TODO: symbol  */
		break;

	case 0x80:
		instr++;
		instr_len++;
		HEXPRINT(instr,1);
		switch (instr[0]) {
		case 0x39:
			/*  80 39 yy   cmpb   $yy,(%ecx)  */
			instr ++;
			imm = (unsigned char)instr[0];
			HEXPRINT(instr,1);
			instr_len += 1;
			HEXSPACES(instr_len);
			debug("cmpb\t$%i,(%%ecx)", (int)imm);
			break;
		case 0x3d:
			/*  80 3d xx xx xx xx yy   cmpb   $yy,xx  */
			/*  Get the address:  */
			instr ++;
			imm2 = instr[0] + (instr[1] << 8) + (instr[2] << 16)
			    + (instr[3] << 24);
			HEXPRINT(instr,4);
			instr_len += 4;
			/*  and unsigned imm byte value:  */
			instr ++;
			imm = (unsigned char)instr[0];
			HEXPRINT(instr,1);
			instr_len += 1;
			HEXSPACES(instr_len);
			debug("cmpb\t$%i,0x%08x", (int)imm, (int)imm2);
			break;
		default:
			HEXSPACES(instr_len);
			debug("UNIMPLEMENTED");
		}
		break;

	case 0x81:
		instr++;
		instr_len++;
		HEXPRINT(instr,1);
		switch (instr[0]) {
		case 0x65:
			instr ++;
			imm = (signed char)instr[0];
			HEXPRINT(instr,1);
			instr_len += 1;
			instr ++;
			imm2 = instr[0] + (instr[1] << 8) + (instr[2] << 16)
			    + (instr[3] << 24);
			HEXPRINT(instr,4);
			instr_len += 4;
			HEXSPACES(instr_len);
			debug("andl\t$0x%08x,0x%x(%%ebp)", (int)imm2, (int)imm);
			break;
		case 0xc8:
		case 0xc9:
		case 0xca:
		case 0xcb:
		case 0xcc:
		case 0xcd:
		case 0xce:
		case 0xcf:
			op = instr[0];
			instr ++;
			imm = instr[0] + (instr[1] << 8) + (instr[2] << 16)
			    + (instr[3] << 24);
			HEXPRINT(instr,4);
			instr_len += 4;
			HEXSPACES(instr_len);
			switch (op) {
			case 0xc8: debug("or\t$0x%08x,%%eax", (int)imm); break;
			case 0xc9: debug("or\t$0x%08x,%%ecx", (int)imm); break;
			case 0xca: debug("or\t$0x%08x,%%edx", (int)imm); break;
			case 0xcb: debug("or\t$0x%08x,%%ebx", (int)imm); break;
			case 0xcc: debug("or\t$0x%08x,%%esp", (int)imm); break;
			case 0xcd: debug("or\t$0x%08x,%%ebp", (int)imm); break;
			case 0xce: debug("or\t$0x%08x,%%esi", (int)imm); break;
			case 0xcf: debug("or\t$0x%08x,%%edi", (int)imm); break;
			}
			break;
		case 0xfb:
		case 0xfe:
			op = instr[0];
			instr ++;
			imm = instr[0] + (instr[1] << 8) + (instr[2] << 16)
			    + (instr[3] << 24);
			HEXPRINT(instr,4);
			instr_len += 4;
			HEXSPACES(instr_len);
			switch (op) {
			case 0xfb:
				debug("cmp\t$0x%08x,%%ebx", (int)imm);
				break;
			case 0xfe:
				debug("cmp\t$0x%08x,%%esi", (int)imm);
				break;
			}
			break;
		default:
			HEXSPACES(instr_len);
			debug("UNIMPLEMENTED");
		}
		break;

	case 0x83:
		instr++;
		instr_len++;
		HEXPRINT(instr,1);
		switch (instr[0]) {
		case 0x3c:
			instr ++;
			instr_len++;
			HEXPRINT(instr,1);
			switch (instr[0]) {
			case 0x81:
				/*  cmpl $imm,(%ecx,%eax,4)  */
				instr ++;
				imm = (signed char)instr[0];
				HEXPRINT(instr,1);
				instr_len += 1;
				HEXSPACES(instr_len);
				debug("cmpl\t$0x%08x,(%%ecx,%%eax,4)", imm);
				break;
			default:
				HEXSPACES(instr_len);
				debug("UNIMPLEMENTED");
			}
			break;
		case 0xc4:
			instr ++;
			imm = (signed char)instr[0];
			HEXPRINT(instr,1);
			instr_len += 1;
			HEXSPACES(instr_len);
			debug("add\t$%i,%%esp", imm);
			break;
		case 0xec:
			instr ++;
			imm = (signed char)instr[0];
			HEXPRINT(instr,1);
			instr_len += 1;
			HEXSPACES(instr_len);
			debug("sub\t$%i,%%esp", imm);
			break;
		default:
			HEXSPACES(instr_len);
			debug("UNIMPLEMENTED");
		}
		break;

	case 0x84:
		instr++;
		instr_len++;
		HEXPRINT(instr,1);
		switch (instr[0]) {
		case 0xc0:
			HEXSPACES(instr_len);
			debug("test\t%%al,%%al");
			break;
		default:
			HEXSPACES(instr_len);
			debug("UNIMPLEMENTED");
		}
		break;

	case 0x85:
		instr++;
		instr_len++;
		HEXPRINT(instr,1);
		switch (instr[0]) {
		case 0xc0:
			HEXSPACES(instr_len);
			debug("test\t%%eax,%%eax");
			break;
		default:
			HEXSPACES(instr_len);
			debug("UNIMPLEMENTED");
		}
		break;

	case 0x88:
		instr++;
		instr_len++;
		HEXPRINT(instr,1);
		switch (instr[0]) {
		case 0x02:
			HEXSPACES(instr_len);
			debug("mov\t%%al,(%%edx)");
			break;
		default:
			HEXSPACES(instr_len);
			debug("UNIMPLEMENTED");
		}
		break;

	case 0x89:
		instr++;
		instr_len++;
		HEXPRINT(instr,1);
		switch (instr[0]) {
		case 0x02:
			HEXSPACES(instr_len);
			debug("mov\t%%eax,(%%edx)");
			break;
		case 0x75:
			instr ++;
			imm = (signed char)instr[0];
			HEXPRINT(instr,1);
			instr_len += 1;
			HEXSPACES(instr_len);
			debug("mov\t%%esi,0x%x(%%ebp)", imm);
			break;
		case 0xc4:
			HEXSPACES(instr_len);
			debug("mov\t%%eax,%%esp");
			break;
		case 0xe5:
			HEXSPACES(instr_len);
			debug("mov\t%%esp,%%ebp");
			break;
		case 0xf6:
			HEXSPACES(instr_len);
			debug("mov\t%%esi,%%esi");
			break;
		default:
			HEXSPACES(instr_len);
			debug("UNIMPLEMENTED");
		}
		break;

	case 0x8a:
		instr++;
		instr_len++;
		HEXPRINT(instr,1);
		switch (instr[0]) {
		case 0x01:
			HEXSPACES(instr_len);
			debug("mov\t(%%ecx),%%al");
			break;
		default:
			HEXSPACES(instr_len);
			debug("UNIMPLEMENTED");
		}
		break;

	case 0x8b:
		instr++;
		instr_len++;
		HEXPRINT(instr,1);
		switch (instr[0]) {
		case 0x4d:
			instr ++;
			imm = (signed char)instr[0];
			HEXPRINT(instr,1);
			instr_len += 1;
			HEXSPACES(instr_len);
			debug("mov\t0x%x(%%ebp),%%ecx", imm);
			break;
		case 0x55:
			instr ++;
			imm = (signed char)instr[0];
			HEXPRINT(instr,1);
			instr_len += 1;
			HEXSPACES(instr_len);
			debug("mov\t0x%x(%%ebp),%%edx", imm);
			break;
		default:
			HEXSPACES(instr_len);
			debug("UNIMPLEMENTED");
		}
		break;

	case 0x8d:
		instr++;
		instr_len++;
		HEXPRINT(instr,1);
		switch (instr[0]) {
		case 0x73:
			instr ++;
			imm = (signed char)instr[0];
			HEXPRINT(instr,1);
			instr_len += 1;
			HEXSPACES(instr_len);
			debug("lea\t0x$%x(%%ebx),%%esi", imm);
			break;
		default:
			HEXSPACES(instr_len);
			debug("UNIMPLEMENTED");
		}
		break;

	case 0x8e:
		instr++;
		instr_len++;
		HEXPRINT(instr,1);
		switch (instr[0]) {
		case 0xd0:
			HEXSPACES(instr_len);
			if (mode == 16)
				debug("mov\t%%ax,%%ss");
			else
				debug("mov\t%%eax,%%ss");
			break;
		default:
			HEXSPACES(instr_len);
			debug("UNIMPLEMENTED");
		}
		break;

	/*  A couple of single-byte opcodes:  */
	case 0x06:
	case 0x07:
	case 0x0e:
	case 0x16:
	case 0x17:
	case 0x1e:
	case 0x1f:
	case 0x40:
	case 0x41:
	case 0x42:
	case 0x43:
	case 0x44:
	case 0x45:
	case 0x46:
	case 0x47:
	case 0x53:
	case 0x55:
	case 0x56:
	case 0x57:
	case 0x90:
	case 0xc3:
	case 0xc9:
	case 0xee:
	case 0xf8:
	case 0xf9:
	case 0xfa:
	case 0xfb:
	case 0xfc:
	case 0xfd:
		HEXSPACES(instr_len);
		switch (op) {
		case 0x06:  debug("push\t%%es"); break;
		case 0x07:  debug("pop\t%%es"); break;
		case 0x0e:  debug("push\t%%cs"); break;
		case 0x16:  debug("push\t%%ss"); break;
		case 0x17:  debug("pop\t%%ss"); break;
		case 0x1e:  debug("push\t%%ds"); break;
		case 0x1f:  debug("pop\t%%ds"); break;
		case 0x40:  debug("inc\t%%eax"); break;
		case 0x41:  debug("inc\t%%ecx"); break;
		case 0x42:  debug("inc\t%%edx"); break;
		case 0x43:  debug("inc\t%%ebx"); break;
		case 0x44:  debug("inc\t%%esp"); break;
		case 0x45:  debug("inc\t%%ebp"); break;
		case 0x46:  debug("inc\t%%esi"); break;
		case 0x47:  debug("inc\t%%edi"); break;
		case 0x53:  debug("push\t%%ebx"); break;
		case 0x55:  debug("push\t%%ebp"); break;
		case 0x56:  debug("push\t%%esi"); break;
		case 0x57:  debug("push\t%%edi"); break;
		case 0x90:  debug("nop"); break;
		case 0xc3:  debug("ret"); break;
		case 0xc9:  debug("leave"); break;
		case 0xee:  debug("out\t%%al,(%%dx)"); break;
		case 0xf8:  debug("clc"); break;
		case 0xf9:  debug("stc"); break;
		case 0xfa:  debug("cli"); break;
		case 0xfb:  debug("sti"); break;
		case 0xfc:  debug("cld"); break;
		case 0xfd:  debug("std"); break;
		}
		break;

	case 0xa0:
		instr ++;
		imm = instr[0] + (instr[1] << 8) + (instr[2] << 16)
		    + (instr[3] << 24);
		HEXPRINT(instr,4);
		instr_len += 4;
		HEXSPACES(instr_len);
		switch (op) {
		case 0xa0: tmp = "al"; break;
		}
		debug("mov\t0x%x,%%%s", (uint32_t)imm, tmp);
		break;

	case 0xaa:
		HEXSPACES(instr_len);
		debug("stosb");
		break;

	case 0xb0:
		instr ++;
		imm = instr[0];
		HEXPRINT(instr,1);
		instr_len += 1;
		HEXSPACES(instr_len);
		debug("mov\t$0x%x,%%al", (uint32_t)imm);
		break;

	case 0xb8:
	case 0xb9:
	case 0xba:
	case 0xbb:
	case 0xbc:
	case 0xbd:
	case 0xbe:
	case 0xbf:
		instr ++;
		if (mode == 16) {
			imm = instr[0] + (instr[1] << 8);
			HEXPRINT(instr,2);
			instr_len += 2;
		} else {
			imm = instr[0] + (instr[1] << 8) + (instr[2] << 16)
			    + (instr[3] << 24);
			HEXPRINT(instr,4);
			instr_len += 4;
		}
		HEXSPACES(instr_len);
		switch (op) {
		case 0xb8: tmp = "eax"; break;
		case 0xb9: tmp = "ecx"; break;
		case 0xba: tmp = "edx"; break;
		case 0xbb: tmp = "ebx"; break;
		case 0xbc: tmp = "esp"; break;
		case 0xbd: tmp = "ebp"; break;
		case 0xbe: tmp = "esi"; break;
		case 0xbf: tmp = "edi"; break;
		}
		if (mode == 16)
			debug("mov\t$0x%x,%%%s", (uint32_t)imm, tmp + 1);
		else
			debug("mov\t$0x%x,%%%s", (uint32_t)imm, tmp);
		break;

	case 0xc1:
		instr++;
		instr_len++;
		HEXPRINT(instr,1);
		switch (instr[0]) {
		case 0xea:
			instr ++;
			imm = (unsigned char)instr[0];
			HEXPRINT(instr,1);
			instr_len += 1;
			HEXSPACES(instr_len);
			debug("shr\t$%i,%%edx", imm);
			break;
		default:
			HEXSPACES(instr_len);
			debug("UNIMPLEMENTED");
		}
		break;

	case 0xc6:
		instr++;
		instr_len++;
		HEXPRINT(instr,1);
		switch (instr[0]) {
		case 0x02:
			instr ++;
			imm = (unsigned char)instr[0];
			HEXPRINT(instr,1);
			instr_len += 1;
			HEXSPACES(instr_len);
			debug("movb\t$%i,(%%edx)", imm);
			break;
		default:
			HEXSPACES(instr_len);
			debug("UNIMPLEMENTED");
		}
		break;

	case 0xc7:
		instr++;
		instr_len++;
		HEXPRINT(instr,1);
		switch (instr[0]) {
		case 0x45:
			instr ++;
			imm = (signed char)instr[0];
			HEXPRINT(instr,1);
			instr_len += 1;
			instr ++;
			imm2 = instr[0] + (instr[1] << 8) + (instr[2] << 16)
			    + (instr[3] << 24);
			HEXPRINT(instr,4);
			instr_len += 4;
			HEXSPACES(instr_len);
			debug("movl\t$0x%08x,0x%x(%%ebp)", (int)imm2, (int)imm);
			break;
		default:
			HEXSPACES(instr_len);
			debug("UNIMPLEMENTED");
		}
		break;

	case 0xe8:	/*  call (16/32 bits)  */
	case 0xe9:	/*  jmp (16/32 bits)  */
	case 0xeb:	/*  jmp (8 bits)  */
		instr ++;
		switch (op) {
		case 0xe8:
		case 0xe9:
			if (mode == 32) {
				imm = instr[0] + (instr[1] << 8) +
				    (instr[2] << 16) + (instr[3] << 24);
				imm += dumpaddr + 5;
				HEXPRINT(instr,4);
				instr_len += 4;
			} else {
				imm = instr[0] + (instr[1] << 8);
				imm += dumpaddr + 3;
				HEXPRINT(instr,2);
				instr_len += 2;
				imm &= 0xffff;
			}
			break;
		case 0xeb:
			imm = (signed char) instr[0];
			imm += dumpaddr + 2;
			HEXPRINT(instr,1);
			instr_len += 1;
			break;
		}
		HEXSPACES(instr_len);
		switch (op) {
		case 0xe8: mnem = "call"; break;
		case 0xe9: mnem = "jmp"; break;
		case 0xeb: mnem = "jmp"; break;
		}
		debug("%s\t$0x%x", mnem, (uint32_t)imm);
		/*  TODO: symbol  */
		break;

	case 0xea:
		/*  ljmp xxxx:yyyy (16-bit mode)  */
		/*  ljmp xxxx:yyyyyyyy (32-bit mode)  */
		instr ++;
		if (mode == 16) {
			imm = instr[0] + (instr[1] << 8);	/*  ofs  */
			imm2 = instr[2] + (instr[3] << 8);	/*  seg  */
			HEXPRINT(instr,4);
			instr_len += 4;
			HEXSPACES(instr_len);
			debug("ljmp\t$0x%04x:0x%04x", imm2, imm);
		} else if (mode == 32) {
			imm = instr[0] + (instr[1] << 8) + (instr[2] << 16)
			    + (instr[3] << 24);
			imm2 = instr[4] + (instr[5] << 8);	/*  seg  */
			HEXPRINT(instr,6);
			instr_len += 6;
			HEXSPACES(instr_len);
			debug("ljmp\t$0x%04x:0x%08x", imm2, imm);
		} else {
			debug("TODO 64-bit ljmp?");
		}
		break;

	default:
		HEXSPACES(instr_len);
		debug("UNIMPLEMENTED");
	}

disasm_ret:
	if (rep)
		debug(" (rep)");

	debug("\n");
	return instr_len;
}


#define MEMORY_RW	x86_memory_rw
#define MEM_X86
#include "memory_rw.c"
#undef MEM_X86
#undef MEMORY_RW


/*
 *  x86_load():
 *
 *  Returns same error code as memory_rw().
 */
static int x86_load(struct cpu *cpu, uint64_t addr, uint64_t *data, int len)
{
	unsigned char databuf[8];
	int res;
	uint64_t d;

	res = cpu->memory_rw(cpu, cpu->mem, addr, &databuf[0], len,
	    MEM_READ, CACHE_DATA);

	d = databuf[0];
	if (len > 1) {
		d += ((uint64_t)databuf[1] << 8);
		if (len > 2) {
			d += ((uint64_t)databuf[2] << 16);
			d += ((uint64_t)databuf[3] << 24);
			if (len > 4) {
				d += ((uint64_t)databuf[4] << 32);
				d += ((uint64_t)databuf[5] << 40);
				d += ((uint64_t)databuf[6] << 48);
				d += ((uint64_t)databuf[7] << 56);
			}
		}
	}

	*data = d;
	return res;
}


/*
 *  x86_store():
 *
 *  Returns same error code as memory_rw().
 */
static int x86_store(struct cpu *cpu, uint64_t addr, uint64_t data, int len)
{
	unsigned char databuf[8];

	/*  x86 is always little-endian:  */
	databuf[0] = data;
	if (len > 1) {
		databuf[1] = data >> 8;
		if (len > 2) {
			databuf[2] = data >> 16;
			databuf[3] = data >> 24;
			if (len > 4) {
				databuf[4] = data >> 32;
				databuf[5] = data >> 40;
				databuf[6] = data >> 48;
				databuf[7] = data >> 56;
				if (len > 8)
					fatal("x86_store: bad len?\n");
			}
		}
	}

	return cpu->memory_rw(cpu, cpu->mem, addr, &databuf[0], len,
	    MEM_WRITE, CACHE_DATA);
}


/*
 *  x86_cmp():
 */
static void x86_cmp(struct cpu *cpu, uint64_t a, uint64_t b)
{
	if (a == b)
		cpu->cd.x86.eflags |= X86_EFLAGS_ZF;
	else
		cpu->cd.x86.eflags &= ~X86_EFLAGS_ZF;

	if (a < b)
		cpu->cd.x86.eflags |= X86_EFLAGS_CF;
	else
		cpu->cd.x86.eflags &= ~X86_EFLAGS_CF;

	/*  TODO: other bits?  */
}


/*
 *  x86_test():
 */
static void x86_test(struct cpu *cpu, uint64_t a, uint64_t b)
{
	a &= b;

	if (a == 0)
		cpu->cd.x86.eflags |= X86_EFLAGS_ZF;
	else
		cpu->cd.x86.eflags &= ~X86_EFLAGS_ZF;

	if ((int32_t)a < 0)
		cpu->cd.x86.eflags |= X86_EFLAGS_SF;
	else
		cpu->cd.x86.eflags &= ~X86_EFLAGS_SF;

	cpu->cd.x86.eflags &= ~X86_EFLAGS_CF;
	cpu->cd.x86.eflags &= ~X86_EFLAGS_OF;
	/*  TODO: PF  */
}


/*
 *  x86_cpu_run_instr():
 *
 *  Execute one instruction on a specific CPU.
 *
 *  Return value is the number of instructions executed during this call,   
 *  0 if no instruction was executed.
 */
int x86_cpu_run_instr(struct emul *emul, struct cpu *cpu)
{
	int i, r, op, len, mode = cpu->cd.x86.mode;
	uint32_t imm, imm2, value;
	unsigned char buf[32];
	unsigned char *instr = buf;
	uint64_t newpc = cpu->pc;
	unsigned char databuf[8];
	uint64_t tmp;

	/*  Check PC against breakpoints:  */
	if (!single_step)
		for (i=0; i<cpu->machine->n_breakpoints; i++)
			if (cpu->pc == cpu->machine->breakpoint_addr[i]) {
				fatal("Breakpoint reached, pc=0x%llx",
				    (long long)cpu->pc);
				single_step = 1;
				return 0;
			}

	/*  Read an instruction from memory:  */
	cpu->cd.x86.cursegment = cpu->cd.x86.cs;
	r = cpu->memory_rw(cpu, cpu->mem, cpu->pc, &buf[0], sizeof(buf),
	    MEM_READ, CACHE_INSTRUCTION);
	if (!r)
		return 0;

	if (cpu->machine->instruction_trace)
		x86_cpu_disassemble_instr(cpu, instr, 1, 0, 0);

	/*  All instructions are at least one byte long :-)  */
	newpc ++;

	/*  Default is to use the data segment, or the stack segment:  */
	cpu->cd.x86.cursegment = cpu->cd.x86.ds;

	/*  Any prefix?  */
	while (instr[0] == 0x66 || instr[0] == 0x26 || instr[0] == 0x36
	    || instr[0] == 0x2e || instr[0] == 0x3e) {
		if (instr[0] == 0x26)
			cpu->cd.x86.cursegment = cpu->cd.x86.es;
		if (instr[0] == 0x2e)
			cpu->cd.x86.cursegment = cpu->cd.x86.cs;
		if (instr[0] == 0x36)
			cpu->cd.x86.cursegment = cpu->cd.x86.ss;
		if (instr[0] == 0x3e)
			cpu->cd.x86.cursegment = cpu->cd.x86.ds;
		if (instr[0] == 0x66) {
			if (mode == 16)
				mode = 32;
			else
				mode = 16;
		}
		/*  TODO: rep, lock etc  */
		instr ++;
	}

	switch ((op = instr[0])) {

	case 0x0f:
		instr ++;
		newpc ++;
		switch (instr[0]) {
		case 0x84:	/*  je  32-bit  */
		case 0x85:	/*  jne 32-bit  */
			instr ++;
			imm = instr[0] + (instr[1] << 8) + (instr[2] << 16)
			    + (instr[3] << 24);
			newpc += 4;
			switch (op) {
			case 0x84:
				if (cpu->cd.x86.eflags & X86_EFLAGS_ZF)
					newpc = newpc + imm;
				break;
			case 0x85:
				if (!(cpu->cd.x86.eflags & X86_EFLAGS_ZF))
					newpc = newpc + imm;
				break;
			}
			break;
		default:
			fatal("x86_cpu_run_instr(): unimplemented subopcode: "
			    "0x0f,0x%02x at pc=0x%016llx\n", instr[0],
			    (long long)cpu->pc);
			cpu->running = 0;
			return 0;
		}
		break;

	case 0x25:
		/*  and $imm,%eax etc  */
		instr ++;
		if (mode == 32) {
			imm = instr[0] + (instr[1] << 8) + (instr[2] << 16)
			    + (instr[3] << 24);
			newpc += 4;
		} else {
			/*  16-bit:  */
			imm = instr[0] + (instr[1] << 8) + 0xffff0000;
			newpc += 2;
		}
		switch (op) {
		case 0x25: cpu->cd.x86.eax &= imm; break;
		}
		break;

	case 0x31:
		instr ++;
		newpc ++;
		switch (instr[0]) {
		case 0xc0:
			/*  xor %eax,%eax  */
			if (mode == 32)
				cpu->cd.x86.eax = 0;
			else if (mode == 16)
				cpu->cd.x86.eax &= ~0xffff;
			else
				fatal("31/c0 TODO\n");
			break;
		default:
			fatal("x86_cpu_run_instr(): unimplemented subopcode: "
			    "0x31,0x%02x at pc=0x%016llx\n", instr[0],
			    (long long)cpu->pc);
			cpu->running = 0;
			return 0;
		}
		break;

	case 0x33:
		instr ++;
		newpc ++;
		switch (instr[0]) {
		case 0xc0:
			/*  xor %eax,%eax  */
			if (mode == 32)
				cpu->cd.x86.eax = 0;
			else if (mode == 16)
				cpu->cd.x86.eax &= ~0xffff;
			else
				fatal("31/c0 TODO\n");
			break;
		default:
			fatal("x86_cpu_run_instr(): unimplemented subopcode: "
			    "0x33,0x%02x at pc=0x%016llx\n", instr[0],
			    (long long)cpu->pc);
			cpu->running = 0;
			return 0;
		}
		break;

	case 0x39:
		instr ++;
		newpc ++;
		switch (instr[0]) {
		case 0xd0:
			/*  cmp %edx,%eax  */
			x86_cmp(cpu, cpu->cd.x86.eax, cpu->cd.x86.edx);
			break;
		default:
			fatal("x86_cpu_run_instr(): unimplemented subopcode: "
			    "0x39,0x%02x at pc=0x%016llx\n", instr[0],
			    (long long)cpu->pc);
			cpu->running = 0;
			return 0;
		}
		break;

	case 0x40:	/*  inc eax  */
		cpu->cd.x86.eax = (uint32_t)(cpu->cd.x86.eax + 1); break;
	case 0x41:	/*  inc ecx  */
		cpu->cd.x86.ecx = (uint32_t)(cpu->cd.x86.ecx + 1); break;
	case 0x42:	/*  inc edx  */
		cpu->cd.x86.edx = (uint32_t)(cpu->cd.x86.edx + 1); break;
	case 0x43:	/*  inc ebx  */
		cpu->cd.x86.ebx = (uint32_t)(cpu->cd.x86.ebx + 1); break;
	case 0x44:	/*  inc esp  */
		cpu->cd.x86.esp = (uint32_t)(cpu->cd.x86.esp + 1); break;
	case 0x45:	/*  inc ebp  */
		cpu->cd.x86.ebp = (uint32_t)(cpu->cd.x86.ebp + 1); break;
	case 0x46:	/*  inc esi  */
		cpu->cd.x86.esi = (uint32_t)(cpu->cd.x86.esi + 1); break;
	case 0x47:	/*  inc edi  */
		cpu->cd.x86.edi = (uint32_t)(cpu->cd.x86.edi + 1); break;

	case 0x06:
	case 0x0e:
	case 0x16:
	case 0x1e:
	case 0x53:
	case 0x55:
	case 0x56:
	case 0x57:
		/*  push  */
		cpu->cd.x86.cursegment = cpu->cd.x86.ss;
		value = 0; len = sizeof(uint32_t);
		if (mode == 16)
			len = sizeof(uint16_t);
		switch (op) {
		case 0x06:	value = cpu->cd.x86.es; len = 2; break;
		case 0x0e:	value = cpu->cd.x86.cs; len = 2; break;
		case 0x16:	value = cpu->cd.x86.ss; len = 2; break;
		case 0x1e:	value = cpu->cd.x86.ds; len = 2; break;
		case 0x53:	value = cpu->cd.x86.ebx; break;
		case 0x55:	value = cpu->cd.x86.ebp; break;
		case 0x56:	value = cpu->cd.x86.esi; break;
		case 0x57:	value = cpu->cd.x86.edi; break;
		}
		if (x86_store(cpu, cpu->cd.x86.esp - len,
		    value, len) != MEMORY_ACCESS_OK)
			return 0;
		cpu->cd.x86.esp -= len;
		break;

	case 0x07:
	case 0x17:
	case 0x1f:
	case 0x58:
	case 0x59:
	case 0x5a:
	case 0x5b:
	case 0x5c:
	case 0x5d:
	case 0x5e:
	case 0x5f:
		/*  pop  */
		cpu->cd.x86.cursegment = cpu->cd.x86.ss;
		value = 0; len = sizeof(uint32_t);
		if (mode == 16)
			len = sizeof(uint16_t);
		switch (op) {
		case 0x07:	len = 2; break;
		case 0x17:	len = 2; break;
		case 0x1f:	len = 2; break;
		}
		if (x86_load(cpu, cpu->cd.x86.esp,
		    &tmp, len) != MEMORY_ACCESS_OK)
			return 0;
		switch (op) {
		case 0x07: cpu->cd.x86.es = tmp; break;
		case 0x17: cpu->cd.x86.ss = tmp; break;
		case 0x1f: cpu->cd.x86.ds = tmp; break;
		default:
			if (mode == 16) {
				switch (op) {
				case 0x58: cpu->cd.x86.eax &= ~0xffff;
					   cpu->cd.x86.eax |= tmp; break;
				case 0x59: cpu->cd.x86.ecx &= ~0xffff;
					   cpu->cd.x86.ecx |= tmp; break;
				case 0x5a: cpu->cd.x86.edx &= ~0xffff;
					   cpu->cd.x86.edx |= tmp; break;
				case 0x5b: cpu->cd.x86.ebx &= ~0xffff;
					   cpu->cd.x86.ebx |= tmp; break;
				case 0x5c: cpu->cd.x86.esp &= ~0xffff;
					   cpu->cd.x86.esp |= tmp; break;
				case 0x5d: cpu->cd.x86.ebp &= ~0xffff;
					   cpu->cd.x86.ebp |= tmp; break;
				case 0x5e: cpu->cd.x86.esi &= ~0xffff;
					   cpu->cd.x86.esi |= tmp; break;
				case 0x5f: cpu->cd.x86.edi &= ~0xffff;
					   cpu->cd.x86.edi |= tmp; break;
				}
			} else {
				switch (op) {
				case 0x58: cpu->cd.x86.eax = tmp; break;
				case 0x59: cpu->cd.x86.ecx = tmp; break;
				case 0x5a: cpu->cd.x86.edx = tmp; break;
				case 0x5b: cpu->cd.x86.ebx = tmp; break;
				case 0x5c: cpu->cd.x86.esp = tmp; break;
				case 0x5d: cpu->cd.x86.ebp = tmp; break;
				case 0x5e: cpu->cd.x86.esi = tmp; break;
				case 0x5f: cpu->cd.x86.edi = tmp; break;
				}
			}
		}
		cpu->cd.x86.esp += len;
		break;

	case 0x68:
		/*  push $imm  */
		cpu->cd.x86.cursegment = cpu->cd.x86.ss;
		instr ++;
		imm = instr[0] + (instr[1] << 8) + (instr[2] << 16)
		    + (instr[3] << 24);
		newpc += 4;
		if (x86_store(cpu, cpu->cd.x86.esp - sizeof(uint32_t),
		    imm, sizeof(uint32_t)) != MEMORY_ACCESS_OK)
			return 0;
		cpu->cd.x86.esp -= sizeof(uint32_t);
		break;

	case 0x73:	/*  jae  */
	case 0x74:	/*  je  */
	case 0x75:	/*  jne  */
	case 0x76:	/*  jna  */
	case 0x77:	/*  ja  */
		instr ++;
		imm = (signed char) instr[0];
		newpc += 1;
		switch (op) {
		case 0x73:
			if (!(cpu->cd.x86.eflags & X86_EFLAGS_CF))
				newpc = newpc + imm;
			break;
		case 0x74:
			if (cpu->cd.x86.eflags & X86_EFLAGS_ZF)
				newpc = newpc + imm;
			break;
		case 0x75:
			if (!(cpu->cd.x86.eflags & X86_EFLAGS_ZF))
				newpc = newpc + imm;
			break;
		case 0x76:
			if ((cpu->cd.x86.eflags & X86_EFLAGS_CF) ||
			    (cpu->cd.x86.eflags & X86_EFLAGS_ZF))
				newpc = newpc + imm;
			break;
		case 0x77:
			if (!((cpu->cd.x86.eflags & X86_EFLAGS_CF) ||
			    (cpu->cd.x86.eflags & X86_EFLAGS_ZF)))
				newpc = newpc + imm;
			break;
		}
		break;

	case 0x80:
		instr ++;
		newpc ++;
		switch (instr[0]) {
		case 0x39:		/*  cmpb $imm,(%ecx)  */
			instr ++;
			imm = (signed char)instr[0];
			newpc ++;
			if (x86_load(cpu, cpu->cd.x86.ecx, &tmp, 1)
			    != MEMORY_ACCESS_OK)
				return 0;
			x86_cmp(cpu, tmp, imm);
			break;
		case 0x3d:		/*  cmpb $imm,imm2  */
			instr ++;
			imm2 = instr[0] + (instr[1] << 8) + (instr[2] << 16)
			    + (instr[3] << 24);
			newpc += 4;
			instr ++;
			imm = (signed char)instr[0];
			newpc ++;
			if (x86_load(cpu, imm2, &tmp, 1) != MEMORY_ACCESS_OK)
				return 0;
			x86_cmp(cpu, tmp, imm);
			break;
		default:
			fatal("x86_cpu_run_instr(): unimplemented subopcode: "
			    "0x80,0x%02x at pc=0x%016llx\n", instr[0],
			    (long long)cpu->pc);
			cpu->running = 0;
			return 0;
		}
		break;

	case 0x81:
		instr ++;
		newpc ++;
		switch (instr[0]) {
		case 0x65:
			instr ++;
			imm = (signed char)instr[0];
			newpc ++;
			instr ++;
			imm2 = instr[0] + (instr[1] << 8) + (instr[2] << 16)
			    + (instr[3] << 24);
			newpc += 4;
			/*  andl $imm2,imm(%ebp)  */
			if (x86_load(cpu, cpu->cd.x86.ebp + imm,
			    &tmp, sizeof(uint32_t)) != MEMORY_ACCESS_OK)
				return 0;
			if (x86_store(cpu, cpu->cd.x86.ebp + imm,
			    tmp & imm2, sizeof(uint32_t)) != MEMORY_ACCESS_OK)
				return 0;
			break;
		case 0xc8:
		case 0xc9:
		case 0xca:
		case 0xcb:
		case 0xcc:
		case 0xcd:
		case 0xce:
		case 0xcf:
			instr ++;
			imm = instr[0] + (instr[1] << 8) + (instr[2] << 16)
			    + (instr[3] << 24);
			newpc += 4;
			switch (instr[0]) {
			case 0xc8: cpu->cd.x86.eax |= imm; break;
			case 0xc9: cpu->cd.x86.ecx |= imm; break;
			case 0xca: cpu->cd.x86.edx |= imm; break;
			case 0xcb: cpu->cd.x86.ebx |= imm; break;
			case 0xcc: cpu->cd.x86.esp |= imm; break;
			case 0xcd: cpu->cd.x86.ebp |= imm; break;
			case 0xce: cpu->cd.x86.esi |= imm; break;
			case 0xcf: cpu->cd.x86.edi |= imm; break;
			}
			break;
		case 0xfb:
		case 0xfe:
			instr ++;
			imm = instr[0] + (instr[1] << 8) + (instr[2] << 16)
			    + (instr[3] << 24);
			newpc += 4;
			switch (instr[0]) {
			case 0xfb:	/*  cmp $imm,%ebx  */
				x86_cmp(cpu, (uint32_t)imm, cpu->cd.x86.ebx);
				break;
			case 0xfe:	/*  cmp $imm,%esi  */
				x86_cmp(cpu, (uint32_t)imm, cpu->cd.x86.esi);
				break;
			}
			break;
		default:
			fatal("x86_cpu_run_instr(): unimplemented subopcode: "
			    "0x81,0x%02x at pc=0x%016llx\n", instr[0],
			    (long long)cpu->pc);
			cpu->running = 0;
			return 0;
		}
		break;

	case 0x83:
		instr ++;
		newpc ++;
		switch (instr[0]) {
		case 0x3c:
			instr ++;
			newpc ++;
			switch (instr[0]) {
			case 0x81:	/*  cmpl $imm,(%ecx,%eax,4)  */
				instr ++;
				imm = (signed char)instr[0];
				newpc ++;
				if (x86_load(cpu, cpu->cd.x86.ecx +
				    4 * cpu->cd.x86.eax, &tmp,
				    sizeof(uint32_t)) != MEMORY_ACCESS_OK) {
					return 0;
				}
				x86_cmp(cpu, (uint32_t)tmp, (uint32_t)imm);
				break;
			default:
				fatal("x86_cpu_run_instr(): unimplemented "
				    "subopcode: 0x83,0x3c,0x%02x at pc=0x"
				    "%016llx\n", instr[0], (long long)cpu->pc);
				cpu->running = 0;
				return 0;
			}
			break;
		case 0xc4:
			instr ++;
			imm = (signed char)instr[0];
			newpc ++;
			cpu->cd.x86.esp += imm;
			break;
		case 0xec:
			instr ++;
			imm = (signed char)instr[0];
			newpc ++;
			cpu->cd.x86.esp -= imm;
			break;
		default:
			fatal("x86_cpu_run_instr(): unimplemented subopcode: "
			    "0x83,0x%02x at pc=0x%016llx\n", instr[0],
			    (long long)cpu->pc);
			cpu->running = 0;
			return 0;
		}
		break;

	case 0x84:
		instr ++;
		newpc ++;
		switch (instr[0]) {
		case 0xc0:
			/*  test %al,%al  */
			x86_test(cpu, cpu->cd.x86.eax & 0xff,
			    cpu->cd.x86.eax & 0xff);
			break;
		default:
			fatal("x86_cpu_run_instr(): unimplemented subopcode: "
			    "0x84,0x%02x at pc=0x%016llx\n", instr[0],
			    (long long)cpu->pc);
			cpu->running = 0;
			return 0;
		}
		break;

	case 0x85:
		instr ++;
		newpc ++;
		switch (instr[0]) {
		case 0xc0:
			/*  test %eax,%eax  */
			x86_test(cpu, cpu->cd.x86.eax, cpu->cd.x86.eax);
			break;
		default:
			fatal("x86_cpu_run_instr(): unimplemented subopcode: "
			    "0x85,0x%02x at pc=0x%016llx\n", instr[0],
			    (long long)cpu->pc);
			cpu->running = 0;
			return 0;
		}
		break;

	case 0x88:
		instr ++;
		newpc ++;
		switch (instr[0]) {
		case 0x02:
			/*  mov %al,(%edx)  */
			if (x86_store(cpu, cpu->cd.x86.edx,
			    cpu->cd.x86.eax & 0xff, 1) != MEMORY_ACCESS_OK)
				return 0;
			break;
		default:
			fatal("x86_cpu_run_instr(): unimplemented subopcode: "
			    "0x85,0x%02x at pc=0x%016llx\n", instr[0],
			    (long long)cpu->pc);
			cpu->running = 0;
			return 0;
		}
		break;

	case 0x89:
		instr ++;
		newpc ++;
		switch (instr[0]) {
		case 0x02:
			/*  mov %eax,(%edx)  */
			if (x86_store(cpu, cpu->cd.x86.edx, cpu->cd.x86.eax,
			    sizeof(uint32_t)) != MEMORY_ACCESS_OK)
				return 0;
			break;
		case 0x75:
			instr ++;
			imm = (signed char)instr[0];
			newpc ++;
			if (x86_store(cpu, cpu->cd.x86.ebp + imm,
			    cpu->cd.x86.esi, sizeof(uint32_t))
			    != MEMORY_ACCESS_OK) {
				return 0;
			}
			break;
		case 0xc4:
			/*  mov %eax,%esp  */
			cpu->cd.x86.esp = cpu->cd.x86.eax;
			break;
		case 0xe5:
			/*  mov %esp,%ebp  */
			cpu->cd.x86.ebp = cpu->cd.x86.esp;
			break;
		case 0xf6:
			/*  mov %esi,%esi  */
			break;
		default:
			fatal("x86_cpu_run_instr(): unimplemented subopcode: "
			    "0x89,0x%02x at pc=0x%016llx\n", instr[0],
			    (long long)cpu->pc);
			cpu->running = 0;
			return 0;
		}
		break;

	case 0x8a:
		instr ++;
		newpc ++;
		switch (instr[0]) {
		case 0x01:
			/*  mov (%ecx),%al  */
			if (x86_load(cpu, cpu->cd.x86.ecx, &tmp, 1)
			    != MEMORY_ACCESS_OK)
				return 0;
			cpu->cd.x86.eax = (cpu->cd.x86.eax & ~0xff) | tmp;
			break;
		default:
			fatal("x86_cpu_run_instr(): unimplemented subopcode: "
			    "0xc6,0x%02x at pc=0x%016llx\n", instr[0],
			    (long long)cpu->pc);
			cpu->running = 0;
			return 0;
		}
		break;

	case 0x8b:
		instr ++;
		newpc ++;
		switch (instr[0]) {
		case 0x4d:
		case 0x55:
			instr ++;
			imm = (signed char)instr[0];
			newpc ++;
			if (x86_load(cpu, cpu->cd.x86.ebp + imm,
			    &tmp, sizeof(uint32_t)) != MEMORY_ACCESS_OK) {
				return 0;
			}
			switch (instr[0]) {
			case 0x4d: cpu->cd.x86.ecx = tmp; break;
			case 0x55: cpu->cd.x86.edx = tmp; break;
			}
			break;
		default:
			fatal("x86_cpu_run_instr(): unimplemented subopcode: "
			    "0x8b,0x%02x at pc=0x%016llx\n", instr[0],
			    (long long)cpu->pc);
			cpu->running = 0;
			return 0;
		}
		break;

	case 0x8d:
		instr ++;
		newpc ++;
		switch (instr[0]) {
		case 0x73:
			instr ++;
			imm = (signed char)instr[0];
			newpc ++;
			cpu->cd.x86.esi = cpu->cd.x86.ebx + imm;
			break;
		default:
			fatal("x86_cpu_run_instr(): unimplemented subopcode: "
			    "0x8d,0x%02x at pc=0x%016llx\n", instr[0],
			    (long long)cpu->pc);
			cpu->running = 0;
			return 0;
		}
		break;

	case 0x8e:
		instr ++;
		newpc ++;
		switch (instr[0]) {
		case 0xd0:
			cpu->cd.x86.ss = cpu->cd.x86.eax & 0xffff;
			break;
		default:
			fatal("x86_cpu_run_instr(): unimplemented subopcode: "
			    "0x8e,0x%02x at pc=0x%016llx\n", instr[0],
			    (long long)cpu->pc);
			cpu->running = 0;
			return 0;
		}
		break;

	case 0x90:
		/*  NOP  */
		break;

	case 0xa0:
		/*  mov imm,%al etc  */
		instr ++;
		imm = instr[0] + (instr[1] << 8) + (instr[2] << 16)
		    + (instr[3] << 24);
		newpc += 4;
		switch (op) {
		case 0xa0:
			if (x86_load(cpu, imm, &tmp, 1) != MEMORY_ACCESS_OK)
				return 0;
			cpu->cd.x86.eax = (cpu->cd.x86.eax & ~0xff) | tmp;
			break;
		}
		break;

	case 0xb0:
		/*  mov $imm,%al etc  */
		instr ++;
		imm = instr[0];
		newpc ++;
		switch (op) {
		case 0xb0:
			cpu->cd.x86.eax = (cpu->cd.x86.eax & ~0xff) | imm;
			break;
		}
		break;

	case 0xb8:
	case 0xb9:
	case 0xba:
	case 0xbb:
	case 0xbc:
	case 0xbd:
	case 0xbe:
	case 0xbf:
		/*  mov $imm,%eax etc  */
		instr ++;
		if (mode == 16) {
			imm = instr[0] + (instr[1] << 8);
			newpc += 2;
			switch (op) {
			case 0xb8: cpu->cd.x86.eax = imm; break;
			case 0xb9: cpu->cd.x86.ecx = imm; break;
			case 0xba: cpu->cd.x86.edx = imm; break;
			case 0xbb: cpu->cd.x86.ebx = imm; break;
			case 0xbc: cpu->cd.x86.esp = imm; break;
			case 0xbd: cpu->cd.x86.ebp = imm; break;
			case 0xbe: cpu->cd.x86.esi = imm; break;
			case 0xbf: cpu->cd.x86.edi = imm; break;
			}
		} else {
			imm = instr[0] + (instr[1] << 8) + (instr[2] << 16)
			    + (instr[3] << 24);
			newpc += 4;
			switch (op) {
			case 0xb8: cpu->cd.x86.eax &= ~0xffff;
				   cpu->cd.x86.eax |= imm; break;
			case 0xb9: cpu->cd.x86.ecx &= ~0xffff;
				   cpu->cd.x86.ecx |= imm; break;
			case 0xba: cpu->cd.x86.edx &= ~0xffff;
				   cpu->cd.x86.edx |= imm; break;
			case 0xbb: cpu->cd.x86.ebx &= ~0xffff;
				   cpu->cd.x86.ebx |= imm; break;
			case 0xbc: cpu->cd.x86.esp &= ~0xffff;
				   cpu->cd.x86.esp |= imm; break;
			case 0xbd: cpu->cd.x86.ebp &= ~0xffff;
				   cpu->cd.x86.ebp |= imm; break;
			case 0xbe: cpu->cd.x86.esi &= ~0xffff;
				   cpu->cd.x86.esi |= imm; break;
			case 0xbf: cpu->cd.x86.edi &= ~0xffff;
				   cpu->cd.x86.edi |= imm; break;
			}
		}
		break;

	case 0xc1:
		instr ++;
		newpc ++;
		switch (instr[0]) {
		case 0xea:
			instr ++;
			imm = (signed char)instr[0];
			newpc ++;
			cpu->cd.x86.edx &= 0xffffffffULL;
			cpu->cd.x86.edx >>= imm;
			break;
		default:
			fatal("x86_cpu_run_instr(): unimplemented subopcode: "
			    "0xc1,0x%02x at pc=0x%016llx\n", instr[0],
			    (long long)cpu->pc);
			cpu->running = 0;
			return 0;
		}
		break;

	case 0xc3:
		/*  ret: pop pc  */
		cpu->cd.x86.cursegment = cpu->cd.x86.ss;
		if (x86_load(cpu, cpu->cd.x86.esp,
		    &tmp, sizeof(uint32_t)) != MEMORY_ACCESS_OK) {
			return 0;
		}
		if (mode == 16)
			cpu->cd.x86.esp = ((cpu->cd.x86.esp + 2) & 0xffff)
			    | (cpu->cd.x86.esp & ~0xffff);
		else
			cpu->cd.x86.esp += sizeof(uint32_t);
		newpc = tmp;
		break;

	case 0xc6:
		instr ++;
		newpc ++;
		switch (instr[0]) {
		case 0x02:
			instr ++;
			imm = instr[0];
			newpc ++;
			/*  movb $imm,(%edx)  */
			if (x86_store(cpu, cpu->cd.x86.edx,
			    imm, 1) != MEMORY_ACCESS_OK)
				return 0;
			break;
		default:
			fatal("x86_cpu_run_instr(): unimplemented subopcode: "
			    "0xc6,0x%02x at pc=0x%016llx\n", instr[0],
			    (long long)cpu->pc);
			cpu->running = 0;
			return 0;
		}
		break;

	case 0xc7:
		instr ++;
		newpc ++;
		switch (instr[0]) {
		case 0x45:
			instr ++;
			imm = (signed char)instr[0];
			newpc ++;
			imm2 = instr[0] + (instr[1] << 8) + (instr[2] << 16)
			    + (instr[3] << 24);
			newpc += 4;
			/*  movl $imm2,imm(%ebp)  */
			if (x86_store(cpu, cpu->cd.x86.ebp + imm,
			    imm2, sizeof(uint32_t)) != MEMORY_ACCESS_OK)
				return 0;
			break;
		default:
			fatal("x86_cpu_run_instr(): unimplemented subopcode: "
			    "0xc7,0x%02x at pc=0x%016llx\n", instr[0],
			    (long long)cpu->pc);
			cpu->running = 0;
			return 0;
		}
		break;

	case 0xc9:
		/*  leave: esp=ebp, and then pop ebp  */
		cpu->cd.x86.cursegment = cpu->cd.x86.ss;
		if (mode == 16) {
			cpu->cd.x86.esp = (cpu->cd.x86.esp & ~0xffff)
			    | (cpu->cd.x86.ebp & 0xffff);
			if (x86_load(cpu, cpu->cd.x86.esp,
			    &tmp, sizeof(uint16_t)) != MEMORY_ACCESS_OK) {
				return 0;
			}
			cpu->cd.x86.esp = (cpu->cd.x86.esp & ~0xffff)
			    | (cpu->cd.x86.esp + 2);
			cpu->cd.x86.ebp = (cpu->cd.x86.ebp & ~0xffff)
			    | tmp;
		} else {
			cpu->cd.x86.esp = cpu->cd.x86.ebp;
			if (x86_load(cpu, cpu->cd.x86.esp,
			    &tmp, sizeof(uint32_t)) != MEMORY_ACCESS_OK) {
				return 0;
			}
			cpu->cd.x86.esp += sizeof(uint32_t);
			cpu->cd.x86.ebp = tmp;
		}
		break;

	case 0xe8:	/*  call  */
	case 0xe9:	/*  jmp  */
	case 0xeb:	/*  jmp (8 bits)  */
		instr ++;
		switch (op) {
		case 0xeb:	/*  8 bits  */
			imm = (signed char) instr[0];
			newpc += 1;
			break;
		default:
			imm = instr[0] + (instr[1] << 8) + (instr[2] << 16)
			    + (instr[3] << 24);
			newpc += 4;
		}
		/*  For a "call", push the return address:  */
		if (op == 0xe8) {
			cpu->cd.x86.cursegment = cpu->cd.x86.ss;
			if (x86_store(cpu, cpu->cd.x86.esp - sizeof(uint32_t),
			    newpc, sizeof(uint32_t))
			    != MEMORY_ACCESS_OK)
				return 0;
			cpu->cd.x86.esp -= sizeof(uint32_t);
		}
		newpc = newpc + imm;
		break;

	case 0xea:
		instr ++;
		if (mode == 32) {
			imm = instr[0] + (instr[1] << 8) + (instr[2] << 16)
			    + (instr[3] << 24);
			newpc = imm;
			cpu->cd.x86.cs = instr[4] + (instr[5] << 8);
		} else if (mode == 16) {
			imm = instr[0] + (instr[1] << 8);
			newpc = imm;
			cpu->cd.x86.cs = instr[2] + (instr[3] << 8);
		} else {
			/*  64-bit  */
			fatal("todo: 64-bit ljmp\n");
			exit(1);
		}
		break;

	case 0xee:
		/*  out %al,(%dx)  */
		databuf[0] = cpu->cd.x86.eax & 0xff;
		cpu->memory_rw(cpu, cpu->mem, (cpu->cd.x86.edx & 0xffff)
		    + 0x100000000ULL, &databuf[0], 1, MEM_WRITE, CACHE_NONE);
		break;

	case 0xf8:	/*  clc  */
		cpu->cd.x86.eflags &= ~X86_EFLAGS_CF;
		break;

	case 0xf9:	/*  stc  */
		cpu->cd.x86.eflags |= X86_EFLAGS_CF;
		break;

	case 0xfa:	/*  cli  */
		cpu->cd.x86.eflags &= ~X86_EFLAGS_IF;
		break;

	case 0xfb:	/*  sti  */
		cpu->cd.x86.eflags |= X86_EFLAGS_IF;
		break;

	case 0xfc:	/*  cld  */
		cpu->cd.x86.eflags &= ~X86_EFLAGS_DF;
		break;

	case 0xfd:	/*  std  */
		cpu->cd.x86.eflags |= X86_EFLAGS_DF;
		break;

	default:
		fatal("x86_cpu_run_instr(): unimplemented opcode 0x%02x"
		    " at pc=0x%016llx\n", instr[0], (long long)newpc);
		cpu->running = 0;
		return 0;
	}

	cpu->pc = newpc;

	return 1;
}


#define CPU_RUN         x86_cpu_run
#define CPU_RINSTR      x86_cpu_run_instr
#define CPU_RUN_X86
#include "cpu_run.c"
#undef CPU_RINSTR
#undef CPU_RUN_X86
#undef CPU_RUN


/*
 *  x86_cpu_family_init():
 *
 *  Fill in the cpu_family struct for x86.
 */
int x86_cpu_family_init(struct cpu_family *fp)
{
	fp->name = "x86";
	fp->cpu_new = x86_cpu_new;
	fp->list_available_types = x86_cpu_list_available_types;
	fp->register_match = x86_cpu_register_match;
	fp->disassemble_instr = x86_cpu_disassemble_instr;
	fp->register_dump = x86_cpu_register_dump;
	fp->run = x86_cpu_run;
	fp->dumpinfo = x86_cpu_dumpinfo;
	/*  fp->show_full_statistics = x86_cpu_show_full_statistics;  */
	/*  fp->tlbdump = x86_cpu_tlbdump;  */
	/*  fp->interrupt = x86_cpu_interrupt;  */
	/*  fp->interrupt_ack = x86_cpu_interrupt_ack;  */
	return 1;
}

#endif	/*  ENABLE_X86  */
