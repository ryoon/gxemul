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
 *  $Id: cpu_arm.c,v 1.3 2005-06-21 09:10:18 debug Exp $
 *
 *  ARM CPU emulation.
 *
 *  TODO: This is just a dummy so far.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "misc.h"


#ifndef	ENABLE_ARM


#include "cpu_arm.h"


/*
 *  arm_cpu_family_init():
 *
 *  Bogus, when ENABLE_ARM isn't defined.
 */
int arm_cpu_family_init(struct cpu_family *fp)
{
	return 0;
}


#else	/*  ENABLE_ARM  */


#include "cpu.h"
#include "cpu_arm.h"
#include "machine.h"
#include "memory.h"
#include "symbol.h"


/*  instr uses the same names as in cpu_arm_instr.c  */
#define instr(n) arm_instr_ ## n

extern volatile int single_step;
extern int old_show_trace_tree;   
extern int old_instruction_trace;
extern int old_quiet_mode;
extern int quiet_mode;


/*
 *  arm_cpu_new():
 *
 *  Create a new ARM cpu object.
 */
struct cpu *arm_cpu_new(struct memory *mem, struct machine *machine,
	int cpu_id, char *cpu_type_name)
{
	struct cpu *cpu;

	if (cpu_type_name == NULL || strcmp(cpu_type_name, "ARM") != 0)
		return NULL;

	cpu = malloc(sizeof(struct cpu));
	if (cpu == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}

	memset(cpu, 0, sizeof(struct cpu));
	cpu->memory_rw          = arm_memory_rw;
	cpu->name               = cpu_type_name;
	cpu->mem                = mem;
	cpu->machine            = machine;
	cpu->cpu_id             = cpu_id;
	cpu->byte_order         = EMUL_BIG_ENDIAN;
	cpu->bootstrap_cpu_flag = 0;
	cpu->running            = 0;

	/*  Only show name and caches etc for CPU nr 0 (in SMP machines):  */
	if (cpu_id == 0) {
		debug("%s", cpu->name);
	}

	return cpu;
}


/*
 *  arm_cpu_dumpinfo():
 */
void arm_cpu_dumpinfo(struct cpu *cpu)
{
	debug("\n");

	/*  TODO  */
}


/*
 *  arm_cpu_list_available_types():
 *
 *  Print a list of available ARM CPU types.
 */
void arm_cpu_list_available_types(void)
{
	/*  TODO  */

	debug("ARM\n");
}


/*
 *  arm_cpu_register_match():
 */
void arm_cpu_register_match(struct machine *m, char *name,
	int writeflag, uint64_t *valuep, int *match_register)
{
	int cpunr = 0;

	/*  CPU number:  */

	/*  TODO  */

	/*  Register name:  */
	if (strcasecmp(name, "pc") == 0) {
		if (writeflag) {
			m->cpus[cpunr]->pc = *valuep;
			m->cpus[cpunr]->cd.arm.r[ARM_PC] = *valuep;
		} else
			*valuep = m->cpus[cpunr]->pc;
		*match_register = 1;
	}

	/*  TODO: _LOTS_ of stuff.  */
}


/*
 *  arm_cpu_register_dump():
 *
 *  Dump cpu registers in a relatively readable format.
 *  
 *  gprs: set to non-zero to dump GPRs and some special-purpose registers.
 *  coprocs: set bit 0..3 to dump registers in coproc 0..3.
 */
void arm_cpu_register_dump(struct cpu *cpu, int gprs, int coprocs)
{
	char *symbol;
	uint64_t offset;
	int i, x = cpu->cpu_id;

	if (gprs) {
		symbol = get_symbol_name(&cpu->machine->symbol_context,
		    cpu->cd.arm.r[ARM_PC], &offset);
		debug("cpu%i:  pc  = 0x%08x", x, (int)cpu->cd.arm.r[ARM_PC]);

		/*  TODO: Flags  */

		debug("  <%s>\n", symbol != NULL? symbol : " no symbol ");

		for (i=0; i<16; i++) {
			if ((i % 4) == 0)
				debug("cpu%i:", x);
			if (i != ARM_PC)
				debug("  r%02i = 0x%08x", i,
				    (int)cpu->cd.arm.r[i]);
			if ((i % 4) == 3)
				debug("\n");
		}
	}
}


/*
 *  arm_cpu_disassemble_instr():
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
int arm_cpu_disassemble_instr(struct cpu *cpu, unsigned char *instr,
        int running, uint64_t dumpaddr, int bintrans)
{
	uint32_t iw;

	if (running)
		dumpaddr = cpu->pc;

	debug("%08x:  ", (int)dumpaddr);

	iw = instr[0] + (instr[1] << 8) + (instr[2] << 16) + (instr[3] << 24);
	debug("%08x\t", (int)iw);

	debug("arm_cpu_disassemble_instr(): TODO\n");

	return sizeof(uint32_t);
}


/*
 *  arm_create_or_reset_tc():
 *
 *  Create the translation cache in memory (ie allocate memory for it), if
 *  necessary, and then reset it to an initial state.
 */
static void arm_create_or_reset_tc(struct cpu *cpu)
{
	if (cpu->cd.arm.translation_cache == NULL) {
		cpu->cd.arm.translation_cache = malloc(
		    ARM_TRANSLATION_CACHE_SIZE + ARM_TRANSLATION_CACHE_MARGIN);
		if (cpu->cd.arm.translation_cache == NULL) {
			fprintf(stderr, "arm_create_or_reset_tc(): out of "
			    "memory when allocating the translation cache\n");
			exit(1);
		}
	}

	/*  Create an empty table at the beginning of the translation cache:  */
	memset(cpu->cd.arm.translation_cache, 0, sizeof(uint32_t) *
	    N_BASE_TABLE_ENTRIES);

	cpu->cd.arm.translation_cache_cur_ofs =
	    N_BASE_TABLE_ENTRIES * sizeof(uint32_t);
}


/*
 *  arm_tc_allocate_default_page():
 *
 *  Create a default page (with just pointers to instr(to_be_translated)
 *  at cpu->cd.arm.translation_cache_cur_ofs.
 */
/*  forward declaration of to_be_translated and end_of_page:  */
static void instr(to_be_translated)(struct cpu *,struct arm_instr_call *);
static void instr(end_of_page)(struct cpu *,struct arm_instr_call *);
static void arm_tc_allocate_default_page(struct cpu *cpu, uint32_t physaddr)
{
	struct arm_tc_physpage *ppp;
	int i;

	/*  Create the physpage header:  */
	ppp = (struct arm_tc_physpage *)(cpu->cd.arm.translation_cache
	    + cpu->cd.arm.translation_cache_cur_ofs);
	ppp->next_ofs = 0;
	ppp->physaddr = physaddr;

	for (i=0; i<IC_ENTRIES_PER_PAGE; i++)
		ppp->ics[i].f = instr(to_be_translated);

	ppp->ics[IC_ENTRIES_PER_PAGE].f = instr(end_of_page);

	cpu->cd.arm.translation_cache_cur_ofs +=
	    sizeof(struct arm_tc_physpage);
}


#define MEMORY_RW	arm_memory_rw
#define MEM_ARM
#include "memory_rw.c"
#undef MEM_ARM
#undef MEMORY_RW


#include "cpu_arm_instr.c"


/*
 *  arm_cpu_run_instr():
 *
 *  Execute one instruction on a specific CPU.
 *
 *  Return value is the number of instructions executed during this call,
 *  0 if no instructions were executed.
 */
int arm_cpu_run_instr(struct emul *emul, struct cpu *cpu)
{
	/*
	 *  Find the correct translated page in the translation cache,
	 *  and start running code on that page.
	 */

	uint32_t cached_pc, physaddr, physpage_ofs;
	int pagenr, table_index;
	uint32_t *physpage_entryp;
	struct arm_tc_physpage *ppp;

	if (cpu->cd.arm.translation_cache == NULL ||
	    cpu->cd.arm.translation_cache_cur_ofs >=
	    ARM_TRANSLATION_CACHE_SIZE)
		arm_create_or_reset_tc(cpu);

	cached_pc = cpu->cd.arm.r[ARM_PC];

	physaddr = cached_pc & ~((IC_ENTRIES_PER_PAGE << 2) | 3);
	/*  TODO: virtual to physical  */

	pagenr = ADDR_TO_PAGENR(physaddr);
	table_index = PAGENR_TO_TABLE_INDEX(pagenr);

	physpage_entryp = &(((uint32_t *)
	    cpu->cd.arm.translation_cache)[table_index]);
	physpage_ofs = *physpage_entryp;

	/*  Traverse the physical page chain:  */
	while (physpage_ofs != 0) {
		fatal("TODO: physpage_ofs != 0 osv\n");
		exit(1);
	}

	/*  If the offset is 0, then we need to create a new "default"
	    empty translation page.  */

	if (physpage_ofs == 0) {
		fatal("CREATING page %i, table index = %i\n",
		    pagenr, table_index);
		*physpage_entryp = cpu->cd.arm.translation_cache_cur_ofs;
		arm_tc_allocate_default_page(cpu, physaddr);
	}

	ppp = (struct arm_tc_physpage *)(cpu->cd.arm.translation_cache
	    + *physpage_entryp);
	cpu->cd.arm.cur_ic_page = &ppp->ics[0];
	cpu->cd.arm.next_ic = cpu->cd.arm.cur_ic_page +
	    PC_TO_IC_ENTRY(cached_pc);

fatal("arm_cpu_run_instr: TODO\n");
printf("cached_pc = 0x%08x  pagenr = %i  table_index = %i, "
"physpage_ofs = 0x%08x\n", cached_pc, pagenr, table_index, physpage_ofs);

	{
		struct arm_instr_call *ic;

		ic = cpu->cd.arm.next_ic ++;
		ic->f(cpu, ic);
	}

	/*  TODO  */
	return 1;
}


#define CPU_RUN         arm_cpu_run
#define CPU_RINSTR      arm_cpu_run_instr
#define CPU_RUN_ARM
#include "cpu_run.c"
#undef CPU_RINSTR
#undef CPU_RUN_ARM
#undef CPU_RUN


/*
 *  arm_cpu_family_init():
 *
 *  Fill in the cpu_family struct for ARM.
 */
int arm_cpu_family_init(struct cpu_family *fp)
{
	fp->name = "ARM";
	fp->cpu_new = arm_cpu_new;
	fp->list_available_types = arm_cpu_list_available_types;
	fp->register_match = arm_cpu_register_match;
	fp->disassemble_instr = arm_cpu_disassemble_instr;
	fp->register_dump = arm_cpu_register_dump;
	fp->run = arm_cpu_run;
	fp->dumpinfo = arm_cpu_dumpinfo;
	/*  fp->show_full_statistics = arm_cpu_show_full_statistics;  */
	/*  fp->tlbdump = arm_cpu_tlbdump;  */
	/*  fp->interrupt = arm_cpu_interrupt;  */
	/*  fp->interrupt_ack = arm_cpu_interrupt_ack;  */
	return 1;
}

#endif	/*  ENABLE_ARM  */
