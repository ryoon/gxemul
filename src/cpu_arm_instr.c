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
 *  $Id: cpu_arm_instr.c,v 1.44 2005-08-06 19:32:43 debug Exp $
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

#define X(n) void arm_instr_ ## n(struct cpu *cpu, \
	struct arm_instr_call *ic)

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


/*  This is for marking a physical page as containing translated or
    combined instructions, respectively:  */
#define	translated	(cpu->cd.arm.cur_physpage->flags |= TRANSLATIONS)
#define	combined	(cpu->cd.arm.cur_physpage->flags |= COMBINATIONS)


/*
 *  nothing:  Do nothing.
 *
 *  The difference between this function and the "nop" instruction is that
 *  this function does not increase the program counter or the number of
 *  translated instructions.  It is used to "get out" of running in translated
 *  mode.
 *
 *  IMPORTANT NOTE: Do a   cpu->running_translated = 0;
 *                  before setting cpu->cd.arm.next_ic = &nothing_call;
 */
X(nothing)
{
	cpu->n_translated_instrs --;
	cpu->cd.arm.next_ic --;
}


static struct arm_instr_call nothing_call = { instr(nothing), {0,0,0} };


/*****************************************************************************/


/*
 *  nop:  Do nothing.
 */
X(nop)
{
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
	cpu->cd.arm.r[ARM_PC] &= ~((ARM_IC_ENTRIES_PER_PAGE-1) << 2);
	cpu->cd.arm.r[ARM_PC] += (low_pc << 2);
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
 *
 *  TODO: How about function call trace?
 */
X(bl)
{
	uint32_t lr, low_pc;

	/*  Figure out what the return (link) address will be:  */
	low_pc = ((size_t)cpu->cd.arm.next_ic - (size_t)
	    cpu->cd.arm.cur_ic_page) / sizeof(struct arm_instr_call);
	lr = cpu->cd.arm.r[ARM_PC];
	lr &= ~((ARM_IC_ENTRIES_PER_PAGE-1) << 2);
	lr += (low_pc << 2);

	/*  Link:  */
	cpu->cd.arm.r[ARM_LR] = lr;

	/*  Calculate new PC from this instruction + arg[0]  */
	cpu->pc = cpu->cd.arm.r[ARM_PC] = lr + (int32_t)ic->arg[0];

	/*  Find the new physical page and update the translation pointers:  */
	arm_pc_to_pointers(cpu);
}
Y(bl)


/*
 *  bl_samepage:  A branch + link within the same page
 *
 *  arg[0] = pointer to new arm_instr_call
 *
 *  TODO: How about function call trace?
 */
X(bl_samepage)
{
	uint32_t lr, low_pc;

	/*  Figure out what the return (link) address will be:  */
	low_pc = ((size_t)cpu->cd.arm.next_ic - (size_t)
	    cpu->cd.arm.cur_ic_page) / sizeof(struct arm_instr_call);
	lr = cpu->cd.arm.r[ARM_PC];
	lr &= ~((ARM_IC_ENTRIES_PER_PAGE-1) << 2);
	lr += (low_pc << 2);

	/*  Link:  */
	cpu->cd.arm.r[ARM_LR] = lr;

	/*  Branch:  */
	cpu->cd.arm.next_ic = (struct arm_instr_call *) ic->arg[0];
}
Y(bl_samepage)


/*
 *  mov_pc:  "mov pc,reg"
 *
 *  arg[0] = pointer to uint32_t in host memory of source register
 */
X(mov_pc)
{
	/*  Update the PC register:  */
	cpu->pc = cpu->cd.arm.r[ARM_PC] = *((uint32_t *)ic->arg[0]);

	/*  TODO: There is no need to update the pointers if this
	    is a return to the same page!  */

	/*  Find the new physical page and update the translation pointers:  */
	arm_pc_to_pointers(cpu);
}
Y(mov_pc)


/*
 *  mov_regreg:
 *
 *  arg[0] = pointer to uint32_t in host memory of destination register
 *  arg[1] = pointer to uint32_t in host memory of source register
 */
X(mov_regreg)
{
	*((uint32_t *)ic->arg[0]) = *((uint32_t *)ic->arg[1]);
}
Y(mov_regreg)


/*
 *  mov:  Set a 32-bit register to a 32-bit value.
 *
 *  arg[0] = pointer to uint32_t in host memory
 *  arg[1] = 32-bit value
 */
X(mov)
{
	*((uint32_t *)ic->arg[0]) = ic->arg[1];
}
Y(mov)


/*
 *  clear:  Set a 32-bit register to 0. (A "mov" to fixed value zero.)
 *
 *  arg[0] = pointer to uint32_t in host memory
 */
X(clear)
{
	*((uint32_t *)ic->arg[0]) = 0;
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


/*
 *  load_byte_imm_pcrel:
 *	Like load_byte_imm, but the source address is the PC register.
 *	Before loading, we have to synchronize the PC register and add 8.
 *
 *  arg[0] = pointer to ARM_PC  (not used here)
 *  arg[1] = 32-bit offset
 *  arg[2] = pointer to uint32_t in host memory where to store the value
 */
X(load_byte_imm_pcrel)
{
	uint32_t low_pc, addr;
	unsigned char data[1];

	low_pc = ((size_t)ic - (size_t)
	    cpu->cd.arm.cur_ic_page) / sizeof(struct arm_instr_call);
	cpu->cd.arm.r[ARM_PC] &= ~((ARM_IC_ENTRIES_PER_PAGE-1) << 2);
	cpu->cd.arm.r[ARM_PC] += (low_pc << 2);

	addr = cpu->cd.arm.r[ARM_PC] + 8 + ic->arg[1];
	if (!cpu->memory_rw(cpu, cpu->mem, addr, data, sizeof(data),
	    MEM_READ, CACHE_DATA)) {
		fatal("load failed: TODO\n");
		exit(1);
	}
	*((uint32_t *)ic->arg[2]) = data[0];
}
Y(load_byte_imm_pcrel)


/*
 *  load_word_imm_pcrel:
 *	Like load_word_imm, but the source address is the PC register.
 *	Before loading, we have to synchronize the PC register and add 8.
 *
 *  arg[0] = pointer to ARM_PC  (not used here)
 *  arg[1] = 32-bit offset
 *  arg[2] = pointer to uint32_t in host memory where to store the value
 */
X(load_word_imm_pcrel)
{
	uint32_t low_pc, addr;
	unsigned char data[sizeof(uint32_t)];

	low_pc = ((size_t)ic - (size_t)
	    cpu->cd.arm.cur_ic_page) / sizeof(struct arm_instr_call);
	cpu->cd.arm.r[ARM_PC] &= ~((ARM_IC_ENTRIES_PER_PAGE-1) << 2);
	cpu->cd.arm.r[ARM_PC] += (low_pc << 2);

	addr = cpu->cd.arm.r[ARM_PC] + 8 + ic->arg[1];
	if (!cpu->memory_rw(cpu, cpu->mem, addr, data, sizeof(data),
	    MEM_READ, CACHE_DATA)) {
		fatal("load failed: TODO\n");
		exit(1);
	}
	/*  TODO: Big endian  */
	*((uint32_t *)ic->arg[2]) = data[0] + (data[1] << 8) +
	    (data[2] << 16) + (data[3] << 24);
}
Y(load_word_imm_pcrel)


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
	uint32_t a, b, c;
	int v, n;
	a = *((uint32_t *)ic->arg[0]);
	b = ic->arg[1];

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


#include "cpu_arm_instr_cmps.c"


/*
 *  sub:  Subtract an immediate value from a 32-bit word, and store the
 *        result in a 32-bit word.
 *
 *  arg[0] = pointer to destination uint32_t in host memory
 *  arg[1] = pointer to source uint32_t in host memory
 *  arg[2] = 32-bit value
 */
X(sub)
{
	*((uint32_t *)ic->arg[0]) = *((uint32_t *)ic->arg[1]) - ic->arg[2];
}
Y(sub)
X(sub_self)
{
	*((uint32_t *)ic->arg[0]) -= ic->arg[2];
}
Y(sub_self)


/*
 *  add:  Add an immediate value to a 32-bit word, and store the
 *        result in a 32-bit word.
 *
 *  arg[0] = pointer to destination uint32_t in host memory
 *  arg[1] = pointer to source uint32_t in host memory
 *  arg[2] = 32-bit value
 */
X(add)
{
	*((uint32_t *)ic->arg[0]) = *((uint32_t *)ic->arg[1]) + ic->arg[2];
}
Y(add)
X(add_self)
{
	*((uint32_t *)ic->arg[0]) += ic->arg[2];
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
	n_back = (addr >> 2) & (ARM_IC_ENTRIES_PER_PAGE-1);

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
	int i;
	uint32_t addr, low_pc, iword, imm;
	unsigned char *page;
	unsigned char ib[4];
	int condition_code, main_opcode, secondary_opcode, s_bit, r16, r12, r8;
	int p_bit, u_bit, b_bit, w_bit, l_bit;
	void (*samepage_function)(struct cpu *, struct arm_instr_call *);

	/*  Figure out the (virtual) address of the instruction:  */
	low_pc = ((size_t)ic - (size_t)cpu->cd.arm.cur_ic_page)
	    / sizeof(struct arm_instr_call);
	addr = cpu->cd.arm.r[ARM_PC] & ~((ARM_IC_ENTRIES_PER_PAGE-1) << 2);
	addr += (low_pc << 2);
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
	r16 = (iword >> 16) & 15;
	r12 = (iword >> 12) & 15;
	r8 = (iword >> 8) & 15;

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
		if ((main_opcode & 2) == 0) {
			if ((iword & 0x0ffffff0) == 0x01a0f000) {
				/*  Hardcoded: mov pc, rX  */
				if ((iword & 15) == ARM_PC) {
					fatal("mov pc,pc?\n");
					goto bad;
				}
				ic->f = cond_instr(mov_pc);
				ic->arg[0] = (size_t)
				    (&cpu->cd.arm.r[iword & 15]);
			} else if ((iword & 0x0fff0ff0) == 0x01a00000) {
				/*  Hardcoded: mov reg,reg  */
				if ((iword & 15) == ARM_PC) {
					fatal("mov reg,pc?\n");
					goto bad;
				}
				ic->f = cond_instr(mov_regreg);
				ic->arg[0] = (size_t)
				    (&cpu->cd.arm.r[r12]);
				ic->arg[1] = (size_t)
				    (&cpu->cd.arm.r[iword & 15]);
			} else {
				fatal("REGISTER FORM! TODO\n");
				goto bad;
			}
			break;
		}
		imm = iword & 0xff;
		r8 <<= 1;
		while (r8-- > 0)
			imm = (imm >> 1) | ((imm & 1) << 31);
		switch (secondary_opcode) {
		case 0x2:				/*  SUB  */
		case 0x4:				/*  ADD  */
			if (s_bit) {
				fatal("add/sub s_bit: TODO\n");
				goto bad;
			}
			if (r12 == ARM_PC || r16 == ARM_PC) {
				fatal("add/sub: PC\n");
				goto bad;
			}
			switch (secondary_opcode) {
			case 0x2:
				ic->f = cond_instr(sub);
				if (r12 == r16) {
					ic->f = cond_instr(sub_self);
					if (imm == 1 && r12 != ARM_PC)
						ic->f = arm_sub_self_1[r12];
					if (imm == 4 && r12 != ARM_PC)
						ic->f = arm_sub_self_4[r12];
				}
				break;
			case 0x4:
				ic->f = cond_instr(add);
				if (r12 == r16) {
					ic->f = cond_instr(add_self);
					if (imm == 1 && r12 != ARM_PC)
						ic->f = arm_add_self_1[r12];
					if (imm == 4 && r12 != ARM_PC)
						ic->f = arm_add_self_4[r12];
				}
				break;
			}
			ic->arg[0] = (size_t)(&cpu->cd.arm.r[r12]);
			ic->arg[1] = (size_t)(&cpu->cd.arm.r[r16]);
			ic->arg[2] = imm;
			break;
		case 0xa:				/*  CMP  */
			if (!s_bit) {
				fatal("cmp !s_bit: TODO\n");
				goto bad;
			}
			ic->f = cond_instr(cmps);
			ic->arg[0] = (size_t)(&cpu->cd.arm.r[r16]);
			ic->arg[1] = imm;
			if (imm == 0 && r16 != ARM_PC)
				ic->f = arm_cmps_0[r16];
			break;
		case 0xd:				/*  MOV  */
			if (s_bit) {
				fatal("mov s_bit: TODO\n");
				goto bad;
			}
			if (r12 == ARM_PC) {
				fatal("TODO: mov used as branch\n");
				goto bad;
			} else {
				ic->f = cond_instr(mov);
				ic->arg[0] = (size_t)(&cpu->cd.arm.r[r12]);
				ic->arg[1] = imm;
				if (imm == 0)
					ic->f = cond_instr(clear);
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
		ic->f = load_store_instr[((iword >> 16) & 0x3f0) + condition_code];
		imm = iword & 0xfff;
		if (!u_bit)
			imm = (int32_t)0-imm;
		if (main_opcode < 6) {
			/*  Immediate:  */
			ic->arg[0] = (size_t)(&cpu->cd.arm.r[r16]);
			ic->arg[1] = (size_t)(imm);
			ic->arg[2] = (size_t)(&cpu->cd.arm.r[r12]);
		}
		if (main_opcode == 4 && b_bit) {
			/*  Post-index, immediate:  */
			if (imm == 1 && !w_bit && l_bit)
				ic->f = instr(store_w0_byte_u1_p0_imm_fixinc1);
			if (w_bit) {
				fatal("load/store: T-bit\n");
				goto bad;
			}
			if (r16 == ARM_PC) {
				fatal("load/store writeback PC: error\n");
				goto bad;
			}
		} else if (main_opcode == 5) {
			/*  Pre-index, immediate:  */
			/*  ldr(b) Rd,[Rn,#imm]  */
			if (l_bit) {
				if (r12 == ARM_PC) {
					fatal("WARNING: ldr to pc register?\n");
					goto bad;
				}
				if (r16 == ARM_PC) {
					if (w_bit) {
						fatal("w bit load etc\n");
						goto bad;
					}
					ic->f = b_bit?
					    cond_instr(load_byte_imm_pcrel) :
					    cond_instr(load_word_imm_pcrel);
				}
			} else {
				if (r12 == ARM_PC) {
					fatal("TODO: store pc\n");
					goto bad;
				}
				if (r16 == ARM_PC) {
					fatal("TODO: store pc rel\n");
					goto bad;
				}
			}
		} else {
			fatal("Specific Load/store TODO\n");
			goto bad;
		}
		break;

	case 0x8:	/*  Multiple load/store...  (Block data transfer)  */
	case 0x9:	/*  xxxx100P USWLnnnn llllllll llllllll  */
		if (l_bit)
			ic->f = cond_instr(bdt_load);
		else
			ic->f = cond_instr(bdt_store);
		ic->arg[0] = (size_t)(&cpu->cd.arm.r[r16]);
		ic->arg[1] = (size_t)iword;
		if (r16 == ARM_PC) {
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
			ic->f = cond_instr(bl);
			samepage_function = cond_instr(bl_samepage);
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
				    ((new_pc & mask_within_page) >> 2));
			}
		}
		break;

	default:goto bad;
	}


#define	DYNTRANS_TO_BE_TRANSLATED_TAIL
#include "cpu_dyntrans.c" 
#undef	DYNTRANS_TO_BE_TRANSLATED_TAIL
}

