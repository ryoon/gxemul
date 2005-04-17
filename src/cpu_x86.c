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
 *  $Id: cpu_x86.c,v 1.20 2005-04-17 00:15:24 debug Exp $
 *
 *  x86 (and amd64) CPU emulation.
 *
 *
 *  TODO:  Pretty much everything.
 *
 *  See http://www.amd.com/us-en/Processors/DevelopWithAMD/
 *	0,,30_2252_875_7044,00.html for more info on AMD25964.
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


static struct x86_model models[] = x86_models;
static char *reg_names[N_X86_REGS] = x86_reg_names;


/*
 *  x86_cpu_new():
 *
 *  Create a new x86 cpu object.
 */
struct cpu *x86_cpu_new(struct memory *mem, struct machine *machine,
	int cpu_id, char *cpu_type_name)
{
	int i = 0;
	struct cpu *cpu;

	if (cpu_type_name == NULL)
		return NULL;

	/*  Try to find a match:  */
	while (models[i].model_number != 0) {
		if (strcasecmp(cpu_type_name, models[i].name) == 0)
			break;
		i++;
	}

	if (models[i].name == NULL)
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

	cpu->cd.x86.model = models[i];
	cpu->cd.x86.mode = 32;
	cpu->cd.x86.bits = 32;

	if (strcasecmp(cpu_type_name, "amd64") == 0) {
		/*  TODO: boot in 64-bit mode?  */
		cpu->cd.x86.bits = 64;
	}

	cpu->cd.x86.r[R_SP] = 0xff0;

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
	debug(" (%i-bit)", cpu->cd.x86.bits);
	debug(", currently in %i-bit mode", cpu->cd.x86.mode);
	debug("\n");
}


/*
 *  x86_cpu_list_available_types():
 *
 *  Print a list of available x86 CPU types.
 */
void x86_cpu_list_available_types(void)
{
	int i = 0, j;

	while (models[i].model_number != 0) {
		debug("%s", models[i].name);

		for (j=0; j<10-strlen(models[i].name); j++)
			debug(" ");
		i++;
		if ((i % 6) == 0 || models[i].name == NULL)
			debug("\n");
	}
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
	int i, x = cpu->cpu_id;

	if (cpu->cd.x86.mode == 16) {
		debug("cpu%i:  cs:ip = 0x%04x:0x%04x\n", x,
		    cpu->cd.x86.s[S_CS], (int)cpu->pc);

		debug("cpu%i:  ax = 0x%04x  bx = 0x%04x  cx = 0x%04x  dx = "
		    "0x%04x\n", x,
		    (int)cpu->cd.x86.r[R_AX], (int)cpu->cd.x86.r[R_BX],
		    (int)cpu->cd.x86.r[R_CX], (int)cpu->cd.x86.r[R_DX]);
		debug("cpu%i:  si = 0x%04x  di = 0x%04x  bp = 0x%04x  sp = "
		    "0x%04x\n", x,
		    (int)cpu->cd.x86.r[R_SI], (int)cpu->cd.x86.r[R_DI],
		    (int)cpu->cd.x86.r[R_BP], (int)cpu->cd.x86.r[R_SP]);

		debug("cpu%i:  ds = 0x%04x  es = 0x%04x  ss = 0x%04x  flags "
		    "= 0x%04x\n", x,
		    (int)cpu->cd.x86.s[S_DS], (int)cpu->cd.x86.s[S_ES],
		    (int)cpu->cd.x86.s[S_SS], (int)cpu->cd.x86.eflags);
	} else if (cpu->cd.x86.mode == 32) {
		symbol = get_symbol_name(&cpu->machine->symbol_context,
		    cpu->pc, &offset);

		debug("cpu%i:  eip=0x", x);
	        debug("%08x", (int)cpu->pc);
		debug("  <%s>\n", symbol != NULL? symbol : " no symbol ");

		debug("cpu%i:  eax=0x%08x  ebx=0x%08x  ecx=0x%08x  edx="
		    "0x%08x\n", x,
		    (int)cpu->cd.x86.r[R_AX], (int)cpu->cd.x86.r[R_BX],
		    (int)cpu->cd.x86.r[R_CX], (int)cpu->cd.x86.r[R_DX]);
		debug("cpu%i:  esi=0x%08x  edi=0x%08x  ebp=0x%08x  esp="
		    "0x%08x\n", x,
		    (int)cpu->cd.x86.r[R_SI], (int)cpu->cd.x86.r[R_DI],
		    (int)cpu->cd.x86.r[R_BP], (int)cpu->cd.x86.r[R_SP]);
	} else {
		/*  64-bit  */
		symbol = get_symbol_name(&cpu->machine->symbol_context,
		    cpu->pc, &offset);

		debug("cpu%i:  rip = 0x", x);
	        debug("%016llx", (long long)cpu->pc);
		debug("  <%s>\n", symbol != NULL? symbol : " no symbol ");

		for (i=0; i<N_X86_REGS; i++) {
			if ((i & 1) == 0)
				debug("cpu%i:", x);
			debug("  r%s = 0x%016llx", reg_names[i],
			    (long long)cpu->cd.x86.r[i]);
			if ((i & 1) == 1)
				debug("\n");
		}
	}

	if (cpu->cd.x86.mode >= 32) {
		debug("cpu%i:  cs=0x%04x  ds=0x%04x  es=0x%04x  "
		    "fs=0x%04x  gs=0x%04x  ss=0x%04x\n", x,
		    (int)cpu->cd.x86.s[S_CS], (int)cpu->cd.x86.s[S_DS],
		    (int)cpu->cd.x86.s[S_ES], (int)cpu->cd.x86.s[S_FS],
		    (int)cpu->cd.x86.s[S_GS], (int)cpu->cd.x86.s[S_SS]);

		debug("cpu%i:  cr0 = 0x%08x  cr3 = 0x%08x  eflags = 0x%08x\n",
		    x, (int)cpu->cd.x86.cr0,
		    (int)cpu->cd.x86.cr3, (int)cpu->cd.x86.eflags);
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

#if 0
TODO: regmatch for 64, 32, 16, and 8 bit register names
#endif
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
			debug("%04x:%04x  ", cpu->cd.x86.s[S_CS],
			    (int)dumpaddr);
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

	case 0x90:
		HEXSPACES(instr_len);
		switch (op) {
		case 0x90:  debug("nop"); break;
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
	cpu->cd.x86.cursegment = cpu->cd.x86.s[S_SS];
	if (x86_store(cpu, cpu->cd.x86.r[R_SP] - len * 1, cpu->cd.x86.eflags,
	    len) != MEMORY_ACCESS_OK)
		fatal("x86_interrupt(): TODO: how to handle this\n");
	if (x86_store(cpu, cpu->cd.x86.r[R_SP] - len * 2, cpu->cd.x86.s[S_CS],
	    len) != MEMORY_ACCESS_OK)
		fatal("x86_interrupt(): TODO: how to handle this\n");
	if (x86_store(cpu, cpu->cd.x86.r[R_SP] - len * 3, cpu->pc,
	    len) != MEMORY_ACCESS_OK)
		fatal("x86_interrupt(): TODO: how to handle this\n");

	cpu->cd.x86.r[R_SP] = (cpu->cd.x86.r[R_SP] & ~0xffff)
	    | ((cpu->cd.x86.r[R_SP] - len*3) & 0xffff);

	/*  TODO: clear the Interrupt Flag?  */

	cpu->cd.x86.s[S_CS] = seg;
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
	if (mode == 16 && ((newpc + (cpu->cd.x86.s[S_CS] << 4)) & 0xff000)
	    == 0xf8000 && cpu->machine->prom_emulation) {
		pc_bios_emul(cpu);
		return 1;
	}

	/*  Read an instruction from memory:  */
	cpu->cd.x86.cursegment = cpu->cd.x86.s[S_CS];

	r = cpu->memory_rw(cpu, cpu->mem, cpu->pc, &buf[0], sizeof(buf),
	    MEM_READ, CACHE_INSTRUCTION);
	if (!r)
		return 0;

	if (cpu->machine->instruction_trace)
		x86_cpu_disassemble_instr(cpu, instr, 1, 0, 0);

	/*  All instructions are at least one byte long :-)  */
	newpc ++;

	/*  Default is to use the data segment, or the stack segment:  */
	cpu->cd.x86.cursegment = cpu->cd.x86.s[S_DS];

	/*  Any prefix?  */
	while (instr[0] == 0x66 || instr[0] == 0x26 || instr[0] == 0x36
	    || instr[0] == 0x2e || instr[0] == 0x3e || instr[0] == 0xf3) {
		if (instr[0] == 0x26)
			cpu->cd.x86.cursegment = cpu->cd.x86.s[S_ES];
		if (instr[0] == 0x2e)
			cpu->cd.x86.cursegment = cpu->cd.x86.s[S_CS];
		if (instr[0] == 0x36)
			cpu->cd.x86.cursegment = cpu->cd.x86.s[S_SS];
		if (instr[0] == 0x3e)
			cpu->cd.x86.cursegment = cpu->cd.x86.s[S_DS];
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

	case 0x90:
		/*  NOP  */
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
