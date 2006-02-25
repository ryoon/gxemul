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
 *  $Id: cpu_avr_instr.c,v 1.9 2006-02-25 18:30:31 debug Exp $
 *
 *  Atmel AVR (8-bit) instructions.
 *
 *  Individual functions should keep track of cpu->n_translated_instrs.
 *  (n_translated_instrs is automatically increased by 1 for each function
 *  call. If no instruction was executed, then it should be decreased. If, say,
 *  4 instructions were combined into one function and executed, then it should
 *  be increased by 3.)
 */


/*****************************************************************************/


void push_value(struct cpu *cpu, uint32_t value, int len)
{
	unsigned char data[4];

	data[0] = value; data[1] = value >> 8;
	data[2] = value >> 16; data[3] = value >> 24;

	if (!cpu->memory_rw(cpu, cpu->mem, cpu->cd.avr.sp + AVR_SRAM_BASE,
	    data, len, MEM_WRITE, CACHE_DATA)) {
		fatal("push_value(): write failed: TODO\n");
		exit(1);
	}

	cpu->cd.avr.sp -= len;
	cpu->cd.avr.sp &= cpu->cd.avr.sram_mask;
}


void pop_value(struct cpu *cpu, uint32_t *value, int len)
{
	unsigned char data[4];

	cpu->cd.avr.sp += len;
	cpu->cd.avr.sp &= cpu->cd.avr.sram_mask;

	if (!cpu->memory_rw(cpu, cpu->mem, cpu->cd.avr.sp + AVR_SRAM_BASE,
	    data, len, MEM_READ, CACHE_DATA)) {
		fatal("pop_value(): write failed: TODO\n");
		exit(1);
	}

	*value = data[0];
	if (len > 1)
		(*value) += (data[1] << 8);
	if (len > 2)
		(*value) += (data[2] << 16);
	if (len > 3)
		(*value) += (data[3] << 24);
}


/*****************************************************************************/


/*
 *  nop:  Do nothing.
 */
X(nop)
{
}


/*
 *  breq:  Conditional relative jump.
 *
 *  arg[1]: relative offset
 */
X(breq)
{
	uint32_t low_pc;

	if (!(cpu->cd.avr.sreg & AVR_SREG_Z))
		return;

	cpu->cd.avr.extra_cycles ++;

	/*  Calculate new PC from the next instruction + arg[1]  */
	low_pc = ((size_t)ic - (size_t)cpu->cd.avr.cur_ic_page) /
	    sizeof(struct avr_instr_call);
	cpu->pc &= ~((AVR_IC_ENTRIES_PER_PAGE-1)
	    << AVR_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (low_pc << AVR_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (int32_t)ic->arg[1];

	/*  Find the new physical page and update the translation pointers:  */
	avr_pc_to_pointers(cpu);
}


/*
 *  breq_samepage:  Continional relative jump (to within the same page).
 *
 *  arg[1] = pointer to new avr_instr_call
 */
X(breq_samepage)
{
	if (!(cpu->cd.avr.sreg & AVR_SREG_Z))
		return;

	cpu->cd.avr.extra_cycles ++;
	cpu->cd.avr.next_ic = (struct avr_instr_call *) ic->arg[1];
}


/*
 *  brne:  Conditional relative jump.
 *
 *  arg[1]: relative offset
 */
X(brne)
{
	uint32_t low_pc;

	if (cpu->cd.avr.sreg & AVR_SREG_Z)
		return;

	cpu->cd.avr.extra_cycles ++;

	/*  Calculate new PC from the next instruction + arg[1]  */
	low_pc = ((size_t)ic - (size_t)cpu->cd.avr.cur_ic_page) /
	    sizeof(struct avr_instr_call);
	cpu->pc &= ~((AVR_IC_ENTRIES_PER_PAGE-1)
	    << AVR_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (low_pc << AVR_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (int32_t)ic->arg[1];

	/*  Find the new physical page and update the translation pointers:  */
	avr_pc_to_pointers(cpu);
}


/*
 *  brne_samepage:  Continional relative jump (to within the same page).
 *
 *  arg[1] = pointer to new avr_instr_call
 */
X(brne_samepage)
{
	if (cpu->cd.avr.sreg & AVR_SREG_Z)
		return;

	cpu->cd.avr.extra_cycles ++;
	cpu->cd.avr.next_ic = (struct avr_instr_call *) ic->arg[1];
}


/*
 *  clX:  Clear an sreg bit.
 */
X(clc) { cpu->cd.avr.sreg &= ~AVR_SREG_C; }
X(clz) { cpu->cd.avr.sreg &= ~AVR_SREG_Z; }
X(cln) { cpu->cd.avr.sreg &= ~AVR_SREG_N; }
X(clv) { cpu->cd.avr.sreg &= ~AVR_SREG_V; }
X(cls) { cpu->cd.avr.sreg &= ~AVR_SREG_S; }
X(clh) { cpu->cd.avr.sreg &= ~AVR_SREG_H; }
X(clt) { cpu->cd.avr.sreg &= ~AVR_SREG_T; }
X(cli) { cpu->cd.avr.sreg &= ~AVR_SREG_I; }


/*
 *  ldi:  Load immediate.
 *
 *  arg[1]: ptr to register
 *  arg[2]: byte value
 */
X(ldi)
{
	*(uint8_t *)(ic->arg[1]) = ic->arg[2];
}


/*
 *  ld_y:  Load byte pointed to by register Y into a register.
 *
 *  arg[1]: ptr to rd
 */
X(ld_y)
{
	if (!cpu->memory_rw(cpu, cpu->mem, AVR_SRAM_BASE + cpu->cd.avr.r[28]
	    + 256*cpu->cd.avr.r[29], (uint8_t *)(ic->arg[1]), 1, MEM_READ,
	    CACHE_DATA)) {
		fatal("ld_y(): read failed: TODO\n");
		exit(1);
	}
	cpu->cd.avr.extra_cycles ++;
}


/*
 *  adiw:  rd+1:rd += constant
 *
 *  arg[1]: ptr to rd
 *  arg[2]: k
 */
X(adiw)
{
	uint32_t value = *(uint8_t *)(ic->arg[1]) +
	    (*(uint8_t *)(ic->arg[1] + 1) << 8);
	value += ic->arg[2];

	cpu->cd.avr.sreg &= ~(AVR_SREG_S | AVR_SREG_V | AVR_SREG_N
	    | AVR_SREG_Z | AVR_SREG_C);

	/*  TODO: is this V bit calculated correctly?  */
	if (value > 0xffff)
		cpu->cd.avr.sreg |= AVR_SREG_C | AVR_SREG_V;
	if (value & 0x8000)
		cpu->cd.avr.sreg |= AVR_SREG_N;
	if (value == 0)
		cpu->cd.avr.sreg |= AVR_SREG_Z;

	if ((cpu->cd.avr.sreg & AVR_SREG_N) ^
	    (cpu->cd.avr.sreg & AVR_SREG_V))
		cpu->cd.avr.sreg |= AVR_SREG_S;

	*(uint8_t *)(ic->arg[1]) = value;
	*(uint8_t *)(ic->arg[1] + 1) = value >> 8;

	cpu->cd.avr.extra_cycles ++;
}


/*
 *  and:  rd = rd & rr
 *
 *  arg[1]: ptr to rr
 *  arg[2]: ptr to rd
 */
X(and)
{
	*(uint8_t *)(ic->arg[2]) &= *(uint8_t *)(ic->arg[1]);
	cpu->cd.avr.sreg &= ~(AVR_SREG_S | AVR_SREG_V | AVR_SREG_N
	    | AVR_SREG_Z);
	if (*(uint8_t *)(ic->arg[2]) == 0)
		cpu->cd.avr.sreg |= AVR_SREG_Z;
	if (*(uint8_t *)(ic->arg[2]) & 0x80)
		cpu->cd.avr.sreg |= AVR_SREG_S | AVR_SREG_N;
}


/*
 *  mov:  Copy register.
 *
 *  arg[1]: ptr to rr
 *  arg[2]: ptr to rd
 */
X(mov)
{
	*(uint8_t *)(ic->arg[2]) = *(uint8_t *)(ic->arg[1]);
}


/*
 *  sts:  Store a register into memory.
 *
 *  arg[1]: pointer to the register
 *  arg[2]: absolute address (16 bits)
 */
X(sts)
{
	uint8_t r = *(uint8_t *)(ic->arg[1]);
	if (!cpu->memory_rw(cpu, cpu->mem, ic->arg[2] + AVR_SRAM_BASE,
	    &r, sizeof(uint8_t), MEM_WRITE, CACHE_DATA)) {
		fatal("sts: write failed: TODO\n");
		exit(1);
	}
	cpu->cd.avr.extra_cycles ++;
}


/*
 *  ret:  Return from subroutine call.
 */
X(ret)
{
	uint32_t new_pc;

	cpu->cd.avr.extra_cycles += 3 + cpu->cd.avr.is_22bit;

	/*  Pop the address of the following instruction:  */
	pop_value(cpu, &new_pc, 2 + cpu->cd.avr.is_22bit);
	cpu->pc = new_pc << 1;

	/*  Find the new physical page and update the translation pointers:  */
	avr_pc_to_pointers(cpu);
}


/*
 *  rcall:  Relative call.
 *
 *  arg[1]: relative offset
 */
X(rcall)
{
	uint32_t low_pc;

	cpu->cd.avr.extra_cycles += 2 + cpu->cd.avr.is_22bit;

	/*  Push the address of the following instruction:  */
	low_pc = ((size_t)ic - (size_t)cpu->cd.avr.cur_ic_page) /
	    sizeof(struct avr_instr_call);
	cpu->pc &= ~((AVR_IC_ENTRIES_PER_PAGE-1)
	    << AVR_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (low_pc << AVR_INSTR_ALIGNMENT_SHIFT);
	push_value(cpu, (cpu->pc >> 1) + 1, 2 + cpu->cd.avr.is_22bit);

	/*  Calculate new PC from the next instruction + arg[1]  */
	cpu->pc += (int32_t)ic->arg[1];

	/*  Find the new physical page and update the translation pointers:  */
	avr_pc_to_pointers(cpu);
}


/*
 *  rjmp:  Relative jump.
 *
 *  arg[1]: relative offset
 */
X(rjmp)
{
	uint32_t low_pc;

	cpu->cd.avr.extra_cycles ++;

	/*  Calculate new PC from the next instruction + arg[1]  */
	low_pc = ((size_t)ic - (size_t)cpu->cd.avr.cur_ic_page) /
	    sizeof(struct avr_instr_call);
	cpu->pc &= ~((AVR_IC_ENTRIES_PER_PAGE-1)
	    << AVR_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (low_pc << AVR_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (int32_t)ic->arg[1];

	/*  Find the new physical page and update the translation pointers:  */
	avr_pc_to_pointers(cpu);
}


/*
 *  rjmp_samepage:  Relative jump (to within the same translated page).
 *
 *  arg[1] = pointer to new avr_instr_call
 */
X(rjmp_samepage)
{
	cpu->cd.avr.extra_cycles ++;
	cpu->cd.avr.next_ic = (struct avr_instr_call *) ic->arg[1];
}


/*
 *  seX:  Set an sreg bit.
 */
X(sec) { cpu->cd.avr.sreg |= AVR_SREG_C; }
X(sez) { cpu->cd.avr.sreg |= AVR_SREG_Z; }
X(sen) { cpu->cd.avr.sreg |= AVR_SREG_N; }
X(sev) { cpu->cd.avr.sreg |= AVR_SREG_V; }
X(ses) { cpu->cd.avr.sreg |= AVR_SREG_S; }
X(seh) { cpu->cd.avr.sreg |= AVR_SREG_H; }
X(set) { cpu->cd.avr.sreg |= AVR_SREG_T; }
X(sei) { cpu->cd.avr.sreg |= AVR_SREG_I; }


/*
 *  push, pop:  Push/pop a register onto/from the stack.
 *
 *  arg[1]: ptr to rd
 */
X(push) { push_value(cpu, *(uint8_t *)(ic->arg[1]), 1);
	  cpu->cd.avr.extra_cycles ++; }
X(pop)  { uint32_t t; pop_value(cpu, &t, 1); *(uint8_t *)(ic->arg[1]) = t;
	  cpu->cd.avr.extra_cycles ++; }


/*
 *  swap:  Swap nibbles in a register.
 *
 *  arg[1]: ptr to rd
 */
X(swap)
{
	uint8_t x = *(uint8_t *)(ic->arg[1]);
	*(uint8_t *)(ic->arg[1]) = (x >> 4) | (x << 4);
}


/*****************************************************************************/


X(end_of_page)
{
	/*  Update the PC:  (offset 0, but on the next page)  */
	cpu->pc &= ~((AVR_IC_ENTRIES_PER_PAGE-1) << 1);
	cpu->pc += (AVR_IC_ENTRIES_PER_PAGE << 1);

	/*  Find the new physical page and update the translation pointers:  */
	avr_pc_to_pointers(cpu);

	/*  end_of_page doesn't count as an executed instruction:  */
	cpu->n_translated_instrs --;
}


/*****************************************************************************/


/*
 *  avr_combine_instructions():
 *
 *  Combine two or more instructions, if possible, into a single function call.
 */
void avr_combine_instructions(struct cpu *cpu, struct avr_instr_call *ic,
	uint32_t addr)
{
	int n_back;
	n_back = (addr >> 1) & (AVR_IC_ENTRIES_PER_PAGE-1);

	if (n_back >= 1) {
		/*  TODO  */
	}

	/*  TODO: Combine forward as well  */
}


/*****************************************************************************/


static uint16_t read_word(struct cpu *cpu, unsigned char *ib, int addr)
{
	uint16_t iword;
	unsigned char *page = cpu->cd.avr.host_load[addr >> 12];

	if (page != NULL) {
		/*  fatal("TRANSLATION HIT!\n");  */
		memcpy(ib, page + (addr & 0xfff), sizeof(uint16_t));
	} else {
		/*  fatal("TRANSLATION MISS!\n");  */
		if (!cpu->memory_rw(cpu, cpu->mem, addr, ib,
		    sizeof(uint16_t), MEM_READ, CACHE_INSTRUCTION)) {
			fatal("to_be_translated(): "
			    "read failed: TODO\n");
			exit(1);
		}
	}

	iword = *((uint16_t *)&ib[0]);

#ifdef HOST_BIG_ENDIAN
	iword = ((iword & 0xff) << 8) |
		((iword & 0xff00) >> 8);
#endif
	return iword;
}


/*
 *  avr_instr_to_be_translated():
 *
 *  Translate an instruction word into an avr_instr_call. ic is filled in with
 *  valid data for the translated instruction, or a "nothing" instruction if
 *  there was a translation failure. The newly translated instruction is then
 *  executed.
 */
X(to_be_translated)
{
	int addr, low_pc, rd, rr, tmp, main_opcode;
	uint16_t iword;
	unsigned char ib[2];
	void (*samepage_function)(struct cpu *, struct avr_instr_call *);

	/*  Figure out the (virtual) address of the instruction:  */
	low_pc = ((size_t)ic - (size_t)cpu->cd.avr.cur_ic_page)
	    / sizeof(struct avr_instr_call);
	addr = cpu->pc & ~((AVR_IC_ENTRIES_PER_PAGE-1) <<
	    AVR_INSTR_ALIGNMENT_SHIFT);
	addr += (low_pc << AVR_INSTR_ALIGNMENT_SHIFT);
	cpu->pc = addr;
	addr &= ~((1 << AVR_INSTR_ALIGNMENT_SHIFT) - 1);

	addr &= cpu->cd.avr.pc_mask;

	/*  Read the instruction word from memory:  */
	iword = read_word(cpu, ib, addr);


#define DYNTRANS_TO_BE_TRANSLATED_HEAD
#include "cpu_dyntrans.c"
#undef  DYNTRANS_TO_BE_TRANSLATED_HEAD


	/*  Default instruction length:  */
	ic->arg[0] = 1;


	/*
	 *  Translate the instruction:
	 */
	main_opcode = iword >> 12;

	switch (main_opcode) {

	case 0x0:
		if (iword == 0x0000) {
			ic->f = instr(nop);
			break;
		}
		goto bad;

	case 0x2:
		if ((iword & 0xfc00) == 0x2000) {
			rd = (iword & 0x1f0) >> 4;
			rr = ((iword & 0x200) >> 5) | (iword & 0xf);
			ic->f = instr(and);
			ic->arg[1] = (size_t)(&cpu->cd.avr.r[rr]);
			ic->arg[2] = (size_t)(&cpu->cd.avr.r[rd]);
			break;
		}
		if ((iword & 0xfc00) == 0x2c00) {
			rd = (iword & 0x1f0) >> 4;
			rr = ((iword & 0x200) >> 5) | (iword & 0xf);
			ic->f = instr(mov);
			ic->arg[1] = (size_t)(&cpu->cd.avr.r[rr]);
			ic->arg[2] = (size_t)(&cpu->cd.avr.r[rd]);
			break;
		}
		goto bad;

	case 0x8:
		if ((iword & 0xfe0f) == 0x8008) {
			rd = (iword >> 4) & 31;
			ic->f = instr(ld_y);
			ic->arg[1] = (size_t)(&cpu->cd.avr.r[rd]);
			break;
		}
		goto bad;

	case 0x9:
		if ((iword & 0xfe0f) == 0x900f) {
			rd = (iword >> 4) & 31;
			ic->f = instr(pop);
			ic->arg[1] = (size_t)(&cpu->cd.avr.r[rd]);
			break;
		}
		if ((iword & 0xfe0f) == 0x9200) {
			uint8_t tmpbytes[2];
			ic->arg[0] = 2;	/*  Note: 2 words!  */
			ic->f = instr(sts);
			rd = (iword >> 4) & 31;
			ic->arg[1] = (size_t)(&cpu->cd.avr.r[rd]);
			ic->arg[2] = read_word(cpu, tmpbytes, addr + 2);
			break;
		}
		if ((iword & 0xfe0f) == 0x920f) {
			rd = (iword >> 4) & 31;
			ic->f = instr(push);
			ic->arg[1] = (size_t)(&cpu->cd.avr.r[rd]);
			break;
		}
		if ((iword & 0xfe0f) == 0x9402) {
			rd = (iword >> 4) & 31;
			ic->f = instr(swap);
			ic->arg[1] = (size_t)(&cpu->cd.avr.r[rd]);
			break;
		}
		if ((iword & 0xff8f) == 0x9408) {
			switch ((iword >> 4) & 7) {
			case 0: ic->f = instr(sec); break;
			case 1: ic->f = instr(sez); break;
			case 2: ic->f = instr(sen); break;
			case 3: ic->f = instr(sev); break;
			case 4: ic->f = instr(ses); break;
			case 5: ic->f = instr(seh); break;
			case 6: ic->f = instr(set); break;
			case 7: ic->f = instr(sei); break;
			}
			break;
		}
		if ((iword & 0xff8f) == 0x9488) {
			switch ((iword >> 4) & 7) {
			case 0: ic->f = instr(clc); break;
			case 1: ic->f = instr(clz); break;
			case 2: ic->f = instr(cln); break;
			case 3: ic->f = instr(clv); break;
			case 4: ic->f = instr(cls); break;
			case 5: ic->f = instr(clh); break;
			case 6: ic->f = instr(clt); break;
			case 7: ic->f = instr(cli); break;
			}
			break;
		}
		if ((iword & 0xffff) == 0x9508) {
			ic->f = instr(ret);
			break;
		}
		if ((iword & 0xff00) == 0x9600) {
			ic->f = instr(adiw);
			rd = ((iword >> 3) & 6) + 24;
			ic->arg[1] = (size_t)(&cpu->cd.avr.r[rd]);
			ic->arg[2] = (iword & 15) + ((iword & 0xc0) >> 2);
			break;
		}
		goto bad;

	case 0xc:	/*  rjmp  */
	case 0xd:	/*  rcall  */
		samepage_function = NULL;
		switch (main_opcode) {
		case 0xc:
			ic->f = instr(rjmp);
			samepage_function = instr(rjmp_samepage);
			break;
		case 0xd:
			ic->f = instr(rcall);
			break;
		}
		ic->arg[1] = (((int16_t)((iword & 0x0fff) << 4)) >> 3) + 2;
		/*  Special case: branch within the same page:  */
		if (samepage_function != NULL) {
			uint32_t mask_within_page =
			    ((AVR_IC_ENTRIES_PER_PAGE-1) <<
			    AVR_INSTR_ALIGNMENT_SHIFT) |
			    ((1 << AVR_INSTR_ALIGNMENT_SHIFT) - 1);
			uint32_t old_pc = addr;
			uint32_t new_pc = old_pc + (int32_t)ic->arg[1];
			if ((old_pc & ~mask_within_page) ==
			    (new_pc & ~mask_within_page)) {
				ic->f = samepage_function;
				ic->arg[1] = (size_t) (
				    cpu->cd.avr.cur_ic_page +
				    ((new_pc & mask_within_page) >>
				    AVR_INSTR_ALIGNMENT_SHIFT));
			}
		}
		break;

	case 0xe:
		rd = ((iword >> 4) & 0xf) + 16;
		ic->f = instr(ldi);
		ic->arg[1] = (size_t)(&cpu->cd.avr.r[rd]);
		ic->arg[2] = ((iword >> 4) & 0xf0) | (iword & 0xf);
		break;

	case 0xf:
		if ((iword & 0xfc07) == 0xf001) {
			ic->f = instr(breq);
			samepage_function = instr(breq_samepage);
			tmp = (iword >> 3) & 0x7f;
			if (tmp >= 64)
				tmp -= 128;
			ic->arg[1] = (tmp + 1) * 2;
			/*  Special case: branch within the same page:  */
			if (samepage_function != NULL) {
				uint32_t mask_within_page =
				    ((AVR_IC_ENTRIES_PER_PAGE-1) <<
				    AVR_INSTR_ALIGNMENT_SHIFT) |
				    ((1 << AVR_INSTR_ALIGNMENT_SHIFT) - 1);
				uint32_t old_pc = addr;
				uint32_t new_pc = old_pc + (int32_t)ic->arg[1];
				if ((old_pc & ~mask_within_page) ==
				    (new_pc & ~mask_within_page)) {
					ic->f = samepage_function;
					ic->arg[1] = (size_t) (
					    cpu->cd.avr.cur_ic_page +
					    ((new_pc & mask_within_page) >>
					    AVR_INSTR_ALIGNMENT_SHIFT));
				}
			}
			break;
		}
/*  TODO: refactor  */
		if ((iword & 0xfc07) == 0xf401) {
			ic->f = instr(brne);
			samepage_function = instr(brne_samepage);
			tmp = (iword >> 3) & 0x7f;
			if (tmp >= 64)
				tmp -= 128;
			ic->arg[1] = (tmp + 1) * 2;
			/*  Special case: branch within the same page:  */
			if (samepage_function != NULL) {
				uint32_t mask_within_page =
				    ((AVR_IC_ENTRIES_PER_PAGE-1) <<
				    AVR_INSTR_ALIGNMENT_SHIFT) |
				    ((1 << AVR_INSTR_ALIGNMENT_SHIFT) - 1);
				uint32_t old_pc = addr;
				uint32_t new_pc = old_pc + (int32_t)ic->arg[1];
				if ((old_pc & ~mask_within_page) ==
				    (new_pc & ~mask_within_page)) {
					ic->f = samepage_function;
					ic->arg[1] = (size_t) (
					    cpu->cd.avr.cur_ic_page +
					    ((new_pc & mask_within_page) >>
					    AVR_INSTR_ALIGNMENT_SHIFT));
				}
			}
			break;
		}
		goto bad;

	default:goto bad;
	}


#define	DYNTRANS_TO_BE_TRANSLATED_TAIL
#include "cpu_dyntrans.c" 
#undef	DYNTRANS_TO_BE_TRANSLATED_TAIL
}

