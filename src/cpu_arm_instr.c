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
 *  $Id: cpu_arm_instr.c,v 1.73 2005-08-24 14:51:24 debug Exp $
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
	{  if (cpu->cd.arm.cpsr & ARM_FLAG_Z)				\
		arm_instr_ ## n (cpu, ic);		}		\
	void arm_instr_ ## n ## __ne(struct cpu *cpu,			\
			struct arm_instr_call *ic)			\
	{  if (!(cpu->cd.arm.cpsr & ARM_FLAG_Z))			\
		arm_instr_ ## n (cpu, ic);		}		\
	void arm_instr_ ## n ## __cs(struct cpu *cpu,			\
			struct arm_instr_call *ic)			\
	{  if (cpu->cd.arm.cpsr & ARM_FLAG_C)				\
		arm_instr_ ## n (cpu, ic);		}		\
	void arm_instr_ ## n ## __cc(struct cpu *cpu,			\
			struct arm_instr_call *ic)			\
	{  if (!(cpu->cd.arm.cpsr & ARM_FLAG_C))			\
		arm_instr_ ## n (cpu, ic);		}		\
	void arm_instr_ ## n ## __mi(struct cpu *cpu,			\
			struct arm_instr_call *ic)			\
	{  if (cpu->cd.arm.cpsr & ARM_FLAG_N)				\
		arm_instr_ ## n (cpu, ic);		}		\
	void arm_instr_ ## n ## __pl(struct cpu *cpu,			\
			struct arm_instr_call *ic)			\
	{  if (!(cpu->cd.arm.cpsr & ARM_FLAG_N))			\
		arm_instr_ ## n (cpu, ic);		}		\
	void arm_instr_ ## n ## __vs(struct cpu *cpu,			\
			struct arm_instr_call *ic)			\
	{  if (cpu->cd.arm.cpsr & ARM_FLAG_V)				\
		arm_instr_ ## n (cpu, ic);		}		\
	void arm_instr_ ## n ## __vc(struct cpu *cpu,			\
			struct arm_instr_call *ic)			\
	{  if (!(cpu->cd.arm.cpsr & ARM_FLAG_V))			\
		arm_instr_ ## n (cpu, ic);		}		\
	void arm_instr_ ## n ## __hi(struct cpu *cpu,			\
			struct arm_instr_call *ic)			\
	{  if (cpu->cd.arm.cpsr & ARM_FLAG_C &&			\
		!(cpu->cd.arm.cpsr & ARM_FLAG_Z))			\
		arm_instr_ ## n (cpu, ic);		}		\
	void arm_instr_ ## n ## __ls(struct cpu *cpu,			\
			struct arm_instr_call *ic)			\
	{  if (cpu->cd.arm.cpsr & ARM_FLAG_Z &&			\
		!(cpu->cd.arm.cpsr & ARM_FLAG_C))			\
		arm_instr_ ## n (cpu, ic);		}		\
	void arm_instr_ ## n ## __ge(struct cpu *cpu,			\
			struct arm_instr_call *ic)			\
	{  if (((cpu->cd.arm.cpsr & ARM_FLAG_N)?1:0) ==		\
		((cpu->cd.arm.cpsr & ARM_FLAG_V)?1:0))			\
		arm_instr_ ## n (cpu, ic);		}		\
	void arm_instr_ ## n ## __lt(struct cpu *cpu,			\
			struct arm_instr_call *ic)			\
	{  if (((cpu->cd.arm.cpsr & ARM_FLAG_N)?1:0) !=		\
		((cpu->cd.arm.cpsr & ARM_FLAG_V)?1:0))			\
		arm_instr_ ## n (cpu, ic);		}		\
	void arm_instr_ ## n ## __gt(struct cpu *cpu,			\
			struct arm_instr_call *ic)			\
	{  if (((cpu->cd.arm.cpsr & ARM_FLAG_N)?1:0) ==		\
		((cpu->cd.arm.cpsr & ARM_FLAG_V)?1:0) &&		\
		!(cpu->cd.arm.cpsr & ARM_FLAG_Z))			\
		arm_instr_ ## n (cpu, ic);		}		\
	void arm_instr_ ## n ## __le(struct cpu *cpu,			\
			struct arm_instr_call *ic)			\
	{  if (((cpu->cd.arm.cpsr & ARM_FLAG_N)?1:0) !=		\
		((cpu->cd.arm.cpsr & ARM_FLAG_V)?1:0) ||		\
		(cpu->cd.arm.cpsr & ARM_FLAG_Z))			\
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
	case 4:	/*  asr #c  (c = 1..32)  */
		if (c == 0)
			c = 32;
		if (update_c) {
			lastbit = ((int64_t)(int32_t)tmp >> (c-1)) & 1;
		}
		tmp = (int64_t)(int32_t)tmp >> c;
		break;
	case 5:	/*  asr Rc  */
		c = cpu->cd.arm.r[c >> 1] & 255;
		if (update_c) {
			if (c == 0)
				update_c = 0;
			else
				lastbit = ((int64_t)(int32_t)tmp >> (c-1)) & 1;
		}
		tmp = (int64_t)(int32_t)tmp >> c;
		break;
	default:fatal("R: unimplemented t=%i\n", t);
		exit(1);
	}
	if (update_c) {
		cpu->cd.arm.cpsr &= ~ARM_FLAG_C;
		if (lastbit)
			cpu->cd.arm.cpsr |= ARM_FLAG_C;
	}
	return tmp;
}


/*****************************************************************************/


/*
 *  nop:  Do nothing.
 *  invalid:  Invalid instructions end up here.
 */
X(nop) { }
X(invalid) {
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
 *  mul: Multiplication
 *
 *  arg[0] = ptr to rd
 *  arg[1] = ptr to rm
 *  arg[2] = ptr to rs
 */
X(mul)
{
	reg(ic->arg[0]) = reg(ic->arg[1]) * reg(ic->arg[2]);
}
Y(mul)


/*
 *  mla: Multiplication with addition
 *
 *  arg[0] = copy of instruction word
 */
X(mla)
{
	/*  xxxx0000 00ASdddd nnnnssss 1001mmmm (Rd,Rm,Rs[,Rn])  */
	uint32_t iw = ic->arg[0];
	int rd = (iw >> 16) & 15, rn = (iw >> 12) & 15,
	    rs = (iw >> 8) & 15,  rm = iw & 15;
	cpu->cd.arm.r[rd] = cpu->cd.arm.r[rm] * cpu->cd.arm.r[rs]
	    + cpu->cd.arm.r[rn];
	if (iw & 0x00100000) {
		cpu->cd.arm.cpsr &= ~(ARM_FLAG_Z | ARM_FLAG_N);
		if (cpu->cd.arm.r[rd] == 0)
			cpu->cd.arm.cpsr |= ARM_FLAG_Z;
		if (cpu->cd.arm.r[rd] & 0x80000000)
			cpu->cd.arm.cpsr |= ARM_FLAG_N;
	}
}
Y(mla)


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
	if (u_bit)
		tmp = (int64_t)(int32_t)tmp
		    * (int64_t)(int32_t)cpu->cd.arm.r[(iw >> 8) & 15];
	else
		tmp *= (uint64_t)cpu->cd.arm.r[(iw >> 8) & 15];
	if (a_bit) {
		uint64_t x = ((uint64_t)cpu->cd.arm.r[(iw >> 16) & 15] << 32)
		    | cpu->cd.arm.r[(iw >> 12) & 15];
		x += tmp;
		cpu->cd.arm.r[(iw >> 16) & 15] = (x >> 32);
		cpu->cd.arm.r[(iw >> 12) & 15] = x;
	} else {
		cpu->cd.arm.r[(iw >> 16) & 15] = (tmp >> 32);
		cpu->cd.arm.r[(iw >> 12) & 15] = tmp;
	}
}
Y(mull)


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
 *  msr: Move to status/flag register from a normal register.
 *
 *  arg[0] = pointer to rm
 *  arg[1] = mask
 */
X(msr)
{
	/*
	 *  TODO:  When switching between modes, copy to/from mirrored
	 *	   register sets!
	 */

	cpu->cd.arm.cpsr &= ~ic->arg[1];
	cpu->cd.arm.cpsr |= (reg(ic->arg[0]) & ic->arg[1]);
}
Y(msr)


/*
 *  mrs: Move from status/flag register to a normal register.
 *
 *  arg[0] = pointer to rd
 */
X(mrs)
{
	reg(ic->arg[0]) = cpu->cd.arm.cpsr;
}
Y(mrs)


/*
 *  mcr_mrc:  Coprocessor move
 *  cdp:      Coprocessor operation
 *
 *  arg[0] = copy of the instruction word
 */
X(mcr_mrc) {
	uint32_t low_pc;
	low_pc = ((size_t)ic - (size_t)
	    cpu->cd.arm.cur_ic_page) / sizeof(struct arm_instr_call);
	cpu->cd.arm.r[ARM_PC] &= ~((ARM_IC_ENTRIES_PER_PAGE-1)
	    << ARM_INSTR_ALIGNMENT_SHIFT);
	cpu->cd.arm.r[ARM_PC] += (low_pc << ARM_INSTR_ALIGNMENT_SHIFT);
	cpu->pc = cpu->cd.arm.r[ARM_PC];
	arm_mcr_mrc(cpu, ic->arg[0]);
}
Y(mcr_mrc)
X(cdp) {
	uint32_t low_pc;
	low_pc = ((size_t)ic - (size_t)
	    cpu->cd.arm.cur_ic_page) / sizeof(struct arm_instr_call);
	cpu->cd.arm.r[ARM_PC] &= ~((ARM_IC_ENTRIES_PER_PAGE-1)
	    << ARM_INSTR_ALIGNMENT_SHIFT);
	cpu->cd.arm.r[ARM_PC] += (low_pc << ARM_INSTR_ALIGNMENT_SHIFT);
	cpu->pc = cpu->cd.arm.r[ARM_PC];
	arm_cdp(cpu, ic->arg[0]);
}
Y(cdp)


/*
 *  swi_useremul: Syscall.
 *
 *  arg[0] = swi number
 */
X(swi_useremul)
{
	useremul_syscall(cpu, ic->arg[0]);
}
Y(swi_useremul)


extern void (*arm_load_store_instr[1024])(struct cpu *,
	struct arm_instr_call *);
X(store_w0_byte_u1_p0_imm);

extern void (*arm_load_store_instr_pc[1024])(struct cpu *,
	struct arm_instr_call *);

extern void (*arm_load_store_instr_3[2048])(struct cpu *,
	struct arm_instr_call *);

extern void (*arm_load_store_instr_3_pc[2048])(struct cpu *,
	struct arm_instr_call *);

extern void (*arm_dpi_instr[2 * 2 * 2 * 16 * 16])(struct cpu *,
	struct arm_instr_call *);
X(cmps);
X(sub);



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
				if (cpu->machine->show_trace_tree)
					cpu_functioncall_trace_return(cpu);
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
 *  cmps rZ,#0) is executed instead.
 *
 *  L: cmps rZ,#0		ic[0]
 *     strb rX,[rY],#1		ic[1]
 *     sub  rZ,rZ,#1		ic[2]
 *     bgt  L			ic[3]
 */
X(fill_loop_test)
{
	uint32_t addr, a, n, ofs, maxlen;
	uint32_t *rzp = (uint32_t *)(size_t)ic[0].arg[0];
	unsigned char *page;

	addr = reg(ic[1].arg[0]);
	page = cpu->cd.arm.host_store[addr >> 12];
	if (page == NULL) {
		instr(cmps)(cpu, ic);
		return;
	}

	n = reg(rzp) + 1;
	ofs = addr & 0xfff;
	maxlen = 4096 - ofs;
	if (n > maxlen)
		n = maxlen;

	/*  printf("x = %x, n = %i\n", reg(ic[1].arg[2]), n);  */
	memset(page + ofs, reg(ic[1].arg[2]), n);

	reg(ic[1].arg[0]) = addr + n;

	reg(rzp) -= n;
	cpu->n_translated_instrs += (4 * n);

	a = reg(rzp);

	cpu->cd.arm.cpsr &=
	    ~(ARM_FLAG_Z | ARM_FLAG_N | ARM_FLAG_V | ARM_FLAG_C);
	if (a != 0)
		cpu->cd.arm.cpsr |= ARM_FLAG_C;
	else
		cpu->cd.arm.cpsr |= ARM_FLAG_Z;
	if ((int32_t)a < 0)
		cpu->cd.arm.cpsr |= ARM_FLAG_N;

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

	if (n_back >= 3) {
		if (ic[-3].f == instr(cmps) &&
		    ic[-3].arg[0] == ic[-1].arg[0] &&
		    ic[-3].arg[1] == 0 &&
		    ic[-2].f == instr(store_w0_byte_u1_p0_imm) &&
		    ic[-2].arg[1] == 1 &&
		    ic[-1].f == instr(sub) &&
		    ic[-1].arg[0] == ic[-1].arg[2] && ic[-1].arg[1] == 1 &&
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
	uint32_t addr, low_pc, iword, imm = 0;
	unsigned char *page;
	unsigned char ib[4];
	int condition_code, main_opcode, secondary_opcode, s_bit, rn, rd, r8;
	int p_bit, u_bit, b_bit, w_bit, l_bit, regform, rm, c, t;
	int any_pc_reg;
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
		if ((iword & 0x0fc000f0) == 0x00000090) {
			/*
			 *  Multiplication:
			 *  xxxx0000 00ASdddd nnnnssss 1001mmmm (Rd,Rm,Rs[,Rn])
			 */
			if (iword & 0x00200000) {
				ic->f = cond_instr(mla);
				ic->arg[0] = iword;
			} else {
				if (s_bit) {
					fatal("s_bit\n");
					goto bad;
				}
				ic->f = cond_instr(mul);
				ic->arg[0] = (size_t)(&cpu->cd.arm.r[rd]);
				ic->arg[1] = (size_t)(&cpu->cd.arm.r[rm]);
				ic->arg[2] = (size_t)(&cpu->cd.arm.r[r8]);
			}
			break;
		}
		if ((iword & 0x0f8000f0) == 0x00800090) {
			/*  Long multiplication:  */
			if (s_bit) {
				fatal("TODO: sbit mull\n");
				goto bad;
			}
			ic->f = cond_instr(mull);
			ic->arg[0] = iword;
			break;
		}
		if ((iword & 0x0fb0fff0) == 0x0120f000) {
			/*  msr: move to [C|S]PSR from a register:  */
			if (rm == ARM_PC) {
				fatal("msr PC?\n");
				goto bad;
			}
			ic->f = cond_instr(msr);
			ic->arg[0] = (size_t)(&cpu->cd.arm.r[rm]);
			switch ((iword >> 16) & 15) {
			case 1:	ic->arg[1] = 0x000000ff; break;
			case 8:	ic->arg[1] = 0xff000000; break;
			case 9:	ic->arg[1] = 0xff0000ff; break;
			default:fatal("unimpl a\n");
				goto bad;
			}
			break;
		}
		if ((iword & 0x0fb0f000) == 0x0320f000) {
			/*  msr: immediate form  */
			fatal("msr: immediate form: TODO\n");
			goto bad;
		}
		if ((iword & 0x0fff0fff) == 0x010f0000) {
			/*  mrs: move from CPSR to a register:  */
			if (rd == ARM_PC) {
				fatal("mrs PC?\n");
				goto bad;
			}
			ic->f = cond_instr(mrs);
			ic->arg[0] = (size_t)(&cpu->cd.arm.r[rd]);
			break;
		}
		if ((iword & 0x0e000090) == 0x00000090) {
			int imm = ((iword >> 4) & 0xf0) | (iword & 0xf);
			int regform = !(iword & 0x00400000);
			p_bit = main_opcode & 1;
			if (rd == ARM_PC || rn == ARM_PC)
				ic->f = arm_load_store_instr_3_pc[
				    condition_code + (l_bit? 16 : 0)
				    + (iword & 0x40? 32 : 0)
				    + (w_bit? 64 : 0)
				    + (iword & 0x20? 128 : 0)
				    + (u_bit? 256 : 0) + (p_bit? 512 : 0)
				    + (regform? 1024 : 0)];
			else
				ic->f = arm_load_store_instr_3[
				    condition_code + (l_bit? 16 : 0)
				    + (iword & 0x40? 32 : 0)
				    + (w_bit? 64 : 0)
				    + (iword & 0x20? 128 : 0)
				    + (u_bit? 256 : 0) + (p_bit? 512 : 0)
				    + (regform? 1024 : 0)];
			ic->arg[0] = (size_t)(&cpu->cd.arm.r[rn]);
			ic->arg[2] = (size_t)(&cpu->cd.arm.r[rd]);
			if (regform)
				ic->arg[1] = iword & 0xf;
			else
				ic->arg[1] = (size_t)(imm);
			break;
		}

		if (iword & 0x80 && !(main_opcode & 2) && iword & 0x10) {
			fatal("reg form blah blah\n");
			goto bad;
		}

		/*  "mov pc,lr" with trace enabled:  */
		if ((iword & 0x0fffffff) == 0x01a0f00e &&
		    cpu->machine->show_trace_tree) {
			ic->f = cond_instr(ret_trace);
			break;
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
			ic->arg[1] = imm;
		} else
			ic->arg[1] = iword;

		ic->arg[0] = (size_t)(&cpu->cd.arm.r[rn]);
		ic->arg[2] = (size_t)(&cpu->cd.arm.r[rd]);
		any_pc_reg = 0;
		if (rn == ARM_PC || rd == ARM_PC)
			any_pc_reg = 1;

		ic->f = arm_dpi_instr[condition_code +
		    16 * secondary_opcode + (s_bit? 256 : 0) +
		    (any_pc_reg? 512 : 0) + (regform? 1024 : 0)];
		break;

	case 0x4:	/*  Load and store...  */
	case 0x5:	/*  xxxx010P UBWLnnnn ddddoooo oooooooo  Immediate  */
	case 0x6:	/*  xxxx011P UBWLnnnn ddddcccc ctt0mmmm  Register  */
	case 0x7:
		if (rd == ARM_PC || rn == ARM_PC)
			ic->f = arm_load_store_instr_pc[((iword >> 16)
			    & 0x3f0) + condition_code];
		else
			ic->f = arm_load_store_instr[((iword >> 16) &
			    0x3f0) + condition_code];
		imm = iword & 0xfff;
		ic->arg[0] = (size_t)(&cpu->cd.arm.r[rn]);
		ic->arg[2] = (size_t)(&cpu->cd.arm.r[rd]);
		if (main_opcode < 6)
			ic->arg[1] = (size_t)(imm);
		else
			ic->arg[1] = iword;
		if (main_opcode == 4) {
			/*  Post-index, immediate:  */
			if (w_bit) {
				fatal("load/store: T-bit\n");
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
		if (iword & 0x10) {
			/*  xxxx1110 oooLNNNN ddddpppp qqq1MMMM  MCR/MRC  */
			ic->arg[0] = iword;
			ic->f = cond_instr(mcr_mrc);
		} else {
			/*  xxxx1110 oooonnnn ddddpppp qqq0mmmm  CDP  */
			ic->arg[0] = iword;
			ic->f = cond_instr(cdp);
		}
		break;

	case 0xf:
		if ((iword & 0x00f00000) == 0x00a00000) {
			ic->arg[0] = iword & 0x00ffffff;
			if (cpu->machine->userland_emul != NULL)
				ic->f = cond_instr(swi_useremul);
			else {
				fatal("swi in non-useremul mode: TODO\n");
				goto bad;
			}
		} else {
			fatal("swi, not 0xaXXXXX: TODO\n");
			goto bad;
		}
		break;

	default:goto bad;
	}


#define	DYNTRANS_TO_BE_TRANSLATED_TAIL
#include "cpu_dyntrans.c" 
#undef	DYNTRANS_TO_BE_TRANSLATED_TAIL
}

