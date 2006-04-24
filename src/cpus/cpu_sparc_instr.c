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
 *  $Id: cpu_sparc_instr.c,v 1.13 2006-04-24 18:23:28 debug Exp $
 *
 *  SPARC instructions.
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


/*****************************************************************************/


/*
 *  call
 *
 *  arg[0] = int32_t displacement compared to the current instruction
 *  arg[1] = int32_t displacement of current instruction compared to
 *           start of the page
 */
X(call)
{
	MODE_uint_t old_pc = cpu->pc;
	old_pc &= ~((SPARC_IC_ENTRIES_PER_PAGE - 1)
	    << SPARC_INSTR_ALIGNMENT_SHIFT);
	old_pc += (int32_t)ic->arg[1];
	cpu->cd.sparc.r[SPARC_REG_O7] = old_pc;
	cpu->delay_slot = TO_BE_DELAYED;
	ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		/*  Note: Must be non-delayed when jumping to the new pc:  */
		cpu->delay_slot = NOT_DELAYED;
		cpu->pc = old_pc + (int32_t)ic->arg[0];
		quick_pc_to_pointers(cpu);
	} else
		cpu->delay_slot = NOT_DELAYED;
}
X(call_trace)
{
	MODE_uint_t old_pc = cpu->pc;
	old_pc &= ~((SPARC_IC_ENTRIES_PER_PAGE - 1)
	    << SPARC_INSTR_ALIGNMENT_SHIFT);
	old_pc += (int32_t)ic->arg[1];
	cpu->cd.sparc.r[SPARC_REG_O7] = old_pc;
	cpu->delay_slot = TO_BE_DELAYED;
	ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		/*  Note: Must be non-delayed when jumping to the new pc:  */
		cpu->delay_slot = NOT_DELAYED;
		cpu->pc = old_pc + (int32_t)ic->arg[0];
		cpu_functioncall_trace(cpu, cpu->pc);
		quick_pc_to_pointers(cpu);
	} else
		cpu->delay_slot = NOT_DELAYED;
}


/*
 *  ba
 *
 *  arg[0] = int32_t displacement compared to the start of the current page
 */
X(ba)
{
	MODE_uint_t old_pc = cpu->pc;
	cpu->delay_slot = TO_BE_DELAYED;
	ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		/*  Note: Must be non-delayed when jumping to the new pc:  */
		cpu->delay_slot = NOT_DELAYED;
		old_pc &= ~((SPARC_IC_ENTRIES_PER_PAGE - 1)
		    << SPARC_INSTR_ALIGNMENT_SHIFT);
		cpu->pc = old_pc + (int32_t)ic->arg[0];
		quick_pc_to_pointers(cpu);
	} else
		cpu->delay_slot = NOT_DELAYED;
}


/*
 *  Jump and link
 *
 *  arg[0] = ptr to rs1
 *  arg[1] = ptr to rs2 or an immediate value (int32_t)
 *  arg[2] = ptr to rd
 */
X(jmpl_imm)
{
	int low_pc = ((size_t)ic - (size_t)cpu->cd.sparc.cur_ic_page)
	    / sizeof(struct sparc_instr_call);
	cpu->pc &= ~((SPARC_IC_ENTRIES_PER_PAGE-1)
	    << SPARC_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (low_pc << SPARC_INSTR_ALIGNMENT_SHIFT);
	reg(ic->arg[2]) = cpu->pc;

	cpu->delay_slot = TO_BE_DELAYED;
	ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;

	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		/*  Note: Must be non-delayed when jumping to the new pc:  */
		cpu->delay_slot = NOT_DELAYED;
		cpu->pc = reg(ic->arg[0]) + (int32_t)ic->arg[1];
		quick_pc_to_pointers(cpu);
	} else
		cpu->delay_slot = NOT_DELAYED;
}
X(jmpl_imm_no_rd)
{
	int low_pc = ((size_t)ic - (size_t)cpu->cd.sparc.cur_ic_page)
	    / sizeof(struct sparc_instr_call);
	cpu->pc &= ~((SPARC_IC_ENTRIES_PER_PAGE-1)
	    << SPARC_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (low_pc << SPARC_INSTR_ALIGNMENT_SHIFT);

	cpu->delay_slot = TO_BE_DELAYED;
	ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;

	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		/*  Note: Must be non-delayed when jumping to the new pc:  */
		cpu->delay_slot = NOT_DELAYED;
		cpu->pc = reg(ic->arg[0]) + (int32_t)ic->arg[1];
		quick_pc_to_pointers(cpu);
	} else
		cpu->delay_slot = NOT_DELAYED;
}
X(jmpl_reg)
{
	int low_pc = ((size_t)ic - (size_t)cpu->cd.sparc.cur_ic_page)
	    / sizeof(struct sparc_instr_call);
	cpu->pc &= ~((SPARC_IC_ENTRIES_PER_PAGE-1)
	    << SPARC_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (low_pc << SPARC_INSTR_ALIGNMENT_SHIFT);
	reg(ic->arg[2]) = cpu->pc;

	cpu->delay_slot = TO_BE_DELAYED;
	ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;

	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		/*  Note: Must be non-delayed when jumping to the new pc:  */
		cpu->delay_slot = NOT_DELAYED;
		cpu->pc = reg(ic->arg[0]) + reg(ic->arg[1]);
		quick_pc_to_pointers(cpu);
	} else
		cpu->delay_slot = NOT_DELAYED;
}
X(jmpl_reg_no_rd)
{
	int low_pc = ((size_t)ic - (size_t)cpu->cd.sparc.cur_ic_page)
	    / sizeof(struct sparc_instr_call);
	cpu->pc &= ~((SPARC_IC_ENTRIES_PER_PAGE-1)
	    << SPARC_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (low_pc << SPARC_INSTR_ALIGNMENT_SHIFT);

	cpu->delay_slot = TO_BE_DELAYED;
	ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;

	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		/*  Note: Must be non-delayed when jumping to the new pc:  */
		cpu->delay_slot = NOT_DELAYED;
		cpu->pc = reg(ic->arg[0]) + reg(ic->arg[1]);
		quick_pc_to_pointers(cpu);
	} else
		cpu->delay_slot = NOT_DELAYED;
}


X(jmpl_imm_trace)
{
	int low_pc = ((size_t)ic - (size_t)cpu->cd.sparc.cur_ic_page)
	    / sizeof(struct sparc_instr_call);
	cpu->pc &= ~((SPARC_IC_ENTRIES_PER_PAGE-1)
	    << SPARC_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (low_pc << SPARC_INSTR_ALIGNMENT_SHIFT);
	reg(ic->arg[2]) = cpu->pc;

	cpu->delay_slot = TO_BE_DELAYED;
	ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;

	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		/*  Note: Must be non-delayed when jumping to the new pc:  */
		cpu->delay_slot = NOT_DELAYED;
		cpu->pc = reg(ic->arg[0]) + (int32_t)ic->arg[1];
		cpu_functioncall_trace(cpu, cpu->pc);
		quick_pc_to_pointers(cpu);
	} else
		cpu->delay_slot = NOT_DELAYED;
}
X(jmpl_reg_trace)
{
	int low_pc = ((size_t)ic - (size_t)cpu->cd.sparc.cur_ic_page)
	    / sizeof(struct sparc_instr_call);
	cpu->pc &= ~((SPARC_IC_ENTRIES_PER_PAGE-1)
	    << SPARC_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (low_pc << SPARC_INSTR_ALIGNMENT_SHIFT);
	reg(ic->arg[2]) = cpu->pc;

	cpu->delay_slot = TO_BE_DELAYED;
	ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;

	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		/*  Note: Must be non-delayed when jumping to the new pc:  */
		cpu->delay_slot = NOT_DELAYED;
		cpu->pc = reg(ic->arg[0]) + reg(ic->arg[1]);
		cpu_functioncall_trace(cpu, cpu->pc);
		quick_pc_to_pointers(cpu);
	} else
		cpu->delay_slot = NOT_DELAYED;
}
X(retl_trace)
{
	int low_pc = ((size_t)ic - (size_t)cpu->cd.sparc.cur_ic_page)
	    / sizeof(struct sparc_instr_call);
	cpu->pc &= ~((SPARC_IC_ENTRIES_PER_PAGE-1)
	    << SPARC_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (low_pc << SPARC_INSTR_ALIGNMENT_SHIFT);

	cpu->delay_slot = TO_BE_DELAYED;
	ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;

	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		/*  Note: Must be non-delayed when jumping to the new pc:  */
		cpu->delay_slot = NOT_DELAYED;
		cpu->pc = reg(ic->arg[0]) + (int32_t)ic->arg[1];
		quick_pc_to_pointers(cpu);
		cpu_functioncall_trace_return(cpu);
	} else
		cpu->delay_slot = NOT_DELAYED;
}


/*
 *  set:  Set a register to a value (e.g. sethi).
 *
 *  arg[0] = ptr to rd
 *  arg[1] = value (uint32_t)
 */
X(set)
{
	reg(ic->arg[0]) = (uint32_t)ic->arg[1];
}


/*
 *  Computational/arithmetic instructions:
 *
 *  arg[0] = ptr to rs1
 *  arg[1] = ptr to rs2 or an immediate value (int32_t)
 *  arg[2] = ptr to rd
 */
X(add)      { reg(ic->arg[2]) = reg(ic->arg[0]) + reg(ic->arg[1]); }
X(add_imm)  { reg(ic->arg[2]) = reg(ic->arg[0]) + (int32_t)ic->arg[1]; }
X(and)      { reg(ic->arg[2]) = reg(ic->arg[0]) & reg(ic->arg[1]); }
X(and_imm)  { reg(ic->arg[2]) = reg(ic->arg[0]) & (int32_t)ic->arg[1]; }
X(or)       { reg(ic->arg[2]) = reg(ic->arg[0]) | reg(ic->arg[1]); }
X(or_imm)   { reg(ic->arg[2]) = reg(ic->arg[0]) | (int32_t)ic->arg[1]; }
X(xor)      { reg(ic->arg[2]) = reg(ic->arg[0]) ^ reg(ic->arg[1]); }
X(xor_imm)  { reg(ic->arg[2]) = reg(ic->arg[0]) ^ (int32_t)ic->arg[1]; }
X(sub)      { reg(ic->arg[2]) = reg(ic->arg[0]) - reg(ic->arg[1]); }
X(sub_imm)  { reg(ic->arg[2]) = reg(ic->arg[0]) - (int32_t)ic->arg[1]; }

X(sll)      { reg(ic->arg[2]) = (uint32_t)reg(ic->arg[0]) <<
		(reg(ic->arg[1]) & 31); }
X(sllx)     { reg(ic->arg[2]) = (uint64_t)reg(ic->arg[0]) <<
		(reg(ic->arg[1]) & 63); }
X(sll_imm)  { reg(ic->arg[2]) = (uint32_t)reg(ic->arg[0]) << ic->arg[1]; }
X(sllx_imm) { reg(ic->arg[2]) = (uint64_t)reg(ic->arg[0]) << ic->arg[1]; }

X(srl)      { reg(ic->arg[2]) = (uint32_t)reg(ic->arg[0]) >>
		(reg(ic->arg[1]) & 31); }
X(srlx)     { reg(ic->arg[2]) = (uint64_t)reg(ic->arg[0]) >>
		(reg(ic->arg[1]) & 63); }
X(srl_imm)  { reg(ic->arg[2]) = (uint32_t)reg(ic->arg[0]) >> ic->arg[1]; }
X(srlx_imm) { reg(ic->arg[2]) = (uint64_t)reg(ic->arg[0]) >> ic->arg[1]; }

X(sra)      { reg(ic->arg[2]) = (int32_t)reg(ic->arg[0]) >>
		(reg(ic->arg[1]) & 31); }
X(srax)     { reg(ic->arg[2]) = (int64_t)reg(ic->arg[0]) >>
		(reg(ic->arg[1]) & 63); }
X(sra_imm)  { reg(ic->arg[2]) = (int32_t)reg(ic->arg[0]) >> ic->arg[1]; }
X(srax_imm) { reg(ic->arg[2]) = (int64_t)reg(ic->arg[0]) >> ic->arg[1]; }


/*****************************************************************************/


X(end_of_page)
{
	/*  Update the PC:  (offset 0, but on the next page)  */
	cpu->pc &= ~((SPARC_IC_ENTRIES_PER_PAGE-1) <<
	    SPARC_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (SPARC_IC_ENTRIES_PER_PAGE <<
	    SPARC_INSTR_ALIGNMENT_SHIFT);

	/*  Find the new physical page and update the translation pointers:  */
	quick_pc_to_pointers(cpu);

	/*  end_of_page doesn't count as an executed instruction:  */
	cpu->n_translated_instrs --;
}


X(end_of_page2)
{
	/*  Synchronize PC on the _second_ instruction on the next page:  */
	int low_pc = ((size_t)ic - (size_t)cpu->cd.sparc.cur_ic_page)
	    / sizeof(struct sparc_instr_call);
	cpu->pc &= ~((SPARC_IC_ENTRIES_PER_PAGE-1)
	    << SPARC_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (low_pc << SPARC_INSTR_ALIGNMENT_SHIFT);

	/*  This doesn't count as an executed instruction.  */
	cpu->n_translated_instrs --;

	quick_pc_to_pointers(cpu);

	if (cpu->delay_slot == NOT_DELAYED)
		return;

	fatal("end_of_page2: fatal error, we're in a delay slot\n");
	exit(1);
}


/*****************************************************************************/


/*
 *  sparc_instr_to_be_translated():
 *
 *  Translate an instruction word into a sparc_instr_call. ic is filled in with
 *  valid data for the translated instruction, or a "nothing" instruction if
 *  there was a translation failure. The newly translated instruction is then
 *  executed.
 */
X(to_be_translated)
{
	MODE_uint_t addr;
	int low_pc;
	int in_crosspage_delayslot = 0;
	uint32_t iword;
	unsigned char *page;
	unsigned char ib[4];
	int main_opcode, op2, rd, rs1, rs2, btype, asi, cc, p, use_imm, x64 = 0;
	int32_t tmpi32, siconst;
	/* void (*samepage_function)(struct cpu *, struct sparc_instr_call *);*/

	/*  Figure out the (virtual) address of the instruction:  */
	low_pc = ((size_t)ic - (size_t)cpu->cd.sparc.cur_ic_page)
	    / sizeof(struct sparc_instr_call);

	/*  Special case for branch with delayslot on the next page:  */
	if (cpu->delay_slot == TO_BE_DELAYED && low_pc == 0) {
		/*  fatal("[ delay-slot translation across page "
		    "boundary ]\n");  */
		in_crosspage_delayslot = 1;
	}

	addr = cpu->pc & ~((SPARC_IC_ENTRIES_PER_PAGE-1)
	    << SPARC_INSTR_ALIGNMENT_SHIFT);
	addr += (low_pc << SPARC_INSTR_ALIGNMENT_SHIFT);
	cpu->pc = addr;
	addr &= ~((1 << SPARC_INSTR_ALIGNMENT_SHIFT) - 1);

	/*  Read the instruction word from memory:  */
#ifdef MODE32
	page = cpu->cd.sparc.host_load[addr >> 12];
#else
	{
		const uint32_t mask1 = (1 << DYNTRANS_L1N) - 1;
		const uint32_t mask2 = (1 << DYNTRANS_L2N) - 1;
		const uint32_t mask3 = (1 << DYNTRANS_L3N) - 1;
		uint32_t x1 = (addr >> (64-DYNTRANS_L1N)) & mask1;
		uint32_t x2 = (addr >> (64-DYNTRANS_L1N-DYNTRANS_L2N)) & mask2;
		uint32_t x3 = (addr >> (64-DYNTRANS_L1N-DYNTRANS_L2N-
		    DYNTRANS_L3N)) & mask3;
		struct DYNTRANS_L2_64_TABLE *l2 = cpu->cd.sparc.l1_64[x1];
		struct DYNTRANS_L3_64_TABLE *l3 = l2->l3[x2];
		page = l3->host_load[x3];
	}
#endif

	if (page != NULL) {
		/*  fatal("TRANSLATION HIT!\n");  */
		memcpy(ib, page + (addr & 0xffc), sizeof(ib));
	} else {
		/*  fatal("TRANSLATION MISS!\n");  */
		if (!cpu->memory_rw(cpu, cpu->mem, addr, ib,
		    sizeof(ib), MEM_READ, CACHE_INSTRUCTION)) {
			fatal("to_be_translated(): "
			    "read failed: TODO\n");
			goto bad;
		}
	}

	/*  SPARC instruction words are always big-endian. Convert
	    to host order:  */
	iword = BE32_TO_HOST( *((uint32_t *)&ib[0]) );


#define DYNTRANS_TO_BE_TRANSLATED_HEAD
#include "cpu_dyntrans.c"
#undef  DYNTRANS_TO_BE_TRANSLATED_HEAD


	/*
	 *  Translate the instruction:
	 */

	main_opcode = iword >> 30;
	rd = (iword >> 25) & 31;
	btype = rd & (N_SPARC_BRANCH_TYPES - 1);
	rs1 = (iword >> 14) & 31;
	use_imm = (iword >> 13) & 1;
	asi = (iword >> 5) & 0xff;
	rs2 = iword & 31;
	siconst = (int16_t)((iword & 0x1fff) << 3) >> 3;
	op2 = (main_opcode == 0)? ((iword >> 22) & 7) : ((iword >> 19) & 0x3f);
	cc = (iword >> 20) & 3;
	p = (iword >> 19) & 1;

	switch (main_opcode) {

	case 0:	switch (op2) {

		case 2:	/*  branch (32-bit integer comparison)  */
			tmpi32 = (iword << 10);
			tmpi32 >>= 8;
			ic->arg[0] = (int32_t)tmpi32 + (addr & 0xffc);
			/*  rd contains the annul bit concatenated with 4 bits
			    of condition code:  */
			/*  TODO: samepage  */
			switch (rd) {
			case 0x08:/*  ba  */
				ic->f = instr(ba);
				break;
			default:goto bad;
			}
			break;

		case 4:	/*  sethi  */
			ic->arg[0] = (size_t)&cpu->cd.sparc.r[rd];
			ic->arg[1] = (iword & 0x3fffff) << 10;
			ic->f = instr(set);
			if (rd == SPARC_ZEROREG)
				ic->f = instr(nop);
			break;

		default:fatal("TODO: unimplemented op2=%i for main "
			    "opcode %i\n", op2, main_opcode);
			goto bad;
		}
		break;

	case 1:	/*  call and link  */
		tmpi32 = (iword << 2);
		ic->arg[0] = (int32_t)tmpi32;
		ic->arg[1] = addr & 0xffc;
		if (cpu->machine->show_trace_tree)
			ic->f = instr(call_trace);
		else
			ic->f = instr(call);
		/*  TODO: samepage  */
		break;

	case 2:	switch (op2) {

		case 0:	/*  add  */
		case 1:	/*  and  */
		case 2:	/*  or  */
		case 3:	/*  xor  */
		case 4:	/*  sub  */
		case 37:/*  sll  */
		case 38:/*  srl  */
		case 39:/*  sra  */
			ic->arg[0] = (size_t)&cpu->cd.sparc.r[rs1];
			ic->f = NULL;
			if (use_imm) {
				ic->arg[1] = siconst;
				switch (op2) {
				case 0:	ic->f = instr(add_imm); break;
				case 1:	ic->f = instr(and_imm); break;
				case 2:	ic->f = instr(or_imm); break;
				case 3:	ic->f = instr(xor_imm); break;
				case 4:	ic->f = instr(sub_imm); break;
				case 37:if (siconst & 0x1000) {
						ic->f = instr(sllx_imm);
						ic->arg[1] &= 63;
						x64 = 1;
					} else {
						ic->f = instr(sll_imm);
						ic->arg[1] &= 31;
					}
					break;
				case 38:if (siconst & 0x1000) {
						ic->f = instr(srlx_imm);
						ic->arg[1] &= 63;
						x64 = 1;
					} else {
						ic->f = instr(srl_imm);
						ic->arg[1] &= 31;
					}
					break;
				case 39:if (siconst & 0x1000) {
						ic->f = instr(srax_imm);
						ic->arg[1] &= 63;
						x64 = 1;
					} else {
						ic->f = instr(sra_imm);
						ic->arg[1] &= 31;
					}
					break;
				}
			} else {
				ic->arg[1] = (size_t)&cpu->cd.sparc.r[rs2];
				switch (op2) {
				case 0:  ic->f = instr(add); break;
				case 1:  ic->f = instr(and); break;
				case 2:  ic->f = instr(or); break;
				case 3:  ic->f = instr(xor); break;
				case 4:  ic->f = instr(sub); break;
				case 37:if (siconst & 0x1000) {
						ic->f = instr(sllx);
						x64 = 1;
					} else
						ic->f = instr(sll);
					break;
				case 38:if (siconst & 0x1000) {
						ic->f = instr(srlx);
						x64 = 1;
					} else
						ic->f = instr(srl);
					break;
				case 39:if (siconst & 0x1000) {
						ic->f = instr(srax);
						x64 = 1;
					} else
						ic->f = instr(sra);
					break;
				}
			}
			if (ic->f == NULL) {
				fatal("TODO: Unimplemented instruction "
				    "(possibly missed use_imm impl.)\n");
				goto bad;
			}
			ic->arg[2] = (size_t)&cpu->cd.sparc.r[rd];
			if (rd == SPARC_ZEROREG)
				ic->f = instr(nop);
			break;

		case 56:/*  jump and link  */
			ic->arg[0] = (size_t)&cpu->cd.sparc.r[rs1];
			ic->arg[2] = (size_t)&cpu->cd.sparc.r[rd];
			if (rd == SPARC_ZEROREG)
				ic->arg[2] = (size_t)&cpu->cd.sparc.scratch;

			if (use_imm) {
				ic->arg[1] = siconst;
				if (rd == SPARC_ZEROREG)
					ic->f = instr(jmpl_imm_no_rd);
				else
					ic->f = instr(jmpl_imm);
			} else {
				ic->arg[1] = (size_t)&cpu->cd.sparc.r[rs2];
				if (rd == SPARC_ZEROREG)
					ic->f = instr(jmpl_reg_no_rd);
				else
					ic->f = instr(jmpl_reg);
			}

			/*  special trace case:  */
			if (cpu->machine->show_trace_tree) {
				if (iword == 0x81c3e008)
					ic->f = instr(retl_trace);
				else {
					if (use_imm)
						ic->f = instr(jmpl_imm_trace);
					else
						ic->f = instr(jmpl_reg_trace);
				}
			}
			break;

		default:fatal("TODO: unimplemented op2=%i for main "
			    "opcode %i\n", op2, main_opcode);
			goto bad;
		}
		break;

	default:fatal("TODO: unimplemented main opcode %i\n", main_opcode);
		goto bad;
	}


	if (x64 && cpu->is_32bit) {
		fatal("TODO: 64-bit instr on 32-bit cpu\n");
		goto bad;
	}


#define	DYNTRANS_TO_BE_TRANSLATED_TAIL
#include "cpu_dyntrans.c" 
#undef	DYNTRANS_TO_BE_TRANSLATED_TAIL
}

