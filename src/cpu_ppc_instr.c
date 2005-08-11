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
 *  $Id: cpu_ppc_instr.c,v 1.12 2005-08-11 21:13:45 debug Exp $
 *
 *  POWER/PowerPC instructions.
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
 *  invalid:  To catch bugs.
 */
X(invalid)
{
	fatal("INTERNAL ERROR\n");
	exit(1);
}


/*
 *  addi:  Add immediate.
 *
 *  arg[0] = pointer to source uint64_t
 *  arg[1] = immediate value (int32_t or larger)
 *  arg[2] = pointer to destination uint64_t
 */
X(addi)
{
	reg(ic->arg[2]) = reg(ic->arg[0]) + (int32_t)ic->arg[1];
}


/*
 *  b:  Branch (to a different translated page)
 *
 *  arg[0] = relative offset (as an int32_t)
 */
X(b)
{
	uint64_t low_pc;

	/*  Calculate new PC from this instruction + arg[0]  */
	low_pc = ((size_t)ic - (size_t)
	    cpu->cd.ppc.cur_ic_page) / sizeof(struct ppc_instr_call);
	cpu->pc &= ~((PPC_IC_ENTRIES_PER_PAGE-1) << 2);
	cpu->pc += (low_pc << 2);
	cpu->pc += (int32_t)ic->arg[0];

	/*  Find the new physical page and update the translation pointers:  */
	ppc_pc_to_pointers(cpu);
}


/*
 *  b_samepage:  Branch (to within the same translated page)
 *
 *  arg[0] = pointer to new ppc_instr_call
 */
X(b_samepage)
{
	cpu->cd.ppc.next_ic = (struct ppc_instr_call *) ic->arg[0];
}


/*
 *  bl:  Branch and Link (to a different translated page)
 *
 *  arg[0] = relative offset (as an int32_t)
 */
X(bl)
{
	uint32_t low_pc;

	/*  Calculate LR:  */
	low_pc = ((size_t)cpu->cd.ppc.next_ic - (size_t)
	    cpu->cd.ppc.cur_ic_page) / sizeof(struct ppc_instr_call);
	cpu->cd.ppc.lr = cpu->pc & ~((PPC_IC_ENTRIES_PER_PAGE-1) << 2);
	cpu->cd.ppc.lr += (low_pc << 2);

	/*  Calculate new PC from this instruction + arg[0]  */
	low_pc = ((size_t)ic - (size_t)
	    cpu->cd.ppc.cur_ic_page) / sizeof(struct ppc_instr_call);
	cpu->pc &= ~((PPC_IC_ENTRIES_PER_PAGE-1) << 2);
	cpu->pc += (low_pc << 2);
	cpu->pc += (int32_t)ic->arg[0];

	/*  Find the new physical page and update the translation pointers:  */
	ppc_pc_to_pointers(cpu);
}


/*
 *  bl_trace:  Branch and Link (to a different translated page)  (with trace)
 *
 *  arg[0] = relative offset (as an int32_t)
 */
X(bl_trace)
{
	uint32_t low_pc;

	/*  Calculate LR:  */
	low_pc = ((size_t)cpu->cd.ppc.next_ic - (size_t)
	    cpu->cd.ppc.cur_ic_page) / sizeof(struct ppc_instr_call);
	cpu->cd.ppc.lr = cpu->pc & ~((PPC_IC_ENTRIES_PER_PAGE-1) << 2);
	cpu->cd.ppc.lr += (low_pc << 2);

	/*  Calculate new PC from this instruction + arg[0]  */
	low_pc = ((size_t)ic - (size_t)
	    cpu->cd.ppc.cur_ic_page) / sizeof(struct ppc_instr_call);
	cpu->pc &= ~((PPC_IC_ENTRIES_PER_PAGE-1) << 2);
	cpu->pc += (low_pc << 2);
	cpu->pc += (int32_t)ic->arg[0];

	cpu_functioncall_trace(cpu, cpu->pc);

	/*  Find the new physical page and update the translation pointers:  */
	ppc_pc_to_pointers(cpu);
}


/*
 *  bl_samepage:  Branch and Link (to within the same translated page)
 *
 *  arg[0] = pointer to new ppc_instr_call
 */
X(bl_samepage)
{
	uint32_t low_pc;

	/*  Calculate LR:  */
	low_pc = ((size_t)cpu->cd.ppc.next_ic - (size_t)
	    cpu->cd.ppc.cur_ic_page) / sizeof(struct ppc_instr_call);
	cpu->cd.ppc.lr = cpu->pc & ~((PPC_IC_ENTRIES_PER_PAGE-1) << 2);
	cpu->cd.ppc.lr += (low_pc << 2);

	cpu->cd.ppc.next_ic = (struct ppc_instr_call *) ic->arg[0];
}


/*
 *  bl_samepage_trace:  Branch and Link (to within the same translated page)
 *
 *  arg[0] = pointer to new ppc_instr_call
 */
X(bl_samepage_trace)
{
	uint32_t low_pc;

	/*  Calculate LR:  */
	low_pc = ((size_t)cpu->cd.ppc.next_ic - (size_t)
	    cpu->cd.ppc.cur_ic_page) / sizeof(struct ppc_instr_call);
	cpu->cd.ppc.lr = cpu->pc & ~((PPC_IC_ENTRIES_PER_PAGE-1) << 2);
	cpu->cd.ppc.lr += (low_pc << 2);

	cpu->cd.ppc.next_ic = (struct ppc_instr_call *) ic->arg[0];

	/*  Calculate new PC (for the trace)  */
	low_pc = ((size_t)cpu->cd.ppc.next_ic - (size_t)
	    cpu->cd.ppc.cur_ic_page) / sizeof(struct ppc_instr_call);
	cpu->pc &= ~((PPC_IC_ENTRIES_PER_PAGE-1) << 2);
	cpu->pc += (low_pc << 2);
	cpu_functioncall_trace(cpu, cpu->pc);
}


/*
 *  crxor:  Condition Register XOR
 *
 *  arg[0] = copy of the instruction word
 */
X(crxor)
{
	uint32_t iword = ic->arg[0];
	int bt = (iword >> 21) & 31;
	int ba = (iword >> 16) & 31;
	int bb = (iword >> 11) & 31;
	ba = (cpu->cd.ppc.cr >> (31-ba)) & 1;
	bb = (cpu->cd.ppc.cr >> (31-bb)) & 1;
	cpu->cd.ppc.cr &= ~(1 << (31-bt));
	if (ba ^ bb)
		cpu->cd.ppc.cr |= (1 << (31-bt));
}


/*
 *  mflr:  Move from Link Register
 *
 *  arg[0] = pointer to destination register
 */
X(mflr)
{
	reg(ic->arg[0]) = cpu->cd.ppc.lr;
}


/*
 *  mfmsr:  Move From MSR
 *
 *  arg[0] = pointer to destination register
 */
X(mfmsr)
{
	reg_access_msr(cpu, (uint64_t*)ic->arg[0], 0);
}


/*
 *  mtmsr:  Move To MSR
 *
 *  arg[0] = pointer to source register
 */
X(mtmsr)
{
	reg_access_msr(cpu, (uint64_t*)ic->arg[0], 1);
}


/*
 *  or:  Or.
 *
 *  arg[0] = pointer to source register rs
 *  arg[1] = pointer to source register rb
 *  arg[2] = pointer to destination register ra
 */
X(or)
{
	reg(ic->arg[2]) = reg(ic->arg[0]) | reg(ic->arg[1]);
}


/*
 *  ori:  OR immediate.
 *
 *  arg[0] = pointer to source uint64_t
 *  arg[1] = immediate value (uint32_t or larger)
 *  arg[2] = pointer to destination uint64_t
 */
X(ori)
{
	reg(ic->arg[2]) = reg(ic->arg[0]) | (uint32_t)ic->arg[1];
}


/*
 *  user_syscall:  Userland syscall.
 *
 *  arg[0] = syscall "level" (usually 0)
 */
X(user_syscall)
{
	useremul_syscall(cpu, ic->arg[0]);
}


/*
 *  xori:  XOR immediate.
 *
 *  arg[0] = pointer to source uint64_t
 *  arg[1] = immediate value (uint32_t or larger)
 *  arg[2] = pointer to destination uint64_t
 */
X(xori)
{
	reg(ic->arg[2]) = reg(ic->arg[0]) ^ (uint32_t)ic->arg[1];
}


#include "tmp_ppc_loadstore.c"


/*****************************************************************************/


X(end_of_page)
{
	/*  Update the PC:  (offset 0, but on the next page)  */
	cpu->pc &= ~((PPC_IC_ENTRIES_PER_PAGE-1) << 2);
	cpu->pc += (PPC_IC_ENTRIES_PER_PAGE << 2);

	/*  Find the new physical page and update the translation pointers:  */
	ppc_pc_to_pointers(cpu);

	/*  end_of_page doesn't count as an executed instruction:  */
	cpu->n_translated_instrs --;
}


/*****************************************************************************/


/*
 *  ppc_combine_instructions():
 *
 *  Combine two or more instructions, if possible, into a single function call.
 */
void COMBINE_INSTRUCTIONS(struct cpu *cpu, struct ppc_instr_call *ic,
	uint32_t addr)
{
	int n_back;
	n_back = (addr >> 2) & (PPC_IC_ENTRIES_PER_PAGE-1);

	if (n_back >= 1) {
		/*  TODO  */
	}

	/*  TODO: Combine forward as well  */
}


/*****************************************************************************/


/*
 *  ppc_instr_to_be_translated():
 *
 *  Translate an instruction word into an ppc_instr_call. ic is filled in with
 *  valid data for the translated instruction, or a "nothing" instruction if
 *  there was a translation failure. The newly translated instruction is then
 *  executed.
 */
X(to_be_translated)
{
	uint64_t addr, low_pc, tmp_addr;
	uint32_t iword;
	unsigned char *page;
	unsigned char ib[4];
	int main_opcode, rt, rs, ra, rb, rc, aa_bit, l_bit, lk_bit, spr,
	    xo, imm, load, size, update, zero;
	void (*samepage_function)(struct cpu *, struct ppc_instr_call *);

	/*  Figure out the (virtual) address of the instruction:  */
	low_pc = ((size_t)ic - (size_t)cpu->cd.ppc.cur_ic_page)
	    / sizeof(struct ppc_instr_call);
	addr = cpu->pc & ~((PPC_IC_ENTRIES_PER_PAGE-1) << 2);
	addr += (low_pc << 2);
	cpu->pc = addr;
	addr &= ~0x3;

	/*  Read the instruction word from memory:  */
	page = cpu->cd.ppc.host_load[addr >> 12];

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

#ifdef HOST_LITTLE_ENDIAN
	if (cpu->byte_order == EMUL_BIG_ENDIAN)
#else
	if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
#endif
		iword = ((iword & 0xff) << 24) |
			((iword & 0xff00) << 8) |
			((iword & 0xff0000) >> 8) |
			((iword & 0xff000000) >> 24);


#define DYNTRANS_TO_BE_TRANSLATED_HEAD
#include "cpu_dyntrans.c"
#undef  DYNTRANS_TO_BE_TRANSLATED_HEAD


	/*
	 *  Translate the instruction:
	 */

	main_opcode = iword >> 26;

	switch (main_opcode) {

	case PPC_HI6_ADDI:
	case PPC_HI6_ADDIS:
		rt = (iword >> 21) & 31; ra = (iword >> 16) & 31;
		ic->f = instr(addi);
		if (ra == 0)
			ic->arg[0] = (size_t)(&cpu->cd.ppc.zero);
		else
			ic->arg[0] = (size_t)(&cpu->cd.ppc.gpr[ra]);
		ic->arg[1] = (ssize_t)(int16_t)(iword & 0xffff);
		if (main_opcode == PPC_HI6_ADDIS)
			ic->arg[1] <<= 16;
		ic->arg[2] = (size_t)(&cpu->cd.ppc.gpr[rt]);
		break;

	case PPC_HI6_ORI:
	case PPC_HI6_ORIS:
	case PPC_HI6_XORI:
	case PPC_HI6_XORIS:
		rs = (iword >> 21) & 31; ra = (iword >> 16) & 31;
		if (main_opcode == PPC_HI6_ORI ||
		    main_opcode == PPC_HI6_ORIS)
			ic->f = instr(ori);
		else
			ic->f = instr(xori);
		ic->arg[0] = (size_t)(&cpu->cd.ppc.gpr[rs]);
		ic->arg[1] = iword & 0xffff;
		if (main_opcode == PPC_HI6_ORIS ||
		    main_opcode == PPC_HI6_XORIS)
			ic->arg[1] <<= 16;
		ic->arg[2] = (size_t)(&cpu->cd.ppc.gpr[ra]);
		break;

	case PPC_HI6_LBZ:
	case PPC_HI6_LBZU:
	case PPC_HI6_LHZ:
	case PPC_HI6_LHZU:
	case PPC_HI6_LWZ:
	case PPC_HI6_LWZU:
	case PPC_HI6_STB:
	case PPC_HI6_STBU:
	case PPC_HI6_STH:
	case PPC_HI6_STHU:
	case PPC_HI6_STW:
	case PPC_HI6_STWU:
		rs = (iword >> 21) & 31;
		ra = (iword >> 16) & 31;
		imm = (int16_t)(iword & 0xffff);
		load = 0; zero = 1; size = 0; update = 0;
		switch (main_opcode) {
		case PPC_HI6_LBZ:  load = 1; break;
		case PPC_HI6_LBZU: load = 1; update = 1; break;
		case PPC_HI6_LHZ:  load = 1; size = 1; break;
		case PPC_HI6_LHZU: load = 1; size = 1; update = 1; break;
		case PPC_HI6_LWZ:  load = 1; size = 2; break;
		case PPC_HI6_LWZU: load = 1; size = 2; update = 1; break;
		case PPC_HI6_STB:  break;
		case PPC_HI6_STBU: update = 1; break;
		case PPC_HI6_STH:  size = 1; break;
		case PPC_HI6_STHU: size = 1; update = 1; break;
		case PPC_HI6_STW:  size = 2; break;
		case PPC_HI6_STWU: size = 2; update = 1; break;
		}
		ic->f =
#ifdef MODE32
		    ppc32_loadstore
#else
		    ppc_loadstore
#endif
		    [size + 4*zero + 8*load + (imm==0? 16 : 0) + 32*update];

		if (ra == 0 && update) {
			fatal("TODO: ra=0 && update?\n");
			goto bad;
		}

		ic->arg[0] = (size_t)(&cpu->cd.ppc.gpr[rs]);
		ic->arg[1] = (size_t)(&cpu->cd.ppc.gpr[ra]);
		ic->arg[2] = (ssize_t)imm;
		break;

	case PPC_HI6_SC:
		ic->arg[0] = (iword >> 5) & 0x7f;
		if (cpu->machine->userland_emul != NULL)
			ic->f = instr(user_syscall);
		else {
			fatal("PPC non-userland SYSCALL: TODO\n");
			goto bad;
		}
		break;

	case PPC_HI6_B:
		aa_bit = (iword & 2) >> 1;
		lk_bit = iword & 1;
		if (aa_bit) {
			fatal("aa_bit: NOT YET\n");
			goto bad;
		}
		tmp_addr = (int64_t)(int32_t)((iword & 0x03fffffc) << 6);
		tmp_addr = (int64_t)tmp_addr >> 6;
		if (lk_bit) {
			if (cpu->machine->show_trace_tree) {
				ic->f = instr(bl_trace);
				samepage_function = instr(bl_samepage_trace);
			} else {
				ic->f = instr(bl);
				samepage_function = instr(bl_samepage);
			}
		} else {
			ic->f = instr(b);
			samepage_function = instr(b_samepage);
		}
		ic->arg[0] = (ssize_t)tmp_addr;
		/*  Branches are calculated as cur PC + offset.  */
		/*  Special case: branch within the same page:  */
		{
			uint64_t mask_within_page =
			    ((PPC_IC_ENTRIES_PER_PAGE-1) << 2) | 3;
			uint64_t old_pc = addr;
			uint64_t new_pc = old_pc + (int32_t)ic->arg[0];
			if ((old_pc & ~mask_within_page) ==
			    (new_pc & ~mask_within_page)) {
				ic->f = samepage_function;
				ic->arg[0] = (size_t) (
				    cpu->cd.ppc.cur_ic_page +
				    ((new_pc & mask_within_page) >> 2));
			}
		}
		break;

	case PPC_HI6_19:
		xo = (iword >> 1) & 1023;
		switch (xo) {

		case PPC_19_ISYNC:
			/*  TODO  */
			ic->f = instr(nop);
			break;

		case PPC_19_CRXOR:
			ic->f = instr(crxor);
			ic->arg[0] = iword;
			break;

		default:goto bad;
		}
		break;

	case PPC_HI6_31:
		xo = (iword >> 1) & 1023;
		switch (xo) {

		case PPC_31_MFSPR:
			rt = (iword >> 21) & 31;
			spr = ((iword >> 6) & 0x3e0) + ((iword >> 16) & 31);
			ic->arg[0] = (size_t)(&cpu->cd.ppc.gpr[rt]);
			switch (spr) {
			case 8:	ic->f = instr(mflr);
				break;
			default:fatal("UNIMPLEMENTED spr %i\n", spr);
				goto bad;
			}
			break;

		case PPC_31_MFMSR:
			rt = (iword >> 21) & 31;
			ic->arg[0] = (size_t)(&cpu->cd.ppc.gpr[rt]);
			ic->f = instr(mfmsr);
			break;

		case PPC_31_MTMSR:
			rs = (iword >> 21) & 31;
			l_bit = (iword >> 16) & 1;
			if (l_bit) {
				fatal("TODO: mtmsr l-bit\n");
				goto bad;
			}
			ic->arg[0] = (size_t)(&cpu->cd.ppc.gpr[rs]);
			ic->f = instr(mtmsr);
			break;

		case PPC_31_OR:
			rs = (iword >> 21) & 31;
			ra = (iword >> 16) & 31;
			rb = (iword >> 11) & 31;
			rc = iword & 1;
			if (rc) {
				fatal("RC bit not yet implemented\n");
				goto bad;
			}
			ic->f = instr(or);
			ic->arg[0] = (size_t)(&cpu->cd.ppc.gpr[rs]);
			ic->arg[1] = (size_t)(&cpu->cd.ppc.gpr[rb]);
			ic->arg[2] = (size_t)(&cpu->cd.ppc.gpr[ra]);
			break;

		default:goto bad;
		}
		break;

	default:goto bad;
	}


#define	DYNTRANS_TO_BE_TRANSLATED_TAIL
#include "cpu_dyntrans.c" 
#undef	DYNTRANS_TO_BE_TRANSLATED_TAIL
}

