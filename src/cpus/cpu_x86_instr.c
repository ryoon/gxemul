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
 *  $Id: cpu_x86_instr.c,v 1.9 2006-04-17 09:50:16 debug Exp $
 *
 *  x86/amd64 instructions.
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
 *  sti, cli, std, cld, stc, clc:  Set/clear flag bits.
 */
X(stc) { cpu->cd.x86.rflags |= X86_FLAGS_CF; }
X(clc) { cpu->cd.x86.rflags &= ~X86_FLAGS_CF; }
X(std) { cpu->cd.x86.rflags |= X86_FLAGS_DF; }
X(cld) { cpu->cd.x86.rflags &= ~X86_FLAGS_DF; }
X(sti) { cpu->cd.x86.rflags |= X86_FLAGS_IF; }
X(cli) { cpu->cd.x86.rflags &= ~X86_FLAGS_IF; }


/*****************************************************************************/


X(end_of_page)
{
	/*  Update the PC:  (offset 0, but on the next page)  */
	cpu->pc &= ~(X86_IC_ENTRIES_PER_PAGE-1);
	cpu->pc += X86_IC_ENTRIES_PER_PAGE;

	/*  Find the new physical page and update the translation pointers:  */
	x86_pc_to_pointers(cpu);

	/*  end_of_page doesn't count as an executed instruction:  */
	cpu->n_translated_instrs --;
}


/*****************************************************************************/


/*
 *  x86_instr_to_be_translated():
 *
 *  Translate an instruction word into an x86_instr_call. ic is filled in with
 *  valid data for the translated instruction, or a "nothing" instruction if
 *  there was a translation failure. The newly translated instruction is then
 *  executed.
 */
X(to_be_translated)
{
	MODE_uint_t addr, low_pc;
	unsigned char *page;
	int main_opcode;
	unsigned char ib[17];
	/* void (*samepage_function)(struct cpu *, struct x86_instr_call *); */

	/*  Figure out the (virtual) address of the instruction:  */
	low_pc = ((size_t)ic - (size_t)cpu->cd.x86.cur_ic_page)
	    / sizeof(struct x86_instr_call);
	addr = cpu->pc & ~(X86_IC_ENTRIES_PER_PAGE-1);
	addr += low_pc;
	cpu->pc = addr;

	if (!cpu->cd.x86.descr_cache[X86_S_CS].valid) {
		fatal("x86_cpu_run_instr(): Invalid CS descriptor?\n");
		exit(1);
	}

	cpu->cd.x86.cursegment = X86_S_CS;
	cpu->cd.x86.seg_override = 0;

/*  Read the instruction word from memory:  */
#ifdef MODE32
	page = cpu->cd.x86.host_load[addr >> 12];
#else
	{
		const uint32_t mask1 = (1 << DYNTRANS_L1N) - 1;
		const uint32_t mask2 = (1 << DYNTRANS_L2N) - 1;
		const uint32_t mask3 = (1 << DYNTRANS_L3N) - 1;
		uint32_t x1 = (addr >> (64-DYNTRANS_L1N)) & mask1;
		uint32_t x2 = (addr >> (64-DYNTRANS_L1N-DYNTRANS_L2N)) & mask2;
		uint32_t x3 = (addr >> (64-DYNTRANS_L1N-DYNTRANS_L2N-
		    DYNTRANS_L3N)) & mask3;
		struct DYNTRANS_L2_64_TABLE *l2 = cpu->cd.x86.l1_64[x1];
		struct DYNTRANS_L3_64_TABLE *l3 = l2->l3[x2];
		page = l3->host_load[x3];
	}
#endif

	if (page != NULL) {
		/*  fatal("TRANSLATION HIT!\n");  */
		ib[0] = page[addr & 0xfff];
	} else {
		/*  fatal("TRANSLATION MISS!\n");  */
		if (!cpu->memory_rw(cpu, cpu->mem, addr, &ib[0],
		    1, MEM_READ, CACHE_INSTRUCTION)) {
			fatal("to_be_translated(): read failed: TODO\n");
			goto bad;
		}
	}

	fatal("X86: ib[0] = 0x%02x\n", ib[0]);


#define DYNTRANS_TO_BE_TRANSLATED_HEAD
#include "cpu_dyntrans.c"
#undef  DYNTRANS_TO_BE_TRANSLATED_HEAD


	/*
	 *  Translate the instruction:
	 */

	main_opcode = ib[0];

	switch (main_opcode) {

	case 0x90:	/*  nop  */
		ic->arg[0] = 1;
		ic->f = instr(nop);
		break;

	case 0xf8:	/*  clc  */
	case 0xf9:	/*  stc  */
	case 0xfa:	/*  cli  */
	case 0xfb:	/*  sti  */
	case 0xfc:	/*  cld  */
	case 0xfd:	/*  std  */
		ic->arg[0] = 1;
		switch (main_opcode) {
		case 0xf8: ic->f = instr(sti); break;
		case 0xf9: ic->f = instr(stc); break;
		case 0xfa: ic->f = instr(cli); break;
		case 0xfb: ic->f = instr(sti); break;
		case 0xfc: ic->f = instr(cld); break;
		case 0xfd: ic->f = instr(std); break;
		}
		break;

	default:goto bad;
	}


#define	DYNTRANS_TO_BE_TRANSLATED_TAIL
#include "cpu_dyntrans.c" 
#undef	DYNTRANS_TO_BE_TRANSLATED_TAIL
}

