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
 *  $Id: cpu_m68k.c,v 1.5 2005-11-13 00:14:07 debug Exp $
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
#include "symbol.h"


#define	DYNTRANS_32
#define	DYNTRANS_VARIABLE_INSTRUCTION_LENGTH
#include "tmp_m68k_head.c"


static char *m68k_aname[] = { "a0", "a1", "a2", "a3", "a4", "a5", "fp", "a7" };


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
	cpu->invalidate_translation_caches =
	    m68k_invalidate_translation_caches;
	cpu->invalidate_code_translation = m68k_invalidate_code_translation;
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
	int running, uint64_t dumpaddr, int bintrans)
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

	if (ib[0] == 0x48) {
		if (ib[1] >= 0x40 && ib[1] <= 0x47) {
			print_spaces(len);
			debug("swap\td%i\n", ib[1] & 7);
		} else if (ib[1] >= 0x48 && ib[1] <= 0x4f) {
			print_spaces(len);
			debug("bkpt\t#%i\n", ib[1] & 7);
		} else {
			print_spaces(len);
			debug("UNIMPLEMENTED 0x%02x%02x\n", ib[0], ib[1]);
		}
	} else if (ib[0] == 0x4a) {
		if (ib[1] == 0xfc) {
			print_spaces(len);
			debug("illegal\n");
		} else {
			print_spaces(len);
			debug("UNIMPLEMENTED 0x%02x%02x\n", ib[0], ib[1]);
		}
	} else if (ib[0] == 0x4e) {
		if (ib[1] >= 0x40 && ib[1] <= 0x4f) {
			print_spaces(len);
			debug("trap\t#%i\n", ib[1] & 15);
		} else if (ib[1] >= 0x50 && ib[1] <= 0x57) {
			print_two(ib, &len);
			print_spaces(len);
			debug("linkw\t%%%s,#%i\n", m68k_aname[ib[1] & 7],
			    ((ib[2] << 8) + ib[3]));
		} else if (ib[1] >= 0x58 && ib[1] <= 0x5f) {
			print_spaces(len);
			debug("unlk\t%%%s\n", m68k_aname[ib[1] & 7]);
		} else if (ib[1] == 0x70) {
			print_spaces(len);
			debug("reset\n");
		} else if (ib[1] == 0x71) {
			print_spaces(len);
			debug("nop\n");
		} else if (ib[1] == 0x72) {
			print_two(ib, &len);
			print_spaces(len);
			debug("stop\t#0x%04x\n", ((ib[2] << 8) + ib[3]));
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
			debug("UNIMPLEMENTED 0x%02x%02x\n", ib[0], ib[1]);
		}
	} else {
		print_spaces(len);
		debug("UNIMPLEMENTED 0x%02x%02x\n", ib[0], ib[1]);
	}

	return len;
}


#include "tmp_m68k_tail.c"

