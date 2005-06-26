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
 *  $Id: cpu_hppa.c,v 1.5 2005-06-26 22:23:42 debug Exp $
 *
 *  HPPA CPU emulation.
 *
 *  TODO: This is just a dummy so far.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "misc.h"


#ifndef	ENABLE_HPPA


#include "cpu_hppa.h"


/*
 *  hppa_cpu_family_init():
 *
 *  Bogus, when ENABLE_HPPA isn't defined.
 */
int hppa_cpu_family_init(struct cpu_family *fp)
{
	return 0;
}


#else	/*  ENABLE_HPPA  */


#include "cpu.h"
#include "cpu_hppa.h"
#include "machine.h"
#include "memory.h"
#include "opcodes_hppa.h"
#include "symbol.h"


extern volatile int single_step;
extern int old_show_trace_tree;   
extern int old_instruction_trace;
extern int old_quiet_mode;
extern int quiet_mode;


/*
 *  hppa_cpu_new():
 *
 *  Create a new HPPA cpu object.
 *
 *  Return 1 on success, 0 if cpu_type_name didn't match a valid HPPA name.
 */
int hppa_cpu_new(struct cpu *cpu, struct memory *mem, struct machine *machine,
	int cpu_id, char *cpu_type_name)
{
	if (strcasecmp(cpu_type_name, "HPPA1.0") != 0 &&
	    strcasecmp(cpu_type_name, "HPPA1.1") != 0 &&
	    strcasecmp(cpu_type_name, "HPPA2.0") != 0)
		return 0;

	cpu->memory_rw  = hppa_memory_rw;
	cpu->byte_order = EMUL_BIG_ENDIAN;	/*  TODO  */

	cpu->cd.hppa.bits = 32;
	if (strcasecmp(cpu_type_name, "HPPA2.0") == 0)
		cpu->cd.hppa.bits = 64;

	/*  Only show name and caches etc for CPU nr 0 (in SMP machines):  */
	if (cpu_id == 0) {
		debug("%s", cpu->name);
	}

	return 1;
}


/*
 *  hppa_cpu_dumpinfo():
 */
void hppa_cpu_dumpinfo(struct cpu *cpu)
{
	debug(" (%i-bit)", cpu->cd.hppa.bits);

	debug("\n");
}


/*
 *  hppa_cpu_list_available_types():
 *
 *  Print a list of available HPPA CPU types.
 */
void hppa_cpu_list_available_types(void)
{
	debug("HPPA1.0   HPPA1.1   HPPA2.0\n");
}


/*
 *  hppa_cpu_register_match():
 */
void hppa_cpu_register_match(struct machine *m, char *name,
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


/*
 *  hppa_cpu_disassemble_instr():
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
int hppa_cpu_disassemble_instr(struct cpu *cpu, unsigned char *instr,
        int running, uint64_t dumpaddr, int bintrans)
{
	uint64_t offset;
	uint32_t iword;
	char *symbol;
	int hi6, imm, rr, rb;

	if (running)
		dumpaddr = cpu->pc;
 
	symbol = get_symbol_name(&cpu->machine->symbol_context,
	    dumpaddr, &offset);
	if (symbol != NULL && offset==0)
		debug("<%s>\n", symbol);
 
	if (cpu->machine->ncpus > 1 && running)
		debug("cpu%i: ", cpu->cpu_id);

	if (cpu->cd.hppa.bits == 32)
		debug("%08x", (int)dumpaddr);
	else
		debug("%016llx", (long long)dumpaddr);
 
	/*  NOTE: Fixed to big-endian.  */
	iword = (instr[0] << 24) + (instr[1] << 16) + (instr[2] << 8)
	    + instr[3];

	debug(": %08x\t", iword);

	if (bintrans && !running) {
		debug("(bintrans)");
		goto disasm_ret;
	}

	/*
	 *  Decode the instruction:
	 */

	hi6 = iword >> 26;

	switch (hi6) {
	case HPPA_LDIL:
		rr = (iword >> 21) & 31;
		imm = assemble_21(iword & 0x1fffff);
		imm <<= 11;
		debug("ldil\t0x%x,r%i", imm, rr);
		break;
	case HPPA_STW:
	case HPPA_STW_1B:
		rb = (iword >> 21) & 31;
		rr = (iword >> 16) & 31;

		/*  TODO:   hahahahaha, assemble_16 is really weird  */

		imm = (int16_t)(iword & 0xffff);
		debug("stw\tr%i,%i(r%i)", rr, imm, rb);
		break;
	default:
		debug("unimplemented hi6=%i", hi6);
	}

disasm_ret:
        debug("\n");
        return sizeof(iword);
}


/*
 *  hppa_cpu_run_instr(): 
 *
 *  Execute one instruction on a specific CPU.
 *
 *  Return value is the number of instructions executed during this call,
 *  0 if no instruction was executed.
 */
int hppa_cpu_run_instr(struct emul *emul, struct cpu *cpu)
{
	uint32_t iword;
	unsigned char buf[4];
	uint64_t cached_pc;
	int r, i, hi6, rt, imm;

	cached_pc = cpu->pc;

	/*  Check PC against breakpoints:  */
	if (!single_step)
		for (i=0; i<cpu->machine->n_breakpoints; i++)
			if (cached_pc == cpu->machine->breakpoint_addr[i]) {
				fatal("Breakpoint reached, pc=0x");
				if (cpu->cd.hppa.bits == 32)
					fatal("%08x", (int)cached_pc);
				else
					fatal("%016llx", (long long)cached_pc);
				fatal("\n");
				single_step = 1;
				return 0;
			}

	r = cpu->memory_rw(cpu, cpu->mem, cached_pc, &buf[0], sizeof(buf),
	    MEM_READ, CACHE_INSTRUCTION | PHYSICAL);
	if (!r)
		return 0;

	iword = (buf[0] << 24) + (buf[1] << 16) + (buf[2] << 8) + buf[3];

	if (cpu->machine->instruction_trace)
		hppa_cpu_disassemble_instr(cpu, buf, 1, 0, 0);

	cpu->cd.hppa.pc_last = cpu->pc;
	cpu->pc += sizeof(iword);

	hi6 = iword >> 26;

	switch (hi6) {
	case HPPA_LDIL:
		rt = (iword >> 21) & 31;
		imm = assemble_21(iword & 0x1fffff) << 1;
		cpu->cd.hppa.gr[rt] = (int64_t)(int32_t) imm;
		break;
	default:
		fatal("[ unimplemented HPPA hi6 = 0x%02x, pc = 0x%016llx ]\n",
		    hi6, (long long) (cpu->cd.hppa.pc_last));
		cpu->running = 0;
		return 0;
	}

	return 1;
}


#define MEMORY_RW	hppa_memory_rw
#define MEM_HPPA
#include "memory_rw.c"
#undef MEM_HPPA
#undef MEMORY_RW


#define	CPU_RUN		hppa_cpu_run
#define	CPU_RINSTR	hppa_cpu_run_instr
#define	CPU_RUN_HPPA
#include "cpu_run.c"
#undef CCPU_RINSTR
#undef CPU_RUN_HPPA
#undef CPU_RUN


/*
 *  hppa_cpu_family_init():
 *
 *  Fill in the cpu_family struct for HPPA.
 */
int hppa_cpu_family_init(struct cpu_family *fp)
{
	fp->name = "HPPA";
	fp->cpu_new = hppa_cpu_new;
	fp->list_available_types = hppa_cpu_list_available_types;
	fp->register_match = hppa_cpu_register_match;
	fp->disassemble_instr = hppa_cpu_disassemble_instr;
	/*  fp->register_dump = hppa_cpu_register_dump;  */
	fp->run = hppa_cpu_run;
	fp->dumpinfo = hppa_cpu_dumpinfo;
	/*  fp->show_full_statistics = hppa_cpu_show_full_statistics;  */
	/*  fp->tlbdump = hppa_cpu_tlbdump;  */
	/*  fp->interrupt = hppa_cpu_interrupt;  */
	/*  fp->interrupt_ack = hppa_cpu_interrupt_ack;  */
	return 1;
}

#endif	/*  ENABLE_HPPA  */
