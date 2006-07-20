/*
 *  Copyright (C) 2006  Anders Gavare.  All rights reserved.
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
 *  $Id: cpu_transputer.c,v 1.1 2006-07-20 21:52:59 debug Exp $
 *
 *  INMOS transputer CPU emulation.
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

#define	DYNTRANS_32
#include "tmp_transputer_head.c"


static char *opcode_names[16] = TRANSPUTER_INSTRUCTIONS;


/*
 *  transputer_cpu_new():
 *
 *  Create a new TRANSPUTER cpu object.
 *
 *  Returns 1 on success, 0 if there was no matching TRANSPUTER processor with
 *  this cpu_type_name.
 */
int transputer_cpu_new(struct cpu *cpu, struct memory *mem,
	struct machine *machine, int cpu_id, char *cpu_type_name)
{
	int i = 0;
	struct transputer_cpu_type_def cpu_type_defs[] =
	    TRANSPUTER_CPU_TYPE_DEFS;

	/*  Scan the cpu_type_defs list for this cpu type:  */
	while (cpu_type_defs[i].name != NULL) {
		if (strcasecmp(cpu_type_defs[i].name, cpu_type_name) == 0) {
			break;
		}
		i++;
	}
	if (cpu_type_defs[i].name == NULL)
		return 0;

	cpu->cd.transputer.cpu_type = cpu_type_defs[i];
	cpu->name = cpu->cd.transputer.cpu_type.name;
	cpu->byte_order = EMUL_LITTLE_ENDIAN;
	cpu->is_32bit = 1;

	if (cpu->cd.transputer.cpu_type.bits != 32) {
		fatal("Only 32-bit Transputer processors can be "
		    "emulated. 16-bit mode is not supported. Sorry.\n");
		exit(1);
	}

	cpu->run_instr = transputer_run_instr;
	cpu->memory_rw = transputer_memory_rw;
	cpu->update_translation_table = transputer_update_translation_table;
	cpu->invalidate_translation_caches =
	    transputer_invalidate_translation_caches;
	cpu->invalidate_code_translation =
	    transputer_invalidate_code_translation;

	/*  Only show name and caches etc for CPU nr 0 (in SMP machines):  */
	if (cpu_id == 0) {
		debug("%s", cpu->name);
	}

	return 1;
}


/*
 *  transputer_cpu_list_available_types():
 *
 *  Print a list of available TRANSPUTER CPU types.
 */
void transputer_cpu_list_available_types(void)
{
	int i = 0, j;
	struct transputer_cpu_type_def tdefs[] = TRANSPUTER_CPU_TYPE_DEFS;
        
	while (tdefs[i].name != NULL) {
		debug("%s", tdefs[i].name);
		for (j = 7 - strlen(tdefs[i].name); j > 0; j --)
			debug(" ");
		i ++;
		if ((i % 8) == 0 || tdefs[i].name == NULL)
			debug("\n");
	}
}


/*
 *  transputer_cpu_dumpinfo():
 */
void transputer_cpu_dumpinfo(struct cpu *cpu)
{
	/*  TODO  */
	debug("\n");
}


/*
 *  transputer_cpu_register_dump():
 *
 *  Dump cpu registers in a relatively readable format.
 *
 *  gprs: set to non-zero to dump GPRs and some special-purpose registers.
 *  coprocs: set bit 0..3 to dump registers in coproc 0..3.
 */
void transputer_cpu_register_dump(struct cpu *cpu, int gprs, int coprocs)
{
	char *symbol;
	uint64_t offset;
	int x = cpu->cpu_id;

	symbol = get_symbol_name(&cpu->machine->symbol_context,
	    cpu->pc, &offset);

	debug("cpu%i: wptr = 0x%08"PRIx32"  oreg = 0x%08"PRIx32"  ip = 0x%08"
	    PRIx32, x, cpu->cd.transputer.wptr, cpu->cd.transputer.oreg,
	    (uint32_t)cpu->pc);
	debug("  <%s>\n", symbol != NULL? symbol : " no symbol ");

	debug("cpu%i:    a = 0x%08"PRIx32"     b = 0x%08"PRIx32"   c = 0x%08"
	    PRIx32"\n", x, cpu->cd.transputer.a, cpu->cd.transputer.b,
	    cpu->cd.transputer.c);

	/*  TODO: Error flags, Floating point registers, etc.  */
}


/*
 *  transputer_cpu_register_match():
 */
void transputer_cpu_register_match(struct machine *m, char *name,
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

	/*  TODO  */
	/*  More register names...  */
}


/*
 *  transputer_cpu_tlbdump():
 *
 *  Called from the debugger to dump the TLB in a readable format.
 *  x is the cpu number to dump, or -1 to dump all CPUs.
 *
 *  If rawflag is nonzero, then the TLB contents isn't formated nicely,
 *  just dumped.
 */
void transputer_cpu_tlbdump(struct machine *m, int x, int rawflag)
{
}


/*
 *  transputer_cpu_gdb_stub():
 *
 *  Execute a "remote GDB" command. Returns a newly allocated response string
 *  on success, NULL on failure.
 */
char *transputer_cpu_gdb_stub(struct cpu *cpu, char *cmd)
{
	fatal("transputer_cpu_gdb_stub(): TODO\n");
	return NULL;
}


/*
 *  transputer_cpu_interrupt():
 */
int transputer_cpu_interrupt(struct cpu *cpu, uint64_t irq_nr)
{
	fatal("transputer_cpu_interrupt(): TODO\n");
	return 0;
}


/*
 *  transputer_cpu_interrupt_ack():
 */
int transputer_cpu_interrupt_ack(struct cpu *cpu, uint64_t irq_nr)
{
	/*  fatal("transputer_cpu_interrupt_ack(): TODO\n");  */
	return 0;
}


/*
 *  transputer_cpu_disassemble_instr():
 *
 *  Convert an instruction word into human readable format, for instruction
 *  tracing. On Transputers, an opcode is one byte, optionally followed by
 *  more instruction bytes.
 *
 *  If running is 1, cpu->pc should be the address of the instruction.
 *
 *  If running is 0, things that depend on the runtime environment (eg.
 *  register contents) will not be shown, and addr will be used instead of
 *  cpu->pc for relative addresses.
 */
int transputer_cpu_disassemble_instr(struct cpu *cpu, unsigned char *ib,
	int running, uint64_t dumpaddr)
{
	uint64_t offset;
	char *symbol;
	int opcode, operand;

	if (running)
		dumpaddr = cpu->pc;

	symbol = get_symbol_name(&cpu->machine->symbol_context,
	    dumpaddr, &offset);
	if (symbol != NULL && offset==0)
		debug("<%s>\n", symbol);

	if (cpu->machine->ncpus > 1 && running)
		debug("cpu%i: ", cpu->cpu_id);

	debug("0x%08x:  ", (int)dumpaddr);

	opcode = ib[0] >> 4;
	operand = ib[0] & 15;

	debug("%02x   ", ib[0]);

	switch (opcode >> 4) {

	default:debug("%-6s %2i\n", opcode_names[opcode], operand);
	}

	return 1;
}


#include "tmp_transputer_tail.c"

