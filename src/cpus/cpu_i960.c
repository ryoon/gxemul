/*
 *  Copyright (C) 2005-2007  Anders Gavare.  All rights reserved.
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
 *  $Id: cpu_i960.c,v 1.13 2006-12-30 13:30:54 debug Exp $
 *
 *  Intel i960 CPU emulation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "cpu.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"
#include "settings.h"
#include "symbol.h"

#define	DYNTRANS_32
#define	DYNTRANS_VARIABLE_INSTRUCTION_LENGTH
#include "tmp_i960_head.c"


/*
 *  i960_cpu_new():
 *
 *  Create a new I960 cpu object.
 *
 *  Returns 1 on success, 0 if there was no matching I960 processor with
 *  this cpu_type_name.
 */
int i960_cpu_new(struct cpu *cpu, struct memory *mem, struct machine *machine,
	int cpu_id, char *cpu_type_name)
{
	if (strcasecmp(cpu_type_name, "i960") != 0)
		return 0;

	cpu->run_instr = i960_run_instr;
	cpu->memory_rw = i960_memory_rw;
	cpu->update_translation_table = i960_update_translation_table;
	cpu->invalidate_translation_caches =
	    i960_invalidate_translation_caches;
	cpu->invalidate_code_translation = i960_invalidate_code_translation;
	cpu->is_32bit = 1;

	cpu->byte_order = EMUL_BIG_ENDIAN;

	/*  Only show name and caches etc for CPU nr 0 (in SMP machines):  */
	if (cpu_id == 0) {
		debug("%s", cpu->name);
	}

	/*  Add all register names to the settings:  */
	CPU_SETTINGS_ADD_REGISTER64("pc", cpu->pc);

	return 1;
}


/*
 *  i960_cpu_list_available_types():
 *
 *  Print a list of available I960 CPU types.
 */
void i960_cpu_list_available_types(void)
{
	debug("i960\n");
	/*  TODO  */
}


/*
 *  i960_cpu_dumpinfo():
 */
void i960_cpu_dumpinfo(struct cpu *cpu)
{
	/*  TODO  */
	debug("\n");
}


/*
 *  i960_cpu_register_dump():
 *
 *  Dump cpu registers in a relatively readable format.
 *
 *  gprs: set to non-zero to dump GPRs and some special-purpose registers.
 *  coprocs: set bit 0..3 to dump registers in coproc 0..3.
 */
void i960_cpu_register_dump(struct cpu *cpu, int gprs, int coprocs)
{
	char *symbol;
	uint64_t offset;
	int x = cpu->cpu_id;

	if (gprs) {
		/*  Special registers (pc, ...) first:  */
		symbol = get_symbol_name(&cpu->machine->symbol_context,
		    cpu->pc, &offset);

		debug("cpu%i: pc  = 0x%08"PRIx32, x, (uint32_t)cpu->pc);
		debug("  <%s>\n", symbol != NULL? symbol : " no symbol ");
	}
}


/*
 *  i960_cpu_tlbdump():
 *
 *  Called from the debugger to dump the TLB in a readable format.
 *  x is the cpu number to dump, or -1 to dump all CPUs.
 *
 *  If rawflag is nonzero, then the TLB contents isn't formated nicely,
 *  just dumped.
 */
void i960_cpu_tlbdump(struct machine *m, int x, int rawflag)
{
}


/*
 *  i960_cpu_gdb_stub():
 *
 *  Execute a "remote GDB" command. Returns a newly allocated response string
 *  on success, NULL on failure.
 */
char *i960_cpu_gdb_stub(struct cpu *cpu, char *cmd)
{
	fatal("i960_cpu_gdb_stub(): TODO\n");
	return NULL;
}


/*
 *  i960_cpu_interrupt():
 */
int i960_cpu_interrupt(struct cpu *cpu, uint64_t irq_nr)
{
	fatal("i960_cpu_interrupt(): TODO\n");
	return 0;
}


/*
 *  i960_cpu_interrupt_ack():
 */
int i960_cpu_interrupt_ack(struct cpu *cpu, uint64_t irq_nr)
{
	/*  fatal("i960_cpu_interrupt_ack(): TODO\n");  */
	return 0;
}


/*  Helper functions:  */
static void print_four(unsigned char *instr, int *len)
{ debug(" %02x%02x%02x%02x", instr[*len], instr[*len+1],
	instr[*len+2], instr[*len+3]); (*len) += 4; }
static void print_spaces(int len) { int i; debug(" "); for (i=0; i<16-len/2*5;
    i++) debug(" "); }


/*
 *  i960_cpu_disassemble_instr():
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
int i960_cpu_disassemble_instr(struct cpu *cpu, unsigned char *ib,
	int running, uint64_t dumpaddr)
{
	uint64_t offset;
	int len = 0;
	char *symbol;

	if (running)
		dumpaddr = cpu->pc;

	symbol = get_symbol_name(&cpu->machine->symbol_context,
	    dumpaddr, &offset);
	if (symbol != NULL && offset==0)
		debug("<%s>\n", symbol);

	if (cpu->machine->ncpus > 1 && running)
		debug("cpu%i: ", cpu->cpu_id);

	debug("0x%08x: ", (int)dumpaddr);

	print_four(ib, &len);

	/*  TODO  */
	print_spaces(len);
	debug("UNIMPLEMENTED 0x%02x%02x\n", ib[0], ib[1]);

	return len;
}


#include "tmp_i960_tail.c"

