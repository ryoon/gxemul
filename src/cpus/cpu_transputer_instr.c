/*
 *  Copyright (C) 2006-2007  Anders Gavare.  All rights reserved.
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
 *  $Id: cpu_transputer_instr.c,v 1.8 2006-12-30 13:30:55 debug Exp $
 *
 *  INMOS transputer instructions.
 *
 *  Individual functions should keep track of cpu->n_translated_instrs.
 *  (n_translated_instrs is automatically increased by 1 for each function
 *  call. If no instruction was executed, then it should be decreased. If, say,
 *  4 instructions were combined into one function and executed, then it should
 *  be increased by 3.)
 *
 *  NOTE: The PC does not need to be synched before e.g. memory_rw(), because
 *        there is no MMU in the Transputer, and hence there can not be any
 *        exceptions on memory accesses.
 */


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
 *  ldnl: Load non-local
 */
X(ldnl)
{
	uint32_t addr, w32;
	uint8_t word[sizeof(uint32_t)];
	unsigned char *page;

	cpu->cd.transputer.oreg |= ic->arg[0];
	addr = cpu->cd.transputer.oreg * sizeof(uint32_t) +
	    cpu->cd.transputer.a;

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
 *  ldnlp: Load non-local pointer
 */
X(ldnlp)
{
	cpu->cd.transputer.a += (cpu->cd.transputer.oreg | ic->arg[0])
	    * sizeof(uint32_t);
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
printf("A w32 = %08"PRIx32"\n", w32);
	} else {
		w32 = *(uint32_t *) &page[TRANSPUTER_PC_TO_IC_ENTRY(addr)];
printf("B w32 = %08"PRIx32"\n", w32);
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
 *  stl: Store local
 */
X(stl)
{
	uint32_t addr, w32;
	unsigned char *page;

	w32 = LE32_TO_HOST(cpu->cd.transputer.a);
	cpu->cd.transputer.oreg |= ic->arg[0];
	addr = cpu->cd.transputer.oreg * sizeof(uint32_t) +
	    cpu->cd.transputer.wptr;

	page = cpu->cd.transputer.host_store[TRANSPUTER_ADDR_TO_PAGENR(addr)];
	if (page == NULL) {
		cpu->memory_rw(cpu, cpu->mem, addr, (void *)&w32, sizeof(w32),
		    MEM_WRITE, CACHE_DATA);
	} else {
		*((uint32_t *) &page[TRANSPUTER_PC_TO_IC_ENTRY(addr)]) = w32;
	}

	cpu->cd.transputer.a = cpu->cd.transputer.b;
	cpu->cd.transputer.b = cpu->cd.transputer.c;
	cpu->cd.transputer.oreg = 0;
}


/*
 *  stnl: Store non-local
 */
X(stnl)
{
	uint32_t addr, w32;
	unsigned char *page;

	w32 = LE32_TO_HOST(cpu->cd.transputer.b);
	cpu->cd.transputer.oreg |= ic->arg[0];
	addr = cpu->cd.transputer.oreg * sizeof(uint32_t) +
	    cpu->cd.transputer.a;

	page = cpu->cd.transputer.host_store[TRANSPUTER_ADDR_TO_PAGENR(addr)];
	if (page == NULL) {
		cpu->memory_rw(cpu, cpu->mem, addr, (void *)&w32, sizeof(w32),
		    MEM_WRITE, CACHE_DATA);
	} else {
		*((uint32_t *) &page[TRANSPUTER_PC_TO_IC_ENTRY(addr)]) = w32;
	}

	cpu->cd.transputer.a = cpu->cd.transputer.c;
	cpu->cd.transputer.oreg = 0;
}


/*
 *  opr: Operate
 *
 *  TODO/NOTE: This doesn't work too well with the way dyntrans is designed
 *             to work. Maybe it should be rewritten some day. But how?
 *             Right now it works almost 100% like an old-style interpretation
 *             function.
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

	case T_OPC_F_ADD:
		{
			uint32_t old_a = cpu->cd.transputer.a;
			cpu->cd.transputer.a = cpu->cd.transputer.b + old_a;
			if ((cpu->cd.transputer.a & 0x80000000) !=
			    (cpu->cd.transputer.b & old_a & 0x80000000))
				cpu->cd.transputer.error = 1;
			cpu->cd.transputer.b = cpu->cd.transputer.c;
		}
		break;

	case T_OPC_F_SUB:
		{
			uint32_t old_a = cpu->cd.transputer.a;
			cpu->cd.transputer.a = cpu->cd.transputer.b - old_a;
			if ((cpu->cd.transputer.a & 0x80000000) !=
			    (cpu->cd.transputer.b & old_a & 0x80000000))
				cpu->cd.transputer.error = 1;
			cpu->cd.transputer.b = cpu->cd.transputer.c;
		}
		break;

	case T_OPC_F_LDPI:
		/*  Load pointer to (next) instruction  */
		{
			int low_pc = ((size_t)ic -
			    (size_t)cpu->cd.transputer.cur_ic_page)
			    / sizeof(struct transputer_instr_call);
			cpu->pc &= ~(TRANSPUTER_IC_ENTRIES_PER_PAGE-1);
			cpu->pc += low_pc;
			cpu->cd.transputer.a += cpu->pc + 1;
		}
		break;

	case T_OPC_F_STHF:
		cpu->cd.transputer.fptrreg0 = cpu->cd.transputer.a;
		cpu->cd.transputer.a = cpu->cd.transputer.b;
		cpu->cd.transputer.b = cpu->cd.transputer.c;
		break;

	case T_OPC_F_STLF:
		cpu->cd.transputer.fptrreg1 = cpu->cd.transputer.a;
		cpu->cd.transputer.a = cpu->cd.transputer.b;
		cpu->cd.transputer.b = cpu->cd.transputer.c;
		break;

	case T_OPC_F_BCNT:
		cpu->cd.transputer.a <<= 2;
		break;

	case T_OPC_F_GAJW:
		{
			uint32_t old_wptr = cpu->cd.transputer.wptr;
			cpu->cd.transputer.wptr = cpu->cd.transputer.a & ~3;
			cpu->cd.transputer.a = old_wptr;
		}
		break;

	case T_OPC_F_WCNT:
		cpu->cd.transputer.c = cpu->cd.transputer.b;
		cpu->cd.transputer.b = cpu->cd.transputer.a & 3;
		cpu->cd.transputer.a >>= 2;
		break;

	case T_OPC_F_MINT:
		cpu->cd.transputer.c = cpu->cd.transputer.b;
		cpu->cd.transputer.b = cpu->cd.transputer.a;
		cpu->cd.transputer.a = 0x80000000;
		break;

	case T_OPC_F_MOVE:
		/*  TODO: This can be optimized a lot by using the host's
		    memmove(). The only important thing to consider is
		    if src or dst crosses a host memblock boundary.  */
		{
			uint32_t i, src = cpu->cd.transputer.c,
			    dst = cpu->cd.transputer.b;
			uint8_t byte;
			for (i=1; i<=cpu->cd.transputer.a; i++) {
				cpu->memory_rw(cpu, cpu->mem, src ++, &byte,
				    1, MEM_READ, CACHE_DATA);
				cpu->memory_rw(cpu, cpu->mem, dst ++, &byte,
				    1, MEM_WRITE, CACHE_DATA);
			}
		}
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
 *  Translate an instruction word into an transputer_instr_call. ic is filled
 *  in with valid data for the translated instruction, or a "nothing"
 *  instruction if there was a translation failure. The newly translated
 *  instruction is then executed.
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

	case T_OPC_LDNL:
		/*  load non-local  */
		ic->f = instr(ldnl);
		break;

	case T_OPC_LDC:
		/*  load constant  */
		ic->f = instr(ldc);
		break;

	case T_OPC_LDNLP:
		/*  load non-local pointer  */
		ic->f = instr(ldnlp);
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

	case T_OPC_STL:
		/*  store local  */
		ic->f = instr(stl);
		break;

	case T_OPC_STNL:
		/*  store non-local  */
		ic->f = instr(stnl);
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

