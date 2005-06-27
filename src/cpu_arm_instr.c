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
 *  $Id: cpu_arm_instr.c,v 1.20 2005-06-27 09:20:19 debug Exp $
 *
 *  ARM instructions.
 *
 *  Individual functions should keep track of cpu->cd.arm.n_translated_instrs.
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


/*  This is for marking a physical page as containing combined instructions:  */
#define	combined	(cpu->cd.arm.cur_physpage->flags |= ARM_COMBINATIONS)


void arm_translate_instruction(struct cpu *cpu, struct arm_instr_call *ic);


/*
 *  nothing:  Do nothing.
 *
 *  The difference between this function and the "nop" instruction is that
 *  this function does not increase the program counter or the number of
 *  translated instructions.  It is used to "get out" of running in translated
 *  mode.
 */
X(nothing)
{
	cpu->cd.arm.running_translated = 0;
	cpu->cd.arm.n_translated_instrs --;
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
	int low_pc;
	uint32_t old_pc;

	/*  fatal("b: arg[0] = 0x%08x, pc=0x%08x\n", ic->arg[0], cpu->pc);  */

	/*  Calculate new PC from this instruction + arg[0]  */
	low_pc = ((size_t)ic - (size_t)
	    cpu->cd.arm.cur_ic_page) / sizeof(struct arm_instr_call);
	cpu->cd.arm.r[ARM_PC] &= ~((IC_ENTRIES_PER_PAGE-1) << 2);
	cpu->cd.arm.r[ARM_PC] += (low_pc << 2);
	old_pc = cpu->cd.arm.r[ARM_PC];
	/*  fatal("b: 3: old_pc=0x%08x\n", old_pc);  */
	cpu->cd.arm.r[ARM_PC] += (int32_t)ic->arg[0];
	cpu->pc = cpu->cd.arm.r[ARM_PC];
	/*  fatal("b: 2: pc=0x%08x\n", cpu->pc);  */

	fatal("b: different page! TODO\n");
	exit(1);
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
 *  TODO: Implement this.
 *  TODO: How about function call trace?
 */
X(bl)
{
	fatal("bl different page: TODO\n");
	exit(1);
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
	lr &= ~((IC_ENTRIES_PER_PAGE-1) << 2);
	lr += (low_pc << 2);

	/*  Link:  */
	cpu->cd.arm.r[ARM_LR] = lr;

	/*  Branch:  */
	cpu->cd.arm.next_ic = (struct arm_instr_call *) ic->arg[0];
}
Y(bl_samepage)


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


/*
 *  load_byte_imm:  Load an 8-bit byte from emulated memory and store it in
 *                  a 32-bit word in host memory.
 *
 *  arg[0] = pointer to uint32_t in host memory of base address
 *  arg[1] = 32-bit offset
 *  arg[2] = pointer to uint32_t in host memory where to store the value
 */
X(load_byte_imm)
{
	unsigned char data[1];
	uint32_t addr = *((uint32_t *)ic->arg[0]) + ic->arg[1];
	if (!cpu->memory_rw(cpu, cpu->mem, addr, data, sizeof(data),
	    MEM_READ, CACHE_DATA)) {
		fatal("load failed: TODO\n");
		exit(1);
	}
	*((uint32_t *)ic->arg[2]) = data[0];
}
Y(load_byte_imm)


/*
 *  load_byte_w_imm:
 *	Load an 8-bit byte from emulated memory and store it in
 *	a 32-bit word in host memory, with address writeback.
 *
 *  arg[0] = pointer to uint32_t in host memory of base address
 *  arg[1] = 32-bit offset
 *  arg[2] = pointer to uint32_t in host memory where to store the value
 */
X(load_byte_w_imm)
{
	unsigned char data[1];
	uint32_t addr = *((uint32_t *)ic->arg[0]) + ic->arg[1];
	if (!cpu->memory_rw(cpu, cpu->mem, addr, data, sizeof(data),
	    MEM_READ, CACHE_DATA)) {
		fatal("load failed: TODO\n");
		exit(1);
	}
	*((uint32_t *)ic->arg[2]) = data[0];
	*((uint32_t *)ic->arg[0]) = addr;
}
Y(load_byte_w_imm)


/*
 *  load_byte_wpost_imm:
 *	Load an 8-bit byte from emulated memory and store it in
 *	a 32-bit word in host memory, with address writeback AFTER the load.
 *
 *  arg[0] = pointer to uint32_t in host memory of base address
 *  arg[1] = 32-bit offset
 *  arg[2] = pointer to uint32_t in host memory where to store the value
 */
X(load_byte_wpost_imm)
{
	unsigned char data[1];
	uint32_t addr = *((uint32_t *)ic->arg[0]);
	if (!cpu->memory_rw(cpu, cpu->mem, addr, data, sizeof(data),
	    MEM_READ, CACHE_DATA)) {
		fatal("load failed: TODO\n");
		exit(1);
	}
	*((uint32_t *)ic->arg[2]) = data[0];
	*((uint32_t *)ic->arg[0]) = addr + ic->arg[1];
}
Y(load_byte_wpost_imm)


/*
 *  store_byte_imm:  Load a word from a 32-bit word in host memory, and store
 *                   the lowest 8 bits of that word at an emulated memory
 *                   address.
 *
 *  arg[0] = pointer to uint32_t in host memory of base address
 *  arg[1] = 32-bit offset
 *  arg[2] = pointer to uint32_t in host memory where to load the value from
 */
X(store_byte_imm)
{
	unsigned char data[1];
	uint32_t addr = *((uint32_t *)ic->arg[0]) + ic->arg[1];
	data[0] = *((uint32_t *)ic->arg[2]);
	if (!cpu->memory_rw(cpu, cpu->mem, addr, data, sizeof(data),
	    MEM_WRITE, CACHE_DATA)) {
		fatal("store failed: TODO\n");
		exit(1);
	}
}
Y(store_byte_imm)


/*
 *  store_byte_wpost_imm:
 *	Load a word from a 32-bit word in host memory, and store
 *	the lowest 8 bits of that word at an emulated memory address.
 *	Then add the immediate offset to the address, and write back
 *	to the first word.
 *
 *  arg[0] = pointer to uint32_t in host memory of base address
 *  arg[1] = 32-bit offset
 *  arg[2] = pointer to uint32_t in host memory where to load the value from
 */
X(store_byte_wpost_imm)
{
	unsigned char data[1];
	uint32_t addr = *((uint32_t *)ic->arg[0]);
	data[0] = *((uint32_t *)ic->arg[2]);
	if (!cpu->memory_rw(cpu, cpu->mem, addr, data, sizeof(data),
	    MEM_WRITE, CACHE_DATA)) {
		fatal("store failed: TODO\n");
		exit(1);
	}
	*((uint32_t *)ic->arg[0]) = addr + ic->arg[1];
}
Y(store_byte_wpost_imm)


/*
 *  load_word_imm:
 *	Load a 32-bit word from emulated memory and store it in
 *	a 32-bit word in host memory.
 *
 *  arg[0] = pointer to uint32_t in host memory of base address
 *  arg[1] = 32-bit offset
 *  arg[2] = pointer to uint32_t in host memory where to store the value
 */
X(load_word_imm)
{
	unsigned char data[sizeof(uint32_t)];
	uint32_t addr = *((uint32_t *)ic->arg[0]) + ic->arg[1];
	if (!cpu->memory_rw(cpu, cpu->mem, addr, data, sizeof(data),
	    MEM_READ, CACHE_DATA)) {
		fatal("load word failed: TODO\n");
		exit(1);
	}
	/*  TODO: Big endian  */
	*((uint32_t *)ic->arg[2]) = data[0] + (data[1] << 8) +
	    (data[2] << 16) + (data[3] << 24);
}
Y(load_word_imm)


/*
 *  load_word_w_imm:
 *	Load a 32-bit word from emulated memory and store it in
 *	a 32-bit word in host memory, with address writeback.
 *
 *  arg[0] = pointer to uint32_t in host memory of base address
 *  arg[1] = 32-bit offset
 *  arg[2] = pointer to uint32_t in host memory where to store the value
 */
X(load_word_w_imm)
{
	unsigned char data[sizeof(uint32_t)];
	uint32_t addr = *((uint32_t *)ic->arg[0]) + ic->arg[1];
	if (!cpu->memory_rw(cpu, cpu->mem, addr, data, sizeof(data),
	    MEM_READ, CACHE_DATA)) {
		fatal("load word failed: TODO\n");
		exit(1);
	}
	/*  TODO: Big endian  */
	*((uint32_t *)ic->arg[2]) = data[0] + (data[1] << 8) +
	    (data[2] << 16) + (data[3] << 24);
	*((uint32_t *)ic->arg[0]) = addr;
}
Y(load_word_w_imm)


/*
 *  store_word_imm:  Load a 32-bit word from host memory and store it
 *                   in emulated memory.
 *
 *  arg[0] = pointer to uint32_t in host memory of base address
 *  arg[1] = 32-bit offset
 *  arg[2] = pointer to uint32_t in host memory where to load the value from.
 */
X(store_word_imm)
{
	unsigned char data[sizeof(uint32_t)];
	uint32_t addr = *((uint32_t *)ic->arg[0]) + ic->arg[1];
	uint32_t x = *((uint32_t *)ic->arg[2]);
	/*  TODO: Big endian  */
	data[0] = x; data[1] = x >> 8; data[2] = x >> 16; data[3] = x >> 24;
	if (!cpu->memory_rw(cpu, cpu->mem, addr, data, sizeof(data),
	    MEM_WRITE, CACHE_DATA)) {
		fatal("store word failed: TODO\n");
		exit(1);
	}
}
Y(store_word_imm)


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
	cpu->cd.arm.r[ARM_PC] &= ~((IC_ENTRIES_PER_PAGE-1) << 2);
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
	cpu->cd.arm.r[ARM_PC] &= ~((IC_ENTRIES_PER_PAGE-1) << 2);
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

	c = a - b;
	cpu->cd.arm.flags &=
	    ~(ARM_FLAG_Z | ARM_FLAG_N | ARM_FLAG_V | ARM_FLAG_C);
	if (c == 0)
		cpu->cd.arm.flags |= ARM_FLAG_Z;
	if ((int32_t)c < 0) {
		cpu->cd.arm.flags |= ARM_FLAG_N;
		n = 1;
	} else
		n = 0;
	v = !n;
	if ((int32_t)a >= (int32_t)b)
		v = n;
	if (v)
		cpu->cd.arm.flags |= ARM_FLAG_V;
	if (a > b)
		cpu->cd.arm.flags |= ARM_FLAG_C;
}
Y(cmps)


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
	cpu->cd.arm.n_translated_instrs ++;
}


/*****************************************************************************/


X(to_be_translated)
{
	/*  Translate the instruction...  */
	arm_translate_instruction(cpu, ic);

	/*  ... and execute it:  */
	ic->f(cpu, ic);
}


X(end_of_page)
{
	printf("end_of_page()! pc=0x%08x\n", cpu->cd.arm.r[ARM_PC]);

	/*  Update the PC:  Offset 0, but then go to next page:  */
	cpu->cd.arm.r[ARM_PC] &= ~((IC_ENTRIES_PER_PAGE-1) << 2);
	cpu->cd.arm.r[ARM_PC] += (IC_ENTRIES_PER_PAGE << 2);
	cpu->pc = cpu->cd.arm.r[ARM_PC];

	/*  Find the new (physical) page:  */
	/*  TODO  */

	printf("TODO: end_of_page()! new pc=0x%08x\n", cpu->cd.arm.r[ARM_PC]);
	exit(1);
}


/*****************************************************************************/


/*
 *  arm_combine_instructions():
 *
 *  Combine two or more instructions, if possible, into a single function call.
 */
void arm_combine_instructions(struct cpu *cpu, struct arm_instr_call *ic)
{
	int n_back;
	n_back = (cpu->pc >> 2) & (IC_ENTRIES_PER_PAGE-1);

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
}


/*
 *  arm_translate_instruction():
 *
 *  Translate an instruction word into an arm_instr_call.
 */
void arm_translate_instruction(struct cpu *cpu, struct arm_instr_call *ic)
{
	uint32_t addr, low_pc, iword, imm;
	unsigned char ib[4];
	int condition_code, main_opcode, secondary_opcode, s_bit, r16, r12, r8;
	int p_bit, u_bit, b_bit, w_bit, l_bit;
	void (*samepage_function)(struct cpu *, struct arm_instr_call *);

	/*  Make sure that PC is in synch:  */
	low_pc = ((size_t)ic - (size_t)cpu->cd.arm.cur_ic_page)
	    / sizeof(struct arm_instr_call);
	cpu->cd.arm.r[ARM_PC] &= ~((IC_ENTRIES_PER_PAGE-1) << 2);
	cpu->cd.arm.r[ARM_PC] += (low_pc << 2);
	cpu->pc = cpu->cd.arm.r[ARM_PC];

	/*  Read the instruction word from memory:  */
	addr = cpu->pc & ~0x3;

	if (!cpu->memory_rw(cpu, cpu->mem, addr, &ib[0],
	    sizeof(ib), MEM_READ, CACHE_INSTRUCTION)) {
		fatal("arm_translate_instruction(): read failed: TODO\n");
		goto bad;
	}

	if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
		iword = ib[0] + (ib[1]<<8) + (ib[2]<<16) + (ib[3]<<24);
	else
		iword = ib[3] + (ib[2]<<8) + (ib[1]<<16) + (ib[0]<<24);

	/*  fatal("{ ARM translating pc=0x%08x iword=0x%08x }\n",
	    addr, iword);  */

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
			fatal("REGISTER FORM! TODO\n");
			goto bad;
		}
		imm = iword & 0xff;
		r8 <<= 1;
		while (r8-- > 0)
			imm = (imm >> 1) | ((imm & 1) << 31);
		switch (secondary_opcode) {
		case 0x2:				/*  SUB  */
		case 0x4:				/*  ADD  */
			if (s_bit) {
				fatal("sub s_bit: TODO\n");
				goto bad;
			}
			switch (secondary_opcode) {
			case 0x2:
				if (r12 == r16)
					ic->f = cond_instr(sub_self);
				else
					ic->f = cond_instr(sub);
				break;
			case 0x4:
				if (r12 == r16)
					ic->f = cond_instr(add_self);
				else
					ic->f = cond_instr(add);
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
		if (main_opcode < 6) {
			/*  Immediate:  */
			imm = iword & 0xfff;
			if (!u_bit)
				imm = (int32_t)0-imm;
			ic->arg[0] = (size_t)(&cpu->cd.arm.r[r16]);
			ic->arg[1] = (size_t)(imm);
			ic->arg[2] = (size_t)(&cpu->cd.arm.r[r12]);
		}
		if (main_opcode == 4 && b_bit) {
			/*  Post-index, immediate:  */
			if (w_bit) {
				fatal("load/store: T-bit\n");
				goto bad;
			}
			if (r16 == ARM_PC) {
				fatal("load/store writeback PC: error\n");
				goto bad;
			}
			if (l_bit)
				ic->f = cond_instr(load_byte_wpost_imm);
			else
				ic->f = cond_instr(store_byte_wpost_imm);
		} else if (main_opcode == 5) {
			/*  Pre-index, immediate:  */
			/*  ldr(b) Rd,[Rn,#imm]  */
			if (l_bit) {
				if (r12 == ARM_PC)
					fatal("WARNING: ldr to pc register?\n");
				if (w_bit) {
					ic->f = b_bit?
					    cond_instr(load_byte_w_imm) :
					    cond_instr(load_word_w_imm);
				} else {
					ic->f = b_bit?
					    cond_instr(load_byte_imm) :
					    cond_instr(load_word_imm);
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
				if (w_bit) {
					fatal("w bit store etc\n");
					goto bad;
				}
				if (r12 == ARM_PC) {
					fatal("TODO: store pc\n");
					goto bad;
				}
				ic->f = b_bit?
				    cond_instr(store_byte_imm) :
				    cond_instr(store_word_imm);
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
		/*  Branches are calculated as PC + 8 + offset:  */
		ic->arg[0] = (int32_t)(ic->arg[0] + 8);

		/*  Special case: branch within the same page:  */
		{
			uint32_t mask_within_page =
			    ((IC_ENTRIES_PER_PAGE-1) << 2) | 3;
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


	/*
	 *  If we end up here, then an instruction was translated. Now it is
	 *  time to check for combinations of instructions that can be
	 *  converted into a single function call.
	 */

	/*  Single-stepping doesn't work with combinations:  */
	if (single_step || cpu->machine->instruction_trace)
		return;

	arm_combine_instructions(cpu, ic);

	return;


bad:	/*
	 *  Nothing was translated. (Unimplemented or illegal instruction.)
	 */
	quiet_mode = 0;
	fatal("arm_translate_instruction(): TODO: "
	    "unimplemented ARM instruction:\n");
	arm_cpu_disassemble_instr(cpu, ib, 1, 0, 0);
	cpu->running = 0;
	cpu->dead = 1;
	cpu->cd.arm.running_translated = 0;
	*ic = nothing_call;
}

