/*
 *  Copyright (C) 2005-2006  Anders Gavare.  All rights reserved.
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
 *  $Id: cpu_avr.c,v 1.9 2006-02-25 18:30:31 debug Exp $
 *
 *  Atmel AVR (8-bit) CPU emulation.
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
#include "tmp_avr_head.c"


/*
 *  avr_cpu_new():
 *
 *  Create a new AVR cpu object.
 *
 *  Returns 1 on success, 0 if there was no matching AVR processor with
 *  this cpu_type_name.
 */
int avr_cpu_new(struct cpu *cpu, struct memory *mem, struct machine *machine,
	int cpu_id, char *cpu_type_name)
{
	int type = 0;

	if (strcasecmp(cpu_type_name, "AVR") == 0 ||
	    strcasecmp(cpu_type_name, "AVR16") == 0)
		type = 16;
	if (strcasecmp(cpu_type_name, "AVR22") == 0)
		type = 22;

	if (type == 0)
		return 0;

	cpu->memory_rw = avr_memory_rw;
	cpu->update_translation_table = avr_update_translation_table;
	cpu->invalidate_translation_caches =
	    avr_invalidate_translation_caches;
	cpu->invalidate_code_translation = avr_invalidate_code_translation;
	cpu->is_32bit = 1;

	cpu->byte_order = EMUL_LITTLE_ENDIAN;

	cpu->cd.avr.is_22bit = (type == 22);
	cpu->cd.avr.pc_mask = cpu->cd.avr.is_22bit? 0x3fffff : 0xffff;

	cpu->cd.avr.sram_mask = 0xff;	/*  256 bytes ram  */
	cpu->cd.avr.sp = cpu->cd.avr.sram_mask - 2;

	/*  Only show name and caches etc for CPU nr 0 (in SMP machines):  */
	if (cpu_id == 0) {
		debug("%s", cpu->name);
	}

	return 1;
}


/*
 *  avr_cpu_list_available_types():
 *
 *  Print a list of available AVR CPU types.
 */
void avr_cpu_list_available_types(void)
{
	debug("AVR\tAVR16\tAVR22\n");
}


/*
 *  avr_cpu_dumpinfo():
 */
void avr_cpu_dumpinfo(struct cpu *cpu)
{
	debug(" (%i-bit program counter)\n",
	    cpu->cd.avr.is_22bit? 22 : 16);
}


/*
 *  avr_cpu_register_dump():
 *
 *  Dump cpu registers in a relatively readable format.
 *
 *  gprs: set to non-zero to dump GPRs and some special-purpose registers.
 *  coprocs: set bit 0..3 to dump registers in coproc 0..3.
 */
void avr_cpu_register_dump(struct cpu *cpu, int gprs, int coprocs)
{
	char *symbol;
	uint64_t offset;
	int i, x = cpu->cpu_id;

	if (gprs) {
		/*  Special registers (pc, ...) first:  */
		symbol = get_symbol_name(&cpu->machine->symbol_context,
		    cpu->pc, &offset);

		debug("cpu%i: sreg = ", x);
		debug("%c", cpu->cd.avr.sreg & AVR_SREG_I? 'I' : 'i');
		debug("%c", cpu->cd.avr.sreg & AVR_SREG_T? 'T' : 't');
		debug("%c", cpu->cd.avr.sreg & AVR_SREG_H? 'H' : 'h');
		debug("%c", cpu->cd.avr.sreg & AVR_SREG_S? 'S' : 's');
		debug("%c", cpu->cd.avr.sreg & AVR_SREG_V? 'V' : 'v');
		debug("%c", cpu->cd.avr.sreg & AVR_SREG_N? 'N' : 'n');
		debug("%c", cpu->cd.avr.sreg & AVR_SREG_Z? 'Z' : 'z');
		debug("%c", cpu->cd.avr.sreg & AVR_SREG_C? 'C' : 'c');
		if (cpu->cd.avr.is_22bit)
			debug("  pc = 0x%06x", (int)cpu->pc);
		else
			debug("  pc = 0x%04x", (int)cpu->pc);
		debug("  <%s>\n", symbol != NULL? symbol : " no symbol ");

		for (i=0; i<N_AVR_REGS; i++) {
			int r = (i >> 3) + ((i & 7) << 2);
			if ((i % 8) == 0)
			        debug("cpu%i:", x);
		        debug(" r%02i=0x%02x", r, cpu->cd.avr.r[r]);
			if ((i % 8) == 7)
				debug("\n");
		}

		debug("cpu%i: x=%i, y=%i, z=%i, sp=0x%04x\n", x,
		    (int)(int16_t)(cpu->cd.avr.r[27]*256 + cpu->cd.avr.r[26]),
		    (int)(int16_t)(cpu->cd.avr.r[29]*256 + cpu->cd.avr.r[28]),
		    (int)(int16_t)(cpu->cd.avr.r[31]*256 + cpu->cd.avr.r[30]),
		    cpu->cd.avr.sp);
	}

	debug("cpu%i: nr of instructions: %lli\n", x,
	    (long long)cpu->machine->ncycles);
	debug("cpu%i: nr of cycles:       %lli\n", x,
	    (long long)(cpu->machine->ncycles + cpu->cd.avr.extra_cycles));
}


/*
 *  avr_cpu_register_match():
 */
void avr_cpu_register_match(struct machine *m, char *name,
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
	} else if (name[0] == 'r' && isdigit((int)name[1])) {
		int nr = atoi(name + 1);
		if (nr >= 0 && nr < N_AVR_REGS) {
			if (writeflag)
				m->cpus[cpunr]->cd.avr.r[nr] = *valuep;
			else
				*valuep = m->cpus[cpunr]->cd.avr.r[nr];
			*match_register = 1;
		}
	}
}


/*
 *  avr_cpu_interrupt():
 */
int avr_cpu_interrupt(struct cpu *cpu, uint64_t irq_nr)
{
	fatal("avr_cpu_interrupt(): TODO\n");
	return 0;
}


/*
 *  avr_cpu_interrupt_ack():
 */
int avr_cpu_interrupt_ack(struct cpu *cpu, uint64_t irq_nr)
{
	/*  fatal("avr_cpu_interrupt_ack(): TODO\n");  */
	return 0;
}


/*  Helper functions:  */
static void print_two(unsigned char *instr, int *len)
{ debug(" %02x %02x", instr[*len], instr[*len+1]); (*len) += 2; }
static void print_spaces(int len) { int i; debug(" "); for (i=0; i<15-len/2*6;
    i++) debug(" "); }


/*
 *  avr_cpu_disassemble_instr():
 *
 *  Convert an instruction word into human readable format, for instruction
 *  tracing and disassembly.
 *
 *  If running is 1, cpu->pc should be the address of the instruction.
 *
 *  If running is 0, things that depend on the runtime environment (eg.
 *  register contents) will not be shown, and addr will be used instead of
 *  cpu->pc for relative addresses.
 */
int avr_cpu_disassemble_instr(struct cpu *cpu, unsigned char *ib,
	int running, uint64_t dumpaddr, int bintrans)
{
	uint64_t offset;
	int len = 0, addr, iw, rd, rr, imm;
	char *symbol;
	char *sreg_names = SREG_NAMES;

	if (running)
		dumpaddr = cpu->pc;

	symbol = get_symbol_name(&cpu->machine->symbol_context,
	    dumpaddr, &offset);
	if (symbol != NULL && offset==0)
		debug("<%s>\n", symbol);

	if (cpu->machine->ncpus > 1 && running)
		debug("cpu%i: ", cpu->cpu_id);

	/*  TODO: 22-bit PC  */
	debug("0x%04x: ", (int)dumpaddr);

	print_two(ib, &len);
	iw = (ib[1] << 8) + ib[0];

	if ((iw & 0xffff) == 0x0000) {
		print_spaces(len);
		debug("nop\n");
	} else if ((iw & 0xfc00) == 0x0c00) {
		print_spaces(len);
		rd = (iw & 0x1f0) >> 4;
		rr = ((iw & 0x200) >> 5) | (iw & 0xf);
		debug("add\tr%i,r%i\n", rd, rr);
	} else if ((iw & 0xfc00) == 0x1c00) {
		print_spaces(len);
		rd = (iw & 0x1f0) >> 4;
		rr = ((iw & 0x200) >> 5) | (iw & 0xf);
		debug("adc\tr%i,r%i\n", rd, rr);
	} else if ((iw & 0xfc00) == 0x2000) {
		print_spaces(len);
		rd = (iw & 0x1f0) >> 4;
		rr = ((iw & 0x200) >> 5) | (iw & 0xf);
		debug("and\tr%i,r%i\n", rd, rr);
	} else if ((iw & 0xfc00) == 0x2c00) {
		print_spaces(len);
		rd = (iw & 0x1f0) >> 4;
		rr = ((iw & 0x200) >> 5) | (iw & 0xf);
		debug("mov\tr%i,r%i\n", rd, rr);
	} else if ((iw & 0xfe0f) == 0x8000) {
		print_spaces(len);
		rd = (iw >> 4) & 31;
		debug("ld\tr%i,Z\n", rd);
	} else if ((iw & 0xfe0f) == 0x8008) {
		print_spaces(len);
		rd = (iw >> 4) & 31;
		debug("ld\tr%i,Y\n", rd);
	} else if ((iw & 0xfe0f) == 0x900c) {
		print_spaces(len);
		rd = (iw >> 4) & 31;
		debug("ld\tr%i,X\n", rd);
	} else if ((iw & 0xfc0f) == 0x900f) {
		print_spaces(len);
		rd = (iw >> 4) & 31;
		debug("%s\tr%i\n", iw & 0x200? "push" : "pop", rd);
	} else if ((iw & 0xfe0f) == 0x9200) {
		print_two(ib, &len);
		addr = (ib[3] << 8) + ib[2];
		print_spaces(len);
		debug("sts\t0x%x,r%i\n", addr, (iw & 0x1f0) >> 4);
	} else if ((iw & 0xfe0f) == 0x9402) {
		print_spaces(len);
		rd = (iw >> 4) & 31;
		debug("swap\tr%i\n", rd);
	} else if ((iw & 0xff0f) == 0x9408) {
		print_spaces(len);
		rd = (iw >> 4) & 7;
		debug("%s%c\n", iw & 0x80? "cl" : "se", sreg_names[rd]);
	} else if ((iw & 0xffef) == 0x9508) {
		/*  ret and reti  */
		print_spaces(len);
		debug("ret%s\n", (iw & 0x10)? "i" : "");
	} else if ((iw & 0xffff) == 0x9588) {
		print_spaces(len);
		debug("sleep\n");
	} else if ((iw & 0xffff) == 0x95a8) {
		print_spaces(len);
		debug("wdr\n");
	} else if ((iw & 0xff00) == 0x9600) {
		print_spaces(len);
		imm = ((iw & 0xc0) >> 2) | (iw & 0xf);
		rd = ((iw >> 4) & 3) * 2 + 24;
		debug("adiw\tr%i:r%i,0x%x\n", rd, rd+1, imm);
	} else if ((iw & 0xe000) == 0xc000) {
		print_spaces(len);
		addr = (int16_t)((iw & 0xfff) << 4);
		addr = (addr >> 3) + dumpaddr + 2;
		debug("%s\t0x%x\n", iw & 0x1000? "rcall" : "rjmp", addr);
	} else if ((iw & 0xf000) == 0xe000) {
		print_spaces(len);
		rd = ((iw >> 4) & 0xf) + 16;
		imm = ((iw >> 4) & 0xf0) | (iw & 0xf);
		debug("ldi\tr%i,0x%x\n", rd, imm);
	} else if ((iw & 0xfc07) == 0xf001) {
/*  TODO: refactor the conditional branch stuff  */
		print_spaces(len);
		addr = (iw >> 3) & 0x7f;
		if (addr >= 64)
			addr -= 128;
		addr = (addr + 1) * 2 + dumpaddr;
		debug("breq\t0x%x\n", addr);
	} else if ((iw & 0xfc07) == 0xf401) {
		print_spaces(len);
		addr = (iw >> 3) & 0x7f;
		if (addr >= 64)
			addr -= 128;
		addr = (addr + 1) * 2 + dumpaddr;
		debug("brne\t0x%x\n", addr);
	} else {
		print_spaces(len);
		debug("UNIMPLEMENTED 0x%04x\n", iw);
	}

	return len;
}


#include "tmp_avr_tail.c"

