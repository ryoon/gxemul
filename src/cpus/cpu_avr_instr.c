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
 *  $Id: cpu_avr_instr.c,v 1.7 2006-02-20 18:54:55 debug Exp $
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


/*
 *  nop:  Do nothing.
 */
X(nop)
{
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
 *  arg[0]: ptr to register
 *  arg[1]: byte value
 */
X(ldi)
{
	*(uint8_t *)(ic->arg[0]) = ic->arg[1];
}


/*
 *  mov:  Copy register.
 *
 *  arg[0]: ptr to rr
 *  arg[1]: ptr to rd
 */
X(mov)
{
	*(uint8_t *)(ic->arg[1]) = *(uint8_t *)(ic->arg[0]);
}


/*
 *  rjmp:  Relative jump.
 *
 *  arg[0]: relative offset
 */
X(rjmp)
{
	uint32_t low_pc;

	cpu->cd.avr.extra_cycles ++;

	/*  Calculate new PC from the next instruction + arg[0]  */
	low_pc = ((size_t)ic - (size_t)cpu->cd.avr.cur_ic_page) /
	    sizeof(struct avr_instr_call);
	cpu->pc &= ~((AVR_IC_ENTRIES_PER_PAGE-1)
	    << AVR_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (low_pc << AVR_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (int32_t)ic->arg[0];

	/*  Find the new physical page and update the translation pointers:  */
	avr_pc_to_pointers(cpu);
}


/*
 *  rjmp_samepage:  Relative jump (to within the same translated page).
 *
 *  arg[0] = pointer to new avr_instr_call
 */
X(rjmp_samepage)
{
	cpu->cd.avr.extra_cycles ++;
	cpu->cd.avr.next_ic = (struct avr_instr_call *) ic->arg[0];
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
 *  swap:  Swap nibbles.
 *
 *  arg[0]: ptr to rd
 */
X(swap)
{
	uint8_t x = *(uint8_t *)(ic->arg[0]);
	*(uint8_t *)(ic->arg[0]) = (x >> 4) | (x << 4);
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
	int addr, low_pc, rd, rr, main_opcode;
	uint16_t iword;
	unsigned char *page;
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
	page = cpu->cd.avr.host_load[addr >> 12];

	if (page != NULL) {
		/*  fatal("TRANSLATION HIT!\n");  */
		memcpy(ib, page + (addr & 0xfff), sizeof(ib));
	} else {
		/*  fatal("TRANSLATION MISS!\n");  */
		if (!cpu->memory_rw(cpu, cpu->mem, addr, ib,
		    sizeof(ib), MEM_READ, CACHE_INSTRUCTION)) {
			fatal("to_be_translated(): "
			    "read failed: TODO\n");
			goto bad;
		}
	}

	iword = *((uint16_t *)&ib[0]);

#ifdef HOST_BIG_ENDIAN
	iword = ((iword & 0xff) << 8) |
		((iword & 0xff00) >> 8);
#endif


#define DYNTRANS_TO_BE_TRANSLATED_HEAD
#include "cpu_dyntrans.c"
#undef  DYNTRANS_TO_BE_TRANSLATED_HEAD


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
		if ((iword & 0xfc00) == 0x2c00) {
			rd = (iword & 0x1f0) >> 4;
			rr = ((iword & 0x200) >> 5) | (iword & 0xf);
			ic->f = instr(mov);
			ic->arg[0] = (size_t)(&cpu->cd.avr.r[rr]);
			ic->arg[1] = (size_t)(&cpu->cd.avr.r[rd]);
			break;
		}
		goto bad;

	case 0x9:
		if ((iword & 0xfe0f) == 0x9402) {
			rd = (iword >> 4) & 31;
			ic->f = instr(swap);
			ic->arg[0] = (size_t)(&cpu->cd.avr.r[rd]);
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
		goto bad;

	case 0xc:
		ic->f = instr(rjmp);
		samepage_function = instr(rjmp_samepage);
		ic->arg[0] = (((int16_t)((iword & 0x0fff) << 4)) >> 3) + 2;
		/*  Special case: branch within the same page:  */
		{
			uint32_t mask_within_page =
			    ((AVR_IC_ENTRIES_PER_PAGE-1) <<
			    AVR_INSTR_ALIGNMENT_SHIFT) |
			    ((1 << AVR_INSTR_ALIGNMENT_SHIFT) - 1);
			uint32_t old_pc = addr;
			uint32_t new_pc = old_pc + (int32_t)ic->arg[0];
			if ((old_pc & ~mask_within_page) ==
			    (new_pc & ~mask_within_page)) {
				ic->f = samepage_function;
				ic->arg[0] = (size_t) (
				    cpu->cd.avr.cur_ic_page +
				    ((new_pc & mask_within_page) >>
				    AVR_INSTR_ALIGNMENT_SHIFT));
			}
		}
		break;

	case 0xe:
		rd = ((iword >> 4) & 0xf) + 16;
		ic->f = instr(ldi);
		ic->arg[0] = (size_t)(&cpu->cd.avr.r[rd]);
		ic->arg[1] = ((iword >> 4) & 0xf0) | (iword & 0xf);
		break;

	default:goto bad;
	}


#define	DYNTRANS_TO_BE_TRANSLATED_TAIL
#include "cpu_dyntrans.c" 
#undef	DYNTRANS_TO_BE_TRANSLATED_TAIL
}

