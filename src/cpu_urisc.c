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
 *  $Id: cpu_urisc.c,v 1.2 2005-03-01 08:23:55 debug Exp $
 *
 *  URISC CPU emulation.  See http://en.wikipedia.org/wiki/URISC for more
 *  information about the "instruction set".
 *
 *
 *  NOTE:
 *
 *	The PC should always be in sync with the memory word at address 0.
 *
 *	The accumulator register should always be in sync with the memory
 *	word following the word at address 0.
 *
 *
 *  TODO:
 *
 *	o)  Non-8-bit word length
 *	o)  Little-endian support?
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "misc.h"


#ifndef	ENABLE_URISC


#include "cpu_urisc.h"


/*
 *  urisc_cpu_family_init():
 *
 *  Bogus, when ENABLE_URISC isn't defined.
 */
int urisc_cpu_family_init(struct cpu_family *fp)
{
	return 0;
}


#else	/*  ENABLE_URISC  */


#include "cpu.h"
#include "cpu_urisc.h"
#include "machine.h"
#include "memory.h"
#include "symbol.h"


extern volatile int single_step;
extern int old_show_trace_tree;   
extern int old_instruction_trace;
extern int old_quiet_mode;
extern int quiet_mode;


/*
 *  urisc_cpu_new():
 *
 *  Create a new URISC cpu object.
 */
struct cpu *urisc_cpu_new(struct memory *mem, struct machine *machine,
	int cpu_id, char *cpu_type_name)
{
	struct cpu *cpu;

	if (cpu_type_name == NULL)
		return NULL;

	cpu = malloc(sizeof(struct cpu));
	if (cpu == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}

	memset(cpu, 0, sizeof(struct cpu));
	cpu->memory_rw          = urisc_memory_rw;
	cpu->name               = cpu_type_name;
	cpu->mem                = mem;
	cpu->machine            = machine;
	cpu->cpu_id             = cpu_id;
	cpu->byte_order         = EMUL_BIG_ENDIAN;

	cpu->cd.urisc.wordlen = 8;

	/*  Only show name and caches etc for CPU nr 0 (in SMP machines):  */
	if (cpu_id == 0) {
		debug("%s", cpu->name);
	}

	return cpu;
}


/*
 *  urisc_cpu_dumpinfo():
 */
void urisc_cpu_dumpinfo(struct cpu *cpu)
{
	debug("\n");

	/*  TODO  */
}


/*
 *  urisc_cpu_list_available_types():
 *
 *  Print a list of available URISC CPU types.
 */
void urisc_cpu_list_available_types(void)
{
	/*  TODO  */

	debug("URISC\n");
}


/*
 *  urisc_cpu_register_dump():
 *  
 *  Dump cpu registers in a relatively readable format.
 *
 *  gprs: set to non-zero to dump GPRs and some special-purpose registers.
 *  coprocs: set bit 0..3 to dump registers in coproc 0..3.
 */
void urisc_cpu_register_dump(struct cpu *cpu, int gprs, int coprocs)
{ 
	char *symbol;
	uint64_t offset;
	int x = cpu->cpu_id;
	char tmps[100];

	symbol = get_symbol_name(&cpu->machine->symbol_context,
	    cpu->pc, &offset);
	sprintf(tmps, "cpu%%i: pc  = 0x%%0%illx", (cpu->cd.urisc.wordlen/4));
	debug(tmps, x, (long long)cpu->pc);
	debug("  <%s>\n", symbol != NULL? symbol : " no symbol ");
	sprintf(tmps, "cpu%%i: acc = 0x%%0%illx\n", (cpu->cd.urisc.wordlen/4));
	debug(tmps, x, (long long)cpu->cd.urisc.acc);
}


/*
 *  urisc_cpu_disassemble_instr():
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
int urisc_cpu_disassemble_instr(struct cpu *cpu, unsigned char *instr,
        int running, uint64_t dumpaddr, int bintrans)
{
	uint64_t offset;
	char *symbol;
	int i;
	char tmps[50];

	if (running)
		dumpaddr = cpu->pc;

	symbol = get_symbol_name(&cpu->machine->symbol_context,
	    dumpaddr, &offset);
	if (symbol != NULL && offset==0)
		debug("<%s>\n", symbol);

	if (cpu->machine->ncpus > 1 && running)
		debug("cpu%i: ", cpu->cpu_id);
	sprintf(tmps, "0x%%0%illx:  0x", cpu->cd.urisc.wordlen / 4);
	debug(tmps, (long long)dumpaddr);

	/*  TODO:  Little-endian?  */

	for (i=0; i<cpu->cd.urisc.wordlen / 8; i++)
		debug("%02x", instr[i]);

	debug("\n");

	return cpu->cd.urisc.wordlen / 8;
}


/*
 *  urisc_cpu_register_match():
 */
void urisc_cpu_register_match(struct machine *m, char *name,
	int writeflag, uint64_t *valuep, int *match_register)
{
	int cpunr = 0;

	/*  CPU number:  */

	/*  TODO  */

	/*  Register name:  */
	if (strcasecmp(name, "pc") == 0) {
		if (writeflag) {
			unsigned char buf[8];
			m->cpus[cpunr]->pc = *valuep;

			/*  TODO: non-8-bit machines  */
			buf[0] = *valuep;

			m->cpus[cpunr]->memory_rw(m->cpus[cpunr],
			    m->cpus[cpunr]->mem, 0, buf,
			    m->cpus[cpunr]->cd.urisc.wordlen / 8, MEM_WRITE,
			    CACHE_NONE | NO_EXCEPTIONS);
		} else
			*valuep = m->cpus[cpunr]->pc;
		*match_register = 1;
	}

	if (strcasecmp(name, "acc") == 0) {
		if (writeflag) {
			unsigned char buf[8];
			m->cpus[cpunr]->cd.urisc.acc = *valuep;

			/*  TODO: non-8-bit machines  */
			buf[0] = *valuep;

			m->cpus[cpunr]->memory_rw(m->cpus[cpunr],
			    m->cpus[cpunr]->mem,
			    m->cpus[cpunr]->cd.urisc.wordlen / 8,
			    buf, m->cpus[cpunr]->cd.urisc.wordlen / 8,
			    MEM_WRITE, CACHE_NONE | NO_EXCEPTIONS);
		} else
			*valuep = m->cpus[cpunr]->cd.urisc.acc;
		*match_register = 1;
	}

	/*  TODO: _LOTS_ of stuff.  */
}


/*
 *  urisc_cpu_run_instr():
 *
 *  Execute one instruction on a specific CPU.
 *
 *  Return value is the number of instructions executed during this call,
 *  0 if no instruction was executed.
 */
int urisc_cpu_run_instr(struct emul *emul, struct cpu *cpu)
{
	unsigned char buf[8];
	unsigned char instr[8];
	uint64_t addr, data, mask = (uint64_t) -1;
	int skip = 0;

	if (cpu->cd.urisc.wordlen < 64)
		mask = ((int64_t)1 << cpu->cd.urisc.wordlen) - 1;

	cpu->pc &= mask;

	/*  Read an instruction:  */
	cpu->memory_rw(cpu, cpu->mem, cpu->pc, instr, cpu->cd.urisc.wordlen/8,
	    MEM_READ, CACHE_INSTRUCTION);

	if (cpu->machine->instruction_trace)
		urisc_cpu_disassemble_instr(cpu, buf, 1, 0, 0);

	/*  Advance the program counter:  */
	cpu->pc += cpu->cd.urisc.wordlen/8;
	cpu->pc &= mask;

	buf[0] = cpu->pc;
	cpu->memory_rw(cpu, cpu->mem, 0, buf, cpu->cd.urisc.wordlen/8,
	    MEM_WRITE, CACHE_DATA);

	addr = instr[0];

	/*  Read data from memory:  */
	cpu->memory_rw(cpu, cpu->mem, addr, buf, cpu->cd.urisc.wordlen/8,
	    MEM_READ, CACHE_DATA);
	data = buf[0];

	skip = (uint64_t)data < (uint64_t)cpu->cd.urisc.acc;

	data -= cpu->cd.urisc.acc;
	data &= mask;

	/*  Write back result to both memory and the accumulator:  */
	cpu->cd.urisc.acc = data;
	buf[0] = cpu->cd.urisc.acc;
	cpu->memory_rw(cpu, cpu->mem, addr, buf,
	    cpu->cd.urisc.wordlen/8, MEM_WRITE, CACHE_DATA);
	cpu->memory_rw(cpu, cpu->mem, cpu->cd.urisc.wordlen/8, buf,
	    cpu->cd.urisc.wordlen/8, MEM_WRITE, CACHE_DATA);

	/*  Skip on borrow:  */
	if (skip) {
		/*  Advance the program counter:  */
		cpu->pc += cpu->cd.urisc.wordlen/8;
		cpu->pc &= mask;

		buf[0] = cpu->pc;
		cpu->memory_rw(cpu, cpu->mem, 0, buf, cpu->cd.urisc.wordlen/8,
		    MEM_WRITE, CACHE_DATA);
	}

	return 1;
}


#define	CPU_RUN		urisc_cpu_run
#define	CPU_RINSTR	urisc_cpu_run_instr
#define	CPU_RUN_URISC
#include "cpu_run.c"
#undef	CPU_RINSTR
#undef CPU_RUN_URISC
#undef CPU_RUN


#define MEMORY_RW	urisc_memory_rw
#define MEM_URISC
#include "memory_rw.c"
#undef MEM_URISC
#undef MEMORY_RW


/*
 *  urisc_cpu_family_init():
 *
 *  Fill in the cpu_family struct for URISC.
 */
int urisc_cpu_family_init(struct cpu_family *fp)
{
	fp->name = "URISC";
	fp->cpu_new = urisc_cpu_new;
	fp->list_available_types = urisc_cpu_list_available_types;
	fp->register_match = urisc_cpu_register_match;
	fp->disassemble_instr = urisc_cpu_disassemble_instr;
	fp->register_dump = urisc_cpu_register_dump;
	fp->run = urisc_cpu_run;
	fp->dumpinfo = urisc_cpu_dumpinfo;
	return 1;
}

#endif	/*  ENABLE_URISC  */
