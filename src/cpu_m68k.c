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
 *  $Id: cpu_m68k.c,v 1.3 2005-08-14 23:44:22 debug Exp $
 *
 *  Motorola 68K CPU emulation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "misc.h"


#ifndef ENABLE_M68K


#include "cpu_m68k.h"


/*
 *  m68k_cpu_family_init():
 *
 *  Bogus function.
 */
int m68k_cpu_family_init(struct cpu_family *fp)
{
	return 0;
}


#else	/*  ENABLE_M68K  */


#include "cpu.h"
#include "cpu_m68k.h"
#include "machine.h"
#include "memory.h"
#include "symbol.h"

#define	DYNTRANS_32
#define	DYNTRANS_VARIABLE_INSTRUCTION_LENGTH
#include "tmp_m68k_head.c"


/*
 *  m68k_cpu_new():
 *
 *  Create a new M68K cpu object.
 *
 *  Returns 1 on success, 0 if there was no matching M68K processor with
 *  this cpu_type_name.
 */
int m68k_cpu_new(struct cpu *cpu, struct memory *mem, struct machine *machine,
	int cpu_id, char *cpu_type_name)
{
	if (strcasecmp(cpu_type_name, "68020") != 0)
		return 0;

	cpu->memory_rw = m68k_memory_rw;
	cpu->update_translation_table = m68k_update_translation_table;
	cpu->invalidate_translation_caches_paddr =
	    m68k_invalidate_translation_caches_paddr;
	cpu->invalidate_code_translation_caches =
	    m68k_invalidate_code_translation_caches;
	cpu->is_32bit = 1;

	cpu->byte_order = EMUL_BIG_ENDIAN;

	/*  Only show name and caches etc for CPU nr 0 (in SMP machines):  */
	if (cpu_id == 0) {
		debug("%s", cpu->name);
	}

	return 1;
}


/*
 *  m68k_cpu_list_available_types():
 *
 *  Print a list of available M68K CPU types.
 */
void m68k_cpu_list_available_types(void)
{
	debug("68020\n");
	/*  TODO  */
}


/*
 *  m68k_cpu_dumpinfo():
 */
void m68k_cpu_dumpinfo(struct cpu *cpu)
{
	/*  TODO  */
	debug("\n");
}


/*
 *  m68k_cpu_register_dump():
 *
 *  Dump cpu registers in a relatively readable format.
 *
 *  gprs: set to non-zero to dump GPRs and some special-purpose registers.
 *  coprocs: set bit 0..3 to dump registers in coproc 0..3.
 */
void m68k_cpu_register_dump(struct cpu *cpu, int gprs, int coprocs)
{
	char *symbol;
	uint64_t offset;
	int x = cpu->cpu_id;

	if (gprs) {
		/*  Special registers (pc, ...) first:  */
		symbol = get_symbol_name(&cpu->machine->symbol_context,
		    cpu->pc, &offset);

		debug("cpu%i: pc  = 0x%08x", x, (int)cpu->pc);
		debug("  <%s>\n", symbol != NULL? symbol : " no symbol ");
	}
}


/*
 *  m68k_cpu_register_match():
 */
void m68k_cpu_register_match(struct machine *m, char *name,
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
}


/*
 *  m68k_cpu_show_full_statistics():
 *
 *  Show detailed statistics on opcode usage on each cpu.
 */
void m68k_cpu_show_full_statistics(struct machine *m)
{
	fatal("m68k_cpu_show_full_statistics(): TODO\n");
}


/*
 *  m68k_cpu_tlbdump():
 *
 *  Called from the debugger to dump the TLB in a readable format.
 *  x is the cpu number to dump, or -1 to dump all CPUs.
 *
 *  If rawflag is nonzero, then the TLB contents isn't formated nicely,
 *  just dumped.
 */
void m68k_cpu_tlbdump(struct machine *m, int x, int rawflag)
{
	fatal("m68k_cpu_tlbdump(): TODO\n");
}


/*
 *  m68k_cpu_interrupt():
 */
int m68k_cpu_interrupt(struct cpu *cpu, uint64_t irq_nr)
{
	fatal("m68k_cpu_interrupt(): TODO\n");
	return 0;
}


/*
 *  m68k_cpu_interrupt_ack():
 */
int m68k_cpu_interrupt_ack(struct cpu *cpu, uint64_t irq_nr)
{
	/*  fatal("m68k_cpu_interrupt_ack(): TODO\n");  */
	return 0;
}


/*
 *  m68k_cpu_disassemble_instr():
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
int m68k_cpu_disassemble_instr(struct cpu *cpu, unsigned char *instr,
	int running, uint64_t dumpaddr, int bintrans)
{
	uint64_t offset;
	char *symbol;

	if (running)
		dumpaddr = cpu->pc;

	symbol = get_symbol_name(&cpu->machine->symbol_context,
	    dumpaddr, &offset);
	if (symbol != NULL && offset==0)
		debug("<%s>\n", symbol);

	if (cpu->machine->ncpus > 1 && running)
		debug("cpu%i: ", cpu->cpu_id);

	debug("0x%08x:", (int)dumpaddr);

	debug("\tTODO\n");

	return 2;
}


#include "tmp_m68k_tail.c"


#endif	/*  ENABLE_M68K  */
