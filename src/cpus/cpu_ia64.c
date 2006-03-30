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
 *  $Id: cpu_ia64.c,v 1.6 2006-03-30 19:36:04 debug Exp $
 *
 *  IA64 CPU emulation.
 *
 *  TODO: Everything.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "cpu.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"
#include "symbol.h"

#include "tmp_ia64_head.c"


/*
 *  ia64_cpu_new():
 *
 *  Create a new IA64 CPU object by filling the CPU struct.
 *  Return 1 on success, 0 if cpu_type_name isn't a valid IA64 processor.
 */
int ia64_cpu_new(struct cpu *cpu, struct memory *mem,
	struct machine *machine, int cpu_id, char *cpu_type_name)
{
	if (strcasecmp(cpu_type_name, "IA64") != 0)
		return 0;

	cpu->memory_rw = ia64_memory_rw;
	cpu->update_translation_table = ia64_update_translation_table;
	cpu->invalidate_translation_caches =
	    ia64_invalidate_translation_caches;
	cpu->invalidate_code_translation = ia64_invalidate_code_translation;
	cpu->is_32bit = 0;

	/*  Only show name and caches etc for CPU nr 0:  */
	if (cpu_id == 0) {
		debug("%s", cpu->name);
	}

	ia64_init_64bit_dummy_tables(cpu);

	return 1;
}


/*
 *  ia64_cpu_dumpinfo():
 */
void ia64_cpu_dumpinfo(struct cpu *cpu)
{
	/*  TODO  */
	debug("\n");
}


/*
 *  ia64_cpu_list_available_types():
 *
 *  Print a list of available IA64 CPU types.
 */
void ia64_cpu_list_available_types(void)
{
	/*  TODO  */

	debug("IA64\n");
}


/*
 *  ia64_cpu_register_match():
 */
void ia64_cpu_register_match(struct machine *m, char *name,
	int writeflag, uint64_t *valuep, int *match_register)
{
	int cpunr = 0;

	/*  CPU number:  */

	/*  TODO  */

	if (strcasecmp(name, "pc") == 0) {
		if (writeflag) {
			m->cpus[cpunr]->pc = *valuep;
		} else
			*valuep = m->cpus[cpunr]->pc;
		*match_register = 1;
	}

	/*  TODO  */
}


/*
 *  ia64_cpu_register_dump():
 *  
 *  Dump cpu registers in a relatively readable format.
 *  
 *  gprs: set to non-zero to dump GPRs and some special-purpose registers.
 *  coprocs: set bit 0..3 to dump registers in coproc 0..3.
 */
void ia64_cpu_register_dump(struct cpu *cpu, int gprs, int coprocs)
{ 
	char *symbol;
	uint64_t offset;
	int x = cpu->cpu_id;

	if (gprs) {
		symbol = get_symbol_name(&cpu->machine->symbol_context,
		    cpu->pc, &offset);
		debug("cpu%i:\t pc = 0x%016"PRIx64, x, (uint64_t)cpu->pc);
		debug("  <%s>\n", symbol != NULL? symbol : " no symbol ");

		/*  TODO  */
	}
}


/*
 *  ia64_cpu_interrupt():
 */
int ia64_cpu_interrupt(struct cpu *cpu, uint64_t irq_nr)
{
	fatal("ia64_cpu_interrupt(): TODO\n");
	return 0;
}


/*
 *  ia64_cpu_interrupt_ack():
 */
int ia64_cpu_interrupt_ack(struct cpu *cpu, uint64_t irq_nr)
{
	/*  fatal("ia64_cpu_interrupt_ack(): TODO\n");  */
	return 0;
}


/*
 *  ia64_cpu_disassemble_instr():
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
int ia64_cpu_disassemble_instr(struct cpu *cpu, unsigned char *ib,
        int running, uint64_t dumpaddr, int bintrans)
{
	uint64_t offset;
	char *symbol;

	if (running)
		dumpaddr = cpu->pc;

	symbol = get_symbol_name(&cpu->machine->symbol_context,
	    dumpaddr, &offset);
	if (symbol != NULL && offset == 0)
		debug("<%s>\n", symbol);

	if (cpu->machine->ncpus > 1 && running)
		debug("cpu%i:\t", cpu->cpu_id);

	debug("%016"PRIx64":  ", (uint64_t) dumpaddr);

debug("TODO\n");

/*	iw = ib[0] + (ib[1]<<8) + (ib[2]<<16) + (ib[3]<<24); */

	return 16;
}


#include "tmp_ia64_tail.c"

