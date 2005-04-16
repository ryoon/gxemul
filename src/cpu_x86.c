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
 *  $Id: cpu_x86.c,v 1.19 2005-04-16 19:59:11 debug Exp $
 *
 *  x86 (and potentially amd64) CPU emulation.
 *
 *  Only a few instructions are supported so far, so it will not run anything
 *  more than hello world. And probably not even that.
 *
 *  Hahaha, this module is super-ugly and should be rewritten from scratch!
 *  The best way would probably be to do it straight out of an amd64 manual.
 *
 *  TODO:
 *
 *	x)  Many instructions should set flags!
 *
 *	x)  This whole file should be refactored!
 *
 *	x)  Better 16-bit support (for booting from disks, MS-DOS etc)
 *		x)  BIOS emulation
 *
 *	x)  0x66 and 0x67 should affect 32/16-bit stuff differently.
 *
 *	x)  AMD64 stuff.
 *
 *	x)  Segmentation for 32-bit mode
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


static uint32_t read_imm_common(unsigned char *instr, int *instr_lenp,
	int len, int printflag)
{
	uint32_t imm;

	if (len == 8)
		imm = instr[0];
	else if (len == 16)
		imm = instr[0] + (instr[1] << 8);
	else
		imm = instr[0] + (instr[1] << 8) +
		    (instr[2] << 16) + (instr[3] << 24);

	if (printflag)
		HEXPRINT(instr, len / 8);

	(*instr_lenp) += (len / 8);

	return imm;
}


static uint32_t read_imm_and_print(unsigned char *instr, int *instr_lenp,
	int mode)
{
	return read_imm_common(instr, instr_lenp, mode, 1);
}


static uint32_t read_imm(unsigned char *instr, uint64_t *newpc,
	int mode)
{
	int x = 0;
	uint32_t r = read_imm_common(instr, &x, mode, 0);
	(*newpc) += x;
	return r;
}


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
	char *symbol, *tmp = "ERROR", *mnem = "ERROR", *e = "e",
	    *prefix = NULL;

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
	while (instr[0] == 0x66 || instr[0] == 0xf3 || instr[0] == 0x26) {
		switch (instr[0]) {
		case 0x26:
			prefix = "es";
			break;
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

	if (mode == 16)
		e = "";

	switch ((op = instr[0])) {

	case 0x02:
		instr++;
		instr_len++;
		HEXPRINT(instr,1);
		switch (instr[0]) {
		case 0x0e:
			instr ++;
			imm = read_imm_and_print(instr, &instr_len, mode);
			HEXSPACES(instr_len);
			debug("add\t0x%x,%%cl", imm);
			break;
		case 0xc3:
			HEXSPACES(instr_len);
			debug("add\t%%bl,%%al");
			break;
		case 0xd7:
			HEXSPACES(instr_len);
			debug("add\t%%bh,%%dl");
			break;
		default:
			HEXSPACES(instr_len);
			debug("UNIMPLEMENTED");
		}
		break;

	case 0x0f:
		instr++;
		instr_len++;
		HEXPRINT(instr,1);
		switch (instr[0]) {
		case 0x84:
		case 0x85:
			op = instr[0];
			instr ++;
			imm = read_imm_and_print(instr, &instr_len, mode);
			imm = dumpaddr + 6 + imm;
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

	case 0x05:
	case 0x25:
		instr ++;
		imm = read_imm_and_print(instr, &instr_len, mode);
		HEXSPACES(instr_len);
		switch (op) {
		case 0x05: mnem = "add"; tmp = "ax"; break;
		case 0x25: mnem = "and"; tmp = "ax"; break;
		}
		debug("%s\t$0x%x,%%%s%s", mnem, imm, e, tmp);
		break;

	case 0x29:
		instr++;
		instr_len++;
		HEXPRINT(instr,1);
		switch (instr[0]) {
		case 0xf6:
			HEXSPACES(instr_len);
			debug("sub\t%%%ssi,%%%ssi", e, e);
			break;
		case 0xff:
			HEXSPACES(instr_len);
			debug("sub\t%%%sdi,%%%sdi", e, e);
			break;
		default:
			HEXSPACES(instr_len);
			debug("UNIMPLEMENTED");
		}
		break;

	case 0x2b:
		instr++;
		instr_len++;
		HEXPRINT(instr,1);
		switch (instr[0]) {
		case 0xf8:
			HEXSPACES(instr_len);
			debug("sub\t%%%sax,%%%sdi", e, e);
			break;
		default:
			HEXSPACES(instr_len);
			debug("UNIMPLEMENTED");
		}
		break;

	case 0x30:
		instr++;
		instr_len++;
		HEXPRINT(instr,1);
		switch (instr[0]) {
		case 0xe4:
			HEXSPACES(instr_len);
			debug("xor\t%%ah,%%ah");
			break;
		default:
			HEXSPACES(instr_len);
			debug("UNIMPLEMENTED");
		}
		break;

	case 0x31:
		instr++;
		instr_len++;
		HEXPRINT(instr,1);
		switch (instr[0]) {
		case 0xc0:
			HEXSPACES(instr_len);
			debug("xor\t%%%sax,%%%sax", e, e);
			break;
		case 0xc9:
			HEXSPACES(instr_len);
			debug("xor\t%%%scx,%%%scx", e, e);
			break;
		case 0xdb:
			HEXSPACES(instr_len);
			debug("xor\t%%%sbx,%%%sbx", e, e);
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
			debug("xor\t%%%sax,%%%sax", e, e);
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
			debug("cmp\t%%%sdx,%%%sax", e, e);
			break;
		default:
			HEXSPACES(instr_len);
			debug("UNIMPLEMENTED");
		}
		break;

	case 0x3c:
		instr ++;
		imm = (unsigned char)instr[0];
		HEXPRINT(instr,1);
		instr_len += 1;
		HEXSPACES(instr_len);
		debug("cmp\t$0x%x,%%al", (int)imm);
		break;

	case 0x68:
		instr ++;
		imm = read_imm_and_print(instr, &instr_len, mode);
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
	case 0x78:
	case 0x79:
	case 0x7a:
	case 0x7b:
	case 0x7c:
	case 0x7d:
	case 0x7e:
	case 0x7f:
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
		case 0x78: mnem = "js"; break;
		case 0x79: mnem = "jns"; break;
		case 0x7a: mnem = "jpe"; break;
		case 0x7b: mnem = "jpo"; break;
		case 0x7c: mnem = "jl"; break;
		case 0x7d: mnem = "jge"; break;
		case 0x7e: mnem = "jle"; break;
		case 0x7f: mnem = "jg"; break;
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
			imm = read_imm_and_print(instr, &instr_len, 8);
			HEXSPACES(instr_len);
			debug("cmpb\t$%i,(%%ecx)", (int)imm);
			break;
		case 0x3d:
			/*  80 3d xx xx xx xx yy   cmpb   $yy,xx  */
			/*  Get the address:  */
			instr ++;
			imm2 = read_imm_and_print(instr, &instr_len, mode);
			/*  and unsigned imm byte value:  */
			instr ++;
			imm = read_imm_and_print(instr, &instr_len, 8);
			HEXSPACES(instr_len);
			debug("cmpb\t$%i,0x%08x", (int)imm, (int)imm2);
			break;
		case 0xe2:
			instr ++;
			imm = read_imm_and_print(instr, &instr_len, 8);
			HEXSPACES(instr_len);
			debug("and\t$0x%x,%%dl", (int)imm);
			break;
		case 0xf9:
			/*  80 f9 yy   cmp   $yy,%cl  */
			instr ++;
			imm = read_imm_and_print(instr, &instr_len, 8);
			HEXSPACES(instr_len);
			debug("cmp\t$0x%x,%%cl", (int)imm);
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
			imm = (signed char)read_imm_and_print(instr,
			    &instr_len, 8);
			instr ++;
			imm2 = read_imm_and_print(instr, &instr_len, mode);
			HEXSPACES(instr_len);
			debug("andl\t$0x%08x,0x%x(%%ebp)", (int)imm2, (int)imm);
			break;
		case 0xc1:
		case 0xc2:
		case 0xc3:
		case 0xc4:
		case 0xc5:
		case 0xc6:
		case 0xc7:
			op = instr[0];
			instr ++;
			imm = read_imm_and_print(instr, &instr_len, mode);
			HEXSPACES(instr_len);
			switch (op) {
			case 0xc1: debug("add\t$0x%x,%%cx", imm, e); break;
			case 0xc2: debug("add\t$0x%x,%%dx", imm, e); break;
			case 0xc3: debug("add\t$0x%x,%%bx", imm, e); break;
			case 0xc4: debug("add\t$0x%x,%%sp", imm, e); break;
			case 0xc5: debug("add\t$0x%x,%%bp", imm, e); break;
			case 0xc6: debug("add\t$0x%x,%%si", imm, e); break;
			case 0xc7: debug("add\t$0x%x,%%di", imm, e); break;
			}
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
			imm = read_imm_and_print(instr, &instr_len, mode);
			HEXSPACES(instr_len);
			switch (op) {
			case 0xc8: debug("or\t$0x%x,%%eax", imm, e); break;
			case 0xc9: debug("or\t$0x%x,%%ecx", imm, e); break;
			case 0xca: debug("or\t$0x%x,%%edx", imm, e); break;
			case 0xcb: debug("or\t$0x%x,%%ebx", imm, e); break;
			case 0xcc: debug("or\t$0x%x,%%esp", imm, e); break;
			case 0xcd: debug("or\t$0x%x,%%ebp", imm, e); break;
			case 0xce: debug("or\t$0x%x,%%esi", imm, e); break;
			case 0xcf: debug("or\t$0x%x,%%edi", imm, e); break;
			}
			break;
		case 0xfb:
		case 0xfe:
			op = instr[0];
			instr ++;
			imm = read_imm_and_print(instr, &instr_len, mode);
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
			debug("add\t$%i,%%%ssp", imm, e);
			break;
		case 0xc7:
			instr ++;
			imm = (signed char)instr[0];
			HEXPRINT(instr,1);
			instr_len += 1;
			HEXSPACES(instr_len);
			debug("add\t$%i,%%%sdi", imm, e);
			break;
		case 0xec:
			instr ++;
			imm = (signed char)instr[0];
			HEXPRINT(instr,1);
			instr_len += 1;
			HEXSPACES(instr_len);
			debug("sub\t$%i,%%%ssp", imm, e);
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
		case 0xd2:
			HEXSPACES(instr_len);
			debug("test\t%%dl,%%dl");
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
		case 0x26:
			instr ++;
			if (mode == 16) {
				imm = instr[0] + (instr[1] << 8);
				HEXPRINT(instr,2);
				instr_len += 2;
			} else {
				imm = instr[0] + (instr[1] << 8) +
				    (instr[2] << 16) + (instr[3] << 24);
				HEXPRINT(instr,4);
				instr_len += 4;
			}
			HEXSPACES(instr_len);
			debug("mov\t%%ah,0x%x", imm);
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
			debug("mov\t%%%sax,(%%%sdx)", e, e);
			break;
		case 0x15:
			HEXSPACES(instr_len);
			debug("mov\t%%%sdx,(%%%sdi)", e, e);
			break;
		case 0x55:
			instr ++;
			imm = (signed char)instr[0];
			HEXPRINT(instr,1);
			instr_len += 1;
			HEXSPACES(instr_len);
			debug("mov\t%%%sdx,0x%x(%%%sdi)", e, imm, e);
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
		case 0xe6:
			HEXSPACES(instr_len);
			debug("mov\t%%esp,%%esi");
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
			debug("mov\t(%%%scx),%%al", e);
			break;
		case 0x0d:
			HEXSPACES(instr_len);
			debug("mov\t(%%%sdi),%%cl", e);
			break;
		case 0x84:
			instr ++;
			imm = read_imm_and_print(instr, &instr_len, mode);
			HEXSPACES(instr_len);
			debug("mov\t0x%x(%%%ssi),%%al", imm, e);
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
			imm = (signed char)read_imm_and_print(instr,
			    &instr_len, 8);
			HEXSPACES(instr_len);
			debug("mov\t0x%x(%%ebp),%%ecx", imm);
			break;
		case 0x55:
			instr ++;
			imm = (signed char)read_imm_and_print(instr,
			    &instr_len, 8);
			HEXSPACES(instr_len);
			debug("mov\t0x%x(%%ebp),%%edx", imm);
			break;
		case 0xd0:
			HEXSPACES(instr_len);
			debug("mov\t%%%sax,%%%sdx", e, e);
			break;
		case 0xf1:
			HEXSPACES(instr_len);
			debug("mov\t%%%scx,%%%ssi", e, e);
			break;
		case 0xf8:
			HEXSPACES(instr_len);
			debug("mov\t%%%sax,%%%sdi", e, e);
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
			imm = (signed char)read_imm_and_print(instr,
			    &instr_len, 8);
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
		case 0xc0:
			HEXSPACES(instr_len);
			debug("mov\t%%ax,%%es");
			break;
		case 0xc1:
			HEXSPACES(instr_len);
			debug("mov\t%%cx,%%es");
			break;
		case 0xd0:
			HEXSPACES(instr_len);
			debug("mov\t%%ax,%%ss");
			break;
		case 0xd1:
			HEXSPACES(instr_len);
			debug("mov\t%%cx,%%ss");
			break;
		case 0xd8:
			HEXSPACES(instr_len);
			debug("mov\t%%ax,%%ds");
			break;
		case 0xd9:
			HEXSPACES(instr_len);
			debug("mov\t%%cx,%%ds");
			break;
		case 0xe0:
			HEXSPACES(instr_len);
			debug("mov\t%%ax,%%fs");
			break;
		case 0xe8:
			HEXSPACES(instr_len);
			debug("mov\t%%ax,%%gs");
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
	case 0x48:
	case 0x49:
	case 0x4a:
	case 0x4b:
	case 0x4c:
	case 0x4d:
	case 0x4e:
	case 0x4f:
	case 0x50:
	case 0x51:
	case 0x52:
	case 0x53:
	case 0x54:
	case 0x55:
	case 0x56:
	case 0x57:
	case 0x58:
	case 0x59:
	case 0x5a:
	case 0x5b:
	case 0x5c:
	case 0x5d:
	case 0x5e:
	case 0x5f:
	case 0x90:
	case 0x9c:
	case 0x9d:
	case 0xaa:
	case 0xab:
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
		case 0x40:  debug("inc\t%%%sax", e); break;
		case 0x41:  debug("inc\t%%%scx", e); break;
		case 0x42:  debug("inc\t%%%sdx", e); break;
		case 0x43:  debug("inc\t%%%sbx", e); break;
		case 0x44:  debug("inc\t%%%ssp", e); break;
		case 0x45:  debug("inc\t%%%sbp", e); break;
		case 0x46:  debug("inc\t%%%ssi", e); break;
		case 0x47:  debug("inc\t%%%sdi", e); break;
		case 0x48:  debug("dec\t%%%sax", e); break;
		case 0x49:  debug("dec\t%%%scx", e); break;
		case 0x4a:  debug("dec\t%%%sdx", e); break;
		case 0x4b:  debug("dec\t%%%sbx", e); break;
		case 0x4c:  debug("dec\t%%%ssp", e); break;
		case 0x4d:  debug("dec\t%%%sbp", e); break;
		case 0x4e:  debug("dec\t%%%ssi", e); break;
		case 0x4f:  debug("dec\t%%%sdi", e); break;
		case 0x50:  debug("push\t%%%sax", e); break;
		case 0x51:  debug("push\t%%%sdx", e); break;
		case 0x52:  debug("push\t%%%scx", e); break;
		case 0x53:  debug("push\t%%%sbx", e); break;
		case 0x54:  debug("push\t%%%ssp", e); break;
		case 0x55:  debug("push\t%%%sbp", e); break;
		case 0x56:  debug("push\t%%%ssi", e); break;
		case 0x57:  debug("push\t%%%sdi", e); break;
		case 0x58:  debug("pop\t%%%sax", e); break;
		case 0x59:  debug("pop\t%%%sdx", e); break;
		case 0x5a:  debug("pop\t%%%scx", e); break;
		case 0x5b:  debug("pop\t%%%sbx", e); break;
		case 0x5c:  debug("pop\t%%%ssp", e); break;
		case 0x5d:  debug("pop\t%%%sbp", e); break;
		case 0x5e:  debug("pop\t%%%ssi", e); break;
		case 0x5f:  debug("pop\t%%%sdi", e); break;
		case 0x90:  debug("nop"); break;
		case 0x9c:  debug("pushf"); break;
		case 0x9d:  debug("popf"); break;
		case 0xaa:  debug("stosb"); break;
		case 0xab:  debug("stosw"); break;
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
	case 0xa1:
	case 0xa2:
	case 0xa3:
		instr ++;
		imm = read_imm_and_print(instr, &instr_len, mode);
		HEXSPACES(instr_len);
		switch (op) {
		case 0xa2:
		case 0xa0: tmp = "al"; break;
		case 0xa3:
		case 0xa1: tmp = (mode==32)? "eax" : "ax"; break;
		}
		switch (op) {
		case 0xa0:
		case 0xa1:
			/*  "load"  */
			debug("mov\t0x%x,%%%s", imm, tmp);
			break;
		case 0xa2:
		case 0xa3:
			/*  "store"  */
			debug("mov\t%%%s,0x%x", tmp, imm);
			break;
		}
		break;

	case 0xb0:
	case 0xb1:
	case 0xb5:
		instr ++;
		imm = read_imm_and_print(instr, &instr_len, 8);
		HEXSPACES(instr_len);
		switch (op) {
		case 0xb0: tmp = "al"; break;
		case 0xb1: tmp = "cl"; break;
		case 0xb5: tmp = "ch"; break;
		}
		debug("mov\t$0x%x,%%%s", (uint32_t)imm, tmp);
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
		imm = read_imm_and_print(instr, &instr_len, mode);
		HEXSPACES(instr_len);
		switch (op) {
		case 0xb8: tmp = "ax"; break;
		case 0xb9: tmp = "cx"; break;
		case 0xba: tmp = "dx"; break;
		case 0xbb: tmp = "bx"; break;
		case 0xbc: tmp = "sp"; break;
		case 0xbd: tmp = "bp"; break;
		case 0xbe: tmp = "si"; break;
		case 0xbf: tmp = "di"; break;
		}
		debug("mov\t$0x%x,%%%s%s", imm, e, tmp);
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

	case 0xcd:
		instr ++;
		imm = instr[0];
		HEXPRINT(instr,1);
		instr_len += 1;
		HEXSPACES(instr_len);
		debug("int\t$0x%x", imm);
		break;

	case 0xd0:
		instr++;
		instr_len++;
		HEXPRINT(instr,1);
		switch (instr[0]) {
		case 0xe9:
			HEXSPACES(instr_len);
			debug("shr\t$1,%%cl");
			break;
		default:
			HEXSPACES(instr_len);
			debug("UNIMPLEMENTED");
		}
		break;

	case 0xd1:
		instr++;
		instr_len++;
		HEXPRINT(instr,1);
		switch (instr[0]) {
		case 0xe9:
			HEXSPACES(instr_len);
			debug("shr\t$1,%%%scx", e);
			break;
		default:
			HEXSPACES(instr_len);
			debug("UNIMPLEMENTED");
		}
		break;

	case 0xd2:
		instr++;
		instr_len++;
		HEXPRINT(instr,1);
		switch (instr[0]) {
		case 0xe8:
			HEXSPACES(instr_len);
			debug("shr\t%%cl,%%al");
			break;
		default:
			HEXSPACES(instr_len);
			debug("UNIMPLEMENTED");
		}
		break;

	case 0xd3:
		instr++;
		instr_len++;
		HEXPRINT(instr,1);
		switch (instr[0]) {
		case 0xe7:
			HEXSPACES(instr_len);
			debug("shr\t%%cl,%%%sdi", e);
			break;
		case 0xe8:
			HEXSPACES(instr_len);
			debug("shr\t%%cl,%%%sax", e);
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

	case 0xf6:
		instr++;
		instr_len++;
		HEXPRINT(instr,1);
		switch (instr[0]) {
		case 0xe2:
			HEXSPACES(instr_len);
			debug("mul\t%%dl");
			break;
		default:
			HEXSPACES(instr_len);
			debug("UNIMPLEMENTED");
		}
		break;

	case 0xf7:
		instr++;
		instr_len++;
		HEXPRINT(instr,1);
		switch (instr[0]) {
		case 0x26:
			instr++;
			if (mode == 32) {
				imm = instr[0] + (instr[1] << 8) +
				    (instr[2] << 16) + (instr[3] << 24);
				HEXPRINT(instr,4);
				instr_len += 4;
			} else {
				imm = instr[0] + (instr[1] << 8);
				HEXPRINT(instr,2);
				instr_len += 2;
			}
			HEXSPACES(instr_len);
			debug("mul\t%sword at 0x%x", mode==32? "d" : "", imm);
			break;
		default:
			HEXSPACES(instr_len);
			debug("UNIMPLEMENTED");
		}
		break;

	case 0xfe:
		instr++;
		instr_len++;
		HEXPRINT(instr,1);
		switch (instr[0]) {
		case 0xc4:
			HEXSPACES(instr_len);
			debug("inc\t%%ah");
			break;
		case 0xc5:
			HEXSPACES(instr_len);
			debug("inc\t%%ch");
			break;
		default:
			HEXSPACES(instr_len);
			debug("UNIMPLEMENTED");
		}
		break;

	default:
		HEXSPACES(instr_len);
		debug("UNIMPLEMENTED");
	}

disasm_ret:
	if (rep)
		debug(" (rep)");
	if (prefix != NULL)
		debug(" (%s)", prefix);

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
 *  x86_interrupt():
 *
 *  NOTE/TODO: Only for 16-bit mode.
 */
static void x86_interrupt(struct cpu *cpu, int nr)
{
	uint64_t seg, ofs;
	const int len = sizeof(uint16_t);

	/*  Read the interrupt vector from beginning of RAM:  */
	cpu->cd.x86.cursegment = 0;
	x86_load(cpu, nr * 4 + 0, &ofs, sizeof(uint16_t));
	x86_load(cpu, nr * 4 + 2, &seg, sizeof(uint16_t));

	/*  Push flags, cs, and ip (pc):  */
	cpu->cd.x86.cursegment = cpu->cd.x86.ss;
	if (x86_store(cpu, cpu->cd.x86.esp - len * 1, cpu->cd.x86.eflags,
	    len) != MEMORY_ACCESS_OK)
		fatal("x86_interrupt(): TODO: how to handle this\n");
	if (x86_store(cpu, cpu->cd.x86.esp - len * 2, cpu->cd.x86.cs,
	    len) != MEMORY_ACCESS_OK)
		fatal("x86_interrupt(): TODO: how to handle this\n");
	if (x86_store(cpu, cpu->cd.x86.esp - len * 3, cpu->pc,
	    len) != MEMORY_ACCESS_OK)
		fatal("x86_interrupt(): TODO: how to handle this\n");

	cpu->cd.x86.esp = (cpu->cd.x86.esp & ~0xffff)
	    | ((cpu->cd.x86.esp - len*3) & 0xffff);

	/*  TODO: clear the Interrupt Flag?  */

	cpu->cd.x86.cs = seg;
	cpu->pc = ofs;
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
	int i, r, rep = 0, op, len, diff, mode = cpu->cd.x86.mode;
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

	/*  16-bit BIOS emulation:  */
	if (mode == 16 && ((newpc + (cpu->cd.x86.cs << 4)) & 0xff000)
	    == 0xf8000 && cpu->machine->prom_emulation) {
		pc_bios_emul(cpu);
		return 1;
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
	    || instr[0] == 0x2e || instr[0] == 0x3e || instr[0] == 0xf3) {
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
		if (instr[0] == 0xf3)
			rep = 1;
		/*  TODO: repnz, lock etc  */
		instr ++;
		newpc ++;
	}

	switch ((op = instr[0])) {

	case 0x02:
		instr ++;
		newpc ++;
		switch (instr[0]) {
		case 0x0e:
			instr ++;
			imm = read_imm(instr, &newpc, mode);
			if (x86_load(cpu, imm, &tmp, 1) != MEMORY_ACCESS_OK)
				return 0;
			cpu->cd.x86.ecx = (cpu->cd.x86.ecx & ~0xff) |
			    ((cpu->cd.x86.ecx + tmp) & 0xff);
			break;
		case 0xc3:
			cpu->cd.x86.eax = (cpu->cd.x86.eax & ~0xff) |
			    ((cpu->cd.x86.eax + cpu->cd.x86.ebx) & 0xff);
			break;
		case 0xd7:
			cpu->cd.x86.edx = (cpu->cd.x86.edx & ~0xff) |
			    ((cpu->cd.x86.edx + (cpu->cd.x86.ebx >> 8)) & 0xff);
			break;
		default:
			fatal("x86_cpu_run_instr(): unimplemented subopcode: "
			    "0x02,0x%02x at pc=0x%016llx\n", instr[0],
			    (long long)cpu->pc);
			cpu->running = 0;
			return 0;
		}
		break;

	case 0x0f:
		instr ++;
		newpc ++;
		switch (instr[0]) {
		case 0x84:	/*  je  32-bit  */
		case 0x85:	/*  jne 32-bit  */
			instr ++;
			imm = read_imm(instr, &newpc, mode);
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

	case 0x05:
	case 0x25:
		/*  and $imm,%eax etc  */
		instr ++;
		imm = read_imm(instr, &newpc, mode);
		if (mode == 16) {
			switch (op) {
			case 0x05:
				cpu->cd.x86.eax = (cpu->cd.x86.eax & ~0xffff) |
				    ((cpu->cd.x86.eax + imm) & 0xffff);
				break;
			case 0x25: cpu->cd.x86.eax &= (imm | 0xffff0000); break;
			}
		} else {
			switch (op) {
			case 0x05: cpu->cd.x86.eax += imm; break;
			case 0x25: cpu->cd.x86.eax &= imm; break;
			}
		}
		break;

	case 0x29:
		instr ++;
		newpc ++;
		switch (instr[0]) {
		case 0xf6:
			/*  sub %esi,%esi  */
			if (mode == 16)
				cpu->cd.x86.esi &= ~0xffff;
			else
				cpu->cd.x86.esi = 0;
			break;
		case 0xff:
			/*  sub %edi,%edi  */
			if (mode == 16)
				cpu->cd.x86.edi &= ~0xffff;
			else
				cpu->cd.x86.edi = 0;
			break;
		default:
			fatal("x86_cpu_run_instr(): unimplemented subopcode: "
			    "0x29,0x%02x at pc=0x%016llx\n", instr[0],
			    (long long)cpu->pc);
			cpu->running = 0;
			return 0;
		}
		break;

	case 0x2b:
		instr ++;
		newpc ++;
		switch (instr[0]) {
		case 0xf8:
			/*  sub %eax,%edi  */
			if (mode == 16)
				cpu->cd.x86.edi = (cpu->cd.x86.edi & ~0xffff) |
				    ((cpu->cd.x86.edi - cpu->cd.x86.eax) &
				    0xffff);
			else
				cpu->cd.x86.edi -= cpu->cd.x86.eax;
			break;
		default:
			fatal("x86_cpu_run_instr(): unimplemented subopcode: "
			    "0x2b,0x%02x at pc=0x%016llx\n", instr[0],
			    (long long)cpu->pc);
			cpu->running = 0;
			return 0;
		}
		break;

	case 0x30:
		instr ++;
		newpc ++;
		switch (instr[0]) {
		case 0xe4:
			/*  xor %ah,%ah  */
			cpu->cd.x86.eax &= ~0xff00;
			break;
		default:
			fatal("x86_cpu_run_instr(): unimplemented subopcode: "
			    "0x30,0x%02x at pc=0x%016llx\n", instr[0],
			    (long long)cpu->pc);
			cpu->running = 0;
			return 0;
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
		case 0xc9:
			/*  xor %ecx,%ecx  */
			if (mode == 32)
				cpu->cd.x86.ecx = 0;
			else if (mode == 16)
				cpu->cd.x86.ecx &= ~0xffff;
			else
				fatal("31/c9 TODO\n");
			break;
		case 0xdb:
			/*  xor %ebx,%ebx  */
			if (mode == 32)
				cpu->cd.x86.ebx = 0;
			else if (mode == 16)
				cpu->cd.x86.ebx &= ~0xffff;
			else
				fatal("31/db TODO\n");
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

	case 0x3c:	/*  cmp $imm,%al  */
		instr ++;
		imm = (unsigned char)instr[0];
		newpc ++;
		x86_cmp(cpu, cpu->cd.x86.eax & 0xff, imm);
		break;

	case 0x40:
	case 0x41:
	case 0x42:
	case 0x43:
	case 0x44:
	case 0x45:
	case 0x46:
	case 0x47:
	case 0x48:
	case 0x49:
	case 0x4a:
	case 0x4b:
	case 0x4c:
	case 0x4d:
	case 0x4e:
	case 0x4f:
		switch (op & 7) {
		case 0:	tmp = cpu->cd.x86.eax; break;
		case 1:	tmp = cpu->cd.x86.ecx; break;
		case 2:	tmp = cpu->cd.x86.edx; break;
		case 3:	tmp = cpu->cd.x86.ebx; break;
		case 4:	tmp = cpu->cd.x86.esp; break;
		case 5:	tmp = cpu->cd.x86.ebp; break;
		case 6:	tmp = cpu->cd.x86.esi; break;
		case 7:	tmp = cpu->cd.x86.edi; break;
		}

		/*  40..47 = inc, 48..4f = dec  */
		if (op < 0x48)
			diff = 1;
		else
			diff = -1;

		if (mode == 16) {
			tmp = (tmp & ~0xffff) | ((tmp + diff) & 0xffff);
			if ((tmp & 0xffff) == 0)
				r = 0;
			else
				r = 1;
		} else {
			tmp += diff;
			if (tmp == 0)
				r = 0;
			else
				r = 1;
		}

		if (r == 0)
			cpu->cd.x86.eflags |= X86_EFLAGS_ZF;
		else
			cpu->cd.x86.eflags &= ~X86_EFLAGS_ZF;

		switch (op & 7) {
		case 0:	cpu->cd.x86.eax = tmp; break;
		case 1:	cpu->cd.x86.ecx = tmp; break;
		case 2:	cpu->cd.x86.edx = tmp; break;
		case 3:	cpu->cd.x86.ebx = tmp; break;
		case 4:	cpu->cd.x86.esp = tmp; break;
		case 5:	cpu->cd.x86.ebp = tmp; break;
		case 6:	cpu->cd.x86.esi = tmp; break;
		case 7:	cpu->cd.x86.edi = tmp; break;
		}
		break;

	case 0x06:
	case 0x0e:
	case 0x16:
	case 0x1e:
	case 0x50:
	case 0x51:
	case 0x52:
	case 0x53:
	case 0x54:
	case 0x55:
	case 0x56:
	case 0x57:
	case 0x9c:
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
		case 0x50:	value = cpu->cd.x86.eax; break;
		case 0x51:	value = cpu->cd.x86.edx; break;
		case 0x52:	value = cpu->cd.x86.ecx; break;
		case 0x53:	value = cpu->cd.x86.ebx; break;
		case 0x54:	value = cpu->cd.x86.esp; break;
		case 0x55:	value = cpu->cd.x86.ebp; break;
		case 0x56:	value = cpu->cd.x86.esi; break;
		case 0x57:	value = cpu->cd.x86.edi; break;
		case 0x9c:	value = cpu->cd.x86.eflags; break;
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
	case 0x9d:
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
				case 0x9d: cpu->cd.x86.eflags &= ~0xffff;
					   cpu->cd.x86.eflags |= tmp; break;
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
				case 0x9d: cpu->cd.x86.eflags = tmp; break;
				}
			}
		}
		cpu->cd.x86.esp += len;
		break;

	case 0x68:
		/*  push $imm  */
		cpu->cd.x86.cursegment = cpu->cd.x86.ss;
		instr ++;
		imm = read_imm(instr, &newpc, mode);
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
	case 0x7c:	/*  jl  */
	case 0x7d:	/*  jge  */
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
		case 0x7c:
			r = cpu->cd.x86.eflags & X86_EFLAGS_SF? 1 : 0;
			r ^= (cpu->cd.x86.eflags & X86_EFLAGS_OF? 1 : 0);
			if (r)
				newpc = newpc + imm;
			break;
		case 0x7d:
			r = cpu->cd.x86.eflags & X86_EFLAGS_SF? 1 : 0;
			r ^= (cpu->cd.x86.eflags & X86_EFLAGS_OF? 1 : 0);
			if (!r)
				newpc = newpc + imm;
			break;
		}
		if (mode == 16)
			newpc &= 0xffff;
		break;

	case 0x80:
		instr ++;
		newpc ++;
		switch (instr[0]) {
		case 0x39:		/*  cmpb $imm,(%ecx)  */
			instr ++;
			imm = (signed char)read_imm(instr, &newpc, 8);
			if (x86_load(cpu, cpu->cd.x86.ecx, &tmp, 1)
			    != MEMORY_ACCESS_OK)
				return 0;
			x86_cmp(cpu, tmp, imm);
			break;
		case 0x3d:		/*  cmpb $imm,imm2  */
			instr ++;
			imm2 = read_imm(instr, &newpc, mode);
			instr ++;
			imm = (signed char)read_imm(instr, &newpc, 8);
			if (x86_load(cpu, imm2, &tmp, 1) != MEMORY_ACCESS_OK)
				return 0;
			x86_cmp(cpu, tmp, imm);
			break;
		case 0xe2:		/*  and $imm,%dl  */
			instr ++;
			imm = read_imm(instr, &newpc, 8);
			cpu->cd.x86.edx = (cpu->cd.x86.edx & ~0xff)
			    | (cpu->cd.x86.edx & imm);
			break;
		case 0xf9:		/*  cmp $imm,%cl  */
			instr ++;
			imm = read_imm(instr, &newpc, 8);
			x86_cmp(cpu, cpu->cd.x86.ecx & 0xff, imm);
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
			imm = (signed char)read_imm(instr, &newpc, 8);
			instr ++;
			imm2 = read_imm(instr, &newpc, mode);
			/*  andl $imm2,imm(%ebp)  */
			if (x86_load(cpu, cpu->cd.x86.ebp + imm,
			    &tmp, mode / 8) != MEMORY_ACCESS_OK)
				return 0;
			if (x86_store(cpu, cpu->cd.x86.ebp + imm,
			    tmp & imm2, mode / 8) != MEMORY_ACCESS_OK)
				return 0;
			break;
		case 0xc1:
		case 0xc2:
		case 0xc3:
		case 0xc4:
		case 0xc5:
		case 0xc6:
		case 0xc7:
			instr ++;
			imm = read_imm(instr, &newpc, mode);
			if (mode == 16) {
				switch (instr[0]) {
				case 0xc1:  cpu->cd.x86.ecx = (cpu->cd.x86.ecx
				    & ~0xffff) | ((cpu->cd.x86.ecx + imm) &
				    0xffff); break;
				case 0xc2:  cpu->cd.x86.edx = (cpu->cd.x86.edx
				    & ~0xffff) | ((cpu->cd.x86.edx + imm) &
				    0xffff); break;
				case 0xc3:  cpu->cd.x86.ebx = (cpu->cd.x86.ebx
				    & ~0xffff) | ((cpu->cd.x86.ebx + imm) &
				    0xffff); break;
				case 0xc4:  cpu->cd.x86.esp = (cpu->cd.x86.esp
				    & ~0xffff) | ((cpu->cd.x86.esp + imm) &
				    0xffff); break;
				case 0xc5:  cpu->cd.x86.ebp = (cpu->cd.x86.ebp
				    & ~0xffff) | ((cpu->cd.x86.ebp + imm) &
				    0xffff); break;
				case 0xc6:  cpu->cd.x86.esi = (cpu->cd.x86.esi
				    & ~0xffff) | ((cpu->cd.x86.esi + imm) &
				    0xffff); break;
				case 0xc7:  cpu->cd.x86.edi = (cpu->cd.x86.edi
				    & ~0xffff) | ((cpu->cd.x86.edi + imm) &
				    0xffff); break;
				}
			} else {
				switch (instr[0]) {
				case 0xc1: cpu->cd.x86.ecx += imm; break;
				case 0xc2: cpu->cd.x86.edx += imm; break;
				case 0xc3: cpu->cd.x86.ebx += imm; break;
				case 0xc4: cpu->cd.x86.esp += imm; break;
				case 0xc5: cpu->cd.x86.ebp += imm; break;
				case 0xc6: cpu->cd.x86.esi += imm; break;
				case 0xc7: cpu->cd.x86.edi += imm; break;
				}
			}
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
			imm = read_imm(instr, &newpc, mode);
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
			imm = read_imm(instr, &newpc, mode);
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
		switch ((op = instr[0])) {
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
		case 0xc7:
			instr ++;
			imm = (signed char)instr[0];
			newpc ++;
			switch (op) {
			case 0xc4:	if (mode == 16) {
						cpu->cd.x86.esp = (~0xffff
						    & cpu->cd.x86.esp) +
						    (cpu->cd.x86.esp + imm)
						    & 0xffff;
					} else
						cpu->cd.x86.esp += imm;
					break;
			case 0xc7:	if (mode == 16) {
						cpu->cd.x86.edi = (~0xffff
						    & cpu->cd.x86.edi) +
						    (cpu->cd.x86.edi + imm)
						    & 0xffff;
					} else
						cpu->cd.x86.edi += imm;
					break;
			}
			break;
		case 0xec:
			instr ++;
			imm = (signed char)read_imm(instr, &newpc, 8);
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
		case 0xd2:
			/*  test %dl,%dl  */
			x86_test(cpu, cpu->cd.x86.edx & 0xff,
			    cpu->cd.x86.edx & 0xff);
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
			    cpu->cd.x86.eax, 1) != MEMORY_ACCESS_OK)
				return 0;
			break;
		case 0x26:	/*  mov %ah,imm  */
			instr ++;
			if (mode == 16) {
				imm = instr[0] + (instr[1] << 8);
				newpc += 2;
			} else {
				imm = instr[0] + (instr[1] << 8) +
				    (instr[2] << 16) + (instr[3] << 24);
				newpc += 4;
			}
			if (x86_store(cpu, imm,
			    cpu->cd.x86.eax >> 8, 1) != MEMORY_ACCESS_OK)
				return 0;
			break;
		default:
			fatal("x86_cpu_run_instr(): unimplemented subopcode: "
			    "0x88,0x%02x at pc=0x%016llx\n", instr[0],
			    (long long)cpu->pc);
			cpu->running = 0;
			return 0;
		}
		break;

	case 0x89:
		instr ++;
		newpc ++;
		len = sizeof(uint32_t);
		if (mode == 16)
			len = sizeof(uint16_t);
		switch (instr[0]) {
		case 0x02:
			/*  mov %eax,(%edx)  */
			if (x86_store(cpu, cpu->cd.x86.edx, cpu->cd.x86.eax,
			    len) != MEMORY_ACCESS_OK)
				return 0;
			break;
		case 0x15:
			/*  mov %edx,(%edi)  */
			if (x86_store(cpu, cpu->cd.x86.edi, cpu->cd.x86.edx,
			    len) != MEMORY_ACCESS_OK)
				return 0;
			break;
		case 0x55:
			/*  mov %edx,imm(%edi)  */
			instr ++;
			imm = (signed char)instr[0];
			newpc ++;
			if (x86_store(cpu, cpu->cd.x86.edi + imm,
			    cpu->cd.x86.edx, len) != MEMORY_ACCESS_OK)
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
		case 0xe6:
			/*  mov %esp,%esi  */
			cpu->cd.x86.esi = cpu->cd.x86.esp;
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
		case 0x0d:
			/*  mov (%edi),%cl  */
			if (x86_load(cpu, cpu->cd.x86.edi, &tmp, 1)
			    != MEMORY_ACCESS_OK)
				return 0;
			cpu->cd.x86.ecx = (cpu->cd.x86.ecx & ~0xff) | tmp;
			break;
		case 0x84:
			/*  mov imm(%esi),%al  */
			imm = read_imm(instr, &newpc, mode);
			if (x86_load(cpu, cpu->cd.x86.esi + imm, &tmp, 1)
			    != MEMORY_ACCESS_OK)
				return 0;
			cpu->cd.x86.eax = (cpu->cd.x86.eax & ~0xff) | tmp;
			break;
		default:
			fatal("x86_cpu_run_instr(): unimplemented subopcode: "
			    "0x8a,0x%02x at pc=0x%016llx\n", instr[0],
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
			imm = (signed char)read_imm(instr, &newpc, 8);
			if (x86_load(cpu, cpu->cd.x86.ebp + imm,
			    &tmp, sizeof(uint32_t)) != MEMORY_ACCESS_OK) {
				return 0;
			}
			switch (instr[0]) {
			case 0x4d: cpu->cd.x86.ecx = tmp; break;
			case 0x55: cpu->cd.x86.edx = tmp; break;
			}
			break;
		case 0xd0:
			if (mode == 16)
				cpu->cd.x86.edx = (cpu->cd.x86.edx & ~0xffff)
				    | (cpu->cd.x86.eax & 0xffff);
			else
				cpu->cd.x86.edx = cpu->cd.x86.eax;
			break;
		case 0xf1:
			if (mode == 16)
				cpu->cd.x86.esi = (cpu->cd.x86.esi & ~0xffff)
				    | (cpu->cd.x86.ecx & 0xffff);
			else
				cpu->cd.x86.esi = cpu->cd.x86.ecx;
			break;
		case 0xf8:
			if (mode == 16)
				cpu->cd.x86.edi = (cpu->cd.x86.edi & ~0xffff)
				    | (cpu->cd.x86.eax & 0xffff);
			else
				cpu->cd.x86.edi = cpu->cd.x86.eax;
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
			imm = (signed char)read_imm(instr, &newpc, 8);
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
		case 0xc0:
			cpu->cd.x86.es = cpu->cd.x86.eax & 0xffff;
			break;
		case 0xc1:
			cpu->cd.x86.es = cpu->cd.x86.ecx & 0xffff;
			break;
		case 0xd0:
			cpu->cd.x86.ss = cpu->cd.x86.eax & 0xffff;
			break;
		case 0xd1:
			cpu->cd.x86.ss = cpu->cd.x86.ecx & 0xffff;
			break;
		case 0xd8:
			cpu->cd.x86.ds = cpu->cd.x86.eax & 0xffff;
			break;
		case 0xd9:
			cpu->cd.x86.ds = cpu->cd.x86.ecx & 0xffff;
			break;
		case 0xe0:
			cpu->cd.x86.fs = cpu->cd.x86.eax & 0xffff;
			break;
		case 0xe8:
			cpu->cd.x86.gs = cpu->cd.x86.eax & 0xffff;
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

	case 0xa0:	/*  mov imm,%al  */
	case 0xa1:	/*  mov imm,%ax  */
	case 0xa2:	/*  mov %al,imm  */
	case 0xa3:	/*  mov %ax,imm  */
		instr ++;
		imm = read_imm(instr, &newpc, mode);
		len = 1;
		if (op & 1)
			len = mode / 8;
		if (op < 0xa2)
			if (x86_load(cpu, imm, &tmp, len) != MEMORY_ACCESS_OK)
				return 0;
		switch (op) {
		case 0xa0:
			cpu->cd.x86.eax = (cpu->cd.x86.eax & ~0xff) | tmp;
			break;
		case 0xa1:
			if (mode == 16) {
				cpu->cd.x86.eax = (cpu->cd.x86.eax & ~0xffff)
				    | tmp;
			} else
				cpu->cd.x86.eax = tmp;
			break;
		case 0xa2:
		case 0xa3:
			tmp = cpu->cd.x86.eax;
			break;
		}
		if (op >= 0xa2)
			if (x86_store(cpu, imm, tmp, len) != MEMORY_ACCESS_OK)
				return 0;
		break;

	case 0xaa:
	case 0xab:
		/*  stosb etc. always uses es:[e]di as destination  */
		cpu->cd.x86.cursegment = cpu->cd.x86.es;
		len = 1;	/*  stosb  */
		if (op == 0xab)
			len = 2;
		for (;;) {
			if (x86_store(cpu, cpu->cd.x86.edi,
			    cpu->cd.x86.eax, len) != MEMORY_ACCESS_OK)
				return 0;

			if (cpu->cd.x86.eflags & X86_EFLAGS_DF)
				cpu->cd.x86.edi -= len;
			else
				cpu->cd.x86.edi += len;

			if (!rep)
				break;
			else {
				uint32_t count = cpu->cd.x86.ecx;
				if (mode == 16)
					count &= 0xffff;
				if (count == 0) {
					fatal("rep with count 0: TODO\n");
					exit(1);
				}
				count --;
				if (mode == 16)
					cpu->cd.x86.ecx = (cpu->cd.x86.ecx
					    & ~0xffff) | (count & 0xffff);
				else
					cpu->cd.x86.ecx = count;
				if (count == 0)
					break;
				/*  TODO: also break on repnz or repz
					if the zero flag is [not] set  */
			}
		}
		break;

	case 0xb0:
	case 0xb1:
	case 0xb5:
		/*  mov $imm,%al etc  */
		instr ++;
		imm = instr[0];
		newpc ++;
		switch (op) {
		case 0xb0:
			cpu->cd.x86.eax = (cpu->cd.x86.eax & ~0xff) | imm;
			break;
		case 0xb1:
			cpu->cd.x86.ecx = (cpu->cd.x86.ecx & ~0xff) | imm;
			break;
		case 0xb5:
			cpu->cd.x86.ecx = (cpu->cd.x86.ecx & ~0xff00) |
			    (imm << 8);
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
		imm = read_imm(instr, &newpc, mode);
		if (mode == 16) {
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
			imm = (signed char)read_imm(instr, &newpc, 8);
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
		    &tmp, mode / 8) != MEMORY_ACCESS_OK) {
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
			imm = read_imm(instr, &newpc, 8);
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
			imm = (signed char)read_imm(instr, &newpc, 8);
			instr ++;
			imm2 = read_imm(instr, &newpc, mode);
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

	case 0xcd:
		instr ++;
		imm = read_imm(instr, &newpc, 8);
		if (mode == 16) {
			cpu->pc = newpc;
			x86_interrupt(cpu, imm);
			return 1;
		} else {
			fatal("x86 'int' only implemented for 16-bit so far\n");
			exit(1);
		}
		break;

	case 0xd0:
		instr ++;
		newpc ++;
		switch (instr[0]) {
		case 0xe9:
			cpu->cd.x86.ecx = (cpu->cd.x86.ecx &
			    ~0xff) | ((cpu->cd.x86.ecx & 0xff) >> 1);
			break;
		default:
			fatal("x86_cpu_run_instr(): unimplemented subopcode: "
			    "0xd0,0x%02x at pc=0x%016llx\n", instr[0],
			    (long long)cpu->pc);
			cpu->running = 0;
			return 0;
		}
		break;

	case 0xd1:
		instr ++;
		newpc ++;
		switch (instr[0]) {
		case 0xe9:
			if (mode == 32)
				cpu->cd.x86.ecx >>= 1;
			else {
				cpu->cd.x86.ecx = (cpu->cd.x86.ecx &
				    ~0xffff) | ((cpu->cd.x86.ecx & 0xffff)
				    >> 1);
			}
			break;
		default:
			fatal("x86_cpu_run_instr(): unimplemented subopcode: "
			    "0xd1,0x%02x at pc=0x%016llx\n", instr[0],
			    (long long)cpu->pc);
			cpu->running = 0;
			return 0;
		}
		break;

	case 0xd2:
		instr ++;
		newpc ++;
		switch (instr[0]) {
		case 0xe8:
			/*  shr %cl, %al  */
			r = cpu->cd.x86.ecx & 31;
			while (r-- > 0)
				cpu->cd.x86.eax = (cpu->cd.x86.eax &
				    ~0xff) | ((cpu->cd.x86.eax & 0xff) >> 1);
			break;
		default:
			fatal("x86_cpu_run_instr(): unimplemented subopcode: "
			    "0xd2,0x%02x at pc=0x%016llx\n", instr[0],
			    (long long)cpu->pc);
			cpu->running = 0;
			return 0;
		}
		break;

	case 0xd3:
		instr ++;
		newpc ++;
		switch (instr[0]) {
		case 0xe7:
			/*  shr %cl, %di  */
			r = cpu->cd.x86.ecx & 31;
			while (r-- > 0) {
				if (mode == 16)
					cpu->cd.x86.edi = (cpu->cd.x86.edi &
					    ~0xffff) | ((cpu->cd.x86.edi &
					    0xffff) >> 1);
				else
					cpu->cd.x86.edi >>= 1;
			}
			break;
		case 0xe8:
			/*  shr %cl, %ax  */
			r = cpu->cd.x86.ecx & 31;
			while (r-- > 0) {
				if (mode == 16)
					cpu->cd.x86.eax = (cpu->cd.x86.eax &
					    ~0xffff) | ((cpu->cd.x86.eax &
					    0xffff) >> 1);
				else
					cpu->cd.x86.eax >>= 1;
			}
			break;
		default:
			fatal("x86_cpu_run_instr(): unimplemented subopcode: "
			    "0xd3,0x%02x at pc=0x%016llx\n", instr[0],
			    (long long)cpu->pc);
			cpu->running = 0;
			return 0;
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
			if (mode == 16) {
				imm = instr[0] + (instr[1] << 8);
				newpc += 2;
			} else {
				imm = instr[0] + (instr[1] << 8) +
				    (instr[2] << 16) + (instr[3] << 24);
				newpc += 4;
			}
		}
		/*  For a "call", push the return address:  */
		if (op == 0xe8) {
			cpu->cd.x86.cursegment = cpu->cd.x86.ss;
			if (x86_store(cpu, cpu->cd.x86.esp - (mode / 8),
			    newpc, mode / 8) != MEMORY_ACCESS_OK)
				return 0;
			cpu->cd.x86.esp -= (mode / 8);
		}
		newpc = newpc + imm;
		if (mode == 16)
			newpc &= 0xffff;
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

	case 0xf6:
		instr ++;
		newpc ++;
		switch (instr[0]) {
		case 0xe2:
			/*  mul dl  */
			tmp = cpu->cd.x86.edx & 0xff;
			tmp *= (cpu->cd.x86.eax & 0xff);
			cpu->cd.x86.eax = (cpu->cd.x86.eax &
			    ~0xffff) | (tmp & 0xffff);
			break;
		default:
			fatal("x86_cpu_run_instr(): unimplemented subopcode: "
			    "0xf6,0x%02x at pc=0x%016llx\n", instr[0],
			    (long long)cpu->pc);
			cpu->running = 0;
			return 0;
		}
		break;

	case 0xf7:
		instr ++;
		newpc ++;
		switch (instr[0]) {
		case 0x26:
			instr ++;
			if (mode == 32) {
				imm = instr[0] + (instr[1] << 8) +
				    (instr[2] << 16) + (instr[3] << 24);
				newpc += 4;
			} else {
				imm = instr[0] + (instr[1] << 8);
				newpc += 2;
			}
			/*  mul [d]word at address imm  */
			if (x86_load(cpu, imm,
			    &tmp, sizeof(uint32_t)) != MEMORY_ACCESS_OK)
				return 0;
			if (mode == 32)
				cpu->cd.x86.eax *= tmp;
			else {
				tmp *= (cpu->cd.x86.eax & 0xffff);
				cpu->cd.x86.eax = (cpu->cd.x86.eax &
				    ~0xffff) | (tmp & 0xffff);
			}
			break;
		default:
			fatal("x86_cpu_run_instr(): unimplemented subopcode: "
			    "0xf7,0x%02x at pc=0x%016llx\n", instr[0],
			    (long long)cpu->pc);
			cpu->running = 0;
			return 0;
		}
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

	case 0xfe:
		instr ++;
		newpc ++;
		switch (instr[0]) {
		case 0xc4:	/*  inc %%ah  */
			cpu->cd.x86.eax = (cpu->cd.x86.eax & ~0xff00)
			    | ((cpu->cd.x86.eax + 0x100) & ~0xff00);
			break;
		case 0xc5:	/*  inc %%ch  */
			cpu->cd.x86.ecx = (cpu->cd.x86.ecx & ~0xff00)
			    | ((cpu->cd.x86.ecx + 0x100) & ~0xff00);
			break;
		default:
			fatal("x86_cpu_run_instr(): unimplemented subopcode: "
			    "0xfe,0x%02x at pc=0x%016llx\n", instr[0],
			    (long long)cpu->pc);
			cpu->running = 0;
			return 0;
		}
		break;

	default:
		fatal("x86_cpu_run_instr(): unimplemented opcode 0x%02x"
		    " at pc=0x%016llx\n", instr[0], (long long)cpu->pc);
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
