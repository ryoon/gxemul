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
 *  $Id: cpu_mips_instr.c,v 1.2 2005-11-30 17:12:32 debug Exp $
 *
 *  MIPS instructions.
 *
 *  Individual functions should keep track of cpu->n_translated_instrs.
 *  (If no instruction was executed, then it should be decreased. If, say, 4
 *  instructions were combined into one function and executed, then it should
 *  be increased by 3.)
 */


/*
 *  nop:  Do nothing.
 */
X(nop)
{
}


/*
 *  invalid_32_64:  Attempt to execute a 64-bit instruction on an
 *                  emulated 32-bit processor.
 */
X(invalid_32_64)
{
	fatal("invalid_32_64: TODO\n");
	exit(1);
}


/*
 *  beq:  Branch if equal
 *  bne:  Branch if not equal
 *  b:  Branch (comparing a register to itself, always true)
 *
 *  arg[0] = pointer to rs
 *  arg[1] = pointer to rt
 *  arg[2] = (int32_t) relative offset from the next instruction
 */
X(beq)
{
	MODE_uint_t rs = reg(ic->arg[0]), rt = reg(ic->arg[1]);
	int x = rs == rt;
	cpu->cd.mips.delay_slot = TO_BE_DELAYED;
	ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	cpu->cd.mips.delay_slot = NOT_DELAYED;
	if (x) {
		cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1) <<
		    MIPS_INSTR_ALIGNMENT_SHIFT);
		cpu->pc += (int32_t)ic->arg[2];
		quick_pc_to_pointers(cpu);
	}
}
X(beq_samepage)
{
	MODE_uint_t rs = reg(ic->arg[0]), rt = reg(ic->arg[1]);
	int x = rs == rt;
	cpu->cd.mips.delay_slot = TO_BE_DELAYED;
	ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	cpu->cd.mips.delay_slot = NOT_DELAYED;
	if (x)
		cpu->cd.mips.next_ic = (struct mips_instr_call *) ic->arg[2];
}
X(bne)
{
	MODE_uint_t rs = reg(ic->arg[0]), rt = reg(ic->arg[1]);
	int x = rs != rt;
	cpu->cd.mips.delay_slot = TO_BE_DELAYED;
	ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	cpu->cd.mips.delay_slot = NOT_DELAYED;
	if (x) {
		cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1) <<
		    MIPS_INSTR_ALIGNMENT_SHIFT);
		cpu->pc += (int32_t)ic->arg[2];
		quick_pc_to_pointers(cpu);
	}
}
X(bne_samepage)
{
	MODE_uint_t rs = reg(ic->arg[0]), rt = reg(ic->arg[1]);
	int x = rs != rt;
	cpu->cd.mips.delay_slot = TO_BE_DELAYED;
	ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	cpu->cd.mips.delay_slot = NOT_DELAYED;
	if (x)
		cpu->cd.mips.next_ic = (struct mips_instr_call *) ic->arg[2];
}
X(b)
{
	cpu->cd.mips.delay_slot = TO_BE_DELAYED;
	ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	cpu->cd.mips.delay_slot = NOT_DELAYED;
	cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1) <<
	    MIPS_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (int32_t)ic->arg[2];
	quick_pc_to_pointers(cpu);
}
X(b_samepage)
{
	cpu->cd.mips.delay_slot = TO_BE_DELAYED;
	ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	cpu->cd.mips.delay_slot = NOT_DELAYED;
	cpu->cd.mips.next_ic = (struct mips_instr_call *) ic->arg[2];
}


/*
 *  2-register + immediate:
 *
 *  arg[0] = pointer to rs
 *  arg[1] = pointer to rt
 *  arg[2] = uint32_t immediate value
 */
X(andi)
{
	reg(ic->arg[1]) = reg(ic->arg[0]) & (int32_t)ic->arg[2];
}
X(ori)
{
	reg(ic->arg[1]) = reg(ic->arg[0]) | (int32_t)ic->arg[2];
}
X(xori)
{
	reg(ic->arg[1]) = reg(ic->arg[0]) ^ (int32_t)ic->arg[2];
}


/*
 *  3-register:
 */
X(slt)
{
#ifdef MODE32
	reg(ic->arg[2]) = (int32_t)reg(ic->arg[0]) < (int32_t)reg(ic->arg[1]);
#else
	reg(ic->arg[2]) = (int64_t)reg(ic->arg[0]) < (int64_t)reg(ic->arg[1]);
#endif
}
X(sltu)
{
#ifdef MODE32
	reg(ic->arg[2]) = (uint32_t)reg(ic->arg[0]) < (uint32_t)reg(ic->arg[1]);
#else
	reg(ic->arg[2]) = (uint64_t)reg(ic->arg[0]) < (uint64_t)reg(ic->arg[1]);
#endif
}


/*
 *  addiu:  Add immediate.
 *
 *  arg[0] = pointer to rs
 *  arg[1] = pointer to rt
 *  arg[2] = (int32_t) immediate value
 */
X(addiu)
{
	reg(ic->arg[1]) = reg(ic->arg[0]) + (int32_t)ic->arg[2];
}


/*
 *  daddiu:  Add immediate (64-bit).
 *
 *  arg[0] = pointer to rs
 *  arg[1] = pointer to rt
 *  arg[2] = (int32_t) immediate value
 */
X(daddiu)
{
	reg(ic->arg[1]) = reg(ic->arg[0]) + (int32_t)ic->arg[2];
}


/*
 *  set:  Set a register to an immediate value.
 *
 *  arg[0] = pointer to the register
 *  arg[1] = (int32_t) immediate value
 */
X(set)
{
	reg(ic->arg[0]) = (int32_t)ic->arg[1];
}


/*****************************************************************************/


/*
 *  b_samepage_addiu:
 *
 *  Combination of branch within the same page, followed by addiu.
 */
X(b_samepage_addiu)
{
	reg(ic[1].arg[1]) = reg(ic[1].arg[0]) + (int32_t)ic[1].arg[2];
	cpu->n_translated_instrs ++;
	cpu->cd.mips.next_ic = (struct mips_instr_call *) ic->arg[2];
}


/*****************************************************************************/


X(end_of_page)
{
	/*  Update the PC:  (offset 0, but on the next page)  */
	cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1) <<
	    MIPS_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (MIPS_IC_ENTRIES_PER_PAGE << MIPS_INSTR_ALIGNMENT_SHIFT);

	if (cpu->cd.mips.delay_slot != NOT_DELAYED) {
		fatal("DELAY SLOT cross page boundary: NOT implemented yet!"
		    " pc=0x%llx\n", (long long)cpu->pc);
		exit(1);
	}

	/*  Find the new physical page and update the translation pointers:  */
	quick_pc_to_pointers(cpu);

	/*  end_of_page doesn't count as an executed instruction:  */
	cpu->n_translated_instrs --;
}


/*****************************************************************************/


/*
 *  Combine: [Conditional] branch, followed by addiu.
 */
void COMBINE(b_addiu)(struct cpu *cpu, struct mips_instr_call *ic,
	int low_addr)
{
	int n_back = (low_addr >> MIPS_INSTR_ALIGNMENT_SHIFT)
	    & (MIPS_IC_ENTRIES_PER_PAGE - 1);

	if (n_back < 1)
		return;

	if (ic[-1].f == instr(b_samepage)) {
		ic[-1].f = instr(b_samepage_addiu);
		combined;
	}

	/*  TODO: other branches that are followed by addiu should be here  */
}
        


/*****************************************************************************/


/*
 *  mips_instr_to_be_translated():
 *
 *  Translate an instruction word into an mips_instr_call. ic is filled in with
 *  valid data for the translated instruction, or a "nothing" instruction if
 *  there was a translation failure. The newly translated instruction is then
 *  executed.
 */
X(to_be_translated)
{
	uint64_t addr, low_pc;
	uint32_t iword, imm;
	unsigned char *page;
	unsigned char ib[4];
	int main_opcode, rt, rs, rd, s6, x64 = 0;
	void (*samepage_function)(struct cpu *, struct mips_instr_call *);

	/*  Figure out the (virtual) address of the instruction:  */
	low_pc = ((size_t)ic - (size_t)cpu->cd.mips.cur_ic_page)
	    / sizeof(struct mips_instr_call);
	addr = cpu->pc & ~((MIPS_IC_ENTRIES_PER_PAGE-1)
	    << MIPS_INSTR_ALIGNMENT_SHIFT);
	addr += (low_pc << MIPS_INSTR_ALIGNMENT_SHIFT);
	cpu->pc = addr;
	addr &= ~((1 << MIPS_INSTR_ALIGNMENT_SHIFT) - 1);

	/*  Read the instruction word from memory:  */
	page = cpu->cd.mips.host_load[(uint32_t)addr >> 12];

	if (page != NULL) {
		/*  fatal("TRANSLATION HIT!\n");  */
		memcpy(ib, page + (addr & 0xfff), sizeof(ib));
	} else {
		/*  fatal("TRANSLATION MISS!\n");  */
		if (!cpu->memory_rw(cpu, cpu->mem, addr, ib,
		    sizeof(ib), MEM_READ, CACHE_INSTRUCTION)) {
			fatal("to_be_translated(): "
			    "read failed: TODO\n");
			goto bad;
		}
	}

	iword = *((uint32_t *)&ib[0]);

	if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
		iword = LE32_TO_HOST(iword);
	else
		iword = BE32_TO_HOST(iword);


#define DYNTRANS_TO_BE_TRANSLATED_HEAD
#include "cpu_dyntrans.c"
#undef  DYNTRANS_TO_BE_TRANSLATED_HEAD


	/*
	 *  Translate the instruction:
 	 *
	 *  NOTE: _NEVER_ allow writes to the zero register; all such
	 *  instructions should be made into NOPs.
	 */

	main_opcode = iword >> 26;
	rs = (iword >> 21) & 31;
	rt = (iword >> 16) & 31;
	rd = (iword >> 11) & 31;
	imm = (int16_t)iword;
	s6 = iword & 63;

	switch (main_opcode) {

	case HI6_SPECIAL:
		switch (s6) {

		case SPECIAL_SLT:
		case SPECIAL_SLTU:
			switch (s6) {
			case SPECIAL_SLT:   ic->f = instr(slt); break;
			case SPECIAL_SLTU:  ic->f = instr(sltu); break;
			}
			ic->arg[0] = (size_t)&cpu->cd.mips.gpr[rs];
			ic->arg[1] = (size_t)&cpu->cd.mips.gpr[rt];
			ic->arg[2] = (size_t)&cpu->cd.mips.gpr[rd];
			if (rd == MIPS_GPR_ZERO)
				ic->f = instr(nop);
			break;

		case SPECIAL_SYNC:
			ic->f = instr(nop);
			break;

		default:goto bad;
		}
		break;

	case HI6_BEQ:
	case HI6_BNE:
		samepage_function = NULL;  /*  get rid of a compiler warning  */
		switch (main_opcode) {
		case HI6_BEQ:
			ic->f = instr(beq);
			samepage_function = instr(beq_samepage);
			/*  Special case: comparing a register with itself:  */
			if (rs == rt) {
				ic->f = instr(b);
				samepage_function = instr(b_samepage);
			}
			break;
		case HI6_BNE:
			ic->f = instr(bne);
			samepage_function = instr(bne_samepage);
		}
		ic->arg[0] = (size_t)&cpu->cd.mips.gpr[rs];
		ic->arg[1] = (size_t)&cpu->cd.mips.gpr[rt];
		ic->arg[2] = (imm << MIPS_INSTR_ALIGNMENT_SHIFT)
		    + (addr & 0xffc) + 4;
		/*  Is the offset from the start of the current page still
		    within the same page? Then use the samepage_function:  */
		if ((uint32_t)ic->arg[2] < (MIPS_IC_ENTRIES_PER_PAGE
		    << MIPS_INSTR_ALIGNMENT_SHIFT)) {
			ic->arg[2] = (size_t) (cpu->cd.mips.cur_ic_page +
			    ((ic->arg[2] >> MIPS_INSTR_ALIGNMENT_SHIFT)
			    & (MIPS_IC_ENTRIES_PER_PAGE - 1)));
			ic->f = samepage_function;
		}
		break;

	case HI6_ADDIU:
	case HI6_DADDIU:
	case HI6_ANDI:
	case HI6_ORI:
	case HI6_XORI:
		ic->arg[0] = (size_t)&cpu->cd.mips.gpr[rs];
		ic->arg[1] = (size_t)&cpu->cd.mips.gpr[rt];
		if (main_opcode == HI6_ADDI ||
		    main_opcode == HI6_ADDIU ||
		    main_opcode == HI6_DADDI ||
		    main_opcode == HI6_DADDIU)
			ic->arg[2] = (int16_t)iword;
		else
			ic->arg[2] = (uint16_t)iword;
		switch (main_opcode) {
		case HI6_ADDIU:   ic->f = instr(addiu); break;
		case HI6_DADDIU:  ic->f = instr(daddiu); x64 = 1; break;
		case HI6_ANDI:    ic->f = instr(andi); break;
		case HI6_ORI:     ic->f = instr(ori); break;
		case HI6_XORI:    ic->f = instr(xori); break;
		}
		if (rt == MIPS_GPR_ZERO)
			ic->f = instr(nop);

		if (ic->f == instr(addiu))
			cpu->cd.mips.combination_check = COMBINE(b_addiu);
		break;

	case HI6_LUI:
		ic->f = instr(set);
		ic->arg[0] = (size_t)&cpu->cd.mips.gpr[rt];
		ic->arg[1] = imm << 16;
		if (rt == MIPS_GPR_ZERO)
			ic->f = instr(nop);
		break;

	default:goto bad;
	}

	if (x64)
		ic->f = instr(invalid_32_64);


#define	DYNTRANS_TO_BE_TRANSLATED_TAIL
#include "cpu_dyntrans.c" 
#undef	DYNTRANS_TO_BE_TRANSLATED_TAIL
}

