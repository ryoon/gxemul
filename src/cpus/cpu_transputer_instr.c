/*
 *  Copyright (C) 2006  Anders Gavare.  All rights reserved.
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
 *  $Id: cpu_transputer_instr.c,v 1.1 2006-07-20 21:53:00 debug Exp $
 *
 *  INMOS transputer instructions.
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


/*
 *  j:  Relative jump
 *
 *  arg[0] = relative distance from the _current_ instruction.
 */
X(j)
{
	/*  Synchronize the PC and then add the operand + 1:  */
	int low_pc = ((size_t)ic - (size_t)cpu->cd.transputer.cur_ic_page)
	    / sizeof(struct transputer_instr_call);
	cpu->pc &= ~(TRANSPUTER_IC_ENTRIES_PER_PAGE-1);
	cpu->pc += low_pc + ic->arg[0];
	quick_pc_to_pointers(cpu);
}


/*
 *  ldc: Load constant
 *
 *  arg[0] = constant
 */
X(ldc)
{
	/*  TODO: Is oreg really cleared like this?  */
	cpu->cd.transputer.oreg = ic->arg[0];
	cpu->cd.transputer.c = cpu->cd.transputer.b;
	cpu->cd.transputer.b = cpu->cd.transputer.a;
	cpu->cd.transputer.a = ic->arg[0];
}


/*****************************************************************************/


X(end_of_page)
{
	/*  Update the PC:  (offset 0, but on the next page)  */
	cpu->pc &= ~((TRANSPUTER_IC_ENTRIES_PER_PAGE-1) << 1);
	cpu->pc += (TRANSPUTER_IC_ENTRIES_PER_PAGE << 1);

	/*  Find the new physical page and update the translation pointers:  */
	transputer_pc_to_pointers(cpu);

	/*  end_of_page doesn't count as an executed instruction:  */
	cpu->n_translated_instrs --;
}


/*****************************************************************************/


/*
 *  transputer_instr_to_be_translated():
 *
 *  Translate an instruction word into an transputer_instr_call. ic is filled in with
 *  valid data for the translated instruction, or a "nothing" instruction if
 *  there was a translation failure. The newly translated instruction is then
 *  executed.
 */
X(to_be_translated)
{
	uint32_t addr, low_pc;
	unsigned char *page;
	unsigned char ib[1];
	/* void (*samepage_function)(struct cpu *,
	    struct transputer_instr_call *);*/

	/*  Figure out the (virtual) address of the instruction:  */
	low_pc = ((size_t)ic - (size_t)cpu->cd.transputer.cur_ic_page)
	    / sizeof(struct transputer_instr_call);
	addr = cpu->pc & ~((TRANSPUTER_IC_ENTRIES_PER_PAGE-1) <<
	    TRANSPUTER_INSTR_ALIGNMENT_SHIFT);
	addr += (low_pc << TRANSPUTER_INSTR_ALIGNMENT_SHIFT);
	cpu->pc = addr;
	addr &= ~((1 << TRANSPUTER_INSTR_ALIGNMENT_SHIFT) - 1);

	/*  Read the instruction word from memory:  */
	page = cpu->cd.transputer.host_load[addr >> 12];

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


#define DYNTRANS_TO_BE_TRANSLATED_HEAD
#include "cpu_dyntrans.c"
#undef  DYNTRANS_TO_BE_TRANSLATED_HEAD


	/*
	 *  Translate the instruction:
	 *  --------------------------
	 *
	 *  Most instructions take the operand as arg[0], so we set it
	 *  here by default:
	 */
	ic->arg[0] = ib[0] & 0xf;

	switch (ib[0] >> 4) {

	case 0:	/*  j, relative jump  */
		ic->f = instr(j);
		ic->arg[0] = (ib[0] & 0xf) + 1;
		/*  TODO: Samepage jump!  */
		break;

	case 4:	/*  ldc, load constant  */
		ic->f = instr(ldc);
		break;

	default:fatal("UNIMPLEMENTED opcode 0x%02x\n", ib[0]);
		goto bad;
	}


#define	DYNTRANS_TO_BE_TRANSLATED_TAIL
#include "cpu_dyntrans.c" 
#undef	DYNTRANS_TO_BE_TRANSLATED_TAIL
}

