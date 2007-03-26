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
 *  $Id: cpu_m68k.c,v 1.17 2007-03-26 02:01:35 debug Exp $
 *
 *  Motorola 68K CPU emulation.
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
#include "tmp_m68k_head.c"


static char *m68k_aname[] = { "a0", "a1", "a2", "a3", "a4", "a5", "fp", "a7" };
static char *m68k_dname[] = { "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7" };


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
	int i = 0;
	struct m68k_cpu_type_def cpu_type_defs[] = M68K_CPU_TYPE_DEFS;

	/*  Scan the cpu_type_defs list for this cpu type:  */
	while (cpu_type_defs[i].name != NULL) {
		if (strcasecmp(cpu_type_defs[i].name, cpu_type_name) == 0) {
			break;
		}
		i++;
	}
	if (cpu_type_defs[i].name == NULL)
		return 0;

	cpu->run_instr = m68k_run_instr;
	cpu->memory_rw = m68k_memory_rw;
	cpu->update_translation_table = m68k_update_translation_table;
	cpu->invalidate_translation_caches =
	    m68k_invalidate_translation_caches;
	cpu->invalidate_code_translation = m68k_invalidate_code_translation;
	cpu->is_32bit = 1;
	cpu->byte_order = EMUL_BIG_ENDIAN;

	cpu->cd.m68k.cpu_type = cpu_type_defs[i];

	/*  Only show name and caches etc for CPU nr 0 (in SMP machines):  */
	if (cpu_id == 0) {
		debug("%s", cpu->name);
	}

	/*  Add all register names to the settings:  */
	CPU_SETTINGS_ADD_REGISTER64("pc", cpu->pc);
	for (i=0; i<N_M68K_AREGS; i++)
		CPU_SETTINGS_ADD_REGISTER32(m68k_aname[i], cpu->cd.m68k.a[i]);
	/*  Both "fp" and "a6" should map to the same register:  */
	CPU_SETTINGS_ADD_REGISTER32("a6", cpu->cd.m68k.a[6]);
	for (i=0; i<N_M68K_DREGS; i++)
		CPU_SETTINGS_ADD_REGISTER32(m68k_dname[i], cpu->cd.m68k.d[i]);

	return 1;
}


/*
 *  m68k_cpu_list_available_types():
 *
 *  Print a list of available M68K CPU types.
 */
void m68k_cpu_list_available_types(void)
{
	int i = 0, j;
	struct m68k_cpu_type_def tdefs[] = M68K_CPU_TYPE_DEFS;

	while (tdefs[i].name != NULL) {
		debug("%s", tdefs[i].name);
		for (j=10 - strlen(tdefs[i].name); j>0; j--)
		        debug(" ");
		i++;
		if ((i % 6) == 0 || tdefs[i].name == NULL)
			debug("\n");
	}
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
	int x = cpu->cpu_id, i;

	if (gprs) {
		/*  Special registers (pc, ...) first:  */
		symbol = get_symbol_name(&cpu->machine->symbol_context,
		    cpu->pc, &offset);

		debug("cpu%i: pc = 0x%08"PRIx32, x, (uint32_t)cpu->pc);
		debug("  <%s>\n", symbol != NULL? symbol : " no symbol ");

		for (i=0; i<N_M68K_AREGS; i++) {
			if ((i % 4) == 0)
				debug("cpu%i:", x);
			debug(" %s = 0x%08"PRIx32" ",
			    m68k_aname[i], cpu->cd.m68k.a[i]);
			if ((i % 4) == 3)
				debug("\n");
		}

		for (i=0; i<N_M68K_DREGS; i++) {
			if ((i % 4) == 0)
				debug("cpu%i:", x);
			debug(" %s = 0x%08"PRIx32" ",
			    m68k_dname[i], cpu->cd.m68k.d[i]);
			if ((i % 4) == 3)
				debug("\n");
		}
	}
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


/*  Helper functions:  */
static void print_two(unsigned char *instr, int *len)
{ debug(" %02x%02x", instr[*len], instr[*len+1]); (*len) += 2; }
static void print_spaces(int len) { int i; debug(" "); for (i=0; i<16-len/2*5;
    i++) debug(" "); }


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
int m68k_cpu_disassemble_instr(struct cpu *cpu, unsigned char *ib,
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

	print_two(ib, &len);

	switch (ib[0] >> 4) {

	case 0x4:
		switch (ib[0] & 0xf) {

		case 0xe:
			if (ib[1] >= 0x50 && ib[1] <= 0x57) {
				print_two(ib, &len);
				print_spaces(len);
				debug("linkw\t%%%s,#%i\n",
				    m68k_aname[ib[1] & 7],
				    ((ib[2] << 8) + ib[3]));
			} else if (ib[1] >= 0x58 && ib[1] <= 0x5f) {
				print_spaces(len);
				debug("unlk\t%%%s\n", m68k_aname[ib[1] & 7]);
			} else if (ib[1] == 0x71) {
				print_spaces(len);
				debug("nop\n");
			} else if (ib[1] == 0x73) {
				print_spaces(len);
				debug("rte\n");
			} else if (ib[1] == 0x74) {
				print_two(ib, &len);
				print_spaces(len);
				debug("rtd\t#0x%04x\n", ((ib[2] << 8) + ib[3]));
			} else if (ib[1] == 0x75) {
				print_spaces(len);
				debug("rts\n");
			} else {
				print_spaces(len);
				debug("UNIMPLEMENTED\n");
			}
			break;

		default:print_spaces(len);
			debug("UNIMPLEMENTED\n");
		}
		break;

	default:print_spaces(len);
		debug("UNIMPLEMENTED\n");
	}

	return len;
}


#include "tmp_m68k_tail.c"

