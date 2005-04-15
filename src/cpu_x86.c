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
 *  $Id: cpu_x86.c,v 1.3 2005-04-15 01:30:56 debug Exp $
 *
 *  x86 (and amd64) CPU emulation.
 *
 *  TODO: This is just a dummy so far.
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

	cpu->cd.x86.bits = 32;
	if (strcasecmp(cpu_type_name, "amd64") == 0)
		cpu->cd.x86.bits = 64;

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
	debug(" (%i bits)\n", cpu->cd.x86.bits);

	/*  TODO  */
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
 *
 *  gprs: set to non-zero to dump GPRs and some special-purpose registers.
 *  coprocs: set bit 0..3 to dump registers in coproc 0..3.
 */
void x86_cpu_register_dump(struct cpu *cpu, int gprs, int coprocs)
{
	char *symbol;
	uint64_t offset, tmp;
	int i, x = cpu->cpu_id;
	int bits32 = cpu->cd.x86.bits == 32;

	if (gprs) {
		symbol = get_symbol_name(&cpu->machine->symbol_context,
		    cpu->pc, &offset);

		debug("cpu%i:  eip=0x", x);
		if (bits32)
		        debug("%08x", (int)cpu->pc);
		else
		        debug("%016llx", (long long)cpu->pc);
		debug("  <%s>\n", symbol != NULL? symbol : " no symbol ");

		debug("cpu%i:  eax=0x%08x  ebx=0x%08x  ecx=0x%08x  edx="
		    "0x%08x\n", x, (int)cpu->cd.x86.eax, (int)cpu->cd.x86.ebx,
		    (int)cpu->cd.x86.ecx, (int)cpu->cd.x86.edx);
		debug("cpu%i:  esi=0x%08x  edi=0x%08x  ebp=0x%08x  esp="
		    "0x%08x\n", x, (int)cpu->cd.x86.esi, (int)cpu->cd.x86.edi,
		    (int)cpu->cd.x86.ebp, (int)cpu->cd.x86.esp);

		debug("cpu%i:  eflags=0x%08x\n", x, (int)cpu->cd.x86.eflags);
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
	if (strcasecmp(name, "pc") == 0) {
		if (writeflag) {
			m->cpus[cpunr]->pc = *valuep;
		} else
			*valuep = m->cpus[cpunr]->pc;
		*match_register = 1;
	}

	/*  TODO: _LOTS_ of stuff.  */
}


#define	HEXPRINT(x,n) { int j; for (j=0; j<(n); j++) debug("%02x",(x)[j]); }
#define	HEXSPACES(i) { int j; for (j=0; j<10-(i);j++) debug("  "); debug(" "); }


static char *reg4[4] = { "ax", "cx", "dx", "bx" };


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
	int instr_len = 0, op;
	uint64_t offset, addr;
	uint32_t imm, imm2, mode32 = 1;
	char *symbol, *tmp, *mnem = "ERROR";

	if (running)
		dumpaddr = cpu->pc;

	symbol = get_symbol_name(&cpu->machine->symbol_context,
	    dumpaddr, &offset);
	if (symbol != NULL && offset==0)
		debug("<%s>\n", symbol);

	if (cpu->machine->ncpus > 1 && running)
		debug("cpu%i: ", cpu->cpu_id);

	if (cpu->cd.x86.bits == 32)
		debug("%08x:  ", (int)dumpaddr);
	else
		debug("%016llx:  ", (long long)dumpaddr);

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
	while (instr[0] == 0x66) {
		switch (instr[0]) {
		case 0x66:
			mode32 ^= 1;
			instr ++;
			instr_len ++;
			HEXPRINT(instr,1);
		}
		/*  TODO: rep, lock etc  */
	}

	switch ((op = instr[0])) {

	case 0x25:
		instr ++;
		imm = instr[0] + (instr[1] << 8) + (instr[2] << 16)
		    + (instr[3] << 24);
		HEXPRINT(instr,4);
		instr_len += 4;
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
			debug("xor\t%%eax,%%eax");
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

	case 0x73:
	case 0x75:
		instr ++;
		imm = (signed char) instr[0];
		HEXPRINT(instr,1);
		instr_len += 1;
		HEXSPACES(instr_len);
		switch (op) {
		case 0x73: mnem = "jae"; break;
		case 0x75: mnem = "jne"; break;
		}
		debug("%s\t$0x%x", mnem, (uint32_t)(imm + dumpaddr + 2));
		/*  TODO: symbol  */
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

	case 0x89:
		instr++;
		instr_len++;
		HEXPRINT(instr,1);
		switch (instr[0]) {
		case 0x02:
			HEXSPACES(instr_len);
			debug("mov\t%%eax,(%%edx)");
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

	/*  A couple of single-byte opcodes:  */
	case 0x53:
	case 0x55:
	case 0x56:
	case 0x57:
	case 0x90:
	case 0xc3:
	case 0xc9:
	case 0xee:
		HEXSPACES(instr_len);
		switch (op) {
		case 0x53:  debug("push\t%%ebx"); break;
		case 0x55:  debug("push\t%%ebp"); break;
		case 0x56:  debug("push\t%%esi"); break;
		case 0x57:  debug("push\t%%edi"); break;
		case 0x90:  debug("nop"); break;
		case 0xc3:  debug("ret"); break;
		case 0xc9:  debug("leave"); break;
		case 0xee:  debug("out\t%%al,(%%dx)"); break;
		}
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
	case 0xbe:
		instr ++;
		imm = instr[0] + (instr[1] << 8) + (instr[2] << 16)
		    + (instr[3] << 24);
		HEXPRINT(instr,4);
		instr_len += 4;
		HEXSPACES(instr_len);
		switch (op) {
		case 0xb8: tmp = "eax"; break;
		case 0xb9: tmp = "ecx"; break;
		case 0xba: tmp = "edx"; break;
		case 0xbb: tmp = "ebx"; break;
		case 0xbe: tmp = "esi"; break;
		}
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

	case 0xe8:	/*  call (32 bits)  */
	case 0xe9:	/*  jmp (32 bits)  */
	case 0xeb:	/*  jmp (8 bits)  */
		instr ++;
		switch (op) {
		case 0xe8:
		case 0xe9:
			imm = instr[0] + (instr[1] << 8) + (instr[2] << 16)
			    + (instr[3] << 24);
			imm += dumpaddr + 5;
			HEXPRINT(instr,4);
			instr_len += 4;
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

	default:
		HEXSPACES(instr_len);
		debug("UNIMPLEMENTED");
	}

disasm_ret:
	debug("\n");
disasm_ret_nonewline:
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
	int i, r, op;
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
	r = cpu->memory_rw(cpu, cpu->mem, cpu->pc, &buf[0], sizeof(buf),
	    MEM_READ, CACHE_INSTRUCTION);
	if (!r)
		return 0;

	if (cpu->machine->instruction_trace)
		x86_cpu_disassemble_instr(cpu, instr, 1, 0, 0);

	/*  All instructions are at least one byte long :-)  */
	newpc ++;

	/*  Any prefix?  */
	while (instr[0] == 0x66) {
		/*  rep, lock etc  */
		fatal("prefix TODO\n");
		exit(1);
	}

	switch ((op = instr[0])) {

	case 0x25:
		/*  and $imm,%eax etc  */
		instr ++;
		imm = instr[0] + (instr[1] << 8) + (instr[2] << 16)
		    + (instr[3] << 24);
		newpc += 4;
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
			cpu->cd.x86.eax = 0;
			break;
		default:
			fatal("x86_cpu_run_instr(): unimplemented subopcode: "
			    "0x31,0x%02x at pc=0x%016llx\n", instr[0],
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

	case 0x53:
	case 0x55:
	case 0x56:
	case 0x57:
		/*  push  */
		value = 0;
		switch (op) {
		case 0x53:	value = cpu->cd.x86.ebx; break;
		case 0x55:	value = cpu->cd.x86.ebp; break;
		case 0x56:	value = cpu->cd.x86.esi; break;
		case 0x57:	value = cpu->cd.x86.edi; break;
		}
		if (x86_store(cpu, cpu->cd.x86.esp - sizeof(uint32_t),
		    value, sizeof(uint32_t)) != MEMORY_ACCESS_OK)
			return 0;
		cpu->cd.x86.esp -= sizeof(uint32_t);
		break;

	case 0x68:
		/*  push $imm  */
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
	case 0x75:	/*  jne  */
		instr ++;
		imm = (signed char) instr[0];
		newpc += 1;
		switch (op) {
		case 0x73:
			if (!(cpu->cd.x86.eflags & X86_EFLAGS_CF))
				newpc = newpc + imm;
			break;
		case 0x75:
			if (!(cpu->cd.x86.eflags & X86_EFLAGS_ZF))
				newpc = newpc + imm;
			break;
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

	case 0x90:
		/*  NOP  */
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
	case 0xbe:
		/*  mov $imm,%eax etc  */
		instr ++;
		imm = instr[0] + (instr[1] << 8) + (instr[2] << 16)
		    + (instr[3] << 24);
		newpc += 4;
		switch (op) {
		case 0xb8: cpu->cd.x86.eax = imm; break;
		case 0xb9: cpu->cd.x86.ecx = imm; break;
		case 0xba: cpu->cd.x86.edx = imm; break;
		case 0xbb: cpu->cd.x86.ebx = imm; break;
		case 0xbe: cpu->cd.x86.ebx = imm; break;
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
		if (x86_load(cpu, cpu->cd.x86.esp,
		    &tmp, sizeof(uint32_t)) != MEMORY_ACCESS_OK) {
			return 0;
		}
		cpu->cd.x86.esp += sizeof(uint32_t);
		newpc = tmp;
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
		cpu->cd.x86.esp = cpu->cd.x86.ebp;
		if (x86_load(cpu, cpu->cd.x86.esp,
		    &tmp, sizeof(uint32_t)) != MEMORY_ACCESS_OK) {
			return 0;
		}
		cpu->cd.x86.esp += sizeof(uint32_t);
		cpu->cd.x86.ebp = tmp;
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
			if (x86_store(cpu, cpu->cd.x86.esp - sizeof(uint32_t),
			    newpc, sizeof(uint32_t))
			    != MEMORY_ACCESS_OK)
				return 0;
			cpu->cd.x86.esp -= sizeof(uint32_t);
		}
		newpc = newpc + imm;
		break;

	case 0xee:
		/*  out %al,(%dx)  */
		databuf[0] = cpu->cd.x86.eax & 0xff;
		cpu->memory_rw(cpu, cpu->mem, (cpu->cd.x86.edx & 0xffff)
		    + 0x100000000ULL, &databuf[0], 1, MEM_WRITE, CACHE_NONE);
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
