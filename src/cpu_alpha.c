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
 *  $Id: cpu_alpha.c,v 1.2 2005-03-13 09:51:41 debug Exp $
 *
 *  Alpha CPU emulation.
 *
 *  TODO: This is just a dummy so far.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "misc.h"


#ifndef	ENABLE_ALPHA


#include "cpu_alpha.h"


/*
 *  alpha_cpu_family_init():
 *
 *  Bogus, when ENABLE_ALPHA isn't defined.
 */
int alpha_cpu_family_init(struct cpu_family *fp)
{
	return 0;
}


#else	/*  ENABLE_ALPHA  */


#include "cpu.h"
#include "cpu_alpha.h"
#include "machine.h"
#include "memory.h"
#include "symbol.h"


extern volatile int single_step;
extern int old_show_trace_tree;   
extern int old_instruction_trace;
extern int old_quiet_mode;
extern int quiet_mode;


/*
 *  alpha_cpu_new():
 *
 *  Create a new Alpha cpu object.
 */
struct cpu *alpha_cpu_new(struct memory *mem, struct machine *machine,
	int cpu_id, char *cpu_type_name)
{
	struct cpu *cpu;

	if (cpu_type_name == NULL || (
	    strcasecmp(cpu_type_name, "ev4") != 0 &&
	    strcasecmp(cpu_type_name, "ev5") != 0 &&
	    strcasecmp(cpu_type_name, "ev6") != 0 &&
	    strcasecmp(cpu_type_name, "ev7") != 0 &&
	    strcasecmp(cpu_type_name, "pca56") != 0) )
		return NULL;

	cpu = malloc(sizeof(struct cpu));
	if (cpu == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}

	memset(cpu, 0, sizeof(struct cpu));
	cpu->memory_rw          = alpha_memory_rw;
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
 *  alpha_cpu_dumpinfo():
 */
void alpha_cpu_dumpinfo(struct cpu *cpu)
{
	debug("\n");

	/*  TODO  */
}


/*
 *  alpha_cpu_list_available_types():
 *
 *  Print a list of available Alpha CPU types.
 */
void alpha_cpu_list_available_types(void)
{
	/*  TODO  */

	debug("EV4       EV5       EV6       EV7       PCA56\n");
}


/*
 *  alpha_cpu_register_match():
 */
void alpha_cpu_register_match(struct machine *m, char *name,
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


#define MEMORY_RW	alpha_memory_rw
#define MEM_ALPHA
#include "memory_rw.c"
#undef MEM_ALPHA
#undef MEMORY_RW


/*
 *  alpha_cpu_family_init():
 *
 *  Fill in the cpu_family struct for Alpha.
 */
int alpha_cpu_family_init(struct cpu_family *fp)
{
	fp->name = "Alpha";
	fp->cpu_new = alpha_cpu_new;
	fp->list_available_types = alpha_cpu_list_available_types;
	fp->register_match = alpha_cpu_register_match;
	/*  fp->disassemble_instr = alpha_cpu_disassemble_instr;  */
	/*  fp->register_dump = alpha_cpu_register_dump;  */
	/*  fp->run = alpha_cpu_run;  */
	fp->dumpinfo = alpha_cpu_dumpinfo;
	/*  fp->show_full_statistics = alpha_cpu_show_full_statistics;  */
	/*  fp->tlbdump = alpha_cpu_tlbdump;  */
	/*  fp->interrupt = alpha_cpu_interrupt;  */
	/*  fp->interrupt_ack = alpha_cpu_interrupt_ack;  */
	return 1;
}

#endif	/*  ENABLE_ALPHA  */
