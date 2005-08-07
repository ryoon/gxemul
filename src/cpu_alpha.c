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
 *  $Id: cpu_alpha.c,v 1.45 2005-08-07 09:26:06 debug Exp $
 *
 *  Alpha CPU emulation.
 *
 *  TODO: Many things.
 *
 *  See http://www.eecs.harvard.edu/~nr/toolkit/specs/alpha.html for info
 *  on instruction formats etc.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "misc.h"


#ifndef	ENABLE_ALPHA


#include "cpu_alpha.h"


/*
 *  alpha_cpu_family_init():
 *
 *  Bogus, when ENABLE_ALPHA isn't defined.
 */
int alpha_cpu_family_init(struct cpu_family *fp)
{
	return 0;
}


#else	/*  ENABLE_ALPHA  */


#include "cpu.h"
#include "machine.h"
#include "memory.h"
#include "symbol.h"

#define	DYNTRANS_MAX_VPH_TLB_ENTRIES		ALPHA_MAX_VPH_TLB_ENTRIES
#define	DYNTRANS_ARCH				alpha
#define	DYNTRANS_ALPHA
#define	DYNTRANS_8K
#define	DYNTRANS_PAGESIZE			8192
#define	DYNTRANS_IC				alpha_instr_call
#define	DYNTRANS_IC_ENTRIES_PER_PAGE		ALPHA_IC_ENTRIES_PER_PAGE
#define	DYNTRANS_TC_PHYSPAGE			alpha_tc_physpage
#define	DYNTRANS_INVALIDATE_TLB_ENTRY		alpha_invalidate_tlb_entry
#define	DYNTRANS_ADDR_TO_PAGENR			ALPHA_ADDR_TO_PAGENR
#define	DYNTRANS_PC_TO_IC_ENTRY			ALPHA_PC_TO_IC_ENTRY
#define	DYNTRANS_TC_ALLOCATE			alpha_tc_allocate_default_page
#define	DYNTRANS_TC_PHYSPAGE			alpha_tc_physpage
#define DYNTRANS_PC_TO_POINTERS			alpha_pc_to_pointers
#define	COMBINE_INSTRUCTIONS			alpha_combine_instructions
#define	DISASSEMBLE				alpha_cpu_disassemble_instr


extern volatile int single_step, single_step_breakpoint;
extern int debugger_n_steps_left_before_interaction;
extern int old_show_trace_tree;   
extern int old_instruction_trace;
extern int old_quiet_mode;
extern int quiet_mode;

/*  instr uses the same names as in cpu_alpha_instr.c  */
#define instr(n) alpha_instr_ ## n

/*  Alpha symbolic register names:  */
static char *alpha_regname[N_ALPHA_REGS] = ALPHA_REG_NAMES; 


/*
 *  alpha_cpu_new():
 *
 *  Create a new Alpha CPU object by filling the CPU struct.
 *  Return 1 on success, 0 if cpu_type_name isn't a valid Alpha processor.
 */
int alpha_cpu_new(struct cpu *cpu, struct memory *mem,
	struct machine *machine, int cpu_id, char *cpu_type_name)
{
	int i;

	if (strcasecmp(cpu_type_name, "Alpha") != 0)
		return 0;

	cpu->memory_rw = alpha_memory_rw;
	cpu->update_translation_table = alpha_update_translation_table;
	cpu->invalidate_translation_caches_paddr =
	    alpha_invalidate_translation_caches_paddr;
	cpu->invalidate_code_translation_caches =
	    alpha_invalidate_code_translation_caches;
	cpu->is_32bit = 0;

	/*  Only show name and caches etc for CPU nr 0:  */
	if (cpu_id == 0) {
		debug("%s", cpu->name);
	}

	/*  Create the default virtual->physical->host translation:  */
	cpu->cd.alpha.vph_default_page = malloc(sizeof(struct alpha_vph_page));
	if (cpu->cd.alpha.vph_default_page == NULL) {
		fprintf(stderr, "out of memory in alpha_cpu_new()\n");
		exit(1);
	}
	memset(cpu->cd.alpha.vph_default_page, 0,
	    sizeof(struct alpha_vph_page));
	for (i=0; i<ALPHA_LEVEL0; i++)
		cpu->cd.alpha.vph_table0[i] = cpu->cd.alpha.vph_table0_kernel[i]
		    = cpu->cd.alpha.vph_default_page;

	return 1;
}


/*
 *  alpha_cpu_dumpinfo():
 */
void alpha_cpu_dumpinfo(struct cpu *cpu)
{
	/*  TODO  */
	debug("\n");
}


/*
 *  alpha_cpu_list_available_types():
 *
 *  Print a list of available Alpha CPU types.
 */
void alpha_cpu_list_available_types(void)
{
	/*  TODO  */

	debug("Alpha\n");
}


/*
 *  alpha_cpu_register_match():
 */
void alpha_cpu_register_match(struct machine *m, char *name,
	int writeflag, uint64_t *valuep, int *match_register)
{
	int i, cpunr = 0;

	/*  CPU number:  */

	/*  TODO  */

	if (strcasecmp(name, "pc") == 0) {
		if (writeflag) {
			m->cpus[cpunr]->pc = *valuep;
		} else
			*valuep = m->cpus[cpunr]->pc;
		*match_register = 1;
	}

	/*  Register names:  */
	for (i=0; i<N_ALPHA_REGS; i++) {
		if (strcasecmp(name, alpha_regname[i]) == 0) {
			if (writeflag)
				m->cpus[cpunr]->cd.alpha.r[i] = *valuep;
			else
				*valuep = m->cpus[cpunr]->cd.alpha.r[i];
			*match_register = 1;
		}
	}
}


/*
 *  alpha_cpu_register_dump():
 *  
 *  Dump cpu registers in a relatively readable format.
 *  
 *  gprs: set to non-zero to dump GPRs and some special-purpose registers.
 *  coprocs: set bit 0..3 to dump registers in coproc 0..3.
 */
void alpha_cpu_register_dump(struct cpu *cpu, int gprs, int coprocs)
{ 
	char *symbol;
	uint64_t offset;
	int i, x = cpu->cpu_id;

	if (gprs) {
		symbol = get_symbol_name(&cpu->machine->symbol_context,
		    cpu->pc, &offset);
		debug("cpu%i:\t pc = 0x%016llx", x, (long long)cpu->pc);
		debug("  <%s>\n", symbol != NULL? symbol : " no symbol ");
		for (i=0; i<N_ALPHA_REGS; i++) {
			int r = (i >> 1) + ((i & 1) << 4);
			if ((i % 2) == 0)
				debug("cpu%i:\t", x);
			if (r != ALPHA_ZERO)
				debug("%3s = 0x%016llx", alpha_regname[r],
				    (long long)cpu->cd.alpha.r[r]);
			debug((i % 2) == 1? "\n" : "   ");
		}
	}
}


/*
 *  alpha_cpu_show_full_statistics():
 *
 *  Show detailed statistics on opcode usage on each cpu.
 */
void alpha_cpu_show_full_statistics(struct machine *m)
{
	fatal("alpha_cpu_show_full_statistics(): TODO\n");
}


/*
 *  alpha_cpu_tlbdump():
 *
 *  Called from the debugger to dump the TLB in a readable format.
 *  x is the cpu number to dump, or -1 to dump all CPUs.
 *
 *  If rawflag is nonzero, then the TLB contents isn't formated nicely,
 *  just dumped.
 */
void alpha_cpu_tlbdump(struct machine *m, int x, int rawflag)
{
	fatal("alpha_cpu_tlbdump(): TODO\n");
}


/*
 *  alpha_cpu_interrupt():
 */
int alpha_cpu_interrupt(struct cpu *cpu, uint64_t irq_nr)
{
	fatal("alpha_cpu_interrupt(): TODO\n");
	return 0;
}


/*
 *  alpha_cpu_interrupt_ack():
 */
int alpha_cpu_interrupt_ack(struct cpu *cpu, uint64_t irq_nr)
{
	/*  fatal("alpha_cpu_interrupt_ack(): TODO\n");  */
	return 0;
}


/*
 *  alpha_print_imm16_disp():
 *
 *  Used internally by alpha_cpu_disassemble_instr().
 */
static void alpha_print_imm16_disp(int imm, int rb)
{
	imm = (int16_t)imm;

	if (imm < 0) {
		debug("-");
		imm = -imm;
	}
	if (imm <= 256)
		debug("%i", imm);
	else
		debug("0x%x", imm);
	if (rb != ALPHA_ZERO)
		debug("(%s)", alpha_regname[rb]);
}


/*
 *  alpha_cpu_disassemble_instr():
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
int alpha_cpu_disassemble_instr(struct cpu *cpu, unsigned char *ib,
        int running, uint64_t dumpaddr, int bintrans)
{
	uint32_t iw;
	uint64_t offset, tmp;
	int opcode, ra, rb, func, rc, imm, floating, rbrc = 0;
	char *symbol, *mnem = NULL;
	char palcode_name[30];

	if (running)
		dumpaddr = cpu->pc;

	symbol = get_symbol_name(&cpu->machine->symbol_context,
	    dumpaddr, &offset);
	if (symbol != NULL && offset == 0)
		debug("<%s>\n", symbol);

	if (cpu->machine->ncpus > 1 && running)
		debug("cpu%i:\t", cpu->cpu_id);

	debug("%016llx:  ", (long long)dumpaddr);

	iw = ib[0] + (ib[1]<<8) + (ib[2]<<16) + (ib[3]<<24);
	debug("%08x\t", (int)iw);

	opcode = iw >> 26;
	ra = (iw >> 21) & 31;
	rb = (iw >> 16) & 31;
	func = (iw >> 5) & 0x7ff;
	rc = iw & 31;
	imm = iw & 0xffff;

	switch (opcode) {
	case 0x00:
		alpha_palcode_name(iw & 0x3ffffff, palcode_name,
		    sizeof(palcode_name));
		debug("call_pal %s\n", palcode_name);
		break;
	case 0x08:
	case 0x09:
		debug("lda%s\t%s,", opcode == 9? "h" : "", alpha_regname[ra]);
		alpha_print_imm16_disp(imm, rb);
		debug("\n");
		break;
	case 0x0a:
	case 0x0b:
	case 0x0c:
	case 0x0d:
	case 0x0e:
	case 0x0f:
	case 0x20:
	case 0x21:
	case 0x22:
	case 0x23:
	case 0x24:
	case 0x25:
	case 0x26:
	case 0x27:
	case 0x28:
	case 0x29:
	case 0x2c:
	case 0x2d:
		floating = 0;
		switch (opcode) {
		case 0x0a: mnem = "ldbu"; break;
		case 0x0b: mnem = "ldq_u"; break;
		case 0x0c: mnem = "ldwu"; break;
		case 0x0d: mnem = "stw"; break;
		case 0x0e: mnem = "stb"; break;
		case 0x0f: mnem = "stq_u"; break;
		case 0x20: mnem = "ldf"; floating = 1; break;
		case 0x21: mnem = "ldg"; floating = 1; break;
		case 0x22: mnem = "lds"; floating = 1; break;
		case 0x23: mnem = "ldt"; floating = 1; break;
		case 0x24: mnem = "stf"; floating = 1; break;
		case 0x25: mnem = "stg"; floating = 1; break;
		case 0x26: mnem = "sts"; floating = 1; break;
		case 0x27: mnem = "stt"; floating = 1; break;
		case 0x28: mnem = "ldl"; break;
		case 0x29: mnem = "ldq"; break;
		case 0x2c: mnem = "stl"; break;
		case 0x2d: mnem = "stq"; break;
		}
		if (opcode == 0x0b && ra == ALPHA_ZERO) {
			debug("unop");
		} else {
			debug("%s\t", mnem);
			if (floating)
				debug("f%i,", ra);
			else
				debug("%s,", alpha_regname[ra]);
			alpha_print_imm16_disp(imm, rb);
		}
		debug("\n");
		break;
	case 0x10:
		switch (func & 0x7f) {
		case 0x00: mnem = "addl"; break;
		case 0x02: mnem = "s4addl"; break;
		case 0x09: mnem = "subl"; break;
		case 0x0b: mnem = "s4subl"; break;
		case 0x0f: mnem = "cmpbge"; break;
		case 0x12: mnem = "s8addl"; break;
		case 0x1b: mnem = "s8subl"; break;
		case 0x1d: mnem = "cmpult"; break;
		case 0x20: mnem = "addq"; break;
		case 0x22: mnem = "s4addq"; break;
		case 0x29: mnem = "subq"; break;
		case 0x2b: mnem = "s4subq"; break;
		case 0x2d: mnem = "cmpeq"; break;
		case 0x32: mnem = "s8addq"; break;
		case 0x3b: mnem = "s8subq"; break;
		case 0x3d: mnem = "cmpule"; break;
		case 0x40: mnem = "addl/v"; break;
		case 0x49: mnem = "subl/v"; break;
		case 0x4d: mnem = "cmplt"; break;
		case 0x60: mnem = "addq/v"; break;
		case 0x69: mnem = "subq/v"; break;
		case 0x6d: mnem = "cmple"; break;
		default:debug("UNIMPLEMENTED opcode 0x%x func 0x%x\n",
			    opcode, func);
		}
		if (mnem == NULL)
			break;
		if (func & 0x80)
			debug("%s\t%s,0x%x,%s\n", mnem,
			    alpha_regname[ra], (rb << 3) + (func >> 8),
			    alpha_regname[rc]);
		else
			debug("%s\t%s,%s,%s\n", mnem, alpha_regname[ra],
			    alpha_regname[rb], alpha_regname[rc]);
		break;
	case 0x11:
		switch (func & 0x7f) {
		case 0x000: mnem = "and"; break;
		case 0x008: mnem = "andnot"; break;
		case 0x014: mnem = "cmovlbs"; break;
		case 0x016: mnem = "cmovlbc"; break;
		case 0x020: mnem = "or"; break;
		case 0x024: mnem = "cmoveq"; break;
		case 0x026: mnem = "cmovne"; break;
		case 0x028: mnem = "ornot"; break;
		case 0x040: mnem = "xor"; break;
		case 0x044: mnem = "cmovlt"; break;
		case 0x046: mnem = "cmovge"; break;
		case 0x048: mnem = "eqv"; break;
		case 0x061: mnem = "amask"; break;
		case 0x064: mnem = "cmovle"; break;
		case 0x066: mnem = "cmovgt"; break;
		default:debug("UNIMPLEMENTED opcode 0x%x func 0x%x\n",
			    opcode, func);
		}
		if (mnem == NULL)
			break;
		/*  Special cases: "nop" etc:  */
		if (func == 0x020 && rc == ALPHA_ZERO)
			debug("nop\n");
		else if (func == 0x020 && (ra == ALPHA_ZERO
		    || rb == ALPHA_ZERO)) {
			if (ra == ALPHA_ZERO && rb == ALPHA_ZERO)
				debug("clr\t%s\n", alpha_regname[rc]);
			else if (ra == ALPHA_ZERO)
				debug("mov\t%s,%s\n", alpha_regname[rb],
				    alpha_regname[rc]);
			else
				debug("mov\t%s,%s\n", alpha_regname[ra],
				    alpha_regname[rc]);
		} else if (func & 0x80)
			debug("%s\t%s,0x%x,%s\n", mnem,
			    alpha_regname[ra], (rb << 3) + (func >> 8),
			    alpha_regname[rc]);
		else
			debug("%s\t%s,%s,%s\n", mnem, alpha_regname[ra],
			    alpha_regname[rb], alpha_regname[rc]);
		break;
	case 0x12:
		switch (func & 0x7f) {
		case 0x02: mnem = "mskbl"; break;
		case 0x06: mnem = "extbl"; break;
		case 0x0b: mnem = "insbl"; break;
		case 0x12: mnem = "mskwl"; break;
		case 0x16: mnem = "extwl"; break;
		case 0x1b: mnem = "inswl"; break;
		case 0x22: mnem = "mskll"; break;
		case 0x26: mnem = "extll"; break;
		case 0x2b: mnem = "insll"; break;
		case 0x30: mnem = "zap"; break;
		case 0x31: mnem = "zapnot"; break;
		case 0x32: mnem = "mskql"; break;
		case 0x34: mnem = "srl"; break;
		case 0x36: mnem = "extql"; break;
		case 0x39: mnem = "sll"; break;
		case 0x3b: mnem = "insql"; break;
		case 0x3c: mnem = "sra"; break;
		case 0x52: mnem = "mskwh"; break;
		case 0x57: mnem = "inswh"; break;
		case 0x5a: mnem = "extwh"; break;
		case 0x62: mnem = "msklh"; break;
		case 0x67: mnem = "inslh"; break;
		case 0x6a: mnem = "extlh"; break;
		case 0x72: mnem = "mskqh"; break;
		case 0x77: mnem = "insqh"; break;
		case 0x7a: mnem = "extqh"; break;
		default:debug("UNIMPLEMENTED opcode 0x%x func 0x%x\n",
			    opcode, func);
		}
		if (mnem == NULL)
			break;
		if (func & 0x80)
			debug("%s\t%s,0x%x,%s\n", mnem,
			    alpha_regname[ra], (rb << 3) + (func >> 8),
			    alpha_regname[rc]);
		else
			debug("%s\t%s,%s,%s\n", mnem, alpha_regname[ra],
			    alpha_regname[rb], alpha_regname[rc]);
		break;
	case 0x13:
		switch (func & 0x7f) {
		case 0x00: mnem = "mull"; break;
		case 0x20: mnem = "mulq"; break;
		case 0x30: mnem = "umulh"; break;
		case 0x40: mnem = "mull/v"; break;
		case 0x60: mnem = "mulq/v"; break;
		default:debug("UNIMPLEMENTED opcode 0x%x func 0x%x\n",
			    opcode, func);
		}
		if (mnem == NULL)
			break;
		if (func & 0x80)
			debug("%s\t%s,0x%x,%s\n", mnem,
			    alpha_regname[ra], (rb << 3) + (func >> 8),
			    alpha_regname[rc]);
		else
			debug("%s\t%s,%s,%s\n", mnem, alpha_regname[ra],
			    alpha_regname[rb], alpha_regname[rc]);
		break;
	case 0x16:
		switch (func & 0x7ff) {
		case 0x080: mnem = "adds"; break;
		case 0x081: mnem = "subs"; break;
		case 0x082: mnem = "muls"; break;
		case 0x083: mnem = "mult"; break;
		case 0x0a0: mnem = "addt"; break;
		case 0x0a1: mnem = "subt"; break;
		case 0x0a2: mnem = "mult"; break;
		case 0x0a3: mnem = "divt"; break;
		case 0x0be: mnem = "cvtqt"; rbrc = 1; break;
		default:debug("UNIMPLEMENTED opcode 0x%x func 0x%x\n",
			    opcode, func);
		}
		if (mnem == NULL)
			break;
		if (rbrc)
			debug("%s\tf%i,f%i\n", mnem, rb, rc);
		else
			debug("%s\tf%i,f%i,f%i\n", mnem, ra, rb, rc);
		break;
	case 0x17:
		switch (func & 0x7ff) {
		case 0x020: mnem = "fabs"; rbrc = 1; break;
		default:debug("UNIMPLEMENTED opcode 0x%x func 0x%x\n",
			    opcode, func);
		}
		if (mnem == NULL)
			break;
		if ((func & 0x7ff) == 0x020 && ra == 31 && rb == 31)
			debug("fclr\tf%i\n", rc);
		else if (rbrc)
			debug("%s\tf%i,f%i\n", mnem, rb, rc);
		else
			debug("%s\tf%i,f%i,f%i\n", mnem, ra, rb, rc);
		break;
	case 0x1a:
		tmp = iw & 0x3fff;
		if (tmp & 0x2000)
			tmp |= 0xffffffffffffc000ULL;
		tmp <<= 2;
		tmp += dumpaddr + sizeof(uint32_t);
		switch ((iw >> 14) & 3) {
		case 0:
		case 1:	if (((iw >> 14) & 3) == 0)
				debug("jmp");
			else
				debug("jsr");
			debug("\t%s,", alpha_regname[ra]);
			debug("(%s),", alpha_regname[rb]);
			debug("0x%llx", (long long)tmp);
			symbol = get_symbol_name(&cpu->machine->symbol_context,
			    tmp, &offset);
			if (symbol != NULL)
				debug("\t<%s>", symbol);
			break;
		case 2:	debug("ret");
			break;
		default:fatal("unimpl JSR!");
		}
		debug("\n");
		break;
	case 0x30:
	case 0x34:
		tmp = iw & 0x1fffff;
		if (tmp & 0x100000)
			tmp |= 0xffffffffffe00000ULL;
		tmp <<= 2;
		tmp += dumpaddr + sizeof(uint32_t);
		debug("%s\t", opcode==0x30? "br" : "bsr");
		if (ra != ALPHA_ZERO)
			debug("%s,", alpha_regname[ra]);
		debug("0x%llx", (long long)tmp);
		symbol = get_symbol_name(&cpu->machine->symbol_context,
		    tmp, &offset);
		if (symbol != NULL)
			debug("\t<%s>", symbol);
		debug("\n");
		break;
	case 0x38:
	case 0x39:
	case 0x3a:
	case 0x3b:
	case 0x3c:
	case 0x3d:
	case 0x3e:
	case 0x3f:
		switch (opcode) {
		case 0x38: mnem = "blbc"; break;
		case 0x39: mnem = "beq"; break;
		case 0x3a: mnem = "blt"; break;
		case 0x3b: mnem = "ble"; break;
		case 0x3c: mnem = "blbs"; break;
		case 0x3d: mnem = "bne"; break;
		case 0x3e: mnem = "bge"; break;
		case 0x3f: mnem = "bgt"; break;
		}
		tmp = iw & 0x1fffff;
		if (tmp & 0x100000)
			tmp |= 0xffffffffffe00000ULL;
		tmp <<= 2;
		tmp += dumpaddr + sizeof(uint32_t);
		debug("%s\t%s,", mnem, alpha_regname[ra]);
		debug("0x%llx", (long long)tmp);
		symbol = get_symbol_name(&cpu->machine->symbol_context,
		    tmp, &offset);
		if (symbol != NULL)
			debug("\t<%s>", symbol);
		debug("\n");
		break;
	default:debug("UNIMPLEMENTED opcode 0x%x\n", opcode);
	}

	return sizeof(uint32_t);
}


#define MEMORY_RW       alpha_userland_memory_rw
#define MEM_ALPHA
#define MEM_USERLAND
#include "memory_rw.c"
#undef MEM_USERLAND
#undef MEM_ALPHA
#undef MEMORY_RW


#include "tmp_alpha_tail.c"


#endif	/*  ENABLE_ALPHA  */
