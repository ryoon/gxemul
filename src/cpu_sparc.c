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
 *  $Id: cpu_sparc.c,v 1.10 2005-06-01 22:09:40 debug Exp $
 *
 *  SPARC CPU emulation.
 *
 *  TODO: This is just a dummy so far.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "misc.h"


#ifndef	ENABLE_SPARC


#include "cpu_sparc.h"


/*
 *  sparc_cpu_family_init():
 *
 *  Bogus, when ENABLE_SPARC isn't defined.
 */
int sparc_cpu_family_init(struct cpu_family *fp)
{
	return 0;
}


#else	/*  ENABLE_SPARC  */


#include "cpu.h"
#include "cpu_sparc.h"
#include "machine.h"
#include "memory.h"
#include "symbol.h"


extern volatile int single_step;
extern int old_show_trace_tree;   
extern int old_instruction_trace;
extern int old_quiet_mode;
extern int quiet_mode;


/*
 *  sparc_cpu_new():
 *
 *  Create a new SPARC cpu object.
 */
struct cpu *sparc_cpu_new(struct memory *mem, struct machine *machine,
	int cpu_id, char *cpu_type_name)
{
	struct cpu *cpu;

	if (cpu_type_name == NULL || strcmp(cpu_type_name, "SPARCV9") != 0)
		return NULL;

	cpu = malloc(sizeof(struct cpu));
	if (cpu == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}

	memset(cpu, 0, sizeof(struct cpu));
	cpu->memory_rw          = sparc_memory_rw;
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
 *  sparc_cpu_dumpinfo():
 */
void sparc_cpu_dumpinfo(struct cpu *cpu)
{
	debug("\n");

	/*  TODO  */
}


/*
 *  sparc_cpu_list_available_types():
 *
 *  Print a list of available SPARC CPU types.
 */
void sparc_cpu_list_available_types(void)
{
	/*  TODO  */

	debug("SPARCV9\n");
}


/*
 *  sparc_cpu_register_match():
 */
void sparc_cpu_register_match(struct machine *m, char *name,
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


#define MEMORY_RW	sparc_memory_rw
#define MEM_SPARC
#include "memory_rw.c"
#undef MEM_SPARC
#undef MEMORY_RW


/*
 *  sparc_cpu_family_init():
 *
 *  Fill in the cpu_family struct for SPARC.
 */
int sparc_cpu_family_init(struct cpu_family *fp)
{
	fp->name = "SPARC";
	fp->cpu_new = sparc_cpu_new;
	fp->list_available_types = sparc_cpu_list_available_types;
	fp->register_match = sparc_cpu_register_match;
	/*  fp->disassemble_instr = sparc_cpu_disassemble_instr;  */
	/*  fp->register_dump = sparc_cpu_register_dump;  */
	/*  fp->run = sparc_cpu_run;  */
	fp->dumpinfo = sparc_cpu_dumpinfo;
	/*  fp->show_full_statistics = sparc_cpu_show_full_statistics;  */
	/*  fp->tlbdump = sparc_cpu_tlbdump;  */
	/*  fp->interrupt = sparc_cpu_interrupt;  */
	/*  fp->interrupt_ack = sparc_cpu_interrupt_ack;  */
	return 1;
}

#endif	/*  ENABLE_SPARC  */
