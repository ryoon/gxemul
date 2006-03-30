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
 *  $Id: cpu_hppa.c,v 1.8 2006-03-30 19:36:04 debug Exp $
 *
 *  HP PA-RISC CPU emulation.
 *
 *  TODO
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


#define DYNTRANS_DUALMODE_32
#include "tmp_hppa_head.c"


/*
 *  hppa_cpu_new():
 *
 *  Create a new HPPA cpu object.
 *
 *  Returns 1 on success, 0 if there was no matching HPPA processor with
 *  this cpu_type_name.
 */
int hppa_cpu_new(struct cpu *cpu, struct memory *mem, struct machine *machine,
	int cpu_id, char *cpu_type_name)
{
	if (strcasecmp(cpu_type_name, "HPPA") != 0)
		return 0;

	cpu->memory_rw = hppa_memory_rw;

	/*  TODO: per CPU type?  */
	cpu->byte_order = EMUL_LITTLE_ENDIAN;
	cpu->is_32bit = 1;
	cpu->cd.hppa.bits = 32;

	if (cpu->is_32bit) {
		cpu->update_translation_table = hppa32_update_translation_table;
		cpu->invalidate_translation_caches =
		    hppa32_invalidate_translation_caches;
		cpu->invalidate_code_translation =
		    hppa32_invalidate_code_translation;
	} else {
		cpu->update_translation_table = hppa_update_translation_table;
		cpu->invalidate_translation_caches =
		    hppa_invalidate_translation_caches;
		cpu->invalidate_code_translation =
		    hppa_invalidate_code_translation;
	}

	/*  Only show name and caches etc for CPU nr 0 (in SMP machines):  */
	if (cpu_id == 0) {
		debug("%s", cpu->name);
	}

	hppa_init_64bit_dummy_tables(cpu);

	return 1;
}


/*
 *  hppa_cpu_list_available_types():
 *
 *  Print a list of available HPPA CPU types.
 */
void hppa_cpu_list_available_types(void)
{
	debug("HPPA\n");
	/*  TODO  */
}


/*
 *  hppa_cpu_dumpinfo():
 */
void hppa_cpu_dumpinfo(struct cpu *cpu)
{
	debug("\n");
	/*  TODO  */
}


/*
 *  hppa_cpu_register_dump():
 *
 *  Dump cpu registers in a relatively readable format.
 *
 *  gprs: set to non-zero to dump GPRs and some special-purpose registers.
 *  coprocs: set bit 0..3 to dump registers in coproc 0..3.
 */
void hppa_cpu_register_dump(struct cpu *cpu, int gprs, int coprocs)
{
	char *symbol;
	uint64_t offset;
	int i, x = cpu->cpu_id, nregs = 32;
	int bits32 = cpu->cd.hppa.bits == 32;

	if (gprs) {
		/*  Special registers (pc, ...) first:  */
		symbol = get_symbol_name(&cpu->machine->symbol_context,
		    cpu->pc, &offset);

		debug("cpu%i: pc  = 0x", x);
		if (bits32)
			debug("%08"PRIx32, (uint32_t) cpu->pc);
		else
			debug("%016"PRIx64, (uint64_t) cpu->pc);
		debug("  <%s>\n", symbol != NULL? symbol : " no symbol ");

		if (bits32) {
			/*  32-bit:  */
			for (i=0; i<nregs; i++) {
				if ((i % 4) == 0)
					debug("cpu%i:", x);
				debug(" r%02i = 0x%08"PRIx32" ", i,
				    (uint32_t)cpu->cd.hppa.r[i]);
				if ((i % 4) == 3)
					debug("\n");
			}
		} else {
			/*  64-bit:  */
			for (i=0; i<nregs; i++) {
				int r = (i >> 1) + ((i & 1) << 4);
				if ((i % 2) == 0)
					debug("cpu%i:", x);
				debug(" r%02i = 0x%016"PRIx64" ", r,
				    (uint64_t) cpu->cd.hppa.r[r]);
				if ((i % 2) == 1)
					debug("\n");
			}
		}
	}
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
}


/*
 *  hppa_cpu_interrupt():
 */
int hppa_cpu_interrupt(struct cpu *cpu, uint64_t irq_nr)
{
	fatal("hppa_cpu_interrupt(): TODO\n");
	return 0;
}


/*
 *  hppa_cpu_interrupt_ack():
 */
int hppa_cpu_interrupt_ack(struct cpu *cpu, uint64_t irq_nr)
{
	/*  fatal("hppa_cpu_interrupt_ack(): TODO\n");  */
	return 0;
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

	if (running)
		dumpaddr = cpu->pc;

	symbol = get_symbol_name(&cpu->machine->symbol_context,
	    dumpaddr, &offset);
	if (symbol != NULL && offset==0)
		debug("<%s>\n", symbol);

	if (cpu->machine->ncpus > 1 && running)
		debug("cpu%i: ", cpu->cpu_id);

	if (cpu->cd.hppa.bits == 32)
		debug("%08"PRIx32, (uint32_t) dumpaddr);
	else
		debug("%016"PRIx64, (uint64_t) dumpaddr);

	if (cpu->byte_order == EMUL_BIG_ENDIAN)
		iword = (instr[0] << 24) + (instr[1] << 16) + (instr[2] << 8)
		    + instr[3];
	else
		iword = (instr[3] << 24) + (instr[2] << 16) + (instr[1] << 8)
		    + instr[0];

	debug(": %08"PRIx32"\t", (uint32_t) iword);

	/*
	 *  Decode the instruction:
	 */

	debug("TODO\n");

	return sizeof(iword);
}


#include "tmp_hppa_tail.c"

