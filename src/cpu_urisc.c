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
 *  $Id: cpu_urisc.c,v 1.1 2005-03-01 06:48:24 debug Exp $
 *
 *  URISC CPU emulation.
 *
 *  TODO: This is just a dummy so far.
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

	symbol = get_symbol_name(&cpu->machine->symbol_context,
	    cpu->pc, &offset);
	debug("cpu%i: pc  = 0x016llx", x, (long long)cpu->pc);
	debug("  <%s>\n", symbol != NULL? symbol : " no symbol ");
	debug("cpu%i: acc = 0x%016llx\n", x, (long long)cpu->cd.urisc.acc);
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
			m->cpus[cpunr]->pc = *valuep;
		} else
			*valuep = m->cpus[cpunr]->pc;
		*match_register = 1;
	}

	if (strcasecmp(name, "acc") == 0) {
		if (writeflag) {
			m->cpus[cpunr]->cd.urisc.acc = *valuep;
		} else
			*valuep = m->cpus[cpunr]->cd.urisc.acc;
		*match_register = 1;
	}

	/*  TODO: _LOTS_ of stuff.  */
}


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
	/*  fp->disassemble_instr = urisc_cpu_disassemble_instr;  */
	fp->register_dump = urisc_cpu_register_dump;
	/*  fp->run = urisc_cpu_run;  */
	fp->dumpinfo = urisc_cpu_dumpinfo;
	/*  fp->show_full_statistics = urisc_cpu_show_full_statistics;  */
	return 1;
}

#endif	/*  ENABLE_URISC  */
