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
 *  $Id: cpu_avr_instr.c,v 1.1 2005-09-17 17:14:27 debug Exp $
 *
 *  Atmel AVR (8-bit) instructions.
 *
 *  Individual functions should keep track of cpu->n_translated_instrs. Since
 *  AVR uses variable length instructions, cpu->cd.avr.next_ic must also be
 *  increased by the number of "instruction slots" that were executed. (I.e.
 *  if an instruction occupying 6 bytes was executed, then next_ic should be
 *  increased by 3.)
 *
 *  (n_translated_instrs is automatically increased by 1 for each function
 *  call. If no instruction was executed, then it should be decreased. If, say,
 *  4 instructions were combined into one function and executed, then it should
 *  be increased by 3.)
 */


/*
 *  nop:  Do nothing.
 */
X(nop)
{
	cpu->cd.avr.next_ic ++;
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
	uint32_t addr, low_pc;
	uint16_t iword;
	unsigned char *page;
	unsigned char ib[2];
	int main_opcode;
	void (*samepage_function)(struct cpu *, struct avr_instr_call *);

	/*  Figure out the (virtual) address of the instruction:  */
	low_pc = ((size_t)ic - (size_t)cpu->cd.avr.cur_ic_page)
	    / sizeof(struct avr_instr_call);
	addr = cpu->pc & ~((AVR_IC_ENTRIES_PER_PAGE-1) <<
	    AVR_INSTR_ALIGNMENT_SHIFT);
	addr += (low_pc << AVR_INSTR_ALIGNMENT_SHIFT);
	cpu->pc = addr;
	addr &= ~((1 << AVR_INSTR_ALIGNMENT_SHIFT) - 1);

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


	fatal("AVR: iword = 0x%04x\n", iword);


#define DYNTRANS_TO_BE_TRANSLATED_HEAD
#include "cpu_dyntrans.c"
#undef  DYNTRANS_TO_BE_TRANSLATED_HEAD


	/*
	 *  Translate the instruction:
	 */


/*  TODO  */


	main_opcode = iword;

	switch (main_opcode) {

	default:goto bad;
	}


#define	DYNTRANS_TO_BE_TRANSLATED_TAIL
#include "cpu_dyntrans.c" 
#undef	DYNTRANS_TO_BE_TRANSLATED_TAIL
}

