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
 *  $Id: cpu_arm_instr.c,v 1.8 2005-06-25 13:57:12 debug Exp $
 *
 *  ARM instructions.
 *
 *  Individual functions should keep track of cpu->cd.arm.n_translated_instrs.
 *  (If no instruction was executed, then it should be decreased. If, say, 4
 *  instructions were combined into one function and executed, then it should
 *  be increased by 3.)
 */

#define X(n) void arm_instr_ ## n(struct cpu *cpu, \
	struct arm_instr_call *ic)


void arm_translate_instruction(struct cpu *cpu);


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
 */
X(b)
{
	/*  Branch to a different page:  */
	int low_pc;
	uint32_t old_pc;
	uint32_t mask_within_page = ((IC_ENTRIES_PER_PAGE-1) << 2) | 3;

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


/*
 *  b_samepage:  Branch (to within the same translated page)
 */
X(b_samepage)
{
	/*  A branch within the same page:  */
	cpu->cd.arm.next_ic = (struct arm_instr_call *) ic->arg[0];
}


/*
 *  bl:  Branch and Link (to a different translated page)
 *
 *  TODO: Implement this.
 *  TODO: How about function call trace?
 */
X(bl)
{
	fatal("bl different page: TODO\n");
	exit(1);
}


/*
 *  bl_samepage:  A branch + link within the same page
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


/*
 *  mov:  Set a 32-bit register to a 32-bit value.
 */
X(mov)
{
	/*  arg[0] = address to an uint32_t, arg[1] = the new value  */
	*((uint32_t *)ic->arg[0]) = ic->arg[1];
}


/*
 *  mov_2:  Double "mov".
 */
X(mov_2)
{
	/*  arg[0] = address to an uint32_t, arg[1] = the new value  */
	/*  Do this for two consecutive instructions.  */
	*((uint32_t *)ic[0].arg[0]) = ic[0].arg[1];
	*((uint32_t *)ic[1].arg[0]) = ic[1].arg[1];

	cpu->cd.arm.next_ic ++;
	cpu->cd.arm.n_translated_instrs ++;
}


/*****************************************************************************/


X(to_be_translated)
{
	int low_pc;

	/*  We didn't execute any translated instruction.  */
	cpu->cd.arm.n_translated_instrs --;

	/*  Go back to the instruction which wasn't translated:  */
	cpu->cd.arm.next_ic = ic;

	/*  Make sure that PC is in synch:  */
	low_pc = ((size_t)cpu->cd.arm.next_ic - (size_t)
	    cpu->cd.arm.cur_ic_page) / sizeof(struct arm_instr_call);
	cpu->cd.arm.r[ARM_PC] &= ~((IC_ENTRIES_PER_PAGE-1) << 2);
	cpu->cd.arm.r[ARM_PC] += (low_pc << 2);
	cpu->pc = cpu->cd.arm.r[ARM_PC];

	/*  Translate the instruction:  */
	arm_translate_instruction(cpu);

	/*  We can just as well execute the instruction right away:  */
	ic = cpu->cd.arm.next_ic ++; ic->f(cpu, ic);
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
 *  arm_translate_instruction():
 *
 *  Translate an instruction word into an arm_instr_call.
 */
void arm_translate_instruction(struct cpu *cpu)
{
	struct arm_instr_call *ic = cpu->cd.arm.next_ic;
	uint32_t addr;
	unsigned char ib[4];
	uint32_t iword;
	int condition_code, main_opcode, secondary_opcode, r16, r12, r8;
	int n_back;

	/*  Read the instruction word from memory:  */
	addr = cpu->pc & ~0x3;

	if (!cpu->memory_rw(cpu, cpu->mem, addr, &ib[0],
	    sizeof(ib), MEM_READ, CACHE_INSTRUCTION)) {
		fatal("arm_translate_instruction(): read failed: TODO\n");
		exit(1);
	}

	if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
		iword = ib[0] + (ib[1]<<8) + (ib[2]<<16) + (ib[3]<<24);
	else
		iword = ib[3] + (ib[2]<<8) + (ib[1]<<16) + (ib[0]<<24);

	fatal("{ ARM translating pc=0x%08x iword=0x%08x }\n", addr, iword);

	/*  The idea of taking bits 27..24 was found here:
	    http://armphetamine.sourceforge.net/oldinfo.html  */
	condition_code = iword >> 28;
	main_opcode = (iword >> 24) & 15;
	secondary_opcode = (iword >> 20) & 15;
	r16 = (iword >> 16) & 15;
	r12 = (iword >> 12) & 15;
	r8 = (iword >> 8) & 15;

	if (condition_code != 0xe) {
		fatal("TODO: ARM condition code 0x%x\n", condition_code);
		exit(1);
	}


	/*
	 *  Translate the instruction:
	 */

	switch (main_opcode) {

	case 0x3:
		switch (secondary_opcode) {
		case 0x0a:					/*  MOV  */
			if (r12 == ARM_PC) {
				fatal("TODO: mov used as branch\n");
				exit(1);
			} else {
				ic->f = instr(mov);
				ic->arg[0] = (size_t)(&cpu->cd.arm.r[r12]);
				/*  TODO: shifter argument  */
				ic->arg[1] = iword & 0xfff;
			}
			break;
		default:goto bad;
		}
		break;

	case 0xa:					/*  B: branch  */
	case 0xb:					/*  BL: branch+link  */
		if (main_opcode == 0x0a)
			ic->f = instr(b);
		else
			ic->f = instr(bl);
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
				if (main_opcode == 0x0a)
					ic->f = instr(b_samepage);
				else
					ic->f = instr(bl_samepage);
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

#if 1
	n_back = (addr >> 2) & (IC_ENTRIES_PER_PAGE-1);
	if (n_back >= 1) {
		/*  Double "mov":  */
		if (ic[0].f == instr(mov) && ic[-1].f == instr(mov)) {
			ic[-1].f = instr(mov_2);
			cpu->cd.arm.cur_physpage->flags |= ARM_COMBINATIONS;
		}
	}
#endif

	return;


bad:	/*
	 *  Nothing was translated. (Unimplemented or illegal instruction.)
	 */

	fatal("unimplemented ARM instruction 0x%08x\n", iword);
	cpu->running = 0;
	cpu->dead = 1;
	cpu->cd.arm.running_translated = 0;
	*ic = nothing_call;
}

