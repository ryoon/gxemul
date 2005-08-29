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
 *  $Id: cpu_sh.c,v 1.1 2005-08-29 14:36:41 debug Exp $
 *
 *  Hitachi SuperH ("SH") CPU emulation.
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


/*  #define	DYNTRANS_DUALMODE_32  */
#define	DYNTRANS_32
#include "tmp_sh_head.c"


/*
 *  sh_cpu_new():
 *
 *  Create a new SH cpu object.
 *
 *  Returns 1 on success, 0 if there was no matching SH processor with
 *  this cpu_type_name.
 */
int sh_cpu_new(struct cpu *cpu, struct memory *mem, struct machine *machine,
	int cpu_id, char *cpu_type_name)
{
	if (strcasecmp(cpu_type_name, "SH") != 0)
		return 0;

	cpu->memory_rw = sh_memory_rw;
	cpu->update_translation_table = sh_update_translation_table;
	cpu->invalidate_translation_caches_paddr =
	    sh_invalidate_translation_caches_paddr;
	cpu->invalidate_code_translation = sh_invalidate_code_translation;

	cpu->byte_order = EMUL_BIG_ENDIAN;
	cpu->is_32bit = 1;
	cpu->cd.sh.bits = 32;
	cpu->cd.sh.compact = 1;

	/*  Only show name and caches etc for CPU nr 0 (in SMP machines):  */
	if (cpu_id == 0) {
		debug("%s", cpu->name);
	}

	return 1;
}


/*
 *  sh_cpu_list_available_types():
 *
 *  Print a list of available SH CPU types.
 */
void sh_cpu_list_available_types(void)
{
	debug("SH\n");
	/*  TODO  */
}


/*
 *  sh_cpu_dumpinfo():
 */
void sh_cpu_dumpinfo(struct cpu *cpu)
{
	debug("\n");
	/*  TODO  */
}


/*
 *  sh_cpu_register_dump():
 *
 *  Dump cpu registers in a relatively readable format.
 *
 *  gprs: set to non-zero to dump GPRs and some special-purpose registers.
 *  coprocs: set bit 0..3 to dump registers in coproc 0..3.
 */
void sh_cpu_register_dump(struct cpu *cpu, int gprs, int coprocs)
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
 *  sh_cpu_register_match():
 */
void sh_cpu_register_match(struct machine *m, char *name,
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
 *  sh_cpu_show_full_statistics():
 *
 *  Show detailed statistics on opcode usage on each cpu.
 */
void sh_cpu_show_full_statistics(struct machine *m)
{
	fatal("sh_cpu_show_full_statistics(): TODO\n");
}


/*
 *  sh_cpu_tlbdump():
 *
 *  Called from the debugger to dump the TLB in a readable format.
 *  x is the cpu number to dump, or -1 to dump all CPUs.
 *
 *  If rawflag is nonzero, then the TLB contents isn't formated nicely,
 *  just dumped.
 */
void sh_cpu_tlbdump(struct machine *m, int x, int rawflag)
{
	fatal("sh_cpu_tlbdump(): TODO\n");
}


/*
 *  sh_cpu_interrupt():
 */
int sh_cpu_interrupt(struct cpu *cpu, uint64_t irq_nr)
{
	fatal("sh_cpu_interrupt(): TODO\n");
	return 0;
}


/*
 *  sh_cpu_interrupt_ack():
 */
int sh_cpu_interrupt_ack(struct cpu *cpu, uint64_t irq_nr)
{
	/*  fatal("sh_cpu_interrupt_ack(): TODO\n");  */
	return 0;
}


/*
 *  sh_cpu_disassemble_instr_compact():
 */
int sh_cpu_disassemble_instr_compact(struct cpu *cpu, unsigned char *instr,
	int running, uint64_t dumpaddr, int bintrans)
{
	uint64_t offset, addr;
	uint16_t iword;
	int hi6;
	char *symbol, *mnem = "ERROR";

	if (cpu->byte_order == EMUL_BIG_ENDIAN)
		iword = (instr[0] << 8) + instr[1];
	else
		iword = (instr[1] << 8) + instr[0];

	debug(": %04x\t", iword);

	/*
	 *  Decode the instruction:
	 */

	debug("TODO\n");

	return sizeof(iword);
}


/*
 *  sh_cpu_disassemble_instr():
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
int sh_cpu_disassemble_instr(struct cpu *cpu, unsigned char *instr,
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

	if (cpu->cd.sh.bits == 32)
		debug("%08x", (int)dumpaddr);
	else
		debug("%016llx", (long long)dumpaddr);

	if (cpu->cd.sh.compact)
		return sh_cpu_disassemble_instr_compact(cpu, instr,
		    running, dumpaddr, bintrans);

	if (cpu->byte_order == EMUL_BIG_ENDIAN)
		iword = (instr[0] << 24) + (instr[1] << 16) + (instr[2] << 8)
		    + instr[3];
	else
		iword = (instr[3] << 24) + (instr[2] << 16) + (instr[1] << 8)
		    + instr[0];

	debug(": %08x\t", iword);

	/*
	 *  Decode the instruction:
	 */

	debug("TODO\n");

	return sizeof(iword);
}


#include "tmp_sh_tail.c"

