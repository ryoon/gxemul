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
 *  $Id: cpu_arm_instr.c,v 1.61 2005-08-19 10:50:48 debug Exp $
 *
 *  ARM instructions.
 *
 *  Individual functions should keep track of cpu->n_translated_instrs.
 *  (If no instruction was executed, then it should be decreased. If, say, 4
 *  instructions were combined into one function and executed, then it should
 *  be increased by 3.)
 */


/*
 *  Helper definitions:
 *
 *  Each instruction is defined like this:
 *
 *	X(foo)
 *	{
 *		code for foo;
 *	}
 *	Y(foo)
 *
 *  The Y macro defines 14 copies of the instruction, one for each possible
 *  condition code. (The NV condition code is not included, and the AL code
 *  uses the main foo function.)  Y also defines an array with pointers to
 *  all of these functions.
 */

#define Y(n) void arm_instr_ ## n ## __eq(struct cpu *cpu,		\
			struct arm_instr_call *ic)			\
	{  if (cpu->cd.arm.flags & ARM_FLAG_Z)				\
		arm_instr_ ## n (cpu, ic);		}		\
	void arm_instr_ ## n ## __ne(struct cpu *cpu,			\
			struct arm_instr_call *ic)			\
	{  if (!(cpu->cd.arm.flags & ARM_FLAG_Z))			\
		arm_instr_ ## n (cpu, ic);		}		\
	void arm_instr_ ## n ## __cs(struct cpu *cpu,			\
			struct arm_instr_call *ic)			\
	{  if (cpu->cd.arm.flags & ARM_FLAG_C)				\
		arm_instr_ ## n (cpu, ic);		}		\
	void arm_instr_ ## n ## __cc(struct cpu *cpu,			\
			struct arm_instr_call *ic)			\
	{  if (!(cpu->cd.arm.flags & ARM_FLAG_C))			\
		arm_instr_ ## n (cpu, ic);		}		\
	void arm_instr_ ## n ## __mi(struct cpu *cpu,			\
			struct arm_instr_call *ic)			\
	{  if (cpu->cd.arm.flags & ARM_FLAG_N)				\
		arm_instr_ ## n (cpu, ic);		}		\
	void arm_instr_ ## n ## __pl(struct cpu *cpu,			\
			struct arm_instr_call *ic)			\
	{  if (!(cpu->cd.arm.flags & ARM_FLAG_N))			\
		arm_instr_ ## n (cpu, ic);		}		\
	void arm_instr_ ## n ## __vs(struct cpu *cpu,			\
			struct arm_instr_call *ic)			\
	{  if (cpu->cd.arm.flags & ARM_FLAG_V)				\
		arm_instr_ ## n (cpu, ic);		}		\
	void arm_instr_ ## n ## __vc(struct cpu *cpu,			\
			struct arm_instr_call *ic)			\
	{  if (!(cpu->cd.arm.flags & ARM_FLAG_V))			\
		arm_instr_ ## n (cpu, ic);		}		\
	void arm_instr_ ## n ## __hi(struct cpu *cpu,			\
			struct arm_instr_call *ic)			\
	{  if (cpu->cd.arm.flags & ARM_FLAG_C &&			\
		!(cpu->cd.arm.flags & ARM_FLAG_Z))			\
		arm_instr_ ## n (cpu, ic);		}		\
	void arm_instr_ ## n ## __ls(struct cpu *cpu,			\
			struct arm_instr_call *ic)			\
	{  if (cpu->cd.arm.flags & ARM_FLAG_Z &&			\
		!(cpu->cd.arm.flags & ARM_FLAG_C))			\
		arm_instr_ ## n (cpu, ic);		}		\
	void arm_instr_ ## n ## __ge(struct cpu *cpu,			\
			struct arm_instr_call *ic)			\
	{  if (((cpu->cd.arm.flags & ARM_FLAG_N)?1:0) ==		\
		((cpu->cd.arm.flags & ARM_FLAG_V)?1:0))			\
		arm_instr_ ## n (cpu, ic);		}		\
	void arm_instr_ ## n ## __lt(struct cpu *cpu,			\
			struct arm_instr_call *ic)			\
	{  if (((cpu->cd.arm.flags & ARM_FLAG_N)?1:0) !=		\
		((cpu->cd.arm.flags & ARM_FLAG_V)?1:0))			\
		arm_instr_ ## n (cpu, ic);		}		\
	void arm_instr_ ## n ## __gt(struct cpu *cpu,			\
			struct arm_instr_call *ic)			\
	{  if (((cpu->cd.arm.flags & ARM_FLAG_N)?1:0) ==		\
		((cpu->cd.arm.flags & ARM_FLAG_V)?1:0) &&		\
		!(cpu->cd.arm.flags & ARM_FLAG_Z))			\
		arm_instr_ ## n (cpu, ic);		}		\
	void arm_instr_ ## n ## __le(struct cpu *cpu,			\
			struct arm_instr_call *ic)			\
	{  if (((cpu->cd.arm.flags & ARM_FLAG_N)?1:0) !=		\
		((cpu->cd.arm.flags & ARM_FLAG_V)?1:0) ||		\
		(cpu->cd.arm.flags & ARM_FLAG_Z))			\
		arm_instr_ ## n (cpu, ic);		}		\
	void (*arm_cond_instr_ ## n  [16])(struct cpu *,		\
			struct arm_instr_call *) = {			\
		arm_instr_ ## n ## __eq, arm_instr_ ## n ## __ne,	\
		arm_instr_ ## n ## __cs, arm_instr_ ## n ## __cc,	\
		arm_instr_ ## n ## __mi, arm_instr_ ## n ## __pl,	\
		arm_instr_ ## n ## __vs, arm_instr_ ## n ## __vc,	\
		arm_instr_ ## n ## __hi, arm_instr_ ## n ## __ls,	\
		arm_instr_ ## n ## __ge, arm_instr_ ## n ## __lt,	\
		arm_instr_ ## n ## __gt, arm_instr_ ## n ## __le,	\
		arm_instr_ ## n , arm_instr_nop };

#define cond_instr(n)	( arm_cond_instr_ ## n  [condition_code] )


/*****************************************************************************/

/*
 *  update_c is set if the C flag should be updated with the last shifted/
 *  rotated bit.
 */
uint32_t R(struct cpu *cpu, struct arm_instr_call *ic,
	uint32_t iword, int update_c)
{
	int t = (iword >> 4) & 7, c = (iword >> 7) & 31;
	int rm = iword & 15, lastbit = 0;
	uint32_t tmp = cpu->cd.arm.r[rm];
	if (rm == ARM_PC) {
		/*  Calculate tmp from this instruction's PC + 8  */
		uint32_t low_pc = ((size_t)ic - (size_t)
		    cpu->cd.arm.cur_ic_page) / sizeof(struct arm_instr_call);
		tmp &= ~((ARM_IC_ENTRIES_PER_PAGE-1) <<
		    ARM_INSTR_ALIGNMENT_SHIFT);
		tmp += (low_pc << ARM_INSTR_ALIGNMENT_SHIFT);
		tmp += 8;
	}
	if ((t & 1) && (c >> 1) == ARM_PC) {
		fatal("TODO: R: rc = PC\n");
		exit(1);
	}
	switch (t) {
	case 0:	/*  lsl #c  (c = 0..31)  */
		if (update_c) {
			if (c == 0)
				update_c = 0;
			else
				lastbit = (tmp << (c-1)) & 0x80000000;
		}
		tmp <<= c;
		break;
	case 1:	/*  lsl Rc  */
		c = cpu->cd.arm.r[c >> 1] & 255;
		if (update_c) {
			if (c == 0)
				update_c = 0;
			else
				lastbit = ((uint64_t)tmp << (c-1)) & 0x80000000;
		}
		tmp = (uint64_t)tmp << c;
		break;
	case 2:	/*  lsr #c  (c = 1..32)  */
		if (c == 0)
			c = 32;
		if (update_c) {
			lastbit = ((uint64_t)tmp >> (c-1)) & 1;
		}
		tmp = (uint64_t)tmp >> c;
		break;
	case 3:	/*  lsr Rc  */
		c = cpu->cd.arm.r[c >> 1] & 255;
		if (update_c) {
			if (c == 0)
				update_c = 0;
			else
				lastbit = ((uint64_t)tmp >> (c-1)) & 1;
		}
		tmp = (uint64_t)tmp >> c;
		break;
	default:fatal("R: unimplemented t\n");
		exit(1);
	}
	if (update_c) {
		cpu->cd.arm.flags &= ~ARM_FLAG_C;
		if (lastbit)
			cpu->cd.arm.flags |= ARM_FLAG_C;
	}
	return tmp;
}


/*****************************************************************************/


/*
 *  nop:  Do nothing.
 */
X(nop)
{
}


/*
 *  invalid:
 */
X(invalid)
{
	fatal("invalid ARM instruction?\n");
	exit(1);
}


/*
 *  b:  Branch (to a different translated page)
 *
 *  arg[0] = relative offset
 */
X(b)
{
	uint32_t low_pc;

	/*  Calculate new PC from this instruction + arg[0]  */
	low_pc = ((size_t)ic - (size_t)
	    cpu->cd.arm.cur_ic_page) / sizeof(struct arm_instr_call);
	cpu->cd.arm.r[ARM_PC] &= ~((ARM_IC_ENTRIES_PER_PAGE-1)
	    << ARM_INSTR_ALIGNMENT_SHIFT);
	cpu->cd.arm.r[ARM_PC] += (low_pc << ARM_INSTR_ALIGNMENT_SHIFT);
	cpu->cd.arm.r[ARM_PC] += (int32_t)ic->arg[0];
	cpu->pc = cpu->cd.arm.r[ARM_PC];

	/*  Find the new physical page and update the translation pointers:  */
	arm_pc_to_pointers(cpu);
}
Y(b)


/*
 *  b_samepage:  Branch (to within the same translated page)
 *
 *  arg[0] = pointer to new arm_instr_call
 */
X(b_samepage)
{
	cpu->cd.arm.next_ic = (struct arm_instr_call *) ic->arg[0];
}
Y(b_samepage)


/*
 *  bl:  Branch and Link (to a different translated page)
 *
 *  arg[0] = relative address
 */
X(bl)
{
	uint32_t lr, low_pc;

	/*  Figure out what the return (link) address will be:  */
	low_pc = ((size_t)cpu->cd.arm.next_ic - (size_t)
	    cpu->cd.arm.cur_ic_page) / sizeof(struct arm_instr_call);
	lr = cpu->cd.arm.r[ARM_PC];
	lr &= ~((ARM_IC_ENTRIES_PER_PAGE-1) << ARM_INSTR_ALIGNMENT_SHIFT);
	lr += (low_pc << ARM_INSTR_ALIGNMENT_SHIFT);

	/*  Link:  */
	cpu->cd.arm.r[ARM_LR] = lr;

	/*  Calculate new PC from this instruction + arg[0]  */
	cpu->pc = cpu->cd.arm.r[ARM_PC] = lr - 4 + (int32_t)ic->arg[0];

	/*  Find the new physical page and update the translation pointers:  */
	arm_pc_to_pointers(cpu);
}
Y(bl)


/*
 *  bl_trace:  Branch and Link (to a different translated page), with trace
 *
 *  Same as for bl.
 */
X(bl_trace)
{
	uint32_t lr, low_pc;

	/*  Figure out what the return (link) address will be:  */
	low_pc = ((size_t)cpu->cd.arm.next_ic - (size_t)
	    cpu->cd.arm.cur_ic_page) / sizeof(struct arm_instr_call);
	lr = cpu->cd.arm.r[ARM_PC];
	lr &= ~((ARM_IC_ENTRIES_PER_PAGE-1) << ARM_INSTR_ALIGNMENT_SHIFT);
	lr += (low_pc << ARM_INSTR_ALIGNMENT_SHIFT);

	/*  Link:  */
	cpu->cd.arm.r[ARM_LR] = lr;

	/*  Calculate new PC from this instruction + arg[0]  */
	cpu->pc = cpu->cd.arm.r[ARM_PC] = lr - 4 + (int32_t)ic->arg[0];

	cpu_functioncall_trace(cpu, cpu->pc);

	/*  Find the new physical page and update the translation pointers:  */
	arm_pc_to_pointers(cpu);
}
Y(bl_trace)


/*
 *  bl_samepage:  A branch + link within the same page
 *
 *  arg[0] = pointer to new arm_instr_call
 */
X(bl_samepage)
{
	uint32_t lr, low_pc;

	/*  Figure out what the return (link) address will be:  */
	low_pc = ((size_t)cpu->cd.arm.next_ic - (size_t)
	    cpu->cd.arm.cur_ic_page) / sizeof(struct arm_instr_call);
	lr = cpu->cd.arm.r[ARM_PC];
	lr &= ~((ARM_IC_ENTRIES_PER_PAGE-1) << ARM_INSTR_ALIGNMENT_SHIFT);
	lr += (low_pc << ARM_INSTR_ALIGNMENT_SHIFT);

	/*  Link:  */
	cpu->cd.arm.r[ARM_LR] = lr;

	/*  Branch:  */
	cpu->cd.arm.next_ic = (struct arm_instr_call *) ic->arg[0];
}
Y(bl_samepage)


/*
 *  bl_samepage_trace:  Branch and Link (to the same page), with trace
 *
 *  Same as for bl_samepage.
 */
X(bl_samepage_trace)
{
	uint32_t tmp_pc, lr, low_pc;

	/*  Figure out what the return (link) address will be:  */
	low_pc = ((size_t)cpu->cd.arm.next_ic - (size_t)
	    cpu->cd.arm.cur_ic_page) / sizeof(struct arm_instr_call);
	lr = cpu->cd.arm.r[ARM_PC];
	lr &= ~((ARM_IC_ENTRIES_PER_PAGE-1) << ARM_INSTR_ALIGNMENT_SHIFT);
	lr += (low_pc << ARM_INSTR_ALIGNMENT_SHIFT);

	/*  Link:  */
	cpu->cd.arm.r[ARM_LR] = lr;

	/*  Branch:  */
	cpu->cd.arm.next_ic = (struct arm_instr_call *) ic->arg[0];

	low_pc = ((size_t)cpu->cd.arm.next_ic - (size_t)
	    cpu->cd.arm.cur_ic_page) / sizeof(struct arm_instr_call);
	tmp_pc = cpu->cd.arm.r[ARM_PC];
	tmp_pc &= ~((ARM_IC_ENTRIES_PER_PAGE-1)
	    << ARM_INSTR_ALIGNMENT_SHIFT);
	tmp_pc += (low_pc << ARM_INSTR_ALIGNMENT_SHIFT);
	cpu_functioncall_trace(cpu, tmp_pc);
}
Y(bl_samepage_trace)


/*
 *  mull: Long multiplication
 *
 *  arg[0] = copy of instruction word
 */
X(mull)
{
	/*  xxxx0000 1UAShhhh llllssss 1001mmmm  */
	uint32_t iw = ic->arg[0];
	int u_bit = (iw >> 22) & 1, a_bit = (iw >> 21) & 1;
	uint64_t tmp = cpu->cd.arm.r[iw & 15];
	if (u_bit) {
		fatal("mull: u bit\n");
		exit(1);
	}
	tmp *= cpu->cd.arm.r[(iw >> 8) & 15];
	if (a_bit) {
		cpu->cd.arm.r[(iw >> 16) & 15] += (tmp >> 32);
		cpu->cd.arm.r[(iw >> 12) & 15] += tmp;
	} else {
		cpu->cd.arm.r[(iw >> 16) & 15] = (tmp >> 32);
		cpu->cd.arm.r[(iw >> 12) & 15] = tmp;
	}
}
Y(mull)


/*
 *  get_cpu_id:
 *
 *  arg[0] = pointer to destination register
 */
X(get_cpu_id)
{
	/*  TODO  */
	reg(ic->arg[0]) = CPU_ID_SA110;
}
Y(get_cpu_id)


/*
 *  mov_pc:  "mov pc,reg"
 *
 *  arg[0] = ignored
 *  arg[1] = pointer to uint32_t in host memory of source register
 */
X(mov_pc)
{
	uint32_t old_pc = cpu->cd.arm.r[ARM_PC];
	uint32_t mask_within_page = ((ARM_IC_ENTRIES_PER_PAGE-1)
	    << ARM_INSTR_ALIGNMENT_SHIFT) |
	    ((1 << ARM_INSTR_ALIGNMENT_SHIFT) - 1);

	/*  Update the PC register:  */
	cpu->pc = cpu->cd.arm.r[ARM_PC] = reg(ic->arg[1]);

	/*
	 *  Is this a return to code within the same page? Then there is no
	 *  need to update all pointers, just next_ic.
	 */
	if ((old_pc & ~mask_within_page) == (cpu->pc & ~mask_within_page)) {
		cpu->cd.arm.next_ic = cpu->cd.arm.cur_ic_page +
		    ((cpu->pc & mask_within_page) >> ARM_INSTR_ALIGNMENT_SHIFT);
	} else {
		/*  Find the new physical page and update pointers:  */
		arm_pc_to_pointers(cpu);
	}
}
Y(mov_pc)


/*
 *  ret_trace:  "mov pc,lr" with trace enabled
 *
 *  arg[0] = ignored  (similar to mov_pc above)
 */
X(ret_trace)
{
	uint32_t old_pc = cpu->cd.arm.r[ARM_PC];
	uint32_t mask_within_page = ((ARM_IC_ENTRIES_PER_PAGE-1)
	    << ARM_INSTR_ALIGNMENT_SHIFT) |
	    ((1 << ARM_INSTR_ALIGNMENT_SHIFT) - 1);

	/*  Update the PC register:  */
	cpu->pc = cpu->cd.arm.r[ARM_PC] = cpu->cd.arm.r[ARM_LR];

	cpu_functioncall_trace_return(cpu);

	/*
	 *  Is this a return to code within the same page? Then there is no
	 *  need to update all pointers, just next_ic.
	 */
	if ((old_pc & ~mask_within_page) == (cpu->pc & ~mask_within_page)) {
		cpu->cd.arm.next_ic = cpu->cd.arm.cur_ic_page +
		    ((cpu->pc & mask_within_page) >> ARM_INSTR_ALIGNMENT_SHIFT);
	} else {
		/*  Find the new physical page and update pointers:  */
		arm_pc_to_pointers(cpu);
	}
}
Y(ret_trace)


/*
 *  mov_regreg:
 *
 *  arg[0] = pointer to uint32_t in host memory of destination register
 *  arg[1] = pointer to uint32_t in host memory of source register
 */
X(mov_regreg)
{
	reg(ic->arg[0]) = reg(ic->arg[1]);
}
Y(mov_regreg)


/*
 *  mov_regform:  Generic mov, register form.
 *
 *  arg[0] = pointer to uint32_t in host memory of destination register
 *  arg[1] = copy of instruction word
 */
X(mov_regform)
{
	reg(ic->arg[0]) = R(cpu, ic, ic->arg[1], 0);
}
Y(mov_regform)


/*
 *  mov:  Set a 32-bit register to a 32-bit value.
 *
 *  arg[0] = pointer to uint32_t in host memory
 *  arg[1] = 32-bit value
 */
X(mov)
{
	reg(ic->arg[0]) = ic->arg[1];
}
Y(mov)


/*
 *  clear:  Set a 32-bit register to 0. (A "mov" to fixed value zero.)
 *
 *  arg[0] = pointer to uint32_t in host memory
 */
X(clear)
{
	reg(ic->arg[0]) = 0;
}
Y(clear)


#include "tmp_arm_include.c"


#define A__NAME arm_instr_store_w0_byte_u1_p0_imm_fixinc1
#define A__NAME__general arm_instr_store_w0_byte_u1_p0_imm_fixinc1__general
#define A__B
#define A__U
#define	A__NOCONDITIONS
#define A__FIXINC	1
#include "cpu_arm_instr_loadstore.c"
#undef A__NOCONDITIONS
#undef A__B
#undef A__U
#undef A__NAME__general
#undef A__NAME


/*  See X(add) below. This is a special case for add Rd,pc,imm  */
X(add_pc) {
	uint32_t low_pc;
	low_pc = ((size_t)ic - (size_t)
	    cpu->cd.arm.cur_ic_page) / sizeof(struct arm_instr_call);
	cpu->cd.arm.r[ARM_PC] &= ~((ARM_IC_ENTRIES_PER_PAGE-1) <<
	    ARM_INSTR_ALIGNMENT_SHIFT);
	cpu->cd.arm.r[ARM_PC] += (low_pc << ARM_INSTR_ALIGNMENT_SHIFT);
	reg(ic->arg[0]) = cpu->cd.arm.r[ARM_PC] + 8 + ic->arg[2];
}
Y(add_pc)


/*
 *  bdt_load:  Block Data Transfer, Load
 *
 *  arg[0] = pointer to uint32_t in host memory, pointing to the base register
 *  arg[1] = 32-bit instruction word. Most bits are read from this.
 */
X(bdt_load)
{
	unsigned char data[4];
	uint32_t *np = (uint32_t *)ic->arg[0];
	uint32_t addr = *np;
	uint32_t iw = ic->arg[1];  /*  xxxx100P USWLnnnn llllllll llllllll  */
	int p_bit = iw & 0x01000000;
	int u_bit = iw & 0x00800000;
	int s_bit = iw & 0x00400000;
	int w_bit = iw & 0x00200000;
	int i;

	if (s_bit) {
		fatal("bdt: TODO: s-bit\n");
		exit(1);
	}

	for (i=(u_bit? 0 : 15); i>=0 && i<=15; i+=(u_bit? 1 : -1))
		if ((iw >> i) & 1) {
			/*  Load register i:  */
			if (p_bit) {
				if (u_bit)
					addr += sizeof(uint32_t);
				else
					addr -= sizeof(uint32_t);
			}
			if (!cpu->memory_rw(cpu, cpu->mem, addr, data,
			    sizeof(data), MEM_READ, CACHE_DATA)) {
				fatal("bdt: load failed: TODO\n");
				exit(1);
			}
			if (cpu->byte_order == EMUL_LITTLE_ENDIAN) {
				cpu->cd.arm.r[i] = data[0] +
				    (data[1] << 8) + (data[2] << 16)
				    + (data[3] << 24);
			} else {
				cpu->cd.arm.r[i] = data[3] +
				    (data[2] << 8) + (data[1] << 16)
				    + (data[0] << 24);
			}
			/*  NOTE: Special case:  */
			if (i == ARM_PC) {
				cpu->cd.arm.r[ARM_PC] &= ~3;
				cpu->pc = cpu->cd.arm.r[ARM_PC];
				/*  TODO: There is no need to update the
				    pointers if this is a return to the
				    same page!  */
				/*  Find the new physical page and update the
				    translation pointers:  */
				arm_pc_to_pointers(cpu);
			}
			if (!p_bit) {
				if (u_bit)
					addr += sizeof(uint32_t);
				else
					addr -= sizeof(uint32_t);
			}
		}

	if (w_bit)
		*np = addr;
}
Y(bdt_load)


/*
 *  bdt_store:  Block Data Transfer, Store
 *
 *  arg[0] = pointer to uint32_t in host memory, pointing to the base register
 *  arg[1] = 32-bit instruction word. Most bits are read from this.
 */
X(bdt_store)
{
	unsigned char data[4];
	uint32_t *np = (uint32_t *)ic->arg[0];
	uint32_t addr = *np;
	uint32_t iw = ic->arg[1];  /*  xxxx100P USWLnnnn llllllll llllllll  */
	int p_bit = iw & 0x01000000;
	int u_bit = iw & 0x00800000;
	int s_bit = iw & 0x00400000;
	int w_bit = iw & 0x00200000;
	int i;

	if (s_bit) {
		fatal("bdt: TODO: s-bit\n");
		exit(1);
	}

	if (iw & 0x8000) {
		/*  Synchronize the program counter:  */
		uint32_t low_pc = ((size_t)ic - (size_t)
		    cpu->cd.arm.cur_ic_page) / sizeof(struct arm_instr_call);
		cpu->cd.arm.r[ARM_PC] &= ~((ARM_IC_ENTRIES_PER_PAGE-1) << 2);
		cpu->cd.arm.r[ARM_PC] += (low_pc << 2);
		cpu->pc = cpu->cd.arm.r[ARM_PC];
	}

	for (i=(u_bit? 0 : 15); i>=0 && i<=15; i+=(u_bit? 1 : -1))
		if ((iw >> i) & 1) {
			/*  Store register i:  */
			uint32_t value = cpu->cd.arm.r[i];
			if (i == ARM_PC)
				value += 12;	/*  TODO: 8 on some ARMs?  */
			if (p_bit) {
				if (u_bit)
					addr += sizeof(uint32_t);
				else
					addr -= sizeof(uint32_t);
			}
			if (cpu->byte_order == EMUL_LITTLE_ENDIAN) {
				data[0] = value;
				data[1] = value >> 8;
				data[2] = value >> 16;
				data[3] = value >> 24;
			} else {
				data[0] = value >> 24;
				data[1] = value >> 16;
				data[2] = value >> 8;
				data[3] = value;
			}
			if (!cpu->memory_rw(cpu, cpu->mem, addr, data,
			    sizeof(data), MEM_WRITE, CACHE_DATA)) {
				fatal("bdt: store failed: TODO\n");
				exit(1);
			}
			if (!p_bit) {
				if (u_bit)
					addr += sizeof(uint32_t);
				else
					addr -= sizeof(uint32_t);
			}
		}

	if (w_bit)
		*np = addr;
}
Y(bdt_store)


/*
 *  cmps:  Compare a 32-bit register to a 32-bit value. (Subtraction.)
 *
 *  arg[0] = pointer to uint32_t in host memory
 *  arg[1] = 32-bit value
 */
X(cmps)
{
	uint32_t a = reg(ic->arg[0]), b = ic->arg[1], c;
	int v, n;
	cpu->cd.arm.flags &=
	    ~(ARM_FLAG_Z | ARM_FLAG_N | ARM_FLAG_V | ARM_FLAG_C);
	c = a - b;
	if (a > b)
		cpu->cd.arm.flags |= ARM_FLAG_C;
	if (c == 0)
		cpu->cd.arm.flags |= ARM_FLAG_Z;
	if ((int32_t)c < 0) {
		cpu->cd.arm.flags |= ARM_FLAG_N;
		n = 1;
	} else
		n = 0;
	if ((int32_t)a >= (int32_t)b)
		v = n;
	else
		v = !n;
	if (v)
		cpu->cd.arm.flags |= ARM_FLAG_V;
}
Y(cmps)


/*
 *  cmns:  Compare a 32-bit register to a 32-bit value. (Addition.)
 *
 *  arg[0] = pointer to uint32_t in host memory
 *  arg[1] = 32-bit value
 */
X(cmns)
{
	uint32_t a = reg(ic->arg[0]), b = ic->arg[1], c;
	int v, n;
	cpu->cd.arm.flags &=
	    ~(ARM_FLAG_Z | ARM_FLAG_N | ARM_FLAG_V | ARM_FLAG_C);
	c = a + b;
	if (c < a)
		cpu->cd.arm.flags |= ARM_FLAG_C;
	if (c == 0)
		cpu->cd.arm.flags |= ARM_FLAG_Z;
	if ((int32_t)c < 0) {
		cpu->cd.arm.flags |= ARM_FLAG_N;
		n = 1;
	} else
		n = 0;
	if ((int32_t)a >= (int32_t)b)
		v = n;
	else
		v = !n;
	if (v)
		cpu->cd.arm.flags |= ARM_FLAG_V;
}
Y(cmns)


/*
 *  cmps_regform:  cmps, register form
 *
 *  arg[0] = pointer to uint32_t in host memory
 *  arg[1] = copy of instruction word
 */
X(cmps_regform)
{
	uint32_t a = reg(ic->arg[0]), b = R(cpu, ic, ic->arg[1], 0), c;
	int v, n;
	cpu->cd.arm.flags &=
	    ~(ARM_FLAG_Z | ARM_FLAG_N | ARM_FLAG_V | ARM_FLAG_C);
	c = a - b;
	if (a > b)
		cpu->cd.arm.flags |= ARM_FLAG_C;
	if (c == 0)
		cpu->cd.arm.flags |= ARM_FLAG_Z;
	if ((int32_t)c < 0) {
		cpu->cd.arm.flags |= ARM_FLAG_N;
		n = 1;
	} else
		n = 0;
	if ((int32_t)a >= (int32_t)b)
		v = n;
	else
		v = !n;
	if (v)
		cpu->cd.arm.flags |= ARM_FLAG_V;
}
Y(cmps_regform)


/*
 *  cmns_regform:  cmns, register form
 *
 *  arg[0] = pointer to uint32_t in host memory
 *  arg[1] = copy of instruction word
 */
X(cmns_regform)
{
	uint32_t a = reg(ic->arg[0]), b = R(cpu, ic, ic->arg[1], 0), c;
	int v, n;
	cpu->cd.arm.flags &=
	    ~(ARM_FLAG_Z | ARM_FLAG_N | ARM_FLAG_V | ARM_FLAG_C);
	c = a + b;
	if (c < a)
		cpu->cd.arm.flags |= ARM_FLAG_C;
	if (c == 0)
		cpu->cd.arm.flags |= ARM_FLAG_Z;
	if ((int32_t)c < 0) {
		cpu->cd.arm.flags |= ARM_FLAG_N;
		n = 1;
	} else
		n = 0;
	if ((int32_t)a >= (int32_t)b)
		v = n;
	else
		v = !n;
	if (v)
		cpu->cd.arm.flags |= ARM_FLAG_V;
}
Y(cmns_regform)


#include "cpu_arm_instr_cmps.c"


/*
 *  add, sub etc:
 *
 *  arg[0] = pointer to destination uint32_t in host memory
 *  arg[1] = pointer to source uint32_t in host memory
 *  arg[2] = 32-bit value    or   copy of instruction word (for register form)
 */
X(and) {
	reg(ic->arg[0]) = reg(ic->arg[1]) & ic->arg[2];
}
Y(and)
X(and_regform) {
	reg(ic->arg[0]) = reg(ic->arg[1]) & R(cpu, ic, ic->arg[2], 0);
}
Y(and_regform)
X(eor) {
	reg(ic->arg[0]) = reg(ic->arg[1]) ^ ic->arg[2];
}
Y(eor)
X(eor_regform) {
	reg(ic->arg[0]) = reg(ic->arg[1]) ^ R(cpu, ic, ic->arg[2], 0);
}
Y(eor_regform)
X(sub) {
	reg(ic->arg[0]) = reg(ic->arg[1]) - ic->arg[2];
}
Y(sub)
X(sub_regform) {
	reg(ic->arg[0]) = reg(ic->arg[1]) - R(cpu, ic, ic->arg[2], 0);
}
Y(sub_regform)
X(rsb) {
	reg(ic->arg[0]) = ic->arg[2] - reg(ic->arg[1]);
}
Y(rsb)
X(rsb_regform) {
	reg(ic->arg[0]) = R(cpu, ic, ic->arg[2], 0) - reg(ic->arg[1]);
}
Y(rsb_regform)
X(add) {
	reg(ic->arg[0]) = reg(ic->arg[1]) + ic->arg[2];
}
Y(add)
X(add_regform) {
	reg(ic->arg[0]) = reg(ic->arg[1]) + R(cpu, ic, ic->arg[2], 0);
}
Y(add_regform)
X(orr) {
	reg(ic->arg[0]) = reg(ic->arg[1]) | ic->arg[2];
}
Y(orr)
X(orr_regform) {
	reg(ic->arg[0]) = reg(ic->arg[1]) | R(cpu, ic, ic->arg[2], 0);
}
Y(orr_regform)
X(bic) {
	reg(ic->arg[0]) = reg(ic->arg[1]) & ~ic->arg[2];
}
Y(bic)
X(bic_regform) {
	reg(ic->arg[0]) = reg(ic->arg[1]) & ~R(cpu, ic, ic->arg[2], 0);
}
Y(bic_regform)

/*  Same as above, but set flags:  */
X(ands) {
	reg(ic->arg[0]) = reg(ic->arg[1]) & ic->arg[2];
	cpu->cd.arm.flags &= ~(ARM_FLAG_Z | ARM_FLAG_N);
	if (reg(ic->arg[0]) == 0)
		cpu->cd.arm.flags |= ARM_FLAG_Z;
	if (reg(ic->arg[0]) & 0x80000000)
		cpu->cd.arm.flags |= ARM_FLAG_N;
}
Y(ands)
X(ands_regform) {
	reg(ic->arg[0]) = reg(ic->arg[1]) & R(cpu, ic, ic->arg[2], 1);
	cpu->cd.arm.flags &= ~(ARM_FLAG_Z | ARM_FLAG_N);
	if (reg(ic->arg[0]) == 0)
		cpu->cd.arm.flags |= ARM_FLAG_Z;
	if (reg(ic->arg[0]) & 0x80000000)
		cpu->cd.arm.flags |= ARM_FLAG_N;
}
Y(ands_regform)
X(subs) {
	uint32_t a = reg(ic->arg[1]), b = ic->arg[2], c;
	int v, n;
	cpu->cd.arm.flags &=
	    ~(ARM_FLAG_Z | ARM_FLAG_N | ARM_FLAG_V | ARM_FLAG_C);
	c = a - b;
	if (a > b)
		cpu->cd.arm.flags |= ARM_FLAG_C;
	if (c == 0)
		cpu->cd.arm.flags |= ARM_FLAG_Z;
	if ((int32_t)c < 0) {
		cpu->cd.arm.flags |= ARM_FLAG_N;
		n = 1;
	} else
		n = 0;
	if ((int32_t)a >= (int32_t)b)
		v = n;
	else
		v = !n;
	if (v)
		cpu->cd.arm.flags |= ARM_FLAG_V;
	reg(ic->arg[0]) = c;
}
Y(subs)
X(adds) {
	uint32_t a = reg(ic->arg[1]), b = ic->arg[2], c;
	int v, n;
	cpu->cd.arm.flags &=
	    ~(ARM_FLAG_Z | ARM_FLAG_N | ARM_FLAG_V | ARM_FLAG_C);
	c = a + b;
	if (c < a)
		cpu->cd.arm.flags |= ARM_FLAG_C;
	if (c == 0)
		cpu->cd.arm.flags |= ARM_FLAG_Z;
	if ((int32_t)c < 0) {
		cpu->cd.arm.flags |= ARM_FLAG_N;
		n = 1;
	} else
		n = 0;
	if ((int32_t)a >= (int32_t)b)
		v = n;
	else
		v = !n;
	if (v)
		cpu->cd.arm.flags |= ARM_FLAG_V;
	reg(ic->arg[0]) = c;
}
Y(adds)

/*  Special cases:  */
X(sub_self) {
	reg(ic->arg[0]) -= ic->arg[2];
}
Y(sub_self)
X(add_self) {
	reg(ic->arg[0]) += ic->arg[2];
}
Y(add_self)


#include "tmp_arm_include_self.c"


/*****************************************************************************/


/*
 *  mov_2:  Double "mov".
 *
 *  The current and the next arm_instr_call are treated as "mov"s.
 */
X(mov_2)
{
	*((uint32_t *)ic[0].arg[0]) = ic[0].arg[1];
	*((uint32_t *)ic[1].arg[0]) = ic[1].arg[1];
	cpu->cd.arm.next_ic ++;
	cpu->n_translated_instrs ++;
}


/*
 *  fill_loop_test:
 *
 *  A byte-fill loop. Fills at most one page at a time. If the page was not
 *  in the host_store table, then the original sequence (beginning with
 *  cmps r2,#0) is executed instead.
 *
 *  Z:cmps r2,#0		ic[0]
 *    strb rX,[rY],#1		ic[1]
 *    sub  r2,r2,#1		ic[2]
 *    bgt  Z			ic[3]
 */
X(fill_loop_test)
{
	uint32_t addr, a, n, ofs, maxlen;
	unsigned char *page;

	addr = *((uint32_t *)ic[1].arg[0]);
	n = cpu->cd.arm.r[2] + 1;
	ofs = addr & 0xfff;
	maxlen = 4096 - ofs;
	if (n > maxlen)
		n = maxlen;

	page = cpu->cd.arm.host_store[addr >> 12];
	if (page == NULL) {
		arm_cmps_0[2](cpu, ic);
		return;
	}

	/*  printf("x = %x, n = %i\n", *((uint32_t *)ic[1].arg[2]), n);  */
	memset(page + ofs, *((uint32_t *)ic[1].arg[2]), n);

	*((uint32_t *)ic[1].arg[0]) = addr + n;

	cpu->cd.arm.r[2] -= n;
	cpu->n_translated_instrs += (4 * n);

	a = cpu->cd.arm.r[2];

	cpu->cd.arm.flags &=
	    ~(ARM_FLAG_Z | ARM_FLAG_N | ARM_FLAG_V | ARM_FLAG_C);
	if (a != 0)
		cpu->cd.arm.flags |= ARM_FLAG_C;
	else
		cpu->cd.arm.flags |= ARM_FLAG_Z;
	if ((int32_t)a < 0)
		cpu->cd.arm.flags |= ARM_FLAG_N;

	cpu->n_translated_instrs --;

	if ((int32_t)a > 0)
		cpu->cd.arm.next_ic --;
	else
		cpu->cd.arm.next_ic += 3;
}


/*****************************************************************************/


X(end_of_page)
{
	/*  Update the PC:  (offset 0, but on the next page)  */
	cpu->cd.arm.r[ARM_PC] &= ~((ARM_IC_ENTRIES_PER_PAGE-1) << 2);
	cpu->cd.arm.r[ARM_PC] += (ARM_IC_ENTRIES_PER_PAGE << 2);
	cpu->pc = cpu->cd.arm.r[ARM_PC];

	/*  Find the new physical page and update the translation pointers:  */
	arm_pc_to_pointers(cpu);

	/*  end_of_page doesn't count as an executed instruction:  */
	cpu->n_translated_instrs --;
}


/*****************************************************************************/


/*
 *  arm_combine_instructions():
 *
 *  Combine two or more instructions, if possible, into a single function call.
 */
void arm_combine_instructions(struct cpu *cpu, struct arm_instr_call *ic,
	uint32_t addr)
{
	int n_back;
	n_back = (addr >> ARM_INSTR_ALIGNMENT_SHIFT)
	    & (ARM_IC_ENTRIES_PER_PAGE-1);

	if (n_back >= 1) {
		/*  Double "mov":  */
		if (ic[-1].f == instr(mov) || ic[-1].f == instr(clear)) {
			if (ic[-1].f == instr(mov) && ic[0].f == instr(mov)) {
				ic[-1].f = instr(mov_2);
				combined;
			}
			if (ic[-1].f == instr(clear) && ic[0].f == instr(mov)) {
				ic[-1].f = instr(mov_2);
				ic[-1].arg[1] = 0;
				combined;
			}
			if (ic[-1].f == instr(mov) && ic[0].f == instr(clear)) {
				ic[-1].f = instr(mov_2);
				ic[0].arg[1] = 0;
				combined;
			}
			if (ic[-1].f == instr(clear) && ic[0].f==instr(clear)) {
				ic[-1].f = instr(mov_2);
				ic[-1].arg[1] = 0;
				ic[0].arg[1] = 0;
				combined;
			}
		}
	}

	if (n_back >= 3) {
		if (ic[-3].f == arm_cmps_0[2] &&
		    ic[-2].f == instr(store_w0_byte_u1_p0_imm) &&
		    ic[-2].arg[1] == 1 &&
		    ic[-1].f == arm_sub_self_1[2] &&
		    ic[ 0].f == instr(b_samepage__gt) &&
		    ic[ 0].arg[0] == (size_t)&ic[-3]) {
			ic[-3].f = instr(fill_loop_test);
			combined;
		}
	}

	/*  TODO: Combine forward as well  */
}


/*****************************************************************************/


/*
 *  arm_instr_to_be_translated():
 *
 *  Translate an instruction word into an arm_instr_call. ic is filled in with
 *  valid data for the translated instruction, or a "nothing" instruction if
 *  there was a translation failure. The newly translated instruction is then
 *  executed.
 */
X(to_be_translated)
{
	uint32_t addr, low_pc, iword, imm;
	unsigned char *page;
	unsigned char ib[4];
	int condition_code, main_opcode, secondary_opcode, s_bit, rn, rd, r8;
	int p_bit, u_bit, b_bit, w_bit, l_bit, regform, rm, c, t;
	int rn_pc_ok, s_bit_ok;
	void (*samepage_function)(struct cpu *, struct arm_instr_call *);

	/*  Figure out the (virtual) address of the instruction:  */
	low_pc = ((size_t)ic - (size_t)cpu->cd.arm.cur_ic_page)
	    / sizeof(struct arm_instr_call);
	addr = cpu->cd.arm.r[ARM_PC] & ~((ARM_IC_ENTRIES_PER_PAGE-1) <<
	    ARM_INSTR_ALIGNMENT_SHIFT);
	addr += (low_pc << ARM_INSTR_ALIGNMENT_SHIFT);
	cpu->pc = cpu->cd.arm.r[ARM_PC] = addr;
	addr &= ~0x3;

	/*  Read the instruction word from memory:  */
	page = cpu->cd.arm.host_load[addr >> 12];

	if (page != NULL) {
		/*  fatal("TRANSLATION HIT!\n");  */
		memcpy(ib, page + (addr & 0xfff), sizeof(ib));
	} else {
		/*  fatal("TRANSLATION MISS!\n");  */
		if (!cpu->memory_rw(cpu, cpu->mem, addr, &ib[0],
		    sizeof(ib), MEM_READ, CACHE_INSTRUCTION)) {
			fatal("to_be_translated(): "
			    "read failed: TODO\n");
			goto bad;
		}
	}

	if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
		iword = ib[0] + (ib[1]<<8) + (ib[2]<<16) + (ib[3]<<24);
	else
		iword = ib[3] + (ib[2]<<8) + (ib[1]<<16) + (ib[0]<<24);

	/*  fatal("{ ARM translating pc=0x%08x iword=0x%08x }\n",
	    addr, iword);  */


#define DYNTRANS_TO_BE_TRANSLATED_HEAD
#include "cpu_dyntrans.c"
#undef  DYNTRANS_TO_BE_TRANSLATED_HEAD


	/*  The idea of taking bits 27..24 was found here:
	    http://armphetamine.sourceforge.net/oldinfo.html  */
	condition_code = iword >> 28;
	main_opcode = (iword >> 24) & 15;
	secondary_opcode = (iword >> 21) & 15;
	u_bit = (iword >> 23) & 1;
	b_bit = (iword >> 22) & 1;
	w_bit = (iword >> 21) & 1;
	s_bit = l_bit = (iword >> 20) & 1;
	rn    = (iword >> 16) & 15;
	rd    = (iword >> 12) & 15;
	r8    = (iword >> 8) & 15;
	c     = (iword >> 7) & 31;
	t     = (iword >> 4) & 7;
	rm    = iword & 15;

	if (condition_code == 0xf) {
		fatal("TODO: ARM condition code 0x%x\n",
		    condition_code);
		goto bad;
	}


	/*
	 *  Translate the instruction:
	 */

	switch (main_opcode) {

	case 0x0:
	case 0x1:
	case 0x2:
	case 0x3:
		/*  Check special cases first:  */
		if ((iword & 0x0f8000f0) == 0x00800090) {
			/*  Long multiplication:  */
			ic->f = cond_instr(mull);
			ic->arg[0] = iword;
			break;
		}

		if (iword & 0x80 && !(main_opcode & 2) && iword & 0x10) {
			fatal("reg form blah blah\n");
			goto bad;
		}

		/*
		 *  Generic Data Processing Instructions:
		 */
		if ((main_opcode & 2) == 0)
			regform = 1;
		else
			regform = 0;

		if (!regform) {
			imm = iword & 0xff;
			r8 <<= 1;
			while (r8-- > 0)
				imm = (imm >> 1) | ((imm & 1) << 31);
		}

		switch (secondary_opcode) {
		case 0x0:				/*  AND  */
		case 0x1:				/*  EOR  */
		case 0x2:				/*  SUB  */
		case 0x3:				/*  RSB  */
		case 0x4:				/*  ADD  */
		case 0xc:				/*  ORR  */
		case 0xe:				/*  BIC  */
			ic->arg[0] = (size_t)(&cpu->cd.arm.r[rd]);
			ic->arg[1] = (size_t)(&cpu->cd.arm.r[rn]);

/*
 *  TODO: Most of these cannot handle when Rx = ARM_PC!
 */


			if (regform)
				ic->arg[2] = iword;
			else
				ic->arg[2] = imm;
			rn_pc_ok = s_bit_ok = 0;
			switch (secondary_opcode) {
			case 0x0:
				if (regform) {
					if (s_bit) {
						ic->f =
						    cond_instr(ands_regform);
						s_bit_ok = 1;
					} else
						ic->f = cond_instr(and_regform);
				} else {
					if (s_bit) {
						ic->f = cond_instr(ands);
						s_bit_ok = 1;
					} else
						ic->f = cond_instr(and);
				}
				break;
			case 0x1:
				if (regform)
					ic->f = cond_instr(eor_regform);
				else
					ic->f = cond_instr(eor);
				break;
			case 0x2:
				if (regform) {
					ic->f = cond_instr(sub_regform);
				} else {
					if (s_bit) {
						ic->f = cond_instr(subs);
						s_bit_ok = 1;
					} else {
						ic->f = cond_instr(sub);
						if (rd == rn) {
							ic->f =
							   cond_instr(sub_self);
							if (imm == 1 &&
							    rd != ARM_PC)
								ic->f =
								  arm_sub_self_1
								  [rd];
							if (imm == 4 &&
							    rd != ARM_PC)
								ic->f =
								  arm_sub_self_4
								  [rd];
						}
					}
				}
				break;
			case 0x3:
				if (regform)
					ic->f = cond_instr(rsb_regform);
				else
					ic->f = cond_instr(rsb);
				break;
			case 0x4:
				if (regform) {
					ic->f = cond_instr(add_regform);
				} else {
					if (s_bit) {
						ic->f = cond_instr(adds);
						s_bit_ok = 1;
					} else {
						ic->f = cond_instr(add);
						if (rn == ARM_PC) {
							ic->f = cond_instr(add_pc);
								rn_pc_ok = 1;
						}
						if (rd == rn && rd != ARM_PC) {
							ic->f = cond_instr(add_self);
						if (imm == 1 && rd != ARM_PC)
							ic->f =
							     arm_add_self_1[rd];
							if (imm == 4 && rd != ARM_PC)
								ic->f =
								    arm_add_self_4[rd];
						}
					}
				}
				break;
			case 0xc:
				if (regform)
					ic->f = cond_instr(orr_regform);
				else
					ic->f = cond_instr(orr);
				break;
			case 0xe:
				if (regform)
					ic->f = cond_instr(bic_regform);
				else
					ic->f = cond_instr(bic);
				break;
			}
			if (s_bit && !s_bit_ok) {
				fatal("add/sub etc s_bit: TODO\n");
				goto bad;
			}
			if (rd == ARM_PC) {
				fatal("regform: rd = PC\n");
				goto bad;
			}
			if (rn == ARM_PC && !rn_pc_ok) {
				fatal("regform: rn = PC\n");
				goto bad;
			}
			break;
		case 0xa:				/*  CMP  */
			if (!s_bit) {
				fatal("cmp !s_bit: TODO\n");
				goto bad;
			}
			ic->arg[0] = (size_t)(&cpu->cd.arm.r[rn]);
			if (regform) {
				ic->arg[1] = iword;
				ic->f = cond_instr(cmps_regform);
			} else {
				ic->arg[1] = imm;
				ic->f = cond_instr(cmps);
				if (imm == 0 && rn != ARM_PC)
					ic->f = arm_cmps_0[rn];
			}
			break;
		case 0xb:				/*  CMN  */
			if (!s_bit) {
				fatal("cmn !s_bit: TODO\n");
				goto bad;
			}
			ic->arg[0] = (size_t)(&cpu->cd.arm.r[rn]);
			if (regform) {
				ic->arg[1] = iword;
				ic->f = cond_instr(cmns_regform);
			} else {
				ic->arg[1] = imm;
				ic->f = cond_instr(cmns);
			}
			break;
		case 0xd:				/*  MOV  */
			if (s_bit) {
				fatal("mov s_bit: TODO\n");
				goto bad;
			}
			if (regform) {
				ic->f = cond_instr(mov_regform);
				ic->arg[0] = (size_t)(&cpu->cd.arm.r[rd]);
				ic->arg[1] = iword;
				if (t == 0 && c == 0) {
					ic->f = cond_instr(mov_regreg);
					ic->arg[1] = (size_t)
					    (&cpu->cd.arm.r[rm]);
					if (rd == ARM_PC)
						ic->f = cond_instr(mov_pc);
					if (rd == ARM_PC && rm == ARM_LR &&
					    cpu->machine->show_trace_tree)
						ic->f = cond_instr(ret_trace);
				} else if (rd == ARM_PC) {
					fatal("mov pc, but too complex\n");
				}
			} else {
				/*  Immediate:  */
				if (rd == ARM_PC) {
					fatal("TODO: mov used as branch\n");
					goto bad;
				} else {
					ic->f = cond_instr(mov);
					ic->arg[0] = (size_t)
					    (&cpu->cd.arm.r[rd]);
					ic->arg[1] = imm;
					if (imm == 0)
						ic->f = cond_instr(clear);
				}
			}
			break;
		default:goto bad;
		}
		break;

	case 0x4:	/*  Load and store...  */
	case 0x5:	/*  xxxx010P UBWLnnnn ddddoooo oooooooo  Immediate  */
	case 0x6:	/*  xxxx011P UBWLnnnn ddddcccc ctt0mmmm  Register  */
	case 0x7:
		p_bit = main_opcode & 1;
		if (rd == ARM_PC || rn == ARM_PC)
			ic->f = load_store_instr_pc[((iword >> 16) & 0x3f0)
			    + condition_code];
		else
			ic->f = load_store_instr[((iword >> 16) & 0x3f0)
			    + condition_code];
		imm = iword & 0xfff;
		ic->arg[0] = (size_t)(&cpu->cd.arm.r[rn]);
		ic->arg[2] = (size_t)(&cpu->cd.arm.r[rd]);
		if (main_opcode < 6)
			ic->arg[1] = (size_t)(imm);
		else
			ic->arg[1] = iword;
		if (main_opcode == 4) {
			/*  Post-index, immediate:  */
			if (imm == 1 && u_bit && !w_bit && l_bit && b_bit)
				ic->f = instr(store_w0_byte_u1_p0_imm_fixinc1);
			if (w_bit) {
				fatal("load/store: T-bit\n");
				goto bad;
			}
			if (rn == ARM_PC) {
				fatal("load/store writeback PC: error\n");
				goto bad;
			}
		} else if ((iword & 0x0e000010) == 0x06000010) {
			fatal("Not a Load/store TODO\n");
			goto bad;
		}
		break;

	case 0x8:	/*  Multiple load/store...  (Block data transfer)  */
	case 0x9:	/*  xxxx100P USWLnnnn llllllll llllllll  */
		if (l_bit)
			ic->f = cond_instr(bdt_load);
		else
			ic->f = cond_instr(bdt_store);
		ic->arg[0] = (size_t)(&cpu->cd.arm.r[rn]);
		ic->arg[1] = (size_t)iword;
		if (rn == ARM_PC) {
			fatal("TODO: bdt with PC as base\n");
			goto bad;
		}
		break;

	case 0xa:					/*  B: branch  */
	case 0xb:					/*  BL: branch+link  */
		if (main_opcode == 0x0a) {
			ic->f = cond_instr(b);
			samepage_function = cond_instr(b_samepage);
		} else {
			if (cpu->machine->show_trace_tree) {
				ic->f = cond_instr(bl_trace);
				samepage_function =
				    cond_instr(bl_samepage_trace);
			} else {
				ic->f = cond_instr(bl);
				samepage_function = cond_instr(bl_samepage);
			}
		}

		ic->arg[0] = (iword & 0x00ffffff) << 2;
		/*  Sign-extend:  */
		if (ic->arg[0] & 0x02000000)
			ic->arg[0] |= 0xfc000000;
		/*
		 *  Branches are calculated as PC + 8 + offset.
		 */
		ic->arg[0] = (int32_t)(ic->arg[0] + 8);

		/*  Special case: branch within the same page:  */
		{
			uint32_t mask_within_page =
			    ((ARM_IC_ENTRIES_PER_PAGE-1) << 2) | 3;
			uint32_t old_pc = addr;
			uint32_t new_pc = old_pc + (int32_t)ic->arg[0];
			if ((old_pc & ~mask_within_page) ==
			    (new_pc & ~mask_within_page)) {
				ic->f = samepage_function;
				ic->arg[0] = (size_t) (
				    cpu->cd.arm.cur_ic_page +
				    ((new_pc & mask_within_page) >>
				    ARM_INSTR_ALIGNMENT_SHIFT));
			}
		}
		break;

	case 0xe:
		if ((iword & 0x0fff0fff) == 0x0e100f10) {
			/*  Get CPU id into register.  */
			ic->arg[0] = (size_t)(&cpu->cd.arm.r[rd]);
			ic->f = cond_instr(get_cpu_id);
			break;
		}
		/*  Unimplemented stuff:  */
		goto bad;

	default:goto bad;
	}


#define	DYNTRANS_TO_BE_TRANSLATED_TAIL
#include "cpu_dyntrans.c" 
#undef	DYNTRANS_TO_BE_TRANSLATED_TAIL
}

