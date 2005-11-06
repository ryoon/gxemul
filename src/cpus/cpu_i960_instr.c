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
 *  $Id: cpu_i960_instr.c,v 1.2 2005-11-06 22:41:12 debug Exp $
 *
 *  Intel i960 instructions.
 *
 *  Individual functions should keep track of cpu->n_translated_instrs.
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
}


/*****************************************************************************/


X(end_of_page)
{
	/*  Update the PC:  (offset 0, but on the next page)  */
	cpu->pc &= ~((I960_IC_ENTRIES_PER_PAGE-1) << 1);
	cpu->pc += (I960_IC_ENTRIES_PER_PAGE << 1);

	/*  Find the new physical page and update the translation pointers:  */
	i960_pc_to_pointers(cpu);

	/*  end_of_page doesn't count as an executed instruction:  */
	cpu->n_translated_instrs --;
}


/*****************************************************************************/


/*
 *  i960_instr_to_be_translated():
 *
 *  Translate an instruction word into an i960_instr_call. ic is filled in with
 *  valid data for the translated instruction, or a "nothing" instruction if
 *  there was a translation failure. The newly translated instruction is then
 *  executed.
 */
X(to_be_translated)
{
	uint32_t addr, low_pc;
	uint16_t iword;
	unsigned char *page;
	unsigned char ib[4];
	int main_opcode;
	/* void (*samepage_function)(struct cpu *, struct i960_instr_call *);*/

	/*  Figure out the (virtual) address of the instruction:  */
	low_pc = ((size_t)ic - (size_t)cpu->cd.i960.cur_ic_page)
	    / sizeof(struct i960_instr_call);
	addr = cpu->pc & ~((I960_IC_ENTRIES_PER_PAGE-1) <<
	    I960_INSTR_ALIGNMENT_SHIFT);
	addr += (low_pc << I960_INSTR_ALIGNMENT_SHIFT);
	cpu->pc = addr;
	addr &= ~((1 << I960_INSTR_ALIGNMENT_SHIFT) - 1);

	/*  Read the instruction word from memory:  */
	page = cpu->cd.i960.host_load[addr >> 12];

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

/*  TODO  */

	iword = *((uint16_t *)&ib[0]);

#ifdef HOST_LITTLE_ENDIAN
	iword = ((iword & 0xff) << 8) |
		((iword & 0xff00) >> 8);
#endif


	fatal("I960: iword = 0x%04x\n", iword);


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

