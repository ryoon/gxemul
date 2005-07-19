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
 *  $Id: cpu_alpha_instr.c,v 1.7 2005-07-19 00:04:41 debug Exp $
 *
 *  Alpha instructions.
 *
 *  Individual functions should keep track of cpu->cd.alpha.n_translated_instrs.
 *  (If no instruction was executed, then it should be decreased. If, say, 4
 *  instructions were combined into one function and executed, then it should
 *  be increased by 3.)
 */


/*
 *  Helper definitions:
 */

#define X(n) void alpha_instr_ ## n(struct cpu *cpu, \
	struct alpha_instr_call *ic)


/*  This is for marking a physical page as containing translated or
    combined instructions, respectively:  */
#define	translated	(cpu->cd.alpha.cur_physpage->flags |= TRANSLATIONS)
#define	combined	(cpu->cd.alpha.cur_physpage->flags |= COMBINATIONS)


/*
 *  nothing:  Do nothing.
 *
 *  The difference between this function and the "nop" instruction is that
 *  this function does not increase the program counter or the number of
 *  translated instructions.  It is used to "get out" of running in translated
 *  mode.
 *
 *  IMPORTANT NOTE: Do a   cpu->cd.alpha.running_translated = 0;
 *                  before setting cpu->cd.alpha.next_ic = &nothing_call;
 */
X(nothing)
{
	cpu->cd.alpha.n_translated_instrs --;
	cpu->cd.alpha.next_ic --;
}


static struct alpha_instr_call nothing_call = { instr(nothing), {0,0,0} };


/*****************************************************************************/


/*
 *  nop:  Do nothing.
 */
X(nop)
{
}


/*
 *  jsr:  Jump to SubRoutine
 *
 *  arg[0] = ptr to uint64_t where to store return PC
 *  arg[1] = ptr to uint64_t of new PC
 */
X(jsr)
{
	uint64_t low_pc;

	low_pc = ((size_t)ic - (size_t)
	    cpu->cd.alpha.cur_ic_page) / sizeof(struct alpha_instr_call);
	cpu->pc &= ~((ALPHA_IC_ENTRIES_PER_PAGE-1) << 2);
	cpu->pc += (low_pc << 2) + 4;

	*((int64_t *)ic->arg[0]) = cpu->pc;
	cpu->pc = *((int64_t *)ic->arg[1]);

	/*  Find the new physical page and update the translation pointers:  */
	alpha_pc_to_pointers(cpu);
}


/*
 *  jsr_0:  JSR, but don't store return PC.
 *
 *  arg[0] = ignored
 *  arg[1] = ptr to uint64_t of new PC
 */
X(jsr_0)
{
	uint64_t low_pc;

	low_pc = ((size_t)ic - (size_t)
	    cpu->cd.alpha.cur_ic_page) / sizeof(struct alpha_instr_call);
	cpu->pc &= ~((ALPHA_IC_ENTRIES_PER_PAGE-1) << 2);
	cpu->pc += (low_pc << 2) + 4;

	cpu->pc = *((int64_t *)ic->arg[1]);

	/*  Find the new physical page and update the translation pointers:  */
	alpha_pc_to_pointers(cpu);
}


/*
 *  br:  Branch (to a different translated page)
 *
 *  arg[0] = relative offset (as an int32_t)
 */
X(br)
{
	uint64_t low_pc;

	/*  Calculate new PC from this instruction + arg[0]  */
	low_pc = ((size_t)ic - (size_t)
	    cpu->cd.alpha.cur_ic_page) / sizeof(struct alpha_instr_call);
	cpu->pc &= ~((ALPHA_IC_ENTRIES_PER_PAGE-1) << 2);
	cpu->pc += (low_pc << 2);
	cpu->pc += (int32_t)ic->arg[0];

	/*  Find the new physical page and update the translation pointers:  */
	alpha_pc_to_pointers(cpu);
}


/*
 *  br:  Branch (to a different translated page), write return address
 *
 *  arg[0] = relative offset (as an int32_t)
 *  arg[1] = pointer to uint64_t where to write return address
 */
X(br_return)
{
	uint64_t low_pc;

	/*  Calculate new PC from this instruction + arg[0]  */
	low_pc = ((size_t)ic - (size_t)
	    cpu->cd.alpha.cur_ic_page) / sizeof(struct alpha_instr_call);
	cpu->pc &= ~((ALPHA_IC_ENTRIES_PER_PAGE-1) << 2);
	cpu->pc += (low_pc << 2);

	/*  ... but first, save away the return address:  */
	*((int64_t *)ic->arg[1]) = cpu->pc + 4;

	cpu->pc += (int32_t)ic->arg[0];

	/*  Find the new physical page and update the translation pointers:  */
	alpha_pc_to_pointers(cpu);
}


/*
 *  beq:  Branch (to a different translated page) if Equal
 *
 *  arg[0] = relative offset (as an int32_t)
 *  arg[1] = pointer to int64_t register
 */
X(beq)
{
	if (*((int64_t *)ic->arg[1]) == 0)
		instr(br)(cpu, ic);
}


/*
 *  bne:  Branch (to a different translated page) if Not Equal
 *
 *  arg[0] = relative offset (as an int32_t)
 *  arg[1] = pointer to int64_t register
 */
X(bne)
{
	if (*((int64_t *)ic->arg[1]) != 0)
		instr(br)(cpu, ic);
}


/*
 *  ble:  Branch (to a different translated page) if Less or Equal
 *
 *  arg[0] = relative offset (as an int32_t)
 *  arg[1] = pointer to int64_t register
 */
X(ble)
{
	if (*((int64_t *)ic->arg[1]) <= 0)
		instr(br)(cpu, ic);
}


/*
 *  bgt:  Branch (to a different translated page) if Greater Than
 *
 *  arg[0] = relative offset (as an int32_t)
 *  arg[1] = pointer to int64_t register
 */
X(bgt)
{
	if (*((int64_t *)ic->arg[1]) > 0)
		instr(br)(cpu, ic);
}


/*
 *  br_samepage:  Branch (to within the same translated page)
 *
 *  arg[0] = pointer to new alpha_instr_call
 */
X(br_samepage)
{
	cpu->cd.alpha.next_ic = (struct alpha_instr_call *) ic->arg[0];
}


/*
 *  br_return_samepage:  Branch (to within the same translated page),
 *                       and save return address
 *
 *  arg[0] = pointer to new alpha_instr_call
 *  arg[1] = pointer to uint64_t where to store return address
 */
X(br_return_samepage)
{
	uint64_t low_pc;

	low_pc = ((size_t)ic - (size_t)
	    cpu->cd.alpha.cur_ic_page) / sizeof(struct alpha_instr_call);
	cpu->pc &= ~((ALPHA_IC_ENTRIES_PER_PAGE-1) << 2);
	cpu->pc += (low_pc << 2);
	*((int64_t *)ic->arg[1]) = cpu->pc + 4;

	cpu->cd.alpha.next_ic = (struct alpha_instr_call *) ic->arg[0];
}


/*
 *  beq_samepage:  Branch (to within the same translated page) if Equal
 *
 *  arg[0] = pointer to new alpha_instr_call
 *  arg[1] = pointer to int64_t register
 */
X(beq_samepage)
{
	if (*((int64_t *)ic->arg[1]) == 0)
		instr(br_samepage)(cpu, ic);
}


/*
 *  bne_samepage:  Branch (to within the same translated page) if Not Equal
 *
 *  arg[0] = pointer to new alpha_instr_call
 *  arg[1] = pointer to int64_t register
 */
X(bne_samepage)
{
	if (*((int64_t *)ic->arg[1]) != 0)
		instr(br_samepage)(cpu, ic);
}


/*
 *  ble_samepage:  Branch (to within the same translated page) if Less or Equal
 *
 *  arg[0] = pointer to new alpha_instr_call
 *  arg[1] = pointer to int64_t register
 */
X(ble_samepage)
{
	if (*((int64_t *)ic->arg[1]) <= 0)
		instr(br_samepage)(cpu, ic);
}


/*
 *  bgt_samepage:  Branch (to within the same translated page) if Greater Than
 *
 *  arg[0] = pointer to new alpha_instr_call
 *  arg[1] = pointer to int64_t register
 */
X(bgt_samepage)
{
	if (*((int64_t *)ic->arg[1]) > 0)
		instr(br_samepage)(cpu, ic);
}


/*
 *  lda:  Load address.
 *
 *  arg[0] = pointer to destination uint64_t
 *  arg[1] = pointer to source uint64_t
 *  arg[2] = offset (possibly as an int32_t)
 */
X(lda)
{
	*((uint64_t *)ic->arg[0]) = *((uint64_t *)ic->arg[1])
	    + (int64_t)(int32_t)ic->arg[2];
}


/*
 *  lda_0:  Load address compared to the zero register.
 *
 *  arg[0] = pointer to destination uint64_t
 *  arg[1] = ignored
 *  arg[2] = offset (possibly as an int32_t)
 */
X(lda_0)
{
	*((uint64_t *)ic->arg[0]) = (int64_t)(int32_t)ic->arg[2];
}


#include "cpu_alpha_instr_inc.c"


/*
 *  sll_imm:  2-register SLL with imm
 *
 *  arg[0] = pointer to destination uint64_t
 *  arg[1] = pointer to source uint64_t
 *  arg[2] = immediate value
 */
X(sll_imm)
{
	*((uint64_t *)ic->arg[0]) = *((uint64_t *)ic->arg[1]) <<
	    (ic->arg[2] & 63);
}


/*
 *  cmple_imm:  CMPLE with immediate
 *
 *  arg[0] = pointer to destination uint64_t
 *  arg[1] = pointer to source uint64_t
 *  arg[2] = immediate value
 */
X(cmple_imm)
{
	if (*((int64_t *)ic->arg[1]) <= (int64_t)(int32_t)ic->arg[2])
		*((uint64_t *)ic->arg[0]) = 1;
	else
		*((uint64_t *)ic->arg[0]) = 0;
}


/*
 *  clear:  Clear a register.
 *
 *  arg[0] = pointer to an uint64_t to clear.
 */
X(clear)
{
	*((uint64_t *)ic->arg[0]) = 0;
}


/*****************************************************************************/


X(end_of_page)
{
	/*  Update the PC:  (offset 0, but on the next page)  */
	cpu->pc &= ~((ALPHA_IC_ENTRIES_PER_PAGE-1) << 2);
	cpu->pc += (ALPHA_IC_ENTRIES_PER_PAGE << 2);

	/*  Find the new physical page and update the translation pointers:  */
	alpha_pc_to_pointers(cpu);

	/*  end_of_page doesn't count as an executed instruction:  */
	cpu->cd.alpha.n_translated_instrs --;
}


/*****************************************************************************/


/*
 *  alpha_combine_instructions():
 *
 *  Combine two or more instructions, if possible, into a single function call.
 */
void alpha_combine_instructions(struct cpu *cpu, struct alpha_instr_call *ic,
	uint64_t addr)
{
	int n_back;
	n_back = (addr >> 2) & (ALPHA_IC_ENTRIES_PER_PAGE-1);

	if (n_back >= 1) {
	}

	/*  TODO: Combine forward as well  */
}


/*****************************************************************************/


/*
 *  alpha_instr_to_be_translated():
 *
 *  Translate an instruction word into an alpha_instr_call. ic is filled in with
 *  valid data for the translated instruction, or a "nothing" instruction if
 *  there was a translation failure. The newly translated instruction is then
 *  executed.
 */
X(to_be_translated)
{
	uint64_t addr, low_pc, iword;
	struct alpha_vph_page *vph_p;
	unsigned char *page;
	unsigned char ib[4];
	void (*samepage_function)(struct cpu *, struct alpha_instr_call *);
	int opcode, ra, rb, func, rc, imm, load;

	/*  Figure out the (virtual) address of the instruction:  */
	low_pc = ((size_t)ic - (size_t)cpu->cd.alpha.cur_ic_page)
	    / sizeof(struct alpha_instr_call);
	addr = cpu->pc & ~((ALPHA_IC_ENTRIES_PER_PAGE-1) << 2);
	addr += (low_pc << 2);
	addr &= ~0x3;

	/*  Read the instruction word from memory:  */
	if ((addr >> 39) == 0) {
		vph_p = cpu->cd.alpha.vph_table0[(addr >> 26) & 8191];
		page = vph_p->host_load[(addr >> 13) & 8191];
	} else if ((addr >> 39) == 0x1fffff8) {
		vph_p = cpu->cd.alpha.vph_table0_kernel[(addr >> 26) & 8191];
		page = vph_p->host_load[(addr >> 13) & 8191];
	} else
		page = NULL;

	if (page != NULL) {
		fatal("TRANSLATION HIT!\n");
		memcpy(ib, page + (addr & 0x1ffc), sizeof(ib));
	} else {
		fatal("TRANSLATION MISS!\n");
		if (!cpu->memory_rw(cpu, cpu->mem, addr, &ib[0],
		    sizeof(ib), MEM_READ, CACHE_INSTRUCTION)) {
			fatal("to_be_translated(): read failed: TODO\n");
			goto bad;
		}
	}

	iword = ib[0] + (ib[1]<<8) + (ib[2]<<16) + (ib[3]<<24);

	/*  fatal("{ Alpha: translating pc=0x%016llx iword=0x%08x }\n",
	    (long long)addr, (int)iword);  */

	opcode = (iword >> 26) & 63;
	ra = (iword >> 21) & 31;
	rb = (iword >> 16) & 31;
	func = (iword >> 5) & 0x7ff;
	rc = iword & 31;
	imm = iword & 0xffff;

	switch (opcode) {
	case 0x08:						/*  LDA  */
	case 0x09:						/*  LDAH  */
		if (ra == ALPHA_ZERO) {
			ic->f = instr(nop);
			break;
		}
		ic->f = instr(lda);
		ic->arg[0] = (size_t) &cpu->cd.alpha.r[ra];
		ic->arg[1] = (size_t) &cpu->cd.alpha.r[rb];
		if (rb == ALPHA_ZERO)
			ic->f = instr(lda_0);
		ic->arg[2] = (ssize_t)(int16_t)imm;
		if (opcode == 9)
			ic->arg[2] <<= 16;
		break;
	case 0x0b:						/*  LDQ_U  */
		if (ra == ALPHA_ZERO) {
			ic->f = instr(nop);
			break;
		}
		fatal("[ Alpha: LDQ_U to non-zero register ]\n");
		goto bad;
	case 0x0a:
	case 0x0c:
	case 0x0d:
	case 0x0e:
	case 0x28:
	case 0x29:
	case 0x2c:
	case 0x2d:
		load = 0;
		switch (opcode) {
		case 0x0a: ic->f = instr(ldbu); load = 1; break;
		case 0x0c: ic->f = instr(ldwu); load = 1; break;
		case 0x0d: ic->f = instr(stw); break;
		case 0x0e: ic->f = instr(stb); break;
		case 0x28: ic->f = instr(ldl); load = 1; break;
		case 0x29: ic->f = instr(ldq); load = 1; break;
		case 0x2c: ic->f = instr(stl); break;
		case 0x2d: ic->f = instr(stq); break;
		}
		if (load && ra == ALPHA_ZERO) {
			ic->f = instr(nop);
			break;
		}
		ic->arg[0] = (size_t) &cpu->cd.alpha.r[ra];
		ic->arg[1] = (size_t) &cpu->cd.alpha.r[rb];
		ic->arg[2] = (ssize_t)(int16_t)imm;
		break;
	case 0x10:
		if (rc == ALPHA_ZERO) {
			ic->f = instr(nop);
			break;
		}
		ic->arg[0] = (size_t) &cpu->cd.alpha.r[rc];
		ic->arg[1] = (size_t) &cpu->cd.alpha.r[ra];
		if (func & 0x80)
			ic->arg[2] = (size_t)((rb << 3) + (func >> 8));
		else
			ic->arg[2] = (size_t) &cpu->cd.alpha.r[rb];
		switch (func & 0xff) {
		case 0x00: ic->f = instr(addl); break;
		case 0x02: ic->f = instr(s4addl); break;
		case 0x09: ic->f = instr(subl); break;
		case 0x0b: ic->f = instr(s4subl); break;
		case 0x12: ic->f = instr(s8addl); break;
		case 0x1b: ic->f = instr(s8subl); break;
		case 0x20: ic->f = instr(addq); break;
		case 0x22: ic->f = instr(s4addq); break;
		case 0x29: ic->f = instr(subq); break;
		case 0x2b: ic->f = instr(s4subq); break;
		case 0x32: ic->f = instr(s8addq); break;
		case 0x3b: ic->f = instr(s8subq); break;

		case 0x80: ic->f = instr(addl_imm); break;
		case 0x82: ic->f = instr(s4addl_imm); break;
		case 0x89: ic->f = instr(subl_imm); break;
		case 0x8b: ic->f = instr(s4subl_imm); break;
		case 0x92: ic->f = instr(s8addl_imm); break;
		case 0x9b: ic->f = instr(s8subl_imm); break;
		case 0xa0: ic->f = instr(addq_imm); break;
		case 0xa2: ic->f = instr(s4addq_imm); break;
		case 0xa9: ic->f = instr(subq_imm); break;
		case 0xab: ic->f = instr(s4subq_imm); break;
		case 0xb2: ic->f = instr(s8addq_imm); break;
		case 0xbb: ic->f = instr(s8subq_imm); break;

		case 0xed:
			ic->f = instr(cmple_imm);
			break;
		default:fatal("[ Alpha: unimplemented function 0x%03x for"
			    " opcode 0x%02x ]\n", func, opcode);
			goto bad;
		}
		break;
	case 0x11:
		if (rc == ALPHA_ZERO) {
			ic->f = instr(nop);
			break;
		}
		ic->arg[0] = (size_t) &cpu->cd.alpha.r[rc];
		ic->arg[1] = (size_t) &cpu->cd.alpha.r[ra];
		if (func & 0x80)
			ic->arg[2] = (size_t)((rb << 3) + (func >> 8));
		else
			ic->arg[2] = (size_t) &cpu->cd.alpha.r[rb];
		switch (func & 0xff) {
		case 0x20:
			ic->f = instr(or);
			/*  TODO: Move if exactly one of ra or rb = 31  */
			if (ra == ALPHA_ZERO && rb == ALPHA_ZERO)
				ic->f = instr(clear);
			break;
		case 0x80:
			ic->f = instr(and_imm);
			break;
		default:fatal("[ Alpha: unimplemented function 0x%03x for"
			    " opcode 0x%02x ]\n", func, opcode);
			goto bad;
		}
		break;
	case 0x12:
		if (rc == ALPHA_ZERO) {
			ic->f = instr(nop);
			break;
		}
		ic->arg[0] = (size_t) &cpu->cd.alpha.r[rc];
		ic->arg[1] = (size_t) &cpu->cd.alpha.r[ra];
		if (func & 0x80)
			ic->arg[2] = (size_t)((rb << 3) + (func >> 8));
		else
			ic->arg[2] = (size_t) &cpu->cd.alpha.r[rb];
		switch (func & 0xff) {
		case 0xb9:
			ic->f = instr(sll_imm);
			break;
		default:fatal("[ Alpha: unimplemented function 0x%03x for"
			    " opcode 0x%02x ]\n", func, opcode);
			goto bad;
		}
		break;
	case 0x1a:
		switch ((iword >> 14) & 3) {
		case 0:	/*  JMP  */
		case 1:	/*  JSR  */
		case 2:	/*  RET  */
			ic->arg[0] = (size_t) &cpu->cd.alpha.r[ra];
			ic->arg[1] = (size_t) &cpu->cd.alpha.r[rb];
			ic->f = instr(jsr);
			if (ra == ALPHA_ZERO)
				ic->f = instr(jsr_0);
			break;
		default:fatal("[ Alpha: unimpl JSR type %i, ra=%i rb=%i ]\n",
			    ((iword >> 14) & 3), ra, rb);
			goto bad;
		}
		break;
	case 0x30:						/*  BR  */
	case 0x39:						/*  BEQ  */
	case 0x3b:						/*  BLE  */
	case 0x3d:						/*  BNE  */
	case 0x3f:						/*  BGT  */
		switch (opcode) {
		case 0x30:
			ic->f = instr(br);
			samepage_function = instr(br_samepage);
			if (ra != ALPHA_ZERO) {
				ic->f = instr(br_return);
				samepage_function = instr(br_return_samepage);
			}
			break;
		case 0x39:
			ic->f = instr(beq);
			samepage_function = instr(beq_samepage);
			break;
		case 0x3b:
			ic->f = instr(ble);
			samepage_function = instr(ble_samepage);
			break;
		case 0x3d:
			ic->f = instr(bne);
			samepage_function = instr(bne_samepage);
			break;
		case 0x3f:
			ic->f = instr(bgt);
			samepage_function = instr(bgt_samepage);
			break;
		}
		ic->arg[1] = (size_t) &cpu->cd.alpha.r[ra];
		ic->arg[0] = (iword & 0x001fffff) << 2;
		/*  Sign-extend:  */
		if (ic->arg[0] & 0x00400000)
			ic->arg[0] |= 0xffffffffff800000ULL;
		/*  Branches are calculated as PC + 4 + offset.  */
		ic->arg[0] = (size_t)(ic->arg[0] + 4);
		/*  Special case: branch within the same page:  */
		{
			uint64_t mask_within_page =
			    ((ALPHA_IC_ENTRIES_PER_PAGE-1) << 2) | 3;
			uint64_t old_pc = addr;
			uint64_t new_pc = old_pc + (int32_t)ic->arg[0];
			if ((old_pc & ~mask_within_page) ==
			    (new_pc & ~mask_within_page)) {
				ic->f = samepage_function;
				ic->arg[0] = (size_t) (
				    cpu->cd.alpha.cur_ic_page +
				    ((new_pc & mask_within_page) >> 2));
			}
		}
		break;
	default:fatal("[ UNIMPLEMENTED Alpha opcode 0x%x ]\n", opcode);
		goto bad;
	}

	translated;

	/*
	 *  If we end up here, then an instruction was translated. Now it is
	 *  time to check for combinations of instructions that can be
	 *  converted into a single function call.
	 */

	/*  Single-stepping doesn't work with combinations:  */
	if (!single_step && !cpu->machine->instruction_trace)
		alpha_combine_instructions(cpu, ic, addr);

	/*  ... and finally execute the translated instruction:  */
	ic->f(cpu, ic);

	return;


bad:	/*
	 *  Nothing was translated. (Unimplemented or illegal instruction.)
	 */
	quiet_mode = 0;
	fatal("to_be_translated(): TODO: unimplemented Alpha instruction:\n");
	alpha_cpu_disassemble_instr(cpu, ib, 1, 0, 0);
	cpu->running = 0;
	cpu->dead = 1;
	cpu->cd.alpha.running_translated = 0;
	ic = cpu->cd.alpha.next_ic = &nothing_call;
	cpu->cd.alpha.next_ic ++;

	/*  Execute the "nothing" instruction:  */
	ic->f(cpu, ic);
}

