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
 *  $Id: cpu_arm.c,v 1.55 2005-08-06 20:58:24 debug Exp $
 *
 *  ARM CPU emulation.
 *
 *  Sources of information refered to in cpu_arm*.c:
 *
 *	(1)  http://www.pinknoise.demon.co.uk/ARMinstrs/ARMinstrs.html
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "misc.h"


#ifndef	ENABLE_ARM


#include "cpu_arm.h"


/*
 *  arm_cpu_family_init():
 *
 *  Bogus, when ENABLE_ARM isn't defined.
 */
int arm_cpu_family_init(struct cpu_family *fp)
{
	return 0;
}


#else	/*  ENABLE_ARM  */


#include "cpu.h"
#include "cpu_arm.h"
#include "machine.h"
#include "memory.h"
#include "symbol.h"

#define	DYNTRANS_MAX_VPH_TLB_ENTRIES		ARM_MAX_VPH_TLB_ENTRIES
#define	DYNTRANS_ARCH				arm
#define	DYNTRANS_ARM
#define	DYNTRANS_32
#define	DYNTRANS_1LEVEL
#define	DYNTRANS_PAGESIZE			4096
#define	DYNTRANS_IC				arm_instr_call
#define	DYNTRANS_IC_ENTRIES_PER_PAGE		ARM_IC_ENTRIES_PER_PAGE
#define	DYNTRANS_TC_PHYSPAGE			arm_tc_physpage
#define	DYNTRANS_INVALIDATE_TLB_ENTRY		arm_invalidate_tlb_entry
#define	DYNTRANS_ADDR_TO_PAGENR			ARM_ADDR_TO_PAGENR
#define	DYNTRANS_PC_TO_IC_ENTRY			ARM_PC_TO_IC_ENTRY
#define DYNTRANS_TC_ALLOCATE			arm_tc_allocate_default_page
#define DYNTRANS_TC_PHYSPAGE			arm_tc_physpage
#define	DYNTRANS_PC_TO_POINTERS			arm_pc_to_pointers
#define	COMBINE_INSTRUCTIONS			arm_combine_instructions
#define	DISASSEMBLE				arm_cpu_disassemble_instr


extern volatile int single_step, single_step_breakpoint;
extern int debugger_n_steps_left_before_interaction;
extern int old_show_trace_tree;
extern int old_instruction_trace;
extern int old_quiet_mode;
extern int quiet_mode;

/*  instr uses the same names as in cpu_arm_instr.c  */
#define instr(n) arm_instr_ ## n

/*  ARM symbolic register names and condition strings:  */
static char *arm_regname[N_ARM_REGS] = ARM_REG_NAMES;
static char *arm_condition_string[16] = ARM_CONDITION_STRINGS;

/*  Data Processing Instructions:  */
static char *arm_dpiname[16] = ARM_DPI_NAMES;
static int arm_dpi_uses_d[16] = { 1,1,1,1,1,1,1,1,0,0,0,0,1,1,1,1 };
static int arm_dpi_uses_n[16] = { 1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,0 };


/*
 *  arm_cpu_new():
 *
 *  Create a new ARM cpu object by filling the CPU struct.
 *  Return 1 on success, 0 if cpu_type_name isn't a valid ARM processor.
 */
int arm_cpu_new(struct cpu *cpu, struct memory *mem,
	struct machine *machine, int cpu_id, char *cpu_type_name)
{
	if (strcmp(cpu_type_name, "ARM") != 0)
		return 0;

	cpu->memory_rw = arm_memory_rw;
	cpu->update_translation_table = arm_update_translation_table;
	cpu->invalidate_translation_caches_paddr =
	    arm_invalidate_translation_caches_paddr;
	cpu->invalidate_code_translation_caches =
	    arm_invalidate_code_translation_caches;
	cpu->is_32bit = 1;

	cpu->cd.arm.flags = ARM_FLAG_I | ARM_FLAG_F | ARM_MODE_USR32;

	/*  Only show name and caches etc for CPU nr 0:  */
	if (cpu_id == 0) {
		debug("%s", cpu->name);
	}

	return 1;
}


/*
 *  arm_cpu_dumpinfo():
 */
void arm_cpu_dumpinfo(struct cpu *cpu)
{
	/*  TODO  */
	debug("\n");
}


/*
 *  arm_cpu_list_available_types():
 *
 *  Print a list of available ARM CPU types.
 */
void arm_cpu_list_available_types(void)
{
	/*  TODO  */

	debug("ARM\n");
}


/*
 *  arm_cpu_register_match():
 */
void arm_cpu_register_match(struct machine *m, char *name,
	int writeflag, uint64_t *valuep, int *match_register)
{
	int i, cpunr = 0;

	/*  CPU number:  */

	/*  TODO  */

	/*  Register names:  */
	for (i=0; i<N_ARM_REGS; i++) {
		if (strcasecmp(name, arm_regname[i]) == 0) {
			if (writeflag) {
				m->cpus[cpunr]->cd.arm.r[i] = *valuep;
				if (i == ARM_PC)
					m->cpus[cpunr]->pc = *valuep;
			} else
				*valuep = m->cpus[cpunr]->cd.arm.r[i];
			*match_register = 1;
		}
	}
}


/*
 *  arm_cpu_register_dump():
 *
 *  Dump cpu registers in a relatively readable format.
 *  
 *  gprs: set to non-zero to dump GPRs and some special-purpose registers.
 *  coprocs: set bit 0..3 to dump registers in coproc 0..3.
 */
void arm_cpu_register_dump(struct cpu *cpu, int gprs, int coprocs)
{
	char *symbol;
	uint64_t offset;
	int mode = cpu->cd.arm.flags & ARM_FLAG_MODE;
	int i, x = cpu->cpu_id;

	if (gprs) {
		symbol = get_symbol_name(&cpu->machine->symbol_context,
		    cpu->cd.arm.r[ARM_PC], &offset);
		debug("cpu%i:  flags = ", x);
		debug("%s%s%s%s%s%s",
		    (cpu->cd.arm.flags & ARM_FLAG_N)? "N" : "n",
		    (cpu->cd.arm.flags & ARM_FLAG_Z)? "Z" : "z",
		    (cpu->cd.arm.flags & ARM_FLAG_C)? "C" : "c",
		    (cpu->cd.arm.flags & ARM_FLAG_V)? "V" : "v",
		    (cpu->cd.arm.flags & ARM_FLAG_I)? "I" : "i",
		    (cpu->cd.arm.flags & ARM_FLAG_F)? "F" : "f");
		if (mode < ARM_MODE_USR32)
			debug("   pc =  0x%07x",
			    (int)(cpu->cd.arm.r[ARM_PC] & 0x03ffffff));
		else
			debug("   pc = 0x%08x", (int)cpu->cd.arm.r[ARM_PC]);

		/*  TODO: Flags  */

		debug("  <%s>\n", symbol != NULL? symbol : " no symbol ");

		for (i=0; i<N_ARM_REGS; i++) {
			if ((i % 4) == 0)
				debug("cpu%i:", x);
			if (i != ARM_PC)
				debug("  %s = 0x%08x", arm_regname[i],
				    (int)cpu->cd.arm.r[i]);
			if ((i % 4) == 3)
				debug("\n");
		}
	}
}


/*
 *  arm_cpu_show_full_statistics():
 *
 *  Show detailed statistics on opcode usage on each cpu.
 */
void arm_cpu_show_full_statistics(struct machine *m)
{
	fatal("arm_cpu_show_full_statistics(): TODO\n");
}


/*
 *  arm_cpu_tlbdump():
 *
 *  Called from the debugger to dump the TLB in a readable format.
 *  x is the cpu number to dump, or -1 to dump all CPUs.
 *
 *  If rawflag is nonzero, then the TLB contents isn't formated nicely,
 *  just dumped.
 */
void arm_cpu_tlbdump(struct machine *m, int x, int rawflag)
{
	fatal("arm_cpu_tlbdump(): TODO\n");
}


/*
 *  arm_cpu_interrupt():
 */
int arm_cpu_interrupt(struct cpu *cpu, uint64_t irq_nr)
{
	fatal("arm_cpu_interrupt(): TODO\n");
	return 0;
}


/*
 *  arm_cpu_interrupt_ack():
 */
int arm_cpu_interrupt_ack(struct cpu *cpu, uint64_t irq_nr)
{
	/*  fatal("arm_cpu_interrupt_ack(): TODO\n");  */
	return 0;
}


/*
 *  arm_cpu_disassemble_instr():
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
int arm_cpu_disassemble_instr(struct cpu *cpu, unsigned char *ib,
        int running, uint64_t dumpaddr, int bintrans)
{
	uint32_t iw, tmp;
	int main_opcode, secondary_opcode, s_bit, r16, r12, r8;
	int i, n, p_bit, u_bit, b_bit, w_bit, l_bit;
	char *symbol, *condition;
	uint64_t offset;

	if (running)
		dumpaddr = cpu->pc;

	symbol = get_symbol_name(&cpu->machine->symbol_context,
	    dumpaddr, &offset);
	if (symbol != NULL && offset == 0)
		debug("<%s>\n", symbol);

	if (cpu->machine->ncpus > 1 && running)
		debug("cpu%i:\t", cpu->cpu_id);

	debug("%08x:  ", (int)dumpaddr);

	if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
		iw = ib[0] + (ib[1]<<8) + (ib[2]<<16) + (ib[3]<<24);
	else
		iw = ib[3] + (ib[2]<<8) + (ib[1]<<16) + (ib[0]<<24);
	debug("%08x\t", (int)iw);

	condition = arm_condition_string[iw >> 28];
	main_opcode = (iw >> 24) & 15;
	secondary_opcode = (iw >> 21) & 15;
	u_bit = (iw >> 23) & 1;
	b_bit = (iw >> 22) & 1;
	w_bit = (iw >> 21) & 1;
	s_bit = l_bit = (iw >> 20) & 1;
	r16 = (iw >> 16) & 15;
	r12 = (iw >> 12) & 15;
	r8 = (iw >> 8) & 15;

	switch (main_opcode) {
	case 0x0:
	case 0x1:
	case 0x2:
	case 0x3:
		/*
		 *  See (1):
		 *  xxxx000a aaaSnnnn ddddcccc ctttmmmm  Register form
		 *  xxxx001a aaaSnnnn ddddrrrr bbbbbbbb  Immediate form
		 */
		if (iw & 0x80 && !(main_opcode & 2)) {
			debug("UNIMPLEMENTED reg (c!=0)\n");
			break;
		}

		debug("%s%s%s\t", arm_dpiname[secondary_opcode],
		    condition, s_bit? "s" : "");
		if (arm_dpi_uses_d[secondary_opcode])
			debug("%s,", arm_regname[r12]);
		if (arm_dpi_uses_n[secondary_opcode])
			debug("%s,", arm_regname[r16]);

		if (main_opcode & 2) {
			/*  Immediate form:  */
			int r = (iw >> 7) & 30;
			uint32_t b = iw & 0xff;
			while (r-- > 0)
				b = (b >> 1) | ((b & 1) << 31);
			if (b < 15)
				debug("#%i", b);
			else
				debug("#0x%x", b);
		} else {
			/*  Register form:  */
			int t = (iw >> 4) & 7;
			int c = (iw >> 7) & 31;
			debug("%s", arm_regname[iw & 15]);
			switch (t) {
			case 0:	if (c != 0)
					debug(" LSL #%i", c);
				break;
			case 1:	debug(" LSL %s", arm_regname[c >> 1]);
				break;
			case 2:	debug(" LSR #%i", c? c : 32);
				break;
			case 3:	debug(" LSR %s", arm_regname[c >> 1]);
				break;
			case 4:	debug(" ASR #%i", c? c : 32);
				break;
			case 5:	debug(" ASR %s", arm_regname[c >> 1]);
				break;
			case 6:	if (c != 0)
					debug(" ROR #%i", c);
				else
					debug(" RRX");
				break;
			case 7:	debug(" ROR %s", arm_regname[c >> 1]);
				break;
			}
		}
		debug("\n");
		break;
	case 0x4:				/*  Single Data Transfer  */
	case 0x5:
	case 0x6:
	case 0x7:
		/*
		 *  See (1):
		 *  xxxx010P UBWLnnnn ddddoooo oooooooo  Immediate form
		 *  xxxx011P UBWLnnnn ddddcccc ctt0mmmm  Register form
		 */
		p_bit = main_opcode & 1;
		if (main_opcode >= 6 && iw & 0x10) {
			debug("TODO: single data transf. but 0x10\n");
			break;
		}
		debug("%s%s%s", l_bit? "ldr" : "str",
		    condition, b_bit? "b" : "");
		if (!p_bit && w_bit)
			debug("t");
		debug("\t%s,[%s", arm_regname[r12], arm_regname[r16]);
		if (main_opcode < 6) {
			/*  Immediate form:  */
			uint32_t imm = iw & 0xfff;
			if (!p_bit)
				debug("]");
			if (imm != 0)
				debug(",#%s%i", u_bit? "" : "-", imm);
			if (p_bit)
				debug("]");
		} else {
			debug(" TODO: REG-form]");
		}
		debug("%s\n", (p_bit && w_bit)? "!" : "");
		break;
	case 0x8:				/*  Block Data Transfer  */
	case 0x9:
		/*  See (1):  xxxx100P USWLnnnn llllllll llllllll  */
		p_bit = main_opcode & 1;
		s_bit = b_bit;
		debug("%s%s", l_bit? "ldm" : "stm", condition);
		switch (u_bit * 2 + p_bit) {
		case 0:	debug("da"); break;
		case 1:	debug("db"); break;
		case 2:	debug("ia"); break;
		case 3:	debug("ib"); break;
		}
		debug("\t%s", arm_regname[r16]);
		if (w_bit)
			debug("!");
		debug(",{");
		n = 0;
		for (i=0; i<16; i++)
			if ((iw >> i) & 1) {
				debug("%s%s", (n > 0)? ",":"", arm_regname[i]);
				n++;
			}
		debug("}");
		if (s_bit)
			debug("^");
		debug("\n");
		break;
	case 0xa:				/*  B: branch  */
	case 0xb:				/*  BL: branch and link  */
		debug("b%s%s\t", main_opcode == 0xa? "" : "l", condition);
		tmp = (iw & 0x00ffffff) << 2;
		if (tmp & 0x02000000)
			tmp |= 0xfc000000;
		tmp = (int32_t)(dumpaddr + tmp + 8);
		debug("0x%x", (int)tmp);
		symbol = get_symbol_name(&cpu->machine->symbol_context,
		    tmp, &offset);
		if (symbol != NULL)
			debug("\t\t<%s>", symbol);
		debug("\n");
		break;
	case 0xc:				/*  Coprocessor  */
	case 0xd:				/*  LDC/STC  */
		/*  xxxx110P UNWLnnnn DDDDpppp oooooooo LDC/STC  */
		debug("TODO: coprocessor LDC/STC\n");
		break;
	case 0xe:				/*  CDP (Coprocessor Op)  */
		/*				    or MRC/MCR!
		 *  According to (1):
		 *  xxxx1110 oooonnnn ddddpppp qqq0mmmm		CDP
		 *  xxxx1110 oooLNNNN ddddpppp qqq1MMMM		MRC/MCR
		 */
		if (iw & 0x10) {
			debug("%s%s\t",
			    (iw & 0x00100000)? "mrc" : "mcr", condition);
			debug("%i,%i,r%i,cr%i,cr%i,%i",
			    (int)((iw >> 8) & 15), (int)((iw >>21) & 7),
			    (int)((iw >>12) & 15), (int)((iw >>16) & 15),
			    (int)((iw >> 0) & 15), (int)((iw >> 5) & 7));
		} else {
			debug("cdp%s\t", condition);
			debug("%i,%i,cr%i,cr%i,cr%i",
			    (int)((iw >> 8) & 15),
			    (int)((iw >>20) & 15),
			    (int)((iw >>12) & 15),
			    (int)((iw >>16) & 15),
			    (int)((iw >> 0) & 15));
			if ((iw >> 5) & 7)
				debug(",0x%x", (int)((iw >> 5) & 7));
		}
		debug("\n");
		break;
	case 0xf:				/*  SWI  */
		debug("swi%s\t", condition);
		debug("0x%x\n", (int)(iw & 0x00ffffff));
		break;
	default:debug("UNIMPLEMENTED\n");
	}

	return sizeof(uint32_t);
}


#include "tmp_arm_tail.c"


#endif	/*  ENABLE_ARM  */
