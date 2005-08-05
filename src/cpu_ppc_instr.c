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
 *  $Id: cpu_ppc_instr.c,v 1.2 2005-08-05 12:45:29 debug Exp $
 *
 *  POWER/PowerPC instructions.
 *
 *  Individual functions should keep track of cpu->n_translated_instrs.
 *  (If no instruction was executed, then it should be decreased. If, say, 4
 *  instructions were combined into one function and executed, then it should
 *  be increased by 3.)
 */


#define X(n) void ppc_instr_ ## n(struct cpu *cpu, \
	struct ppc_instr_call *ic)

/*  This is for marking a physical page as containing translated or
    combined instructions, respectively:  */
#define	translated	(cpu->cd.ppc.cur_physpage->flags |= TRANSLATIONS)
#define	combined	(cpu->cd.ppc.cur_physpage->flags |= COMBINATIONS)


/*
 *  nothing:  Do nothing.
 *
 *  The difference between this function and the "nop" instruction is that
 *  this function does not increase the program counter or the number of
 *  translated instructions.  It is used to "get out" of running in translated
 *  mode.
 *
 *  IMPORTANT NOTE: Do a   cpu->running_translated = 0;
 *                  before setting cpu->cd.ppc.next_ic = &nothing_call;
 */
X(nothing)
{
	cpu->n_translated_instrs --;
	cpu->cd.ppc.next_ic --;
}


static struct ppc_instr_call nothing_call = { instr(nothing), {0,0,0} };


/*****************************************************************************/


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
	cpu->pc &= ~((PPC_IC_ENTRIES_PER_PAGE-1) << 2);
	cpu->pc += (PPC_IC_ENTRIES_PER_PAGE << 2);

	/*  Find the new physical page and update the translation pointers:  */
	ppc_pc_to_pointers(cpu);

	/*  end_of_page doesn't count as an executed instruction:  */
	cpu->n_translated_instrs --;
}


/*****************************************************************************/


/*
 *  ppc_combine_instructions():
 *
 *  Combine two or more instructions, if possible, into a single function call.
 */
void ppc_combine_instructions(struct cpu *cpu, struct ppc_instr_call *ic,
	uint32_t addr)
{
	int n_back;
	n_back = (addr >> 2) & (PPC_IC_ENTRIES_PER_PAGE-1);

	if (n_back >= 1) {
		/*  TODO  */
	}

	/*  TODO: Combine forward as well  */
}


/*****************************************************************************/


/*
 *  ppc_instr_to_be_translated():
 *
 *  Translate an instruction word into an ppc_instr_call. ic is filled in with
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
	int main_opcode;
	void (*samepage_function)(struct cpu *, struct ppc_instr_call *);

	/*  Figure out the (virtual) address of the instruction:  */
	low_pc = ((size_t)ic - (size_t)cpu->cd.ppc.cur_ic_page)
	    / sizeof(struct ppc_instr_call);
	addr = cpu->pc & ~((PPC_IC_ENTRIES_PER_PAGE-1) << 2);
	addr += (low_pc << 2);
	cpu->pc = addr;
	addr &= ~0x3;

	/*  Read the instruction word from memory:  */
	page = cpu->cd.ppc.host_load[addr >> 12];

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

	iword = *((uint32_t *)&ib[0]);

#ifdef HOST_LITTLE_ENDIAN
	if (cpu->byte_order == EMUL_BIG_ENDIAN)
#else
	if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
#endif
		iword = ((iword & 0xff) << 24) |
			((iword & 0xff00) << 8) |
			((iword & 0xff0000) >> 8) |
			((iword & 0xff000000) >> 24);

	fatal("{ PPC translating pc=0x%08x iword=0x%08x }\n",
	    addr, iword);


	/*
	 *  Translate the instruction:
	 */

	main_opcode = iword >> 26;

	switch (main_opcode) {

	default:goto bad;
	}

#define	DYNTRANS_TO_BE_TRANSLATED_TAIL
#define	COMBINE_INSTRUCTIONS		ppc_combine_instructions
#define	DISASSEMBLE			ppc_cpu_disassemble_instr
#include "cpu_dyntrans.c" 
#undef	DISASSEMBLE
#undef	COMBINE_INSTRUCTIONS
#undef	DYNTRANS_TO_BE_TRANSLATED_TAIL
}

