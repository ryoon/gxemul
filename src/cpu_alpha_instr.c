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
 *  $Id: cpu_alpha_instr.c,v 1.32 2005-08-06 19:32:43 debug Exp $
 *
 *  Alpha instructions.
 *
 *  Individual functions should keep track of cpu->n_translated_instrs.
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
 *  IMPORTANT NOTE: Do a   cpu->running_translated = 0;
 *                  before setting cpu->cd.alpha.next_ic = &nothing_call;
 */
X(nothing)
{
	cpu->n_translated_instrs --;
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
 *  call_pal:  PALcode call
 *
 *  arg[0] = pal nr
 */
X(call_pal)
{
	uint64_t low_pc;

	/*  Synchronize PC first:  */
	low_pc = ((size_t)ic - (size_t)
	    cpu->cd.alpha.cur_ic_page) / sizeof(struct alpha_instr_call);
	cpu->pc &= ~((ALPHA_IC_ENTRIES_PER_PAGE-1) << 2);
	cpu->pc += (low_pc << 2) + 4;

	alpha_palcode(cpu, ic->arg[0]);

	if (!cpu->running) {
		cpu->running_translated = 0;
		cpu->n_translated_instrs --;
		cpu->cd.alpha.next_ic = &nothing_call;
	} else {
		/*  PC might have been changed by the palcode call.  */
		/*  Find the new physical page and update the translation
		    pointers:  */
		alpha_pc_to_pointers(cpu);
	}
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

	/*  TODO: just set cpu->cd.alpha.next_ic if it's the same page  */

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

	/*  TODO: just set cpu->cd.alpha.next_ic if it's the same page  */

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
 *  blbs:  Branch (to a different translated page) if Low Bit Set
 *
 *  arg[0] = relative offset (as an int32_t)
 *  arg[1] = pointer to int64_t register
 */
X(blbs)
{
	if (*((int64_t *)ic->arg[1]) & 1)
		instr(br)(cpu, ic);
}


/*
 *  blbc:  Branch (to a different translated page) if Low Bit Clear
 *
 *  arg[0] = relative offset (as an int32_t)
 *  arg[1] = pointer to int64_t register
 */
X(blbc)
{
	if (!(*((int64_t *)ic->arg[1]) & 1))
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
 *  blt:  Branch (to a different translated page) if Less Than
 *
 *  arg[0] = relative offset (as an int32_t)
 *  arg[1] = pointer to int64_t register
 */
X(blt)
{
	if (*((int64_t *)ic->arg[1]) < 0)
		instr(br)(cpu, ic);
}


/*
 *  bge:  Branch (to a different translated page) if Greater or Equal
 *
 *  arg[0] = relative offset (as an int32_t)
 *  arg[1] = pointer to int64_t register
 */
X(bge)
{
	if (*((int64_t *)ic->arg[1]) >= 0)
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
 *  blbs_samepage:  Branch (to within the same translated page) if Low Bit Set
 *
 *  arg[0] = pointer to new alpha_instr_call
 *  arg[1] = pointer to int64_t register
 */
X(blbs_samepage)
{
	if (*((int64_t *)ic->arg[1]) & 1)
		instr(br_samepage)(cpu, ic);
}


/*
 *  blbc_samepage:  Branch (to within the same translated page) if Low Bit Clear
 *
 *  arg[0] = pointer to new alpha_instr_call
 *  arg[1] = pointer to int64_t register
 */
X(blbc_samepage)
{
	if (!(*((int64_t *)ic->arg[1]) & 1))
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
 *  blt_samepage:  Branch (to within the same translated page) if Less Than
 *
 *  arg[0] = pointer to new alpha_instr_call
 *  arg[1] = pointer to int64_t register
 */
X(blt_samepage)
{
	if (*((int64_t *)ic->arg[1]) < 0)
		instr(br_samepage)(cpu, ic);
}


/*
 *  bge_samepage:  Branch (to within the same translated page) if Greater or Equal
 *
 *  arg[0] = pointer to new alpha_instr_call
 *  arg[1] = pointer to int64_t register
 */
X(bge_samepage)
{
	if (*((int64_t *)ic->arg[1]) >= 0)
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
 *  umulh:  Unsigned Multiply 64x64 => 128. Store high part in dest reg.
 *
 *  arg[0] = pointer to destination uint64_t
 *  arg[1] = pointer to source uint64_t
 *  arg[2] = pointer to source uint64_t
 */
X(umulh)
{
	uint64_t reshi = 0, reslo = 0;
	uint64_t s1 = *((uint64_t *)ic->arg[1]), s2 = *((uint64_t *)ic->arg[2]);
	int i, bit;

	for (i=0; i<64; i++) {
		bit = (s1 & 0x8000000000000000ULL)? 1 : 0;
		s1 <<= 1;

		/*  If bit in s1 set, then add s2 to reshi/lo:  */
		if (bit) {
			uint64_t old_reslo = reslo;
			reslo += s2;
			if (reslo < old_reslo)
				reshi ++;
		}

		if (i != 63) {
			reshi <<= 1;
			reshi += (reslo & 0x8000000000000000ULL? 1 : 0);
			reslo <<= 1;
		}
	}

	*((uint64_t *)ic->arg[0]) = reshi;
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


#include "tmp_alpha_misc.c"


/*****************************************************************************/


X(end_of_page)
{
	/*  Update the PC:  (offset 0, but on the next page)  */
	cpu->pc &= ~((ALPHA_IC_ENTRIES_PER_PAGE-1) << 2);
	cpu->pc += (ALPHA_IC_ENTRIES_PER_PAGE << 2);

	/*  Find the new physical page and update the translation pointers:  */
	alpha_pc_to_pointers(cpu);

	/*  end_of_page doesn't count as an executed instruction:  */
	cpu->n_translated_instrs --;
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
	uint64_t addr, low_pc;
	uint32_t iword;
	struct alpha_vph_page *vph_p;
	unsigned char *page;
	unsigned char ib[4];
	void (*samepage_function)(struct cpu *, struct alpha_instr_call *);
	int i, opcode, ra, rb, func, rc, imm, load, loadstore_type, fp;

	/*  Figure out the (virtual) address of the instruction:  */
	low_pc = ((size_t)ic - (size_t)cpu->cd.alpha.cur_ic_page)
	    / sizeof(struct alpha_instr_call);
	addr = cpu->pc & ~((ALPHA_IC_ENTRIES_PER_PAGE-1) << 2);
	addr += (low_pc << 2);
	addr &= ~0x3;
	cpu->pc = addr;

	/*  Read the instruction word from memory:  */
	if ((addr >> ALPHA_TOPSHIFT) == 0) {
		vph_p = cpu->cd.alpha.vph_table0[(addr >>
		    ALPHA_LEVEL0_SHIFT) & 8191];
		page = vph_p->host_load[(addr >> ALPHA_LEVEL1_SHIFT) & 8191];
	} else if ((addr >> ALPHA_TOPSHIFT) == ALPHA_TOP_KERNEL) {
		vph_p = cpu->cd.alpha.vph_table0_kernel[(addr >>
		    ALPHA_LEVEL0_SHIFT) & 8191];
		page = vph_p->host_load[(addr >> ALPHA_LEVEL1_SHIFT) & 8191];
	} else
		page = NULL;

	if (page != NULL) {
		/*  fatal("TRANSLATION HIT!\n");  */
		memcpy(ib, page + (addr & 8191), sizeof(ib));
	} else {
		/*  fatal("TRANSLATION MISS!\n");  */
		if (!cpu->memory_rw(cpu, cpu->mem, addr, &ib[0],
		    sizeof(ib), MEM_READ, CACHE_INSTRUCTION)) {
			fatal("to_be_translated(): read failed: TODO\n");
			goto bad;
		}
	}

#ifdef HOST_LITTLE_ENDIAN
	iword = *((uint32_t *)&ib[0]);
#else
	iword = ib[0] + (ib[1]<<8) + (ib[2]<<16) + (ib[3]<<24);
#endif

	/*  fatal("{ Alpha: translating pc=0x%016llx iword=0x%08x }\n",
	    (long long)addr, (int)iword);  */


#define DYNTRANS_TO_BE_TRANSLATED_HEAD
#include "cpu_dyntrans.c"
#undef	DYNTRANS_TO_BE_TRANSLATED_HEAD


	opcode = (iword >> 26) & 63;
	ra = (iword >> 21) & 31;
	rb = (iword >> 16) & 31;
	func = (iword >> 5) & 0x7ff;
	rc = iword & 31;
	imm = iword & 0xffff;

	switch (opcode) {
	case 0x00:						/*  CALL_PAL  */
		ic->f = instr(call_pal);
		ic->arg[0] = (size_t) (iword & 0x3ffffff);
		break;
	case 0x08:						/*  LDA  */
	case 0x09:						/*  LDAH  */
		if (ra == ALPHA_ZERO) {
			ic->f = instr(nop);
			break;
		}
		/*  TODO: A special case which is common is to add or subtract
		    a small offset from sp.  */
		ic->f = instr(lda);
		ic->arg[0] = (size_t) &cpu->cd.alpha.r[ra];
		ic->arg[1] = (size_t) &cpu->cd.alpha.r[rb];
		if (rb == ALPHA_ZERO)
			ic->f = instr(lda_0);
		ic->arg[2] = (ssize_t)(int16_t)imm;
		if (opcode == 0x09)
			ic->arg[2] <<= 16;
		break;
	case 0x0b:						/*  LDQ_U  */
	case 0x0f:						/*  STQ_U  */
		if (ra == ALPHA_ZERO && opcode == 0x0b) {
			ic->f = instr(nop);
			break;
		}
		if (opcode == 0x0b)
			ic->f = instr(ldq_u);
		else
			ic->f = instr(stq_u);
		ic->arg[0] = (size_t) &cpu->cd.alpha.r[ra];
		ic->arg[1] = (size_t) &cpu->cd.alpha.r[rb];
		ic->arg[2] = (ssize_t)(int16_t)imm;
		break;
	case 0x0a:
	case 0x0c:
	case 0x0d:
	case 0x0e:
	case 0x22:
	case 0x23:
	case 0x26:
	case 0x27:
	case 0x28:
	case 0x29:
	case 0x2c:
	case 0x2d:
		loadstore_type = 0; fp = 0; load = 0;
		switch (opcode) {
		case 0x0a: loadstore_type = 0; load = 1; break;	/*  ldbu  */
		case 0x0c: loadstore_type = 1; load = 1; break;	/*  ldwu  */
		case 0x0d: loadstore_type = 1; break;		/*  stw  */
		case 0x0e: loadstore_type = 0; break;		/*  stb  */
		case 0x22: loadstore_type = 2; load = 1; fp = 1; break; /*lds*/
		case 0x23: loadstore_type = 3; load = 1; fp = 1; break; /*ldt*/
		case 0x26: loadstore_type = 2; fp = 1; break;	/*  sts  */
		case 0x27: loadstore_type = 3; fp = 1; break;	/*  stt  */
		case 0x28: loadstore_type = 2; load = 1; break;	/*  ldl  */
		case 0x29: loadstore_type = 3; load = 1; break;	/*  ldq  */
		case 0x2c: loadstore_type = 2; break;		/*  stl  */
		case 0x2d: loadstore_type = 3; break;		/*  stq  */
		}
		ic->f = alpha_loadstore[8*load + (imm==0? 4 : 0)
		    + loadstore_type
		    + (cpu->machine->dyntrans_alignment_check? 16:0)];
		if (load && ra == ALPHA_ZERO) {
			ic->f = instr(nop);
			break;
		}
		if (fp)
			ic->arg[0] = (size_t) &cpu->cd.alpha.f[ra];
		else
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
		case 0x1d: ic->f = instr(cmpult); break;
		case 0x20: ic->f = instr(addq); break;
		case 0x22: ic->f = instr(s4addq); break;
		case 0x29: ic->f = instr(subq); break;
		case 0x2b: ic->f = instr(s4subq); break;
		case 0x2d: ic->f = instr(cmpeq); break;
		case 0x32: ic->f = instr(s8addq); break;
		case 0x3b: ic->f = instr(s8subq); break;
		case 0x3d: ic->f = instr(cmpule); break;
		case 0x4d: ic->f = instr(cmplt); break;
		case 0x6d: ic->f = instr(cmple); break;

		case 0x80: ic->f = instr(addl_imm); break;
		case 0x82: ic->f = instr(s4addl_imm); break;
		case 0x89: ic->f = instr(subl_imm); break;
		case 0x8b: ic->f = instr(s4subl_imm); break;
		case 0x92: ic->f = instr(s8addl_imm); break;
		case 0x9b: ic->f = instr(s8subl_imm); break;
		case 0x9d: ic->f = instr(cmpult_imm); break;
		case 0xa0: ic->f = instr(addq_imm); break;
		case 0xa2: ic->f = instr(s4addq_imm); break;
		case 0xa9: ic->f = instr(subq_imm); break;
		case 0xab: ic->f = instr(s4subq_imm); break;
		case 0xad: ic->f = instr(cmpeq_imm); break;
		case 0xb2: ic->f = instr(s8addq_imm); break;
		case 0xbb: ic->f = instr(s8subq_imm); break;
		case 0xbd: ic->f = instr(cmpule_imm); break;
		case 0xcd: ic->f = instr(cmplt_imm); break;
		case 0xed: ic->f = instr(cmple_imm); break;

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
		case 0x00: ic->f = instr(and); break;
		case 0x08: ic->f = instr(andnot); break;
		case 0x14: ic->f = instr(cmovlbs); break;
		case 0x16: ic->f = instr(cmovlbc); break;
		case 0x20: ic->f = instr(or);
			   if (ra == ALPHA_ZERO || rb == ALPHA_ZERO) {
				if (ra == ALPHA_ZERO)
					ra = rb;
				ic->f = alpha_mov_r_r[ra + rc*32];
			   }
			   break;
		case 0x24: ic->f = instr(cmoveq); break;
		case 0x26: ic->f = instr(cmovne); break;
		case 0x28: ic->f = instr(ornot); break;
		case 0x40: ic->f = instr(xor); break;
		case 0x44: ic->f = instr(cmovlt); break;
		case 0x46: ic->f = instr(cmovge); break;
		case 0x48: ic->f = instr(xornot); break;
		case 0x64: ic->f = instr(cmovle); break;
		case 0x66: ic->f = instr(cmovgt); break;
		case 0x80: ic->f = instr(and_imm); break;
		case 0x88: ic->f = instr(andnot_imm); break;
		case 0x94: ic->f = instr(cmovlbs_imm); break;
		case 0x96: ic->f = instr(cmovlbc_imm); break;
		case 0xa0: ic->f = instr(or_imm); break;
		case 0xa4: ic->f = instr(cmoveq_imm); break;
		case 0xa6: ic->f = instr(cmovne_imm); break;
		case 0xa8: ic->f = instr(ornot_imm); break;
		case 0xc0: ic->f = instr(xor_imm); break;
		case 0xc4: ic->f = instr(cmovlt_imm); break;
		case 0xc6: ic->f = instr(cmovge_imm); break;
		case 0xc8: ic->f = instr(xornot_imm); break;
		case 0xe4: ic->f = instr(cmovle_imm); break;
		case 0xe6: ic->f = instr(cmovgt_imm); break;
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
		case 0x02: ic->f = instr(mskbl); break;
		case 0x06: ic->f = instr(extbl); break;
		case 0x0b: ic->f = instr(insbl); break;
		case 0x12: ic->f = instr(mskwl); break;
		case 0x16: ic->f = instr(extwl); break;
		case 0x1b: ic->f = instr(inswl); break;
		case 0x22: ic->f = instr(mskll); break;
		case 0x26: ic->f = instr(extll); break;
		case 0x2b: ic->f = instr(insll); break;
		case 0x30: ic->f = instr(zap); break;
		case 0x31: ic->f = instr(zapnot); break;
		case 0x32: ic->f = instr(mskql); break;
		case 0x34: ic->f = instr(srl); break;
		case 0x36: ic->f = instr(extql); break;
		case 0x39: ic->f = instr(sll); break;
		case 0x3b: ic->f = instr(insql); break;
		case 0x3c: ic->f = instr(sra); break;
		case 0x52: ic->f = instr(mskwh); break;
		case 0x57: ic->f = instr(inswh); break;
		case 0x5a: ic->f = instr(extwh); break;
		case 0x62: ic->f = instr(msklh); break;
		case 0x67: ic->f = instr(inslh); break;
		case 0x6a: ic->f = instr(extlh); break;
		case 0x72: ic->f = instr(mskqh); break;
		case 0x77: ic->f = instr(insqh); break;
		case 0x7a: ic->f = instr(extqh); break;
		case 0x82: ic->f = instr(mskbl_imm); break;
		case 0x86: ic->f = instr(extbl_imm); break;
		case 0x8b: ic->f = instr(insbl_imm); break;
		case 0x92: ic->f = instr(mskwl_imm); break;
		case 0x96: ic->f = instr(extwl_imm); break;
		case 0x9b: ic->f = instr(inswl_imm); break;
		case 0xa2: ic->f = instr(mskll_imm); break;
		case 0xa6: ic->f = instr(extll_imm); break;
		case 0xab: ic->f = instr(insll_imm); break;
		case 0xb0: ic->f = instr(zap_imm); break;
		case 0xb1: ic->f = instr(zapnot_imm); break;
		case 0xb2: ic->f = instr(mskql_imm); break;
		case 0xb4: ic->f = instr(srl_imm); break;
		case 0xb6: ic->f = instr(extql_imm); break;
		case 0xb9: ic->f = instr(sll_imm); break;
		case 0xbb: ic->f = instr(insql_imm); break;
		case 0xbc: ic->f = instr(sra_imm); break;
		case 0xd2: ic->f = instr(mskwh_imm); break;
		case 0xd7: ic->f = instr(inswh_imm); break;
		case 0xda: ic->f = instr(extwh_imm); break;
		case 0xe2: ic->f = instr(msklh_imm); break;
		case 0xe7: ic->f = instr(inslh_imm); break;
		case 0xea: ic->f = instr(extlh_imm); break;
		case 0xf2: ic->f = instr(mskqh_imm); break;
		case 0xf7: ic->f = instr(insqh_imm); break;
		case 0xfa: ic->f = instr(extqh_imm); break;
		default:fatal("[ Alpha: unimplemented function 0x%03x for"
			    " opcode 0x%02x ]\n", func, opcode);
			goto bad;
		}
		break;
	case 0x13:
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
		case 0x30: ic->f = instr(umulh); break;
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
	case 0x34:						/*  BSR  */
	case 0x38:						/*  BLBC  */
	case 0x39:						/*  BEQ  */
	case 0x3a:						/*  BLT  */
	case 0x3b:						/*  BLE  */
	case 0x3c:						/*  BLBS  */
	case 0x3d:						/*  BNE  */
	case 0x3e:						/*  BGE  */
	case 0x3f:						/*  BGT  */
		/*  To avoid a GCC warning:  */
		samepage_function = instr(nop);
		switch (opcode) {
		case 0x30:
		case 0x34:
			ic->f = instr(br);
			samepage_function = instr(br_samepage);
			if (ra != ALPHA_ZERO) {
				ic->f = instr(br_return);
				samepage_function = instr(br_return_samepage);
			}
			break;
		case 0x38:
			ic->f = instr(blbc);
			samepage_function = instr(blbc_samepage);
			break;
		case 0x39:
			ic->f = instr(beq);
			samepage_function = instr(beq_samepage);
			break;
		case 0x3a:
			ic->f = instr(blt);
			samepage_function = instr(blt_samepage);
			break;
		case 0x3b:
			ic->f = instr(ble);
			samepage_function = instr(ble_samepage);
			break;
		case 0x3c:
			ic->f = instr(blbs);
			samepage_function = instr(blbs_samepage);
			break;
		case 0x3d:
			ic->f = instr(bne);
			samepage_function = instr(bne_samepage);
			break;
		case 0x3e:
			ic->f = instr(bge);
			samepage_function = instr(bge_samepage);
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


#define DYNTRANS_TO_BE_TRANSLATED_TAIL
#include "cpu_dyntrans.c"
#undef	DYNTRANS_TO_BE_TRANSLATED_TAIL
}

