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
 *  $Id: cpu_ppc_instr.c,v 1.35 2005-11-21 22:27:16 debug Exp $
 *
 *  POWER/PowerPC instructions.
 *
 *  Individual functions should keep track of cpu->n_translated_instrs.
 *  (If no instruction was executed, then it should be decreased. If, say, 4
 *  instructions were combined into one function and executed, then it should
 *  be increased by 3.)
 */


#define DOT0(n) X(n ## _dot) { instr(n)(cpu,ic); \
	update_cr0(cpu, reg(ic->arg[0])); }
#define DOT1(n) X(n ## _dot) { instr(n)(cpu,ic); \
	update_cr0(cpu, reg(ic->arg[1])); }
#define DOT2(n) X(n ## _dot) { instr(n)(cpu,ic); \
	update_cr0(cpu, reg(ic->arg[2])); }


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
	fatal("PPC: invalid(): INTERNAL ERROR\n");
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
 *  andi_dot:  AND immediate, update CR.
 *
 *  arg[0] = pointer to source uint64_t
 *  arg[1] = immediate value (uint32_t)
 *  arg[2] = pointer to destination uint64_t
 */
X(andi_dot)
{
	MODE_uint_t tmp = reg(ic->arg[0]) & (uint32_t)ic->arg[1];
	reg(ic->arg[2]) = tmp;
	update_cr0(cpu, tmp);
}


/*
 *  addic:  Add immediate, Carry.
 *
 *  arg[0] = pointer to source register
 *  arg[1] = immediate value (int32_t or larger)
 *  arg[2] = pointer to destination register
 */
X(addic)
{
	/*  TODO/NOTE: Only for 32-bit mode, so far!  */
	uint64_t tmp = (uint32_t)reg(ic->arg[0]);
	uint64_t tmp2 = tmp;
	cpu->cd.ppc.spr[SPR_XER] &= ~PPC_XER_CA;
	tmp2 += (uint32_t)ic->arg[1];
	if ((tmp2 >> 32) != (tmp >> 32))
		cpu->cd.ppc.spr[SPR_XER] |= PPC_XER_CA;
	reg(ic->arg[2]) = (uint32_t)tmp2;
}


/*
 *  subfic:  Subtract from immediate, Carry.
 *
 *  arg[0] = pointer to source uint64_t
 *  arg[1] = immediate value (int32_t or larger)
 *  arg[2] = pointer to destination uint64_t
 */
X(subfic)
{
	MODE_uint_t tmp = (int64_t)(int32_t)ic->arg[1];
	cpu->cd.ppc.spr[SPR_XER] &= ~PPC_XER_CA;
	if (tmp >= reg(ic->arg[0]))
		cpu->cd.ppc.spr[SPR_XER] |= PPC_XER_CA;
	reg(ic->arg[2]) = tmp - reg(ic->arg[0]);
}


/*
 *  addic_dot:  Add immediate, Carry.
 *
 *  arg[0] = pointer to source uint64_t
 *  arg[1] = immediate value (int32_t or larger)
 *  arg[2] = pointer to destination uint64_t
 */
X(addic_dot)
{
	/*  TODO/NOTE: Only for 32-bit mode, so far!  */
	uint64_t tmp = (uint32_t)reg(ic->arg[0]);
	uint64_t tmp2 = tmp;
	cpu->cd.ppc.spr[SPR_XER] &= ~PPC_XER_CA;
	tmp2 += (uint32_t)ic->arg[1];
	if ((tmp2 >> 32) != (tmp >> 32))
		cpu->cd.ppc.spr[SPR_XER] |= PPC_XER_CA;
	reg(ic->arg[2]) = (uint32_t)tmp2;
	update_cr0(cpu, (uint32_t)tmp2);
}


/*
 *  bclr:  Branch Conditional to Link Register
 *
 *  arg[0] = bo
 *  arg[1] = bi
 *  arg[2] = bh
 */
X(bclr)
{
	int bo = ic->arg[0], bi = ic->arg[1]  /* , bh = ic->arg[2]  */;
	int ctr_ok, cond_ok;
	uint64_t old_pc = cpu->pc;
	MODE_uint_t tmp, addr = cpu->cd.ppc.spr[SPR_LR];
	if (!(bo & 4))
		cpu->cd.ppc.spr[SPR_CTR] --;
	ctr_ok = (bo >> 2) & 1;
	tmp = cpu->cd.ppc.spr[SPR_CTR];
	ctr_ok |= ( (tmp != 0) ^ ((bo >> 1) & 1) );
	cond_ok = (bo >> 4) & 1;
	cond_ok |= ( ((bo >> 3) & 1) == ((cpu->cd.ppc.cr >> (31-bi)) & 1) );
	if (ctr_ok && cond_ok) {
		uint64_t mask_within_page =
		    ((PPC_IC_ENTRIES_PER_PAGE-1) << PPC_INSTR_ALIGNMENT_SHIFT)
		    | ((1 << PPC_INSTR_ALIGNMENT_SHIFT) - 1);
		cpu->pc = addr & ~((1 << PPC_INSTR_ALIGNMENT_SHIFT) - 1);
		/*  TODO: trace in separate (duplicate) function?  */
		if (cpu->machine->show_trace_tree)
			cpu_functioncall_trace_return(cpu);
		if ((old_pc  & ~mask_within_page) ==
		    (cpu->pc & ~mask_within_page)) {
			cpu->cd.ppc.next_ic =
			    cpu->cd.ppc.cur_ic_page +
			    ((cpu->pc & mask_within_page) >>
			    PPC_INSTR_ALIGNMENT_SHIFT);
		} else {
			/*  Find the new physical page and update pointers:  */
			DYNTRANS_PC_TO_POINTERS(cpu);
		}
	}
}
X(bclr_l)
{
	uint64_t low_pc, old_pc = cpu->pc;
	int bo = ic->arg[0], bi = ic->arg[1]  /* , bh = ic->arg[2]  */;
	int ctr_ok, cond_ok;
	MODE_uint_t tmp, addr = cpu->cd.ppc.spr[SPR_LR];
	if (!(bo & 4))
		cpu->cd.ppc.spr[SPR_CTR] --;
	ctr_ok = (bo >> 2) & 1;
	tmp = cpu->cd.ppc.spr[SPR_CTR];
	ctr_ok |= ( (tmp != 0) ^ ((bo >> 1) & 1) );
	cond_ok = (bo >> 4) & 1;
	cond_ok |= ( ((bo >> 3) & 1) == ((cpu->cd.ppc.cr >> (31-bi)) & 1) );

	/*  Calculate return PC:  */
	low_pc = ((size_t)ic - (size_t)
	    cpu->cd.ppc.cur_ic_page) / sizeof(struct ppc_instr_call) + 1;
	cpu->cd.ppc.spr[SPR_LR] = cpu->pc & ~((PPC_IC_ENTRIES_PER_PAGE-1) << 2);
	cpu->cd.ppc.spr[SPR_LR] += (low_pc << 2);

	if (ctr_ok && cond_ok) {
		uint64_t mask_within_page =
		    ((PPC_IC_ENTRIES_PER_PAGE-1) << PPC_INSTR_ALIGNMENT_SHIFT)
		    | ((1 << PPC_INSTR_ALIGNMENT_SHIFT) - 1);
		cpu->pc = addr & ~((1 << PPC_INSTR_ALIGNMENT_SHIFT) - 1);
		/*  TODO: trace in separate (duplicate) function?  */
		if (cpu->machine->show_trace_tree)
			cpu_functioncall_trace_return(cpu);
		if (cpu->machine->show_trace_tree)
			cpu_functioncall_trace(cpu, cpu->pc);
		if ((old_pc  & ~mask_within_page) ==
		    (cpu->pc & ~mask_within_page)) {
			cpu->cd.ppc.next_ic =
			    cpu->cd.ppc.cur_ic_page +
			    ((cpu->pc & mask_within_page) >>
			    PPC_INSTR_ALIGNMENT_SHIFT);
		} else {
			/*  Find the new physical page and update pointers:  */
			DYNTRANS_PC_TO_POINTERS(cpu);
		}
	}
}


/*
 *  bcctr:  Branch Conditional to Count register
 *
 *  arg[0] = bo
 *  arg[1] = bi
 *  arg[2] = bh
 */
X(bcctr)
{
	int bo = ic->arg[0], bi = ic->arg[1]  /* , bh = ic->arg[2]  */;
	uint64_t old_pc = cpu->pc;
	MODE_uint_t addr = cpu->cd.ppc.spr[SPR_CTR];
	int cond_ok = (bo >> 4) & 1;
	cond_ok |= ( ((bo >> 3) & 1) == ((cpu->cd.ppc.cr >> (31-bi)) & 1) );
	if (cond_ok) {
		uint64_t mask_within_page =
		    ((PPC_IC_ENTRIES_PER_PAGE-1) << PPC_INSTR_ALIGNMENT_SHIFT)
		    | ((1 << PPC_INSTR_ALIGNMENT_SHIFT) - 1);
		cpu->pc = addr & ~((1 << PPC_INSTR_ALIGNMENT_SHIFT) - 1);
		/*  TODO: trace in separate (duplicate) function?  */
		if (cpu->machine->show_trace_tree)
			cpu_functioncall_trace_return(cpu);
		if ((old_pc  & ~mask_within_page) ==
		    (cpu->pc & ~mask_within_page)) {
			cpu->cd.ppc.next_ic =
			    cpu->cd.ppc.cur_ic_page +
			    ((cpu->pc & mask_within_page) >>
			    PPC_INSTR_ALIGNMENT_SHIFT);
		} else {
			/*  Find the new physical page and update pointers:  */
			DYNTRANS_PC_TO_POINTERS(cpu);
		}
	}
}
X(bcctr_l)
{
	uint64_t low_pc, old_pc = cpu->pc;
	int bo = ic->arg[0], bi = ic->arg[1]  /* , bh = ic->arg[2]  */;
	MODE_uint_t addr = cpu->cd.ppc.spr[SPR_CTR];
	int cond_ok = (bo >> 4) & 1;
	cond_ok |= ( ((bo >> 3) & 1) == ((cpu->cd.ppc.cr >> (31-bi)) & 1) );

	/*  Calculate return PC:  */
	low_pc = ((size_t)ic - (size_t)
	    cpu->cd.ppc.cur_ic_page) / sizeof(struct ppc_instr_call) + 1;
	cpu->cd.ppc.spr[SPR_LR] = cpu->pc & ~((PPC_IC_ENTRIES_PER_PAGE-1) << 2);
	cpu->cd.ppc.spr[SPR_LR] += (low_pc << 2);

	if (cond_ok) {
		uint64_t mask_within_page =
		    ((PPC_IC_ENTRIES_PER_PAGE-1) << PPC_INSTR_ALIGNMENT_SHIFT)
		    | ((1 << PPC_INSTR_ALIGNMENT_SHIFT) - 1);
		cpu->pc = addr & ~((1 << PPC_INSTR_ALIGNMENT_SHIFT) - 1);
		/*  TODO: trace in separate (duplicate) function?  */
		if (cpu->machine->show_trace_tree)
			cpu_functioncall_trace(cpu, cpu->pc);
		if ((old_pc  & ~mask_within_page) ==
		    (cpu->pc & ~mask_within_page)) {
			cpu->cd.ppc.next_ic =
			    cpu->cd.ppc.cur_ic_page +
			    ((cpu->pc & mask_within_page) >>
			    PPC_INSTR_ALIGNMENT_SHIFT);
		} else {
			/*  Find the new physical page and update pointers:  */
			DYNTRANS_PC_TO_POINTERS(cpu);
		}
	}
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
	DYNTRANS_PC_TO_POINTERS(cpu);
}
X(ba)
{
	cpu->pc = (int32_t)ic->arg[0];
	DYNTRANS_PC_TO_POINTERS(cpu);
}


/*
 *  bc:  Branch Conditional (to a different translated page)
 *
 *  arg[0] = relative offset (as an int32_t)
 *  arg[1] = bo
 *  arg[2] = bi
 */
X(bc)
{
	MODE_uint_t tmp;
	int ctr_ok, cond_ok, bi = ic->arg[2], bo = ic->arg[1];
	if (!(bo & 4))
		cpu->cd.ppc.spr[SPR_CTR] --;
	ctr_ok = (bo >> 2) & 1;
	tmp = cpu->cd.ppc.spr[SPR_CTR];
	ctr_ok |= ( (tmp != 0) ^ ((bo >> 1) & 1) );
	cond_ok = (bo >> 4) & 1;
	cond_ok |= ( ((bo >> 3) & 1) ==
	    ((cpu->cd.ppc.cr >> (31-bi)) & 1)  );
	if (ctr_ok && cond_ok)
		instr(b)(cpu,ic);
}
X(bcl)
{
	MODE_uint_t tmp;
	int ctr_ok, cond_ok, bi = ic->arg[2], bo = ic->arg[1], low_pc;

	/*  Calculate LR:  */
	low_pc = ((size_t)ic - (size_t)
	    cpu->cd.ppc.cur_ic_page) / sizeof(struct ppc_instr_call) + 1;
	cpu->cd.ppc.spr[SPR_LR] = cpu->pc & ~((PPC_IC_ENTRIES_PER_PAGE-1) << 2);
	cpu->cd.ppc.spr[SPR_LR] += (low_pc << 2);

	if (!(bo & 4))
		cpu->cd.ppc.spr[SPR_CTR] --;
	ctr_ok = (bo >> 2) & 1;
	tmp = cpu->cd.ppc.spr[SPR_CTR];
	ctr_ok |= ( (tmp != 0) ^ ((bo >> 1) & 1) );
	cond_ok = (bo >> 4) & 1;
	cond_ok |= ( ((bo >> 3) & 1) ==
	    ((cpu->cd.ppc.cr >> (31-bi)) & 1)  );
	if (ctr_ok && cond_ok)
		instr(b)(cpu,ic);
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
 *  bc_samepage:  Branch Conditional (to within the same page)
 *
 *  arg[0] = new ic ptr
 *  arg[1] = bo
 *  arg[2] = bi
 */
X(bc_samepage)
{
	MODE_uint_t tmp;
	int ctr_ok, cond_ok, bi = ic->arg[2], bo = ic->arg[1];
	if (!(bo & 4))
		cpu->cd.ppc.spr[SPR_CTR] --;
	ctr_ok = (bo >> 2) & 1;
	tmp = cpu->cd.ppc.spr[SPR_CTR];
	ctr_ok |= ( (tmp != 0) ^ ((bo >> 1) & 1) );
	cond_ok = (bo >> 4) & 1;
	cond_ok |= ( ((bo >> 3) & 1) ==
	    ((cpu->cd.ppc.cr >> (31-bi)) & 1)  );
	if (ctr_ok && cond_ok)
		instr(b_samepage)(cpu,ic);
}
X(bcl_samepage)
{
	MODE_uint_t tmp;
	int ctr_ok, cond_ok, bi = ic->arg[2], bo = ic->arg[1], low_pc;

	/*  Calculate LR:  */
	low_pc = ((size_t)ic - (size_t)
	    cpu->cd.ppc.cur_ic_page) / sizeof(struct ppc_instr_call) + 1;
	cpu->cd.ppc.spr[SPR_LR] = cpu->pc & ~((PPC_IC_ENTRIES_PER_PAGE-1) << 2);
	cpu->cd.ppc.spr[SPR_LR] += (low_pc << 2);

	if (!(bo & 4))
		cpu->cd.ppc.spr[SPR_CTR] --;
	ctr_ok = (bo >> 2) & 1;
	tmp = cpu->cd.ppc.spr[SPR_CTR];
	ctr_ok |= ( (tmp != 0) ^ ((bo >> 1) & 1) );
	cond_ok = (bo >> 4) & 1;
	cond_ok |= ( ((bo >> 3) & 1) ==
	    ((cpu->cd.ppc.cr >> (31-bi)) & 1)  );
	if (ctr_ok && cond_ok)
		instr(b_samepage)(cpu,ic);
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
	low_pc = ((size_t)ic - (size_t)
	    cpu->cd.ppc.cur_ic_page) / sizeof(struct ppc_instr_call) + 1;
	cpu->cd.ppc.spr[SPR_LR] = cpu->pc & ~((PPC_IC_ENTRIES_PER_PAGE-1) << 2);
	cpu->cd.ppc.spr[SPR_LR] += (low_pc << 2);

	/*  Calculate new PC from this instruction + arg[0]  */
	low_pc = ((size_t)ic - (size_t)
	    cpu->cd.ppc.cur_ic_page) / sizeof(struct ppc_instr_call);
	cpu->pc &= ~((PPC_IC_ENTRIES_PER_PAGE-1) << 2);
	cpu->pc += (low_pc << 2);
	cpu->pc += (int32_t)ic->arg[0];

	/*  Find the new physical page and update the translation pointers:  */
	DYNTRANS_PC_TO_POINTERS(cpu);
}
X(bla)
{
	uint32_t low_pc = ((size_t)ic - (size_t)
	    cpu->cd.ppc.cur_ic_page) / sizeof(struct ppc_instr_call) + 1;
	cpu->cd.ppc.spr[SPR_LR] = cpu->pc & ~((PPC_IC_ENTRIES_PER_PAGE-1) << 2);
	cpu->cd.ppc.spr[SPR_LR] += (low_pc << 2);
	cpu->pc = (int32_t)ic->arg[0];
	DYNTRANS_PC_TO_POINTERS(cpu);
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
	low_pc = ((size_t)ic - (size_t)
	    cpu->cd.ppc.cur_ic_page) / sizeof(struct ppc_instr_call) + 1;
	cpu->cd.ppc.spr[SPR_LR] = cpu->pc & ~((PPC_IC_ENTRIES_PER_PAGE-1) << 2);
	cpu->cd.ppc.spr[SPR_LR] += (low_pc << 2);

	/*  Calculate new PC from this instruction + arg[0]  */
	low_pc = ((size_t)ic - (size_t)
	    cpu->cd.ppc.cur_ic_page) / sizeof(struct ppc_instr_call);
	cpu->pc &= ~((PPC_IC_ENTRIES_PER_PAGE-1) << 2);
	cpu->pc += (low_pc << 2);
	cpu->pc += (int32_t)ic->arg[0];

	cpu_functioncall_trace(cpu, cpu->pc);

	/*  Find the new physical page and update the translation pointers:  */
	DYNTRANS_PC_TO_POINTERS(cpu);
}
X(bla_trace)
{
	uint32_t low_pc = ((size_t)ic - (size_t)
	    cpu->cd.ppc.cur_ic_page) / sizeof(struct ppc_instr_call) + 1;
	cpu->cd.ppc.spr[SPR_LR] = cpu->pc & ~((PPC_IC_ENTRIES_PER_PAGE-1) << 2);
	cpu->cd.ppc.spr[SPR_LR] += (low_pc << 2);
	cpu->pc = (int32_t)ic->arg[0];
	cpu_functioncall_trace(cpu, cpu->pc);
	DYNTRANS_PC_TO_POINTERS(cpu);
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
	low_pc = ((size_t)ic - (size_t)
	    cpu->cd.ppc.cur_ic_page) / sizeof(struct ppc_instr_call) + 1;
	cpu->cd.ppc.spr[SPR_LR] = cpu->pc & ~((PPC_IC_ENTRIES_PER_PAGE-1) << 2);
	cpu->cd.ppc.spr[SPR_LR] += (low_pc << 2);

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
	low_pc = ((size_t)ic - (size_t)
	    cpu->cd.ppc.cur_ic_page) / sizeof(struct ppc_instr_call) + 1;
	cpu->cd.ppc.spr[SPR_LR] = cpu->pc & ~((PPC_IC_ENTRIES_PER_PAGE-1) << 2);
	cpu->cd.ppc.spr[SPR_LR] += (low_pc << 2);

	cpu->cd.ppc.next_ic = (struct ppc_instr_call *) ic->arg[0];

	/*  Calculate new PC (for the trace)  */
	low_pc = ((size_t)cpu->cd.ppc.next_ic - (size_t)
	    cpu->cd.ppc.cur_ic_page) / sizeof(struct ppc_instr_call);
	cpu->pc &= ~((PPC_IC_ENTRIES_PER_PAGE-1) << 2);
	cpu->pc += (low_pc << 2);
	cpu_functioncall_trace(cpu, cpu->pc);
}


/*
 *  cntlzw:  Count leading zeroes (32-bit word).
 *
 *  arg[0] = ptr to rs
 *  arg[1] = ptr to ra
 */
X(cntlzw)
{
	uint32_t tmp = reg(ic->arg[0]);
	int i;
	for (i=0; i<32; i++) {
		if (tmp & 0x80000000)
			break;
		tmp <<= 1;
	}
	reg(ic->arg[1]) = i;
}


/*
 *  cmpd:  Compare Doubleword
 *
 *  arg[0] = ptr to ra
 *  arg[1] = ptr to rb
 *  arg[2] = bf
 */
X(cmpd)
{
	int64_t tmp = reg(ic->arg[0]), tmp2 = reg(ic->arg[1]);
	int bf = ic->arg[2], c;
	if (tmp < tmp2)
		c = 8;
	else if (tmp > tmp2)
		c = 4;
	else
		c = 2;
	/*  SO bit, copied from XER  */
	c |= ((cpu->cd.ppc.spr[SPR_XER] >> 31) & 1);
	cpu->cd.ppc.cr &= ~(0xf << (28 - 4*bf));
	cpu->cd.ppc.cr |= (c << (28 - 4*bf));
}


/*
 *  cmpld:  Compare Doubleword, unsigned
 *
 *  arg[0] = ptr to ra
 *  arg[1] = ptr to rb
 *  arg[2] = bf
 */
X(cmpld)
{
	uint64_t tmp = reg(ic->arg[0]), tmp2 = reg(ic->arg[1]);
	int bf = ic->arg[2], c;
	if (tmp < tmp2)
		c = 8;
	else if (tmp > tmp2)
		c = 4;
	else
		c = 2;
	/*  SO bit, copied from XER  */
	c |= ((cpu->cd.ppc.spr[SPR_XER] >> 31) & 1);
	cpu->cd.ppc.cr &= ~(0xf << (28 - 4*bf));
	cpu->cd.ppc.cr |= (c << (28 - 4*bf));
}


/*
 *  cmpdi:  Compare Doubleword immediate
 *
 *  arg[0] = ptr to ra
 *  arg[1] = int32_t imm
 *  arg[2] = bf
 */
X(cmpdi)
{
	int64_t tmp = reg(ic->arg[0]), imm = (int32_t)ic->arg[1];
	int bf = ic->arg[2], c;
	if (tmp < imm)
		c = 8;
	else if (tmp > imm)
		c = 4;
	else
		c = 2;
	/*  SO bit, copied from XER  */
	c |= ((cpu->cd.ppc.spr[SPR_XER] >> 31) & 1);
	cpu->cd.ppc.cr &= ~(0xf << (28 - 4*bf));
	cpu->cd.ppc.cr |= (c << (28 - 4*bf));
}


/*
 *  cmpldi:  Compare Doubleword immediate, logical
 *
 *  arg[0] = ptr to ra
 *  arg[1] = int32_t imm
 *  arg[2] = bf
 */
X(cmpldi)
{
	uint64_t tmp = reg(ic->arg[0]), imm = (uint32_t)ic->arg[1];
	int bf = ic->arg[2], c;
	if (tmp < imm)
		c = 8;
	else if (tmp > imm)
		c = 4;
	else
		c = 2;
	/*  SO bit, copied from XER  */
	c |= ((cpu->cd.ppc.spr[SPR_XER] >> 31) & 1);
	cpu->cd.ppc.cr &= ~(0xf << (28 - 4*bf));
	cpu->cd.ppc.cr |= (c << (28 - 4*bf));
}


/*
 *  cmpw:  Compare Word
 *
 *  arg[0] = ptr to ra
 *  arg[1] = ptr to rb
 *  arg[2] = bf
 */
X(cmpw)
{
	int32_t tmp = reg(ic->arg[0]), tmp2 = reg(ic->arg[1]);
	int bf = ic->arg[2], c;
	if (tmp < tmp2)
		c = 8;
	else if (tmp > tmp2)
		c = 4;
	else
		c = 2;
	/*  SO bit, copied from XER  */
	c |= ((cpu->cd.ppc.spr[SPR_XER] >> 31) & 1);
	cpu->cd.ppc.cr &= ~(0xf << (28 - 4*bf));
	cpu->cd.ppc.cr |= (c << (28 - 4*bf));
}


/*
 *  cmplw:  Compare Word, unsigned
 *
 *  arg[0] = ptr to ra
 *  arg[1] = ptr to rb
 *  arg[2] = bf
 */
X(cmplw)
{
	uint32_t tmp = reg(ic->arg[0]), tmp2 = reg(ic->arg[1]);
	int bf = ic->arg[2], c;
	if (tmp < tmp2)
		c = 8;
	else if (tmp > tmp2)
		c = 4;
	else
		c = 2;
	/*  SO bit, copied from XER  */
	c |= ((cpu->cd.ppc.spr[SPR_XER] >> 31) & 1);
	cpu->cd.ppc.cr &= ~(0xf << (28 - 4*bf));
	cpu->cd.ppc.cr |= (c << (28 - 4*bf));
}


/*
 *  cmpwi:  Compare Word immediate
 *
 *  arg[0] = ptr to ra
 *  arg[1] = int32_t imm
 *  arg[2] = bf
 */
X(cmpwi)
{
	int32_t tmp = reg(ic->arg[0]), imm = ic->arg[1];
	int bf = ic->arg[2], c;
	if (tmp < imm)
		c = 8;
	else if (tmp > imm)
		c = 4;
	else
		c = 2;
	/*  SO bit, copied from XER  */
	c |= ((cpu->cd.ppc.spr[SPR_XER] >> 31) & 1);
	cpu->cd.ppc.cr &= ~(0xf << (28 - 4*bf));
	cpu->cd.ppc.cr |= (c << (28 - 4*bf));
}


/*
 *  cmplwi:  Compare Word immediate, logical
 *
 *  arg[0] = ptr to ra
 *  arg[1] = int32_t imm
 *  arg[2] = bf
 */
X(cmplwi)
{
	uint32_t tmp = reg(ic->arg[0]), imm = ic->arg[1];
	int bf = ic->arg[2], c;
	if (tmp < imm)
		c = 8;
	else if (tmp > imm)
		c = 4;
	else
		c = 2;
	/*  SO bit, copied from XER  */
	c |= ((cpu->cd.ppc.spr[SPR_XER] >> 31) & 1);
	cpu->cd.ppc.cr &= ~(0xf << (28 - 4*bf));
	cpu->cd.ppc.cr |= (c << (28 - 4*bf));
}


/*
 *  dcbz:  Data-Cache Block Zero
 *
 *  arg[0] = ptr to ra (or zero)
 *  arg[1] = ptr to rb
 */
X(dcbz)
{
	MODE_uint_t addr = reg(ic->arg[0]) + reg(ic->arg[1]);
	unsigned char cacheline[128];
	int cacheline_size = 1 << cpu->cd.ppc.cpu_type.dlinesize;
	int cleared = 0;

	/*  Synchronize the PC first:  */
	cpu->pc = (cpu->pc & ~0xfff) + ic->arg[2];

	addr &= ~(cacheline_size - 1);
	memset(cacheline, 0, sizeof(cacheline));

	/*  TODO: Don't use memory_rw() unless it is necessary.  */
	while (cleared < cacheline_size) {
		int to_clear = cacheline_size < sizeof(cacheline)?
		    cacheline_size : sizeof(cacheline);
		if (cpu->memory_rw(cpu, cpu->mem, addr, cacheline, to_clear,
		    MEM_WRITE, CACHE_DATA) != MEMORY_ACCESS_OK) {
			/*  exception  */
			return;
		}

		cleared += to_clear;
		addr += to_clear;
	}
}


/*
 *  fmr:  Floating-point Move
 *
 *  arg[0] = ptr to frb
 *  arg[1] = ptr to frt
 */
X(fmr)
{
	*(uint64_t *)ic->arg[1] = *(uint64_t *)ic->arg[0];
}


/*
 *  llsc: Load-linked and store conditional
 *
 *  arg[0] = copy of the instruction word.
 */
X(llsc)
{
	int iw = ic->arg[0], len = 4, load = 0, xo = (iw >> 1) & 1023;
	int i, rc = iw & 1, rt, ra, rb;
	uint64_t addr = 0, value;
	unsigned char d[8];

	switch (xo) {
	case PPC_31_LDARX:
		len = 8;
	case PPC_31_LWARX:
		load = 1;
		break;
	case PPC_31_STDCX_DOT:
		len = 8;
	case PPC_31_STWCX_DOT:
		break;
	}

	rt = (iw >> 21) & 31;
	ra = (iw >> 16) & 31;
	rb = (iw >> 11) & 31;

	if (ra != 0)
		addr = cpu->cd.ppc.gpr[ra];
	addr += cpu->cd.ppc.gpr[rb];

	if (load) {
		if (rc) {
			fatal("ll: rc-bit set?\n");
			exit(1);
		}
		if (cpu->memory_rw(cpu, cpu->mem, addr, d, len,
		    MEM_READ, CACHE_DATA) != MEMORY_ACCESS_OK) {
			fatal("ll: error: TODO\n");
			exit(1);
		}

		value = 0;
		for (i=0; i<len; i++) {
			value <<= 8;
			if (cpu->byte_order == EMUL_BIG_ENDIAN)
				value |= d[i];
			else
				value |= d[len - 1 - i];
		}

		cpu->cd.ppc.gpr[rt] = value;
		cpu->cd.ppc.ll_addr = addr;
		cpu->cd.ppc.ll_bit = 1;
	} else {
		uint32_t old_so = cpu->cd.ppc.spr[SPR_XER] & PPC_XER_SO;
		if (!rc) {
			fatal("sc: rc-bit not set?\n");
			exit(1);
		}

		value = cpu->cd.ppc.gpr[rt];

		/*  "If the store is performed, bits 0-2 of Condition
		    Register Field 0 are set to 0b001, otherwise, they are
		    set to 0b000. The SO bit of the XER is copied to to bit
		    4 of Condition Register Field 0.  */
		if (!cpu->cd.ppc.ll_bit || cpu->cd.ppc.ll_addr != addr) {
			cpu->cd.ppc.cr &= 0x0fffffff;
			if (old_so)
				cpu->cd.ppc.cr |= 0x10000000;
			cpu->cd.ppc.ll_bit = 0;
			return;
		}

		for (i=0; i<len; i++) {
			if (cpu->byte_order == EMUL_BIG_ENDIAN)
				d[len - 1 - i] = value >> (8*i);
			else
				d[i] = value >> (8*i);
		}

		if (cpu->memory_rw(cpu, cpu->mem, addr, d, len,
		    MEM_WRITE, CACHE_DATA) != MEMORY_ACCESS_OK) {
			fatal("sc: error: TODO\n");
			exit(1);
		}

		cpu->cd.ppc.cr &= 0x0fffffff;
		cpu->cd.ppc.cr |= 0x20000000;	/*  success!  */
		if (old_so)
			cpu->cd.ppc.cr |= 0x10000000;

		/*  Clear _all_ CPUs' ll_bits:  */
		for (i=0; i<cpu->machine->ncpus; i++)
			cpu->machine->cpus[i]->cd.ppc.ll_bit = 0;
	}
}


/*
 *  mtsr:  Move To Segment Register
 *
 *  arg[0] = sr number, or for indirect mode: ptr to rb
 *  arg[1] = ptr to rt
 */
X(mtsr)
{
	/*  TODO: This only works for 32-bit mode  */
	cpu->cd.ppc.sr[ic->arg[0]] = reg(ic->arg[1]);

	cpu->invalidate_translation_caches(cpu, 0, INVALIDATE_ALL);
}
X(mtsrin)
{
	/*  TODO: This only works for 32-bit mode  */
	uint32_t sr_num = reg(ic->arg[0]) >> 28;
	cpu->cd.ppc.sr[sr_num] = reg(ic->arg[1]);

	cpu->invalidate_translation_caches(cpu, 0, INVALIDATE_ALL);
}


/*
 *  mfsrin, mtsrin:  Move From/To Segment Register Indirect
 *
 *  arg[0] = sr number, or for indirect mode: ptr to rb
 *  arg[1] = ptr to rt
 */
X(mfsr)
{
	/*  TODO: This only works for 32-bit mode  */
	reg(ic->arg[1]) = cpu->cd.ppc.sr[ic->arg[0]];
}
X(mfsrin)
{
	/*  TODO: This only works for 32-bit mode  */
	uint32_t sr_num = reg(ic->arg[0]) >> 28;
	reg(ic->arg[1]) = cpu->cd.ppc.sr[sr_num];
}


/*
 *  rldicr:
 *
 *  arg[0] = copy of the instruction word
 */
X(rldicr)
{
	int rs = (ic->arg[0] >> 21) & 31;
	int ra = (ic->arg[0] >> 16) & 31;
	int sh = ((ic->arg[0] >> 11) & 31) | ((ic->arg[0] & 2) << 4);
	int me = ((ic->arg[0] >> 6) & 31) | (ic->arg[0] & 0x20);
	int rc = ic->arg[0] & 1;
	uint64_t tmp = cpu->cd.ppc.gpr[rs];
	/*  TODO: Fix this, its performance is awful:  */
	while (sh-- != 0) {
		int b = (tmp >> 63) & 1;
		tmp = (tmp << 1) | b;
	}
	while (me++ < 63)
		tmp &= ~((uint64_t)1 << (63-me));
	cpu->cd.ppc.gpr[ra] = tmp;
	if (rc)
		update_cr0(cpu, tmp);
}


/*
 *  rlwnm:
 *
 *  arg[0] = ptr to ra
 *  arg[1] = mask
 *  arg[2] = copy of the instruction word
 */
X(rlwnm)
{
	uint32_t tmp, iword = ic->arg[2];
	int rs = (iword >> 21) & 31;
	int rb = (iword >> 11) & 31;
	int sh = cpu->cd.ppc.gpr[rb] & 0x1f;
	tmp = (uint32_t)cpu->cd.ppc.gpr[rs];
	tmp = (tmp << sh) | (tmp >> (32-sh));
	tmp &= (uint32_t)ic->arg[1];
	reg(ic->arg[0]) = tmp;
}
DOT0(rlwnm)


/*
 *  rlwinm:
 *
 *  arg[0] = ptr to ra
 *  arg[1] = mask
 *  arg[2] = copy of the instruction word
 */
X(rlwinm)
{
	uint32_t tmp, iword = ic->arg[2];
	int rs = (iword >> 21) & 31;
	int sh = (iword >> 11) & 31;
	tmp = (uint32_t)cpu->cd.ppc.gpr[rs];
	tmp = (tmp << sh) | (tmp >> (32-sh));
	tmp &= (uint32_t)ic->arg[1];
	reg(ic->arg[0]) = tmp;
}
DOT0(rlwinm)


/*
 *  rlwimi:
 *
 *  arg[0] = ptr to rs
 *  arg[1] = ptr to ra
 *  arg[2] = copy of the instruction word
 */
X(rlwimi)
{
	MODE_uint_t tmp = reg(ic->arg[0]), ra = reg(ic->arg[1]);
	uint32_t iword = ic->arg[2];
	int sh = (iword >> 11) & 31;
	int mb = (iword >> 6) & 31;
	int me = (iword >> 1) & 31;   
	int rc = iword & 1;

	tmp = (tmp << sh) | (tmp >> (32-sh));

	for (;;) {
		uint64_t mask;
		mask = (uint64_t)1 << (31-mb);
		ra &= ~mask;
		ra |= (tmp & mask);
		if (mb == me)
			break;
		mb ++;
		if (mb == 32)
			mb = 0;
	}
	reg(ic->arg[1]) = ra;
	if (rc)
		update_cr0(cpu, ra);
}


/*
 *  srawi:
 *
 *  arg[0] = ptr to rs
 *  arg[1] = ptr to ra
 *  arg[2] = sh (shift amount)
 */
X(srawi)
{
	uint32_t tmp = reg(ic->arg[0]);
	int i = 0, j = 0, sh = ic->arg[2];

	cpu->cd.ppc.spr[SPR_XER] &= ~PPC_XER_CA;
	if (tmp & 0x80000000)
		i = 1;
	while (sh-- > 0) {
		if (tmp & 1)
			j ++;
		tmp >>= 1;
		if (tmp & 0x40000000)
			tmp |= 0x80000000;
	}
	if (i && j>0)
		cpu->cd.ppc.spr[SPR_XER] |= PPC_XER_CA;
	reg(ic->arg[1]) = (int64_t)(int32_t)tmp;
}
DOT1(srawi)


/*
 *  mcrf:  Move inside condition register
 *
 *  arg[0] = bf,  arg[1] = bfa
 */
X(mcrf)
{
	int bf = ic->arg[0], bfa = ic->arg[1];
	uint32_t tmp = (cpu->cd.ppc.cr >> (28 - bfa*4)) & 0xf;
	cpu->cd.ppc.cr &= ~(0xf << (28 - bf*4));
	cpu->cd.ppc.cr |= (tmp << (28 - bf*4));
}


/*
 *  crand, crxor etc:  Condition Register operations
 *
 *  arg[0] = copy of the instruction word
 */
X(crand) {
	uint32_t iword = ic->arg[0]; int bt = (iword >> 21) & 31;
	int ba = (iword >> 16) & 31, bb = (iword >> 11) & 31;
	ba = (cpu->cd.ppc.cr >> (31-ba)) & 1;
	bb = (cpu->cd.ppc.cr >> (31-bb)) & 1;
	cpu->cd.ppc.cr &= ~(1 << (31-bt));
	if (ba & bb)
		cpu->cd.ppc.cr |= (1 << (31-bt));
}
X(crandc) {
	uint32_t iword = ic->arg[0]; int bt = (iword >> 21) & 31;
	int ba = (iword >> 16) & 31, bb = (iword >> 11) & 31;
	ba = (cpu->cd.ppc.cr >> (31-ba)) & 1;
	bb = (cpu->cd.ppc.cr >> (31-bb)) & 1;
	cpu->cd.ppc.cr &= ~(1 << (31-bt));
	if (!(ba & bb))
		cpu->cd.ppc.cr |= (1 << (31-bt));
}
X(creqv) {
	uint32_t iword = ic->arg[0]; int bt = (iword >> 21) & 31;
	int ba = (iword >> 16) & 31, bb = (iword >> 11) & 31;
	ba = (cpu->cd.ppc.cr >> (31-ba)) & 1;
	bb = (cpu->cd.ppc.cr >> (31-bb)) & 1;
	cpu->cd.ppc.cr &= ~(1 << (31-bt));
	if (!(ba ^ bb))
		cpu->cd.ppc.cr |= (1 << (31-bt));
}
X(cror) {
	uint32_t iword = ic->arg[0]; int bt = (iword >> 21) & 31;
	int ba = (iword >> 16) & 31, bb = (iword >> 11) & 31;
	ba = (cpu->cd.ppc.cr >> (31-ba)) & 1;
	bb = (cpu->cd.ppc.cr >> (31-bb)) & 1;
	cpu->cd.ppc.cr &= ~(1 << (31-bt));
	if (ba | bb)
		cpu->cd.ppc.cr |= (1 << (31-bt));
}
X(crxor) {
	uint32_t iword = ic->arg[0]; int bt = (iword >> 21) & 31;
	int ba = (iword >> 16) & 31, bb = (iword >> 11) & 31;
	ba = (cpu->cd.ppc.cr >> (31-ba)) & 1;
	bb = (cpu->cd.ppc.cr >> (31-bb)) & 1;
	cpu->cd.ppc.cr &= ~(1 << (31-bt));
	if (ba ^ bb)
		cpu->cd.ppc.cr |= (1 << (31-bt));
}


/*
 *  mfspr: Move from SPR
 *
 *  arg[0] = pointer to destination register
 *  arg[1] = pointer to source SPR
 */
X(mfspr) {
	reg(ic->arg[0]) = reg(ic->arg[1]);
}
X(mfspr_pmc1) {
	/*
	 *  TODO: This is a temporary hack to make NetBSD/ppc detect
	 *  a 10.0 MHz CPU.
	 */
	reg(ic->arg[0]) = 1000000;
}
X(mftb) {
	/*  NOTE/TODO: This increments the time base (slowly) if it
	    is being polled.  */
	if (++cpu->cd.ppc.spr[SPR_TBL] == 0)
		cpu->cd.ppc.spr[SPR_TBU] ++;
	reg(ic->arg[0]) = cpu->cd.ppc.spr[SPR_TBL];
}
X(mftbu) {
	reg(ic->arg[0]) = cpu->cd.ppc.spr[SPR_TBU];
}


/*
 *  mtspr: Move to SPR.
 *
 *  arg[0] = pointer to source register
 *  arg[1] = pointer to the SPR
 */
X(mtspr) {
	reg(ic->arg[1]) = reg(ic->arg[0]);
}


/*
 *  rfi:  Return from Interrupt
 */
X(rfi)
{
	uint64_t tmp;

	reg_access_msr(cpu, &tmp, 0, 0);
	tmp &= ~0xffff;
	tmp |= (cpu->cd.ppc.spr[SPR_SRR1] & 0xffff);
	reg_access_msr(cpu, &tmp, 1, 0);

	cpu->pc = cpu->cd.ppc.spr[SPR_SRR0];
	DYNTRANS_PC_TO_POINTERS(cpu);
}


/*
 *  mfcr:  Move From Condition Register
 *
 *  arg[0] = pointer to destination register
 */
X(mfcr)
{
	reg(ic->arg[0]) = cpu->cd.ppc.cr;
}


/*
 *  mfmsr:  Move From MSR
 *
 *  arg[0] = pointer to destination register
 */
X(mfmsr)
{
	reg_access_msr(cpu, (uint64_t*)ic->arg[0], 0, 0);
}


/*
 *  mtmsr:  Move To MSR
 *
 *  arg[0] = pointer to source register
 */
X(mtmsr)
{
	/*  Synchronize the PC (pointing to _after_ this instruction)  */
	cpu->pc = (cpu->pc & ~0xfff) + ic->arg[1];

	reg_access_msr(cpu, (uint64_t*)ic->arg[0], 1, 1);
}


/*
 *  mtcrf:  Move To Condition Register Fields
 *
 *  arg[0] = pointer to source register
 */
X(mtcrf)
{
	cpu->cd.ppc.cr &= ~ic->arg[1];
	cpu->cd.ppc.cr |= (reg(ic->arg[0]) & ic->arg[1]);
}


/*
 *  mulli:  Multiply Low Immediate.
 *
 *  arg[0] = pointer to source register ra
 *  arg[1] = int32_t immediate
 *  arg[2] = pointer to destination register rt
 */
X(mulli)
{
	reg(ic->arg[2]) = (uint32_t)(reg(ic->arg[0]) * (int32_t)ic->arg[1]);
}


/*
 *  Load/Store Multiple:
 *
 *  arg[0] = rs  (or rt for loads)  NOTE: not a pointer
 *  arg[1] = ptr to ra
 *  arg[2] = int32_t immediate offset
 */
X(lmw) {
	MODE_uint_t addr = reg(ic->arg[1]) + (int32_t)ic->arg[2];
	unsigned char d[4];
	int rs = ic->arg[0];

	int low_pc = ((size_t)ic - (size_t)cpu->cd.ppc.cur_ic_page)
	    / sizeof(struct ppc_instr_call);
	cpu->pc &= ~((PPC_IC_ENTRIES_PER_PAGE-1)
	    << PPC_INSTR_ALIGNMENT_SHIFT);
	cpu->pc |= (low_pc << PPC_INSTR_ALIGNMENT_SHIFT);

	while (rs <= 31) {
		if (cpu->memory_rw(cpu, cpu->mem, addr, d, sizeof(d),
		    MEM_READ, CACHE_DATA) != MEMORY_ACCESS_OK) {
			/*  exception  */
			return;
		}

		if (cpu->byte_order == EMUL_BIG_ENDIAN)
			cpu->cd.ppc.gpr[rs] = (d[0] << 24) + (d[1] << 16)
			    + (d[2] << 8) + d[3];
		else
			cpu->cd.ppc.gpr[rs] = (d[3] << 24) + (d[2] << 16)
			    + (d[1] << 8) + d[0];

		rs ++;
		addr += sizeof(uint32_t);
	}
}
X(stmw) {
	MODE_uint_t addr = reg(ic->arg[1]) + (int32_t)ic->arg[2];
	unsigned char d[4];
	int rs = ic->arg[0];

	int low_pc = ((size_t)ic - (size_t)cpu->cd.ppc.cur_ic_page)
	    / sizeof(struct ppc_instr_call);
	cpu->pc &= ~((PPC_IC_ENTRIES_PER_PAGE-1)
	    << PPC_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (low_pc << PPC_INSTR_ALIGNMENT_SHIFT);

	while (rs <= 31) {
		uint32_t tmp = cpu->cd.ppc.gpr[rs];
		if (cpu->byte_order == EMUL_BIG_ENDIAN) {
			d[3] = tmp; d[2] = tmp >> 8;
			d[1] = tmp >> 16; d[0] = tmp >> 24;
		} else {
			d[0] = tmp; d[1] = tmp >> 8;
			d[2] = tmp >> 16; d[3] = tmp >> 24;
		}
		if (cpu->memory_rw(cpu, cpu->mem, addr, d, sizeof(d),
		    MEM_WRITE, CACHE_DATA) != MEMORY_ACCESS_OK) {
			/*  exception  */
			return;
		}

		rs ++;
		addr += sizeof(uint32_t);
	}
}


/*
 *  Load/store string:
 *
 *  arg[0] = rs   (well, rt for lswi)
 *  arg[1] = ptr to ra (or ptr to zero)
 *  arg[2] = nb
 */
X(lswi)
{
	MODE_uint_t addr = reg(ic->arg[1]);
	int rt = ic->arg[0], nb = ic->arg[2];
	unsigned char d;
	int sub = 0;

	int low_pc = ((size_t)ic - (size_t)cpu->cd.ppc.cur_ic_page)
	    / sizeof(struct ppc_instr_call);
	cpu->pc &= ~((PPC_IC_ENTRIES_PER_PAGE-1)
	    << PPC_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (low_pc << PPC_INSTR_ALIGNMENT_SHIFT);

	while (nb > 0) {
		if (cpu->memory_rw(cpu, cpu->mem, addr, &d, 1,
		    MEM_READ, CACHE_DATA) != MEMORY_ACCESS_OK) {
			/*  exception  */
			return;
		}

		if (cpu->cd.ppc.mode == MODE_POWER && sub == 0)
			cpu->cd.ppc.gpr[rt] = 0;
		cpu->cd.ppc.gpr[rt] &= ~(0xff << (24-8*sub));
		cpu->cd.ppc.gpr[rt] |= (d << (24-8*sub));
		sub ++;
		if (sub == 4) {
			rt = (rt + 1) & 31;
			sub = 0;
		}
		addr ++;
		nb --;
	}
}
X(stswi)
{
	MODE_uint_t addr = reg(ic->arg[1]);
	int rs = ic->arg[0], nb = ic->arg[2];
	uint32_t cur = cpu->cd.ppc.gpr[rs];
	unsigned char d;
	int sub = 0;

	int low_pc = ((size_t)ic - (size_t)cpu->cd.ppc.cur_ic_page)
	    / sizeof(struct ppc_instr_call);
	cpu->pc &= ~((PPC_IC_ENTRIES_PER_PAGE-1)
	    << PPC_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (low_pc << PPC_INSTR_ALIGNMENT_SHIFT);

	while (nb > 0) {
		d = cur >> 24;
		if (cpu->memory_rw(cpu, cpu->mem, addr, &d, 1,
		    MEM_WRITE, CACHE_DATA) != MEMORY_ACCESS_OK) {
			/*  exception  */
			return;
		}
		cur <<= 8;
		sub ++;
		if (sub == 4) {
			rs = (rs + 1) & 31;
			sub = 0;
			cur = cpu->cd.ppc.gpr[rs];
		}
		addr ++;
		nb --;
	}
}


/*
 *  Shifts, and, or, xor, etc.
 *
 *  arg[0] = pointer to source register rs
 *  arg[1] = pointer to source register rb
 *  arg[2] = pointer to destination register ra
 */
X(extsb) {
#ifdef MODE32
	reg(ic->arg[2]) = (int32_t)(int8_t)reg(ic->arg[0]);
#else
	reg(ic->arg[2]) = (int64_t)(int8_t)reg(ic->arg[0]);
#endif
}
DOT2(extsb)
X(extsh) {
#ifdef MODE32
	reg(ic->arg[2]) = (int32_t)(int16_t)reg(ic->arg[0]);
#else
	reg(ic->arg[2]) = (int64_t)(int16_t)reg(ic->arg[0]);
#endif
}
DOT2(extsh)
X(extsw) {
#ifdef MODE32
	fatal("TODO: extsw: invalid instruction\n"); exit(1);
#else
	reg(ic->arg[2]) = (int64_t)(int32_t)reg(ic->arg[0]);
#endif
}
DOT2(extsw)
X(slw) {	reg(ic->arg[2]) = (uint64_t)reg(ic->arg[0])
		    << (reg(ic->arg[1]) & 31); }
DOT2(slw)
X(sraw)
{
	uint32_t tmp = reg(ic->arg[0]);
	int i = 0, j = 0, sh = reg(ic->arg[1]) & 31;

	cpu->cd.ppc.spr[SPR_XER] &= ~PPC_XER_CA;
	if (tmp & 0x80000000)
		i = 1;
	while (sh-- > 0) {
		if (tmp & 1)
			j ++;
		tmp >>= 1;
		if (tmp & 0x40000000)
			tmp |= 0x80000000;
	}
	if (i && j>0)
		cpu->cd.ppc.spr[SPR_XER] |= PPC_XER_CA;
	reg(ic->arg[2]) = (int64_t)(int32_t)tmp;
}
DOT2(sraw)
X(srw) {	reg(ic->arg[2]) = (uint64_t)reg(ic->arg[0])
		    >> (reg(ic->arg[1]) & 31); }
DOT2(srw)
X(and) {	reg(ic->arg[2]) = reg(ic->arg[0]) & reg(ic->arg[1]); }
X(and_dot) {	reg(ic->arg[2]) = reg(ic->arg[0]) & reg(ic->arg[1]);
		update_cr0(cpu, reg(ic->arg[2])); }
X(nand) {	reg(ic->arg[2]) = ~(reg(ic->arg[0]) & reg(ic->arg[1])); }
X(nand_dot) {	reg(ic->arg[2]) = ~(reg(ic->arg[0]) & reg(ic->arg[1]));
		update_cr0(cpu, reg(ic->arg[2])); }
X(andc) {	reg(ic->arg[2]) = reg(ic->arg[0]) & (~reg(ic->arg[1])); }
X(andc_dot) {	reg(ic->arg[2]) = reg(ic->arg[0]) & (~reg(ic->arg[1]));
		update_cr0(cpu, reg(ic->arg[2])); }
X(nor) {	reg(ic->arg[2]) = ~(reg(ic->arg[0]) | reg(ic->arg[1])); }
X(nor_dot) {	reg(ic->arg[2]) = ~(reg(ic->arg[0]) | reg(ic->arg[1]));
		update_cr0(cpu, reg(ic->arg[2])); }
X(or) {		reg(ic->arg[2]) = reg(ic->arg[0]) | reg(ic->arg[1]); }
X(or_dot) {	reg(ic->arg[2]) = reg(ic->arg[0]) | reg(ic->arg[1]);
		update_cr0(cpu, reg(ic->arg[2])); }
X(orc) {	reg(ic->arg[2]) = reg(ic->arg[0]) | (~reg(ic->arg[1])); }
X(orc_dot) {	reg(ic->arg[2]) = reg(ic->arg[0]) | (~reg(ic->arg[1]));
		update_cr0(cpu, reg(ic->arg[2])); }
X(xor) {	reg(ic->arg[2]) = reg(ic->arg[0]) ^ reg(ic->arg[1]); }
X(xor_dot) {	reg(ic->arg[2]) = reg(ic->arg[0]) ^ reg(ic->arg[1]);
		update_cr0(cpu, reg(ic->arg[2])); }


/*
 *  neg:
 *
 *  arg[0] = pointer to source register ra
 *  arg[1] = pointer to destination register rt
 */
X(neg) {	reg(ic->arg[1]) = -reg(ic->arg[0]); }
DOT1(neg)


/*
 *  mullw, mulhw[u], divw[u]:
 *
 *  arg[0] = pointer to source register ra
 *  arg[1] = pointer to source register rb
 *  arg[2] = pointer to destination register rt
 */
X(mullw)
{
	int32_t sum = (int32_t)reg(ic->arg[0]) * (int32_t)reg(ic->arg[1]);
	reg(ic->arg[2]) = (int32_t)sum;
}
X(mulhw)
{
	int64_t sum;
	sum = (int64_t)(int32_t)reg(ic->arg[0])
	    * (int64_t)(int32_t)reg(ic->arg[1]);
	reg(ic->arg[2]) = sum >> 32;
}
X(mulhwu)
{
	uint64_t sum;
	sum = (uint64_t)(uint32_t)reg(ic->arg[0])
	    * (uint64_t)(uint32_t)reg(ic->arg[1]);
	reg(ic->arg[2]) = sum >> 32;
}
X(divw)
{
	int32_t a = reg(ic->arg[0]), b = reg(ic->arg[1]);
	int32_t sum;
	if (b == 0)
		sum = 0;
	else
		sum = a / b;
	reg(ic->arg[2]) = (uint32_t)sum;
}
X(divwu)
{
	uint32_t a = reg(ic->arg[0]), b = reg(ic->arg[1]);
	uint32_t sum;
	if (b == 0)
		sum = 0;
	else
		sum = a / b;
	reg(ic->arg[2]) = sum;
}


/*
 *  add:  Add.
 *
 *  arg[0] = pointer to source register ra
 *  arg[1] = pointer to source register rb
 *  arg[2] = pointer to destination register rt
 */
X(add)     { reg(ic->arg[2]) = reg(ic->arg[0]) + reg(ic->arg[1]); }
X(add_dot) { instr(add)(cpu,ic); update_cr0(cpu, reg(ic->arg[2])); }


/*
 *  addc:  Add carrying.
 *
 *  arg[0] = pointer to source register ra
 *  arg[1] = pointer to source register rb
 *  arg[2] = pointer to destination register rt
 */
X(addc)
{
	/*  TODO: this only works in 32-bit mode  */
	uint64_t tmp = (uint32_t)reg(ic->arg[0]);
	uint64_t tmp2 = tmp;
	cpu->cd.ppc.spr[SPR_XER] &= ~PPC_XER_CA;
	tmp += (uint32_t)reg(ic->arg[1]);
	if ((tmp >> 32) != (tmp2 >> 32))
		cpu->cd.ppc.spr[SPR_XER] |= PPC_XER_CA;
	reg(ic->arg[2]) = (uint32_t)tmp;
}


/*
 *  adde:  Add extended, etc.
 *
 *  arg[0] = pointer to source register ra
 *  arg[1] = pointer to source register rb
 *  arg[2] = pointer to destination register rt
 */
X(adde)
{
	/*  TODO: this only works in 32-bit mode  */
	int old_ca = cpu->cd.ppc.spr[SPR_XER] & PPC_XER_CA;
	uint64_t tmp = (uint32_t)reg(ic->arg[0]);
	uint64_t tmp2 = tmp;
	cpu->cd.ppc.spr[SPR_XER] &= ~PPC_XER_CA;
	tmp += (uint32_t)reg(ic->arg[1]);
	if (old_ca)
		tmp ++;
	if ((tmp >> 32) != (tmp2 >> 32))
		cpu->cd.ppc.spr[SPR_XER] |= PPC_XER_CA;
	reg(ic->arg[2]) = (uint32_t)tmp;
}
X(adde_dot) { instr(adde)(cpu,ic); update_cr0(cpu, reg(ic->arg[2])); }
X(addme)
{
	/*  TODO: this only works in 32-bit mode  */
	int old_ca = cpu->cd.ppc.spr[SPR_XER] & PPC_XER_CA;
	uint64_t tmp = (uint32_t)reg(ic->arg[0]);
	uint64_t tmp2 = tmp;
	cpu->cd.ppc.spr[SPR_XER] &= ~PPC_XER_CA;
	if (old_ca)
		tmp ++;
	tmp += 0xffffffffULL;
	if ((tmp >> 32) != (tmp2 >> 32))
		cpu->cd.ppc.spr[SPR_XER] |= PPC_XER_CA;
	reg(ic->arg[2]) = (uint32_t)tmp;
}
X(addme_dot) { instr(addme)(cpu,ic); update_cr0(cpu, reg(ic->arg[2])); }
X(addze)
{
	/*  TODO: this only works in 32-bit mode  */
	int old_ca = cpu->cd.ppc.spr[SPR_XER] & PPC_XER_CA;
	uint64_t tmp = (uint32_t)reg(ic->arg[0]);
	uint64_t tmp2 = tmp;
	cpu->cd.ppc.spr[SPR_XER] &= ~PPC_XER_CA;
	if (old_ca)
		tmp ++;
	if ((tmp >> 32) != (tmp2 >> 32))
		cpu->cd.ppc.spr[SPR_XER] |= PPC_XER_CA;
	reg(ic->arg[2]) = (uint32_t)tmp;
}
X(addze_dot) { instr(addze)(cpu,ic); update_cr0(cpu, reg(ic->arg[2])); }


/*
 *  subf:  Subf, etc.
 *
 *  arg[0] = pointer to source register ra
 *  arg[1] = pointer to source register rb
 *  arg[2] = pointer to destination register rt
 */
X(subf)
{
	reg(ic->arg[2]) = reg(ic->arg[1]) - reg(ic->arg[0]);
}
DOT2(subf)
X(subfc)
{
	cpu->cd.ppc.spr[SPR_XER] &= ~PPC_XER_CA;
	if (reg(ic->arg[1]) >= reg(ic->arg[0]))
		cpu->cd.ppc.spr[SPR_XER] |= PPC_XER_CA;
	reg(ic->arg[2]) = reg(ic->arg[1]) - reg(ic->arg[0]);
}
DOT2(subfc)
X(subfe)
{
	int old_ca = (cpu->cd.ppc.spr[SPR_XER] & PPC_XER_CA)? 1 : 0;
	cpu->cd.ppc.spr[SPR_XER] &= ~PPC_XER_CA;
	if (reg(ic->arg[1]) == reg(ic->arg[0])) {
		if (old_ca)
			cpu->cd.ppc.spr[SPR_XER] |= PPC_XER_CA;
	} else if (reg(ic->arg[1]) >= reg(ic->arg[0]))
		cpu->cd.ppc.spr[SPR_XER] |= PPC_XER_CA;

	/*
	 *  TODO: The register value calculation should be correct,
	 *  but the CA bit calculation above is probably not.
	 */

	reg(ic->arg[2]) = reg(ic->arg[1]) - reg(ic->arg[0]) - (old_ca? 0 : 1);
}
DOT2(subfe)
X(subfme)
{
	int old_ca = cpu->cd.ppc.spr[SPR_XER] & PPC_XER_CA;
	uint64_t tmp = (uint32_t)(~reg(ic->arg[0]));
	tmp += 0xffffffffULL;
	cpu->cd.ppc.spr[SPR_XER] &= ~PPC_XER_CA;
	if (old_ca)
		tmp ++;
	if ((tmp >> 32) != 0)
		cpu->cd.ppc.spr[SPR_XER] |= PPC_XER_CA;
	reg(ic->arg[2]) = (uint32_t)tmp;
}
DOT2(subfme)
X(subfze)
{
	int old_ca = cpu->cd.ppc.spr[SPR_XER] & PPC_XER_CA;
	uint64_t tmp = (uint32_t)(~reg(ic->arg[0]));
	uint64_t tmp2 = tmp;
	cpu->cd.ppc.spr[SPR_XER] &= ~PPC_XER_CA;
	if (old_ca)
		tmp ++;
	if ((tmp >> 32) != (tmp2 >> 32))
		cpu->cd.ppc.spr[SPR_XER] |= PPC_XER_CA;
	reg(ic->arg[2]) = (uint32_t)tmp;
}
DOT2(subfze)


/*
 *  ori, xori etc.:
 *
 *  arg[0] = pointer to source uint64_t
 *  arg[1] = immediate value (uint32_t or larger)
 *  arg[2] = pointer to destination uint64_t
 */
X(ori)  { reg(ic->arg[2]) = reg(ic->arg[0]) | (uint32_t)ic->arg[1]; }
X(xori) { reg(ic->arg[2]) = reg(ic->arg[0]) ^ (uint32_t)ic->arg[1]; }


/*
 *  tlbie:  TLB invalidate
 */
X(tlbie)
{
	cpu->invalidate_translation_caches(cpu, 0, INVALIDATE_ALL);
}


/*
 *  sc: Syscall.
 */
X(sc)
{
	/*  Synchronize the PC (pointing to _after_ this instruction)  */
	cpu->pc = (cpu->pc & ~0xfff) + ic->arg[1];

	ppc_exception(cpu, PPC_EXCEPTION_SC);
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
 *  openfirmware:
 */
X(openfirmware)
{
	of_emul(cpu);
	cpu->pc = cpu->cd.ppc.spr[SPR_LR];
	if (cpu->machine->show_trace_tree)
		cpu_functioncall_trace_return(cpu);
	DYNTRANS_PC_TO_POINTERS(cpu);
}


/*
 *  tlbli:
 */
X(tlbli)
{
}


/*
 *  tlbld:
 */
X(tlbld)
{
	MODE_uint_t vaddr = reg(ic->arg[0]);
	MODE_uint_t paddr = cpu->cd.ppc.spr[SPR_RPA];

	/*  TODO?  */
}


#include "tmp_ppc_loadstore.c"


/*****************************************************************************/


X(end_of_page)
{
	/*  Update the PC:  (offset 0, but on the next page)  */
	cpu->pc &= ~((PPC_IC_ENTRIES_PER_PAGE-1) << 2);
	cpu->pc += (PPC_IC_ENTRIES_PER_PAGE << 2);

	/*  Find the new physical page and update the translation pointers:  */
	DYNTRANS_PC_TO_POINTERS(cpu);

	/*  end_of_page doesn't count as an executed instruction:  */
	cpu->n_translated_instrs --;
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
	uint32_t iword, mask;
	unsigned char *page;
	unsigned char ib[4];
	int main_opcode, rt, rs, ra, rb, rc, aa_bit, l_bit, lk_bit, spr, sh,
	    xo, imm, load, size, update, zero, bf, bo, bi, bh, oe_bit, n64=0,
	    bfa, fp, byterev, nb, mb, me;
	void (*samepage_function)(struct cpu *, struct ppc_instr_call *);
	void (*rc_f)(struct cpu *, struct ppc_instr_call *);

	/*  Figure out the (virtual) address of the instruction:  */
	low_pc = ((size_t)ic - (size_t)cpu->cd.ppc.cur_ic_page)
	    / sizeof(struct ppc_instr_call);
	addr = cpu->pc & ~((PPC_IC_ENTRIES_PER_PAGE-1)
	    << PPC_INSTR_ALIGNMENT_SHIFT);
	addr += (low_pc << PPC_INSTR_ALIGNMENT_SHIFT);
	cpu->pc = addr;
	addr &= ~((1 << PPC_INSTR_ALIGNMENT_SHIFT) - 1);

	/*  Read the instruction word from memory:  */
	page = cpu->cd.ppc.host_load[addr >> 12];

	if (page != NULL) {
		/*  fatal("TRANSLATION HIT!\n");  */
		memcpy(ib, page + (addr & 0xfff), sizeof(ib));
	} else {
		/*  fatal("TRANSLATION MISS!\n");  */
		if (!cpu->memory_rw(cpu, cpu->mem, addr, ib,
		    sizeof(ib), MEM_READ, CACHE_INSTRUCTION)) {
			fatal("PPC to_be_translated(): "
			    "read failed: TODO\n");
			exit(1);
			/*  goto bad;  */
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

	case PPC_HI6_MULLI:
		rt = (iword >> 21) & 31;
		ra = (iword >> 16) & 31;
		imm = (int16_t)(iword & 0xffff);
		ic->f = instr(mulli);
		ic->arg[0] = (size_t)(&cpu->cd.ppc.gpr[ra]);
		ic->arg[1] = (ssize_t)imm;
		ic->arg[2] = (size_t)(&cpu->cd.ppc.gpr[rt]);
		break;

	case PPC_HI6_SUBFIC:
		rt = (iword >> 21) & 31;
		ra = (iword >> 16) & 31;
		imm = (int16_t)(iword & 0xffff);
		ic->f = instr(subfic);
		ic->arg[0] = (size_t)(&cpu->cd.ppc.gpr[ra]);
		ic->arg[1] = (ssize_t)imm;
		ic->arg[2] = (size_t)(&cpu->cd.ppc.gpr[rt]);
		break;

	case PPC_HI6_CMPLI:
	case PPC_HI6_CMPI:
		bf = (iword >> 23) & 7;
		l_bit = (iword >> 21) & 1;
		ra = (iword >> 16) & 31;
		if (main_opcode == PPC_HI6_CMPLI) {
			imm = iword & 0xffff;
			if (l_bit)
				ic->f = instr(cmpldi);
			else
				ic->f = instr(cmplwi);
		} else {
			imm = (int16_t)(iword & 0xffff);
			if (l_bit)
				ic->f = instr(cmpdi);
			else
				ic->f = instr(cmpwi);
		}
		ic->arg[0] = (size_t)(&cpu->cd.ppc.gpr[ra]);
		ic->arg[1] = (ssize_t)imm;
		ic->arg[2] = bf;
		break;

	case PPC_HI6_ADDIC:
	case PPC_HI6_ADDIC_DOT:
		if (cpu->cd.ppc.bits == 64) {
			fatal("addic for 64-bit: TODO\n");
			goto bad;
		}
		rt = (iword >> 21) & 31;
		ra = (iword >> 16) & 31;
		imm = (int16_t)(iword & 0xffff);
		if (main_opcode == PPC_HI6_ADDIC)
			ic->f = instr(addic);
		else
			ic->f = instr(addic_dot);
		ic->arg[0] = (size_t)(&cpu->cd.ppc.gpr[ra]);
		ic->arg[1] = imm;
		ic->arg[2] = (size_t)(&cpu->cd.ppc.gpr[rt]);
		break;

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

	case PPC_HI6_ANDI_DOT:
	case PPC_HI6_ANDIS_DOT:
		rs = (iword >> 21) & 31; ra = (iword >> 16) & 31;
		ic->f = instr(andi_dot);
		ic->arg[0] = (size_t)(&cpu->cd.ppc.gpr[rs]);
		ic->arg[1] = iword & 0xffff;
		if (main_opcode == PPC_HI6_ANDIS_DOT)
			ic->arg[1] <<= 16;
		ic->arg[2] = (size_t)(&cpu->cd.ppc.gpr[ra]);
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
	case PPC_HI6_LHA:
	case PPC_HI6_LHAU:
	case PPC_HI6_LWZ:
	case PPC_HI6_LWZU:
	case PPC_HI6_LFD:
	case PPC_HI6_STB:
	case PPC_HI6_STBU:
	case PPC_HI6_STH:
	case PPC_HI6_STHU:
	case PPC_HI6_STW:
	case PPC_HI6_STWU:
	case PPC_HI6_STFD:
		rs = (iword >> 21) & 31;
		ra = (iword >> 16) & 31;
		imm = (int16_t)(iword & 0xffff);
		load = 0; zero = 1; size = 0; update = 0; fp = 0;
		switch (main_opcode) {
		case PPC_HI6_LBZ:  load=1; break;
		case PPC_HI6_LBZU: load=1; update=1; break;
		case PPC_HI6_LHA:  load=1; size=1; zero=0; break;
		case PPC_HI6_LHAU: load=1; size=1; zero=0; update=1; break;
		case PPC_HI6_LHZ:  load=1; size=1; break;
		case PPC_HI6_LHZU: load=1; size=1; update = 1; break;
		case PPC_HI6_LWZ:  load=1; size=2; break;
		case PPC_HI6_LWZU: load=1; size=2; update = 1; break;
		case PPC_HI6_LFD:  load=1; size=3; fp = 1; break;
		case PPC_HI6_STB:  break;
		case PPC_HI6_STBU: update = 1; break;
		case PPC_HI6_STH:  size = 1; break;
		case PPC_HI6_STHU: size = 1; update = 1; break;
		case PPC_HI6_STW:  size = 2; break;
		case PPC_HI6_STWU: size = 2; update = 1; break;
		case PPC_HI6_STFD: size=3; fp = 1; break;
		}
		ic->f =
#ifdef MODE32
		    ppc32_loadstore
#else
		    ppc_loadstore
#endif
		    [size + 4*zero + 8*load + (imm==0? 16 : 0)
		    + 32*update];
		if (ra == 0 && update) {
			fatal("TODO: ra=0 && update?\n");
			goto bad;
		}
		if (fp)
			ic->arg[0] = (size_t)(&cpu->cd.ppc.fpr[rs]);
		else
			ic->arg[0] = (size_t)(&cpu->cd.ppc.gpr[rs]);
		if (ra == 0)
			ic->arg[1] = (size_t)(&cpu->cd.ppc.zero);
		else
			ic->arg[1] = (size_t)(&cpu->cd.ppc.gpr[ra]);
		ic->arg[2] = (ssize_t)imm;
		break;

	case PPC_HI6_BC:
		aa_bit = (iword >> 1) & 1;
		lk_bit = iword & 1;
		bo = (iword >> 21) & 31;
		bi = (iword >> 16) & 31;
		tmp_addr = (int64_t)(int16_t)(iword & 0xfffc);
		if (aa_bit) {
			fatal("aa_bit: NOT YET\n");
			goto bad;
		}
		if (lk_bit) {
			ic->f = instr(bcl);
			samepage_function = instr(bcl_samepage);
		} else {
			ic->f = instr(bc);
			samepage_function = instr(bc_samepage);
		}
		ic->arg[0] = (ssize_t)tmp_addr;
		ic->arg[1] = bo;
		ic->arg[2] = bi;
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

	case PPC_HI6_SC:
		ic->arg[0] = (iword >> 5) & 0x7f;
		ic->arg[1] = (addr & 0xfff) + 4;
		if (cpu->machine->userland_emul != NULL)
			ic->f = instr(user_syscall);
		else if (iword == 0x44ee0002) {
			/*  Special case/magic hack for OpenFirmware emul:  */
			ic->f = instr(openfirmware);
		} else
			ic->f = instr(sc);
		break;

	case PPC_HI6_B:
		aa_bit = (iword & 2) >> 1;
		lk_bit = iword & 1;
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
		if (aa_bit) {
			if (lk_bit) {
				if (cpu->machine->show_trace_tree) {
					ic->f = instr(bla_trace);
				} else {
					ic->f = instr(bla);
				}
			} else {
				ic->f = instr(ba);
			}
			ic->arg[0] = (ssize_t)tmp_addr;
		}
		break;

	case PPC_HI6_19:
		xo = (iword >> 1) & 1023;
		switch (xo) {

		case PPC_19_BCLR:
		case PPC_19_BCCTR:
			bo = (iword >> 21) & 31;
			bi = (iword >> 16) & 31;
			bh = (iword >> 11) & 3;
			lk_bit = iword & 1;
			if (xo == PPC_19_BCLR) {
				if (lk_bit)
					ic->f = instr(bclr_l);
				else
					ic->f = instr(bclr);
			} else {
				if (lk_bit)
					ic->f = instr(bcctr_l);
				else
					ic->f = instr(bcctr);
			}
			ic->arg[0] = bo;
			ic->arg[1] = bi;
			ic->arg[2] = bh;
			break;

		case PPC_19_ISYNC:
			/*  TODO  */
			ic->f = instr(nop);
			break;

		case PPC_19_RFI:
			ic->f = instr(rfi);
			break;

		case PPC_19_MCRF:
			bf = (iword >> 23) & 7;
			bfa = (iword >> 18) & 7;
			ic->arg[0] = bf;
			ic->arg[1] = bfa;
			ic->f = instr(mcrf);
			break;

		case PPC_19_CRAND:
		case PPC_19_CRANDC:
		case PPC_19_CREQV:
		case PPC_19_CROR:
		case PPC_19_CRXOR:
			switch (xo) {
			case PPC_19_CRAND:  ic->f = instr(crand); break;
			case PPC_19_CRANDC: ic->f = instr(crandc); break;
			case PPC_19_CREQV:  ic->f = instr(creqv); break;
			case PPC_19_CROR:   ic->f = instr(cror); break;
			case PPC_19_CRXOR:  ic->f = instr(crxor); break;
			}
			ic->arg[0] = iword;
			break;

		default:goto bad;
		}
		break;

	case PPC_HI6_RLWNM:
	case PPC_HI6_RLWINM:
		ra = (iword >> 16) & 31;
		mb = (iword >> 6) & 31;
		me = (iword >> 1) & 31;   
		rc = iword & 1;
		mask = 0;
		for (;;) {
			mask |= ((uint32_t)0x80000000 >> mb);
			if (mb == me)
				break;
			mb ++; mb &= 31;
		}
		switch (main_opcode) {
		case PPC_HI6_RLWNM:
			ic->f = rc? instr(rlwnm_dot) : instr(rlwnm); break;
		case PPC_HI6_RLWINM:
			ic->f = rc? instr(rlwinm_dot) : instr(rlwinm); break;
		}
		ic->arg[0] = (size_t)(&cpu->cd.ppc.gpr[ra]);
		ic->arg[1] = mask;
		ic->arg[2] = (uint32_t)iword;
		break;

	case PPC_HI6_RLWIMI:
		rs = (iword >> 21) & 31;
		ra = (iword >> 16) & 31;
		ic->f = instr(rlwimi);
		ic->arg[0] = (size_t)(&cpu->cd.ppc.gpr[rs]);
		ic->arg[1] = (size_t)(&cpu->cd.ppc.gpr[ra]);
		ic->arg[2] = (uint32_t)iword;
		break;

	case PPC_HI6_LMW:
	case PPC_HI6_STMW:
		/*  NOTE: Loads use rt, not rs.  */
		rs = (iword >> 21) & 31;
		ra = (iword >> 16) & 31;
		ic->arg[0] = rs;
		if (ra == 0)
			ic->arg[1] = (size_t)(&cpu->cd.ppc.zero);
		else
			ic->arg[1] = (size_t)(&cpu->cd.ppc.gpr[ra]);
		ic->arg[2] = (int32_t)(int16_t)iword;
		switch (main_opcode) {
		case PPC_HI6_LMW:
			ic->f = instr(lmw);
			break;
		case PPC_HI6_STMW:
			ic->f = instr(stmw);
			break;
		}
		break;

	case PPC_HI6_30:
		xo = (iword >> 2) & 7;
		switch (xo) {

		case PPC_30_RLDICR:
			ic->f = instr(rldicr);
			ic->arg[0] = iword;
			if (cpu->cd.ppc.bits == 32) {
				fatal("TODO: rldicr in 32-bit mode?\n");
				goto bad;
			}
			break;

		default:goto bad;
		}
		break;

	case PPC_HI6_31:
		xo = (iword >> 1) & 1023;
		switch (xo) {

		case PPC_31_CMPL:
		case PPC_31_CMP:
			bf = (iword >> 23) & 7;
			l_bit = (iword >> 21) & 1;
			ra = (iword >> 16) & 31;
			rb = (iword >> 11) & 31;
			if (xo == PPC_31_CMPL) {
				if (l_bit)
					ic->f = instr(cmpld);
				else
					ic->f = instr(cmplw);
			} else {
				if (l_bit)
					ic->f = instr(cmpd);
				else
					ic->f = instr(cmpw);
			}
			ic->arg[0] = (size_t)(&cpu->cd.ppc.gpr[ra]);
			ic->arg[1] = (size_t)(&cpu->cd.ppc.gpr[rb]);
			ic->arg[2] = bf;
			break;

		case PPC_31_CNTLZW:
			rs = (iword >> 21) & 31;
			ra = (iword >> 16) & 31;
			rc = iword & 1;
			if (rc) {
				fatal("TODO: rc\n");
				goto bad;
			}
			ic->arg[0] = (size_t)(&cpu->cd.ppc.gpr[rs]);
			ic->arg[1] = (size_t)(&cpu->cd.ppc.gpr[ra]);
			ic->f = instr(cntlzw);
			break;

		case PPC_31_MFSPR:
			rt = (iword >> 21) & 31;
			spr = ((iword >> 6) & 0x3e0) + ((iword >> 16) & 31);
			debug_spr_usage(cpu->pc, spr);
			ic->arg[0] = (size_t)(&cpu->cd.ppc.gpr[rt]);
			ic->arg[1] = (size_t)(&cpu->cd.ppc.spr[spr]);
			switch (spr) {
			case SPR_PMC1:	ic->f = instr(mfspr_pmc1); break;
			default:	ic->f = instr(mfspr);
			}
			break;

		case PPC_31_MTSPR:
			rs = (iword >> 21) & 31;
			spr = ((iword >> 6) & 0x3e0) + ((iword >> 16) & 31);
			debug_spr_usage(cpu->pc, spr);
			ic->arg[0] = (size_t)(&cpu->cd.ppc.gpr[rs]);
			ic->arg[1] = (size_t)(&cpu->cd.ppc.spr[spr]);
			ic->f = instr(mtspr);
			break;

		case PPC_31_MFCR:
			rt = (iword >> 21) & 31;
			ic->arg[0] = (size_t)(&cpu->cd.ppc.gpr[rt]);
			ic->f = instr(mfcr);
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
			ic->arg[1] = (addr & 0xfff) + 4;
			ic->f = instr(mtmsr);
			break;

		case PPC_31_MTCRF:
			rs = (iword >> 21) & 31;
			{
				int i, fxm = (iword >> 12) & 255;
				uint32_t tmp = 0;
				for (i=0; i<8; i++, fxm <<= 1) {
					tmp <<= 4;
					if (fxm & 128)
						tmp |= 0xf;
				}
				ic->arg[1] = (uint32_t)tmp;
			}
			ic->arg[0] = (size_t)(&cpu->cd.ppc.gpr[rs]);
			ic->f = instr(mtcrf);
			break;

		case PPC_31_MFSRIN:
		case PPC_31_MTSRIN:
			rt = (iword >> 21) & 31;
			rb = (iword >> 11) & 31;
			ic->arg[0] = (size_t)(&cpu->cd.ppc.gpr[rb]);
			ic->arg[1] = (size_t)(&cpu->cd.ppc.gpr[rt]);
			switch (xo) {
			case PPC_31_MFSRIN: ic->f = instr(mfsrin); break;
			case PPC_31_MTSRIN: ic->f = instr(mtsrin); break;
			}
			if (cpu->cd.ppc.bits == 64) {
				fatal("Not yet for 64-bit mode\n");
				goto bad;
			}
			break;

		case PPC_31_MFSR:
		case PPC_31_MTSR:
			rt = (iword >> 21) & 31;
			ic->arg[0] = (iword >> 16) & 15;
			ic->arg[1] = (size_t)(&cpu->cd.ppc.gpr[rt]);
			switch (xo) {
			case PPC_31_MFSR:   ic->f = instr(mfsr); break;
			case PPC_31_MTSR:   ic->f = instr(mtsr); break;
			}
			if (cpu->cd.ppc.bits == 64) {
				fatal("Not yet for 64-bit mode\n");
				goto bad;
			}
			break;

		case PPC_31_SRAWI:
			rs = (iword >> 21) & 31;
			ra = (iword >> 16) & 31;
			sh = (iword >> 11) & 31;
			rc = iword & 1;
			ic->arg[0] = (size_t)(&cpu->cd.ppc.gpr[rs]);
			ic->arg[1] = (size_t)(&cpu->cd.ppc.gpr[ra]);
			ic->arg[2] = sh;
			if (rc)
				ic->f = instr(srawi_dot);
			else
				ic->f = instr(srawi);
			break;

		case PPC_31_SYNC:
		case PPC_31_EIEIO:
		case PPC_31_DCBST:
		case PPC_31_DCBTST:
		case PPC_31_DCBF:
		case PPC_31_DCBT:
		case PPC_31_ICBI:
			ic->f = instr(nop);
			break;

		case PPC_31_DCBZ:
			ra = (iword >> 16) & 31;
			rb = (iword >> 11) & 31;
			if (ra == 0)
				ic->arg[0] = (size_t)(&cpu->cd.ppc.zero);
			else
				ic->arg[0] = (size_t)(&cpu->cd.ppc.gpr[ra]);
			ic->arg[1] = (size_t)(&cpu->cd.ppc.gpr[rb]);
			ic->arg[2] = addr & 0xfff;
			ic->f = instr(dcbz);
			break;

		case PPC_31_TLBIA:
		case PPC_31_TLBIE:
		case PPC_31_TLBSYNC:
			/*  TODO: These are bogus.  */
			ic->f = instr(tlbie);
			break;

		case PPC_31_TLBLD:	/*  takes an arg  */
			rb = (iword >> 11) & 31;
			ic->arg[0] = (size_t)(&cpu->cd.ppc.gpr[rb]);
			ic->f = instr(tlbld);
			break;

		case PPC_31_TLBLI:	/*  takes an arg  */
			rb = (iword >> 11) & 31;
			ic->arg[0] = (size_t)(&cpu->cd.ppc.gpr[rb]);
			ic->f = instr(tlbli);
			break;

		case PPC_31_MFTB:
			rt = (iword >> 21) & 31;
			spr = ((iword >> 6) & 0x3e0) + ((iword >> 16) & 31);
			ic->arg[0] = (size_t)(&cpu->cd.ppc.gpr[rt]);
			switch (spr) {
			case 268: ic->f = instr(mftb); break;
			case 269: ic->f = instr(mftbu); break;
			default:fatal("mftb spr=%i?\n", spr);
				goto bad;
			}
			break;

		case PPC_31_NEG:
			rt = (iword >> 21) & 31;
			ra = (iword >> 16) & 31;
			rc = iword & 1;
			ic->arg[0] = (size_t)(&cpu->cd.ppc.gpr[ra]);
			ic->arg[1] = (size_t)(&cpu->cd.ppc.gpr[rt]);
			if (rc)
				ic->f = instr(neg_dot);
			else
				ic->f = instr(neg);
			break;

		case PPC_31_LWARX:
		case PPC_31_LDARX:
		case PPC_31_STWCX_DOT:
		case PPC_31_STDCX_DOT:
			ic->arg[0] = iword;
			ic->f = instr(llsc);
			break;

		case PPC_31_LSWI:
		case PPC_31_STSWI:
			rs = (iword >> 21) & 31;
			ra = (iword >> 16) & 31;
			nb = (iword >> 11) & 31;
			ic->arg[0] = rs;
			if (ra == 0)
				ic->arg[1] = (size_t)(&cpu->cd.ppc.zero);
			else
				ic->arg[1] = (size_t)(&cpu->cd.ppc.gpr[ra]);
			ic->arg[2] = nb == 0? 32 : nb;
			switch (xo) {
			case PPC_31_LSWI:  ic->f = instr(lswi); break;
			case PPC_31_STSWI: ic->f = instr(stswi); break;
			}
			break;

		case 0x1c3:
			fatal("[ mtdcr: TODO ]\n");
			ic->f = instr(nop);
			break;

		case PPC_31_LBZX:
		case PPC_31_LBZUX:
		case PPC_31_LHAX:
		case PPC_31_LHAUX:
		case PPC_31_LHZX:
		case PPC_31_LHZUX:
		case PPC_31_LWZX:
		case PPC_31_LWZUX:
		case PPC_31_LHBRX:
		case PPC_31_LWBRX:
		case PPC_31_STBX:
		case PPC_31_STBUX:
		case PPC_31_STHX:
		case PPC_31_STHUX:
		case PPC_31_STWX:
		case PPC_31_STWUX:
		case PPC_31_STDX:
		case PPC_31_STDUX:
		case PPC_31_STHBRX:
		case PPC_31_STWBRX:
			rs = (iword >> 21) & 31;
			ra = (iword >> 16) & 31;
			rb = (iword >> 11) & 31;
			ic->arg[0] = (size_t)(&cpu->cd.ppc.gpr[rs]);
			if (ra == 0)
				ic->arg[1] = (size_t)(&cpu->cd.ppc.zero);
			else
				ic->arg[1] = (size_t)(&cpu->cd.ppc.gpr[ra]);
			ic->arg[2] = (size_t)(&cpu->cd.ppc.gpr[rb]);
			load = 0; zero = 1; size = 0; update = 0; byterev = 0;
			switch (xo) {
			case PPC_31_LBZX:  load = 1; break;
			case PPC_31_LBZUX: load=update=1; break;
			case PPC_31_LHAX:  size=1; load=1; zero=0; break;
			case PPC_31_LHAUX: size=1; load=update=1; zero=0; break;
			case PPC_31_LHZX:  size=1; load=1; break;
			case PPC_31_LHZUX: size=1; load=update = 1; break;
			case PPC_31_LWZX:  size=2; load=1; break;
			case PPC_31_LWZUX: size=2; load=update = 1; break;
			case PPC_31_LHBRX: size=1; load=1; byterev=1;
					   ic->f = instr(lhbrx); break;
			case PPC_31_LWBRX: size =2; load = 1; byterev=1;
					   ic->f = instr(lwbrx); break;
			case PPC_31_STBX:  break;
			case PPC_31_STBUX: update = 1; break;
			case PPC_31_STHX:  size = 1; break;
			case PPC_31_STHUX: size = 1; update = 1; break;
			case PPC_31_STWX:  size = 2; break;
			case PPC_31_STWUX: size = 2; update = 1; break;
			case PPC_31_STDX:  size = 3; break;
			case PPC_31_STDUX: size = 3; update = 1; break;
			case PPC_31_STHBRX:size = 1; byterev = 1;
					   ic->f = instr(sthbrx); break;
			case PPC_31_STWBRX:size = 2; byterev = 1;
					   ic->f = instr(stwbrx); break;
			}
			if (!byterev) {
				ic->f =
#ifdef MODE32
				    ppc32_loadstore_indexed
#else
				    ppc_loadstore_indexed
#endif
				    [size + 4*zero + 8*load + 16*update];
			}
			if (ra == 0 && update) {
				fatal("TODO: ra=0 && update?\n");
				goto bad;
			}
			break;

		case PPC_31_EXTSB:
		case PPC_31_EXTSH:
		case PPC_31_EXTSW:
		case PPC_31_SLW:
		case PPC_31_SRAW:
		case PPC_31_SRW:
		case PPC_31_AND:
		case PPC_31_NAND:
		case PPC_31_ANDC:
		case PPC_31_NOR:
		case PPC_31_OR:
		case PPC_31_ORC:
		case PPC_31_XOR:
			rs = (iword >> 21) & 31;
			ra = (iword >> 16) & 31;
			rb = (iword >> 11) & 31;
			rc = iword & 1;
			rc_f = NULL;
			switch (xo) {
			case PPC_31_EXTSB:ic->f = instr(extsb);
					  rc_f  = instr(extsb_dot); break;
			case PPC_31_EXTSH:ic->f = instr(extsh);
					  rc_f  = instr(extsh_dot); break;
			case PPC_31_EXTSW:ic->f = instr(extsw);
					  rc_f  = instr(extsw_dot); break;
			case PPC_31_SLW:  ic->f = instr(slw);
					  rc_f  = instr(slw_dot); break;
			case PPC_31_SRAW: ic->f = instr(sraw);
					  rc_f  = instr(sraw_dot); break;
			case PPC_31_SRW:  ic->f = instr(srw);
					  rc_f  = instr(srw_dot); break;
			case PPC_31_AND:  ic->f = instr(and);
					  rc_f  = instr(and_dot); break;
			case PPC_31_NAND: ic->f = instr(nand);
					  rc_f  = instr(nand_dot); break;
			case PPC_31_ANDC: ic->f = instr(andc);
					  rc_f  = instr(andc_dot); break;
			case PPC_31_NOR:  ic->f = instr(nor);
					  rc_f  = instr(nor_dot); break;
			case PPC_31_OR:   ic->f = instr(or);
					  rc_f  = instr(or_dot); break;
			case PPC_31_ORC:  ic->f = instr(orc);
					  rc_f  = instr(orc_dot); break;
			case PPC_31_XOR:  ic->f = instr(xor);
					  rc_f  = instr(xor_dot); break;
			}
			ic->arg[0] = (size_t)(&cpu->cd.ppc.gpr[rs]);
			ic->arg[1] = (size_t)(&cpu->cd.ppc.gpr[rb]);
			ic->arg[2] = (size_t)(&cpu->cd.ppc.gpr[ra]);
			if (rc)
				ic->f = rc_f;
			break;

		case PPC_31_MULLW:
		case PPC_31_MULHW:
		case PPC_31_MULHWU:
		case PPC_31_DIVW:
		case PPC_31_DIVWU:
		case PPC_31_ADD:
		case PPC_31_ADDC:
		case PPC_31_ADDE:
		case PPC_31_ADDME:
		case PPC_31_ADDZE:
		case PPC_31_SUBF:
		case PPC_31_SUBFC:
		case PPC_31_SUBFE:
		case PPC_31_SUBFME:
		case PPC_31_SUBFZE:
			rt = (iword >> 21) & 31;
			ra = (iword >> 16) & 31;
			rb = (iword >> 11) & 31;
			oe_bit = (iword >> 10) & 1;
			rc = iword & 1;
			if (oe_bit) {
				fatal("oe_bit not yet implemented\n");
				goto bad;
			}
			switch (xo) {
			case PPC_31_MULLW:  ic->f = instr(mullw); break;
			case PPC_31_MULHW:  ic->f = instr(mulhw); break;
			case PPC_31_MULHWU: ic->f = instr(mulhwu); break;
			case PPC_31_DIVW:   ic->f = instr(divw); n64=1; break;
			case PPC_31_DIVWU:  ic->f = instr(divwu); n64=1; break;
			case PPC_31_ADD:    ic->f = instr(add); break;
			case PPC_31_ADDC:   ic->f = instr(addc); n64=1; break;
			case PPC_31_ADDE:   ic->f = instr(adde); n64=1; break;
			case PPC_31_ADDME:  ic->f = instr(addme); n64=1; break;
			case PPC_31_ADDZE:  ic->f = instr(addze); n64=1; break;
			case PPC_31_SUBF:   ic->f = instr(subf); break;
			case PPC_31_SUBFC:  ic->f = instr(subfc); break;
			case PPC_31_SUBFE:  ic->f = instr(subfe); n64=1; break;
			case PPC_31_SUBFME: ic->f = instr(subfme); n64=1; break;
			case PPC_31_SUBFZE: ic->f = instr(subfze); n64=1;break;
			}
			if (rc) {
				switch (xo) {
				case PPC_31_ADD:
					ic->f = instr(add_dot); break;
				case PPC_31_ADDE:
					ic->f = instr(adde_dot); break;
				case PPC_31_ADDME:
					ic->f = instr(addme_dot); break;
				case PPC_31_ADDZE:
					ic->f = instr(addze_dot); break;
				case PPC_31_SUBF:
					ic->f = instr(subf_dot); break;
				case PPC_31_SUBFC:
					ic->f = instr(subfc_dot); break;
				case PPC_31_SUBFE:
					ic->f = instr(subfe_dot); break;
				case PPC_31_SUBFME:
					ic->f = instr(subfme_dot); break;
				case PPC_31_SUBFZE:
					ic->f = instr(subfze_dot); break;
				default:fatal("RC bit not yet implemented\n");
					goto bad;
				}
			}
			ic->arg[0] = (size_t)(&cpu->cd.ppc.gpr[ra]);
			ic->arg[1] = (size_t)(&cpu->cd.ppc.gpr[rb]);
			ic->arg[2] = (size_t)(&cpu->cd.ppc.gpr[rt]);
			if (cpu->cd.ppc.bits == 64 && n64) {
				fatal("Not yet for 64-bit mode\n");
				goto bad;
			}
			break;

		default:goto bad;
		}
		break;

	case PPC_HI6_63:
		xo = (iword >> 1) & 1023;
		rt = (iword >> 21) & 31;
		ra = (iword >> 16) & 31;
		rb = (iword >> 11) & 31;
		rc = iword & 1;

		switch (xo) {

		case PPC_63_FMR:
			if (rc) {
				fatal("FMR with rc-bit: TODO\n");
				goto bad;
			}
			ic->f = instr(fmr);
			ic->arg[0] = (size_t)(&cpu->cd.ppc.fpr[rb]);
			ic->arg[1] = (size_t)(&cpu->cd.ppc.fpr[rt]);
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

