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
 *  $Id: cpu_transputer_instr.c,v 1.3 2006-07-23 11:22:00 debug Exp $
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
 */
X(j)
{
	/*  Synchronize the PC and then add the operand + 1:  */
	int low_pc = ((size_t)ic - (size_t)cpu->cd.transputer.cur_ic_page)
	    / sizeof(struct transputer_instr_call);
	cpu->pc &= ~(TRANSPUTER_IC_ENTRIES_PER_PAGE-1);
	cpu->pc += low_pc + ic->arg[0] + 1 + cpu->cd.transputer.oreg;
	cpu->cd.transputer.oreg = 0;
	quick_pc_to_pointers(cpu);
}


/*
 *  ldlp: Load local pointer
 */
X(ldlp)
{
	cpu->cd.transputer.c = cpu->cd.transputer.b;
	cpu->cd.transputer.b = cpu->cd.transputer.a;
	cpu->cd.transputer.oreg |= ic->arg[0];
	cpu->cd.transputer.a = cpu->cd.transputer.oreg * sizeof(uint32_t) +
	    cpu->cd.transputer.wptr;
	cpu->cd.transputer.oreg = 0;
}


/*
 *  pfix: Prefix
 *
 *  arg[0] = nibble
 */
X(pfix)
{
	cpu->cd.transputer.oreg |= ic->arg[0];
	cpu->cd.transputer.oreg <<= 4;
}


/*
 *  ldc: Load constant
 */
X(ldc)
{
	cpu->cd.transputer.c = cpu->cd.transputer.b;
	cpu->cd.transputer.b = cpu->cd.transputer.a;
	cpu->cd.transputer.a = cpu->cd.transputer.oreg | ic->arg[0];
	cpu->cd.transputer.oreg = 0;
}


/*
 *  nfix: Negative prefix
 */
X(nfix)
{
	cpu->cd.transputer.oreg |= ic->arg[0];
	cpu->cd.transputer.oreg = (~cpu->cd.transputer.oreg) << 4;
}


/*
 *  ldl: Load local
 */
X(ldl)
{
	uint32_t addr;
	uint8_t word[sizeof(uint32_t)];
	uint32_t w32;
	unsigned char *page;

	cpu->cd.transputer.c = cpu->cd.transputer.b;
	cpu->cd.transputer.b = cpu->cd.transputer.a;
	cpu->cd.transputer.oreg |= ic->arg[0];
	addr = cpu->cd.transputer.oreg * sizeof(uint32_t) +
	    cpu->cd.transputer.wptr;

	page = cpu->cd.transputer.host_load[TRANSPUTER_ADDR_TO_PAGENR(addr)];
	if (page == NULL) {
		cpu->memory_rw(cpu, cpu->mem, addr, word, sizeof(word),
		    MEM_READ, CACHE_DATA);
		w32 = *(uint32_t *) &word[0];
	} else {
		w32 = *(uint32_t *) &page[TRANSPUTER_PC_TO_IC_ENTRY(addr)];
	}

	cpu->cd.transputer.a = LE32_TO_HOST(w32);
	cpu->cd.transputer.oreg = 0;
}


/*
 *  ajw: Adjust workspace
 */
X(ajw)
{
	cpu->cd.transputer.oreg |= ic->arg[0];
	cpu->cd.transputer.wptr += (cpu->cd.transputer.oreg * sizeof(uint32_t));
	cpu->cd.transputer.oreg = 0;
}


/*
 *  eqc: Equal to constant
 */
X(eqc)
{
	cpu->cd.transputer.a = (cpu->cd.transputer.a ==
	    (cpu->cd.transputer.oreg | ic->arg[0]));
	cpu->cd.transputer.oreg = 0;
}


/*
 *  opr: Operate
 *
 *  TODO/NOTE: This doesn't work too well with the way dyntrans is designed
 *             to work. Maybe it should be rewritten some day.
 */
X(opr)
{
	cpu->cd.transputer.oreg |= ic->arg[0];

	switch (cpu->cd.transputer.oreg) {

	case T_OPC_F_REV:
		{
			uint32_t tmp = cpu->cd.transputer.b;
			cpu->cd.transputer.b = cpu->cd.transputer.a;
			cpu->cd.transputer.a = tmp;
		}
		break;

	case T_OPC_F_MINT:
		cpu->cd.transputer.c = cpu->cd.transputer.b;
		cpu->cd.transputer.b = cpu->cd.transputer.a;
		cpu->cd.transputer.a = 0x80000000;
		break;

	default:fatal("UNIMPLEMENTED opr oreg 0x%"PRIx32"\n",
		    cpu->cd.transputer.oreg);
		exit(1);
	}

	cpu->cd.transputer.oreg = 0;
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

	case T_OPC_J:
		/*  relative jump  */
		ic->f = instr(j);
		/*  TODO: Samepage jump!  */

		if (cpu->cd.transputer.cpu_type.features & T_DEBUG
		    && (ib[0] & 0xf) == 0) {
			/*
			 *  From Wikipedia:  ... "and, later, the T225. This
			 *  added debugging breakpoint support (by extending
			 *  the instruction J 0)"
			 */
			fatal("TODO: Transputer Debugger support!\n");
			goto bad;
		}
		break;

	case T_OPC_LDLP:
		/*  load local pointer  */
		ic->f = instr(ldlp);
		break;

	case T_OPC_PFIX:
		/*  prefix  */
		ic->f = instr(pfix);
		break;

	case T_OPC_LDC:
		/*  load constant  */
		ic->f = instr(ldc);
		break;

	case T_OPC_NFIX:
		/*  negative prefix  */
		ic->f = instr(nfix);
		break;

	case T_OPC_LDL:
		/*  load local  */
		ic->f = instr(ldl);
		break;

	case T_OPC_AJW:
		/*  adjust workspace  */
		ic->f = instr(ajw);
		break;

	case T_OPC_EQC:
		/*  equal to constant  */
		ic->f = instr(eqc);
		break;

	case T_OPC_OPR:
		/*  operate  */
		ic->f = instr(opr);
		break;

	default:fatal("UNIMPLEMENTED opcode 0x%02x\n", ib[0]);
		goto bad;
	}


#define	DYNTRANS_TO_BE_TRANSLATED_TAIL
#include "cpu_dyntrans.c" 
#undef	DYNTRANS_TO_BE_TRANSLATED_TAIL
}

