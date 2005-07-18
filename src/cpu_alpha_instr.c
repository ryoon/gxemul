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
 *  $Id: cpu_alpha_instr.c,v 1.5 2005-07-18 11:28:32 debug Exp $
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


void alpha_generic_stb(struct cpu *cpu, struct alpha_instr_call *ic)
{
	unsigned char data[1];
	uint64_t addr = *((uint64_t *)ic->arg[1]);

	addr += (int32_t)ic->arg[2];
	data[0] = *((uint64_t *)ic->arg[0]);

	if (!cpu->memory_rw(cpu, cpu->mem, addr, data, sizeof(data),
	    MEM_WRITE, CACHE_DATA)) {
		fatal("store failed: TODO\n");
		exit(1);
	}
}


void alpha_generic_ldl(struct cpu *cpu, struct alpha_instr_call *ic)
{
	unsigned char data[4];
	uint64_t addr = *((uint64_t *)ic->arg[1]) + (int32_t)ic->arg[2];
	int32_t data_x;

	if (!cpu->memory_rw(cpu, cpu->mem, addr, data, sizeof(data),
	    MEM_READ, CACHE_DATA)) {
		fatal("load failed: TODO\n");
		exit(1);
	}
	data_x = data[0];
	data_x += (data[1] << 8);
	data_x += (data[2] << 16);
	data_x += (data[3] << 24);
	*((uint64_t *)ic->arg[0]) = data_x;
}


void alpha_generic_ldq(struct cpu *cpu, struct alpha_instr_call *ic)
{
	unsigned char data[8];
	uint64_t addr = *((uint64_t *)ic->arg[1]) + (int32_t)ic->arg[2];
	int64_t data_x;

	if (!cpu->memory_rw(cpu, cpu->mem, addr, data, sizeof(data),
	    MEM_READ, CACHE_DATA)) {
		fatal("load failed: TODO\n");
		exit(1);
	}
	data_x = data[0];
	data_x += (data[1] << 8);
	data_x += (data[2] << 16);
	data_x += (data[3] << 24);
	data_x += ((int64_t)data[4] << 32);
	data_x += ((int64_t)data[5] << 40);
	data_x += ((int64_t)data[6] << 48);
	data_x += ((int64_t)data[7] << 56);
	*((uint64_t *)ic->arg[0]) = data_x;
}


void alpha_generic_stq(struct cpu *cpu, struct alpha_instr_call *ic)
{
	unsigned char data[8];
	uint64_t addr = *((uint64_t *)ic->arg[1]);
	uint64_t data_x = *((uint64_t *)ic->arg[0]);

	addr += (int32_t)ic->arg[2];
	data[0] = data_x;
	data[1] = data_x >> 8;
	data[2] = data_x >> 16;
	data[3] = data_x >> 24;
	data[4] = data_x >> 32;
	data[5] = data_x >> 40;
	data[6] = data_x >> 48;
	data[7] = data_x >> 56;
	if (!cpu->memory_rw(cpu, cpu->mem, addr, data, sizeof(data),
	    MEM_WRITE, CACHE_DATA)) {
		fatal("store failed: TODO\n");
		exit(1);
	}
}


/*
 *  stb:  Store byte.
 *
 *  arg[0] = pointer to the register to store (uint64_t)
 *  arg[1] = pointer to the base register (uint64_t)
 *  arg[2] = offset (possibly as an int32_t)
 */
X(stb)
{
	int first, a, b, c;
	uint64_t addr = *((uint64_t *)ic->arg[1]);
	addr += (int64_t)ic->arg[2];

	first = addr >> 39;
	a = (addr >> 26) & 8191;
	b = (addr >> 13) & 8191;
	c = addr & 8191;

	if (first == 0) {
		struct alpha_vph_page *vph_p;
		unsigned char *page;
		vph_p = cpu->cd.alpha.vph_table0[a];
		page = vph_p->host_load[b];
		if (page != NULL)
			page[c] = *((uint64_t *)ic->arg[0]);
		else
			alpha_generic_stb(cpu, ic);
	} else
		alpha_generic_stb(cpu, ic);
}


/*
 *  stq:  Store quad.
 *
 *  arg[0] = pointer to the register to store (uint64_t)
 *  arg[1] = pointer to the base register (uint64_t)
 *  arg[2] = offset (possibly as an int32_t)
 */
X(stq)
{
	int first, a, b, c;
	uint64_t addr = *((uint64_t *)ic->arg[1]);
	addr += (int32_t)ic->arg[2];

	first = addr >> 39;
	a = (addr >> 26) & 8191;
	b = (addr >> 13) & 8191;
	c = addr & 8191;

	if (first == 0) {
		struct alpha_vph_page *vph_p;
		unsigned char *page;
		vph_p = cpu->cd.alpha.vph_table0[a];
		page = vph_p->host_load[b];
		if (page != NULL) {
			uint64_t d = *((uint64_t *)ic->arg[0]);
			page[c] = d;
			page[c+1] = d >> 8;
			page[c+2] = d >> 16;
			page[c+3] = d >> 24;
			page[c+4] = d >> 32;
			page[c+5] = d >> 40;
			page[c+6] = d >> 48;
			page[c+7] = d >> 56;
		} else
			alpha_generic_stq(cpu, ic);
	} else
		alpha_generic_stq(cpu, ic);
}


/*
 *  ldl:  Load long.
 *
 *  arg[0] = pointer to the register to load to (uint64_t)
 *  arg[1] = pointer to the base register (uint64_t)
 *  arg[2] = offset (possibly as an int32_t)
 */
X(ldl)
{
	int first, a, b, c;
	uint64_t addr = *((uint64_t *)ic->arg[1]);
	addr += (int32_t)ic->arg[2];

	first = addr >> 39;
	a = (addr >> 26) & 8191;
	b = (addr >> 13) & 8191;
	c = addr & 8191;

	if (first == 0) {
		struct alpha_vph_page *vph_p;
		unsigned char *page;
		vph_p = cpu->cd.alpha.vph_table0[a];
		page = vph_p->host_load[b];
		if (page != NULL) {
			int32_t d;
			d = page[c];
			d += (page[c+1] << 8);
			d += (page[c+2] << 16);
			d += (page[c+3] << 24);
			*((uint64_t *)ic->arg[0]) = d;
		} else
			alpha_generic_ldl(cpu, ic);
	} else
		alpha_generic_ldl(cpu, ic);
}


/*
 *  ldq:  Load quad.
 *
 *  arg[0] = pointer to the register to load to (uint64_t)
 *  arg[1] = pointer to the base register (uint64_t)
 *  arg[2] = offset (possibly as an int32_t)
 */
X(ldq)
{
	int first, a, b, c;
	uint64_t addr = *((uint64_t *)ic->arg[1]);
	addr += (int32_t)ic->arg[2];

	first = addr >> 39;
	a = (addr >> 26) & 8191;
	b = (addr >> 13) & 8191;
	c = addr & 8191;

	if (first == 0) {
		struct alpha_vph_page *vph_p;
		unsigned char *page;
		vph_p = cpu->cd.alpha.vph_table0[a];
		page = vph_p->host_load[b];
		if (page != NULL) {
			int64_t d;
			d = page[c];
			d += (page[c+1] << 8);
			d += (page[c+2] << 16);
			d += (page[c+3] << 24);
			d += ((int64_t)page[c+4] << 32);
			d += ((int64_t)page[c+5] << 40);
			d += ((int64_t)page[c+6] << 48);
			d += ((int64_t)page[c+7] << 56);
			*((uint64_t *)ic->arg[0]) = d;
		} else
			alpha_generic_ldq(cpu, ic);
	} else
		alpha_generic_ldq(cpu, ic);
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


/*
 *  or:  3-register OR
 *
 *  arg[0] = pointer to destination uint64_t
 *  arg[1] = pointer to source uint64_t nr 1
 *  arg[2] = pointer to source uint64_t nr 2
 */
X(or)
{
	*((uint64_t *)ic->arg[0]) =
	    *((uint64_t *)ic->arg[1]) | *((uint64_t *)ic->arg[2]);
}


/*
 *  and_imm:  2-register AND with imm
 *
 *  arg[0] = pointer to destination uint64_t
 *  arg[1] = pointer to source uint64_t
 *  arg[2] = immediate value
 */
X(and_imm)
{
	*((uint64_t *)ic->arg[0]) = *((uint64_t *)ic->arg[1]) & ic->arg[2];
}


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
 *  addl:  ADD long
 *
 *  arg[0] = pointer to destination uint64_t
 *  arg[1] = pointer to source uint64_t nr 1
 *  arg[2] = pointer to source uint64_t nr 2
 */
X(addl)
{
	int32_t x;

	x = *((uint64_t *)ic->arg[1]) + *((uint64_t *)ic->arg[2]);
	*((uint64_t *)ic->arg[0]) = x;
}


/*
 *  addq:  ADD quad
 *
 *  arg[0] = pointer to destination uint64_t
 *  arg[1] = pointer to source uint64_t nr 1
 *  arg[2] = pointer to source uint64_t nr 2
 */
X(addq)
{
	int64_t x;

	x = *((uint64_t *)ic->arg[1]) + *((uint64_t *)ic->arg[2]);
	*((uint64_t *)ic->arg[0]) = x;
}


/*
 *  addl_imm:  ADD long, immediate
 *
 *  arg[0] = pointer to destination uint64_t
 *  arg[1] = pointer to source uint64_t
 *  arg[2] = immediate value
 */
X(addl_imm)
{
	int32_t x;

	x = *((int64_t *)ic->arg[1]) + ic->arg[2];
	*((uint64_t *)ic->arg[0]) = x;
}


/*
 *  addq_imm:  ADD quad, immediate
 *
 *  arg[0] = pointer to destination uint64_t
 *  arg[1] = pointer to source uint64_t
 *  arg[2] = immediate value
 */
X(addq_imm)
{
	int64_t x;

	x = *((int64_t *)ic->arg[1]) + ic->arg[2];
	*((uint64_t *)ic->arg[0]) = x;
}


/*
 *  s8addq_imm:  ADD quad, scalar 8, immediate
 *
 *  arg[0] = pointer to destination uint64_t
 *  arg[1] = pointer to source uint64_t
 *  arg[2] = immediate value
 */
X(s8addq_imm)
{
	int64_t x;

	x = 8 * (*((int64_t *)ic->arg[1])) + ic->arg[2];
	*((uint64_t *)ic->arg[0]) = x;
}


/*
 *  subl_imm:  SUB long, immediate
 *
 *  arg[0] = pointer to destination uint64_t
 *  arg[1] = pointer to source uint64_t
 *  arg[2] = immediate value
 */
X(subl_imm)
{
	int32_t x;

	x = *((uint64_t *)ic->arg[1]) - ic->arg[2];
	*((uint64_t *)ic->arg[0]) = x;
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
	case 0x0e:
	case 0x28:
	case 0x29:
	case 0x2d:
		load = 0;
		switch (opcode) {
		case 0x0e: ic->f = instr(stb); break;
		case 0x28: ic->f = instr(ldl); load = 1; break;
		case 0x29: ic->f = instr(ldq); load = 1; break;
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
		case 0x00:
			ic->f = instr(addl);
			break;
		case 0x20:
			ic->f = instr(addq);
			break;
		case 0x80:
			ic->f = instr(addl_imm);
			break;
		case 0x89:
			ic->f = instr(subl_imm);
			break;
		case 0xa0:
			ic->f = instr(addq_imm);
			break;
		case 0xb2:
			ic->f = instr(s8addq_imm);
			break;
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
	case 0x30:						/*  BR  */
	case 0x39:						/*  BEQ  */
	case 0x3b:						/*  BLE  */
	case 0x3f:						/*  BGT  */
		switch (opcode) {
		case 0x30:
			if (ra != ALPHA_ZERO)
				fatal("[ WARNING! Alpha 'br' but ra "
				    "isn't zero? ]\n");
			ic->f = instr(br);
			samepage_function = instr(br_samepage);
			break;
		case 0x39:
			ic->f = instr(beq);
			samepage_function = instr(beq_samepage);
			break;
		case 0x3b:
			ic->f = instr(ble);
			samepage_function = instr(ble_samepage);
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

