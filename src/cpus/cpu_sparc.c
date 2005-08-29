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
 *  $Id: cpu_sparc.c,v 1.1 2005-08-29 14:36:41 debug Exp $
 *
 *  SPARC CPU emulation.
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

#define	DYNTRANS_DUALMODE_32
/*  #define DYNTRANS_32  */
#include "tmp_sparc_head.c"


/*
 *  sparc_cpu_new():
 *
 *  Create a new SPARC cpu object.
 *
 *  Returns 1 on success, 0 if there was no matching SPARC processor with
 *  this cpu_type_name.
 */
int sparc_cpu_new(struct cpu *cpu, struct memory *mem, struct machine *machine,
	int cpu_id, char *cpu_type_name)
{
	if (strcasecmp(cpu_type_name, "SPARCv9") != 0)
		return 0;

	cpu->memory_rw = sparc_memory_rw;
	cpu->update_translation_table = sparc_update_translation_table;
	cpu->invalidate_translation_caches_paddr =
	    sparc_invalidate_translation_caches_paddr;
	cpu->invalidate_code_translation =
	    sparc_invalidate_code_translation;

	cpu->byte_order = EMUL_BIG_ENDIAN;
	cpu->is_32bit = 0;

	/*  Only show name and caches etc for CPU nr 0 (in SMP machines):  */
	if (cpu_id == 0) {
		debug("%s", cpu->name);
	}

	return 1;
}


/*
 *  sparc_cpu_list_available_types():
 *
 *  Print a list of available SPARC CPU types.
 */
void sparc_cpu_list_available_types(void)
{
	debug("SPARCv9\n");
	/*  TODO  */
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
 *  sparc_cpu_register_dump():
 *
 *  Dump cpu registers in a relatively readable format.
 *
 *  gprs: set to non-zero to dump GPRs and some special-purpose registers.
 *  coprocs: set bit 0..3 to dump registers in coproc 0..3.
 */
void sparc_cpu_register_dump(struct cpu *cpu, int gprs, int coprocs)
{
	char *symbol;
	uint64_t offset, tmp;
	int i, x = cpu->cpu_id;
	int bits32 = 0;

	if (gprs) {
		/*  Special registers (pc, ...) first:  */
		symbol = get_symbol_name(&cpu->machine->symbol_context,
		    cpu->pc, &offset);

		debug("cpu%i: pc  = 0x", x);
		if (bits32)
			debug("%08x", (int)cpu->pc);
		else
			debug("%016llx", (long long)cpu->pc);
		debug("  <%s>\n", symbol != NULL? symbol : " no symbol ");

		/*  TODO  */
	}
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
}


/*
 *  sparc_cpu_show_full_statistics():
 *
 *  Show detailed statistics on opcode usage on each cpu.
 */
void sparc_cpu_show_full_statistics(struct machine *m)
{
	fatal("sparc_cpu_show_full_statistics(): TODO\n");
}


/*
 *  sparc_cpu_tlbdump():
 *
 *  Called from the debugger to dump the TLB in a readable format.
 *  x is the cpu number to dump, or -1 to dump all CPUs.
 *
 *  If rawflag is nonzero, then the TLB contents isn't formated nicely,
 *  just dumped.
 */
void sparc_cpu_tlbdump(struct machine *m, int x, int rawflag)
{
	fatal("sparc_cpu_tlbdump(): TODO\n");
}


/*
 *  sparc_cpu_interrupt():
 */
int sparc_cpu_interrupt(struct cpu *cpu, uint64_t irq_nr)
{
	fatal("sparc_cpu_interrupt(): TODO\n");
	return 0;
}


/*
 *  sparc_cpu_interrupt_ack():
 */
int sparc_cpu_interrupt_ack(struct cpu *cpu, uint64_t irq_nr)
{
	/*  fatal("sparc_cpu_interrupt_ack(): TODO\n");  */
	return 0;
}


/*
 *  sparc_cpu_disassemble_instr():
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
int sparc_cpu_disassemble_instr(struct cpu *cpu, unsigned char *instr,
	int running, uint64_t dumpaddr, int bintrans)
{
	uint64_t offset, addr;
	uint32_t iword;
	int hi6;
	char *symbol, *mnem = "ERROR";

	if (running)
		dumpaddr = cpu->pc;

	symbol = get_symbol_name(&cpu->machine->symbol_context,
	    dumpaddr, &offset);
	if (symbol != NULL && offset==0)
		debug("<%s>\n", symbol);

	if (cpu->machine->ncpus > 1 && running)
		debug("cpu%i: ", cpu->cpu_id);

/*	if (cpu->cd.sparc.bits == 32)
		debug("%08x", (int)dumpaddr);
	else
*/		debug("%016llx", (long long)dumpaddr);

	iword = (instr[0] << 24) + (instr[1] << 16) + (instr[2] << 8)
	    + instr[3];

	debug(": %08x\t", iword);

	/*
	 *  Decode the instruction:
	 */

	hi6 = iword >> 26;

	switch (hi6) {
	default:
		/*  TODO  */
		debug("unimplemented hi6 = 0x%02x", hi6);
	}

	debug("\n");
	return sizeof(iword);
}


#include "tmp_sparc_tail.c"

