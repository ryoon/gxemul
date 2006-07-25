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
 *  $Id: cpu_sh_instr.c,v 1.8 2006-07-25 21:03:25 debug Exp $
 *
 *  SH instructions.
 *
 *  Individual functions should keep track of cpu->n_translated_instrs.
 *  (If no instruction was executed, then it should be decreased. If, say, 4
 *  instructions were combined into one function and executed, then it should
 *  be increased by 3.)
 */


/*
 *  nop: Nothing
 */
X(nop)
{
}


/*
 *  mov_rm_rn:  Copy rm into rn
 *
 *  arg[0] = ptr to rm
 *  arg[1] = ptr to rn
 */
X(mov_rm_rn)
{
	reg(ic->arg[1]) = reg(ic->arg[0]);
}


/*
 *  mov_imm_rn:  Set rn to an signed 8-bit value
 *
 *  arg[0] = int8_t imm, extended to at least int32_t
 *  arg[1] = ptr to rn
 */
X(mov_imm_rn)
{
	reg(ic->arg[1]) = (int32_t)ic->arg[0];
}


/*
 *  mov_l_disp_pc_rn:  Set rn to an immediate value relative to the current pc
 *
 *  arg[0] = offset from beginning of the current pc's page
 *  arg[1] = ptr to rn
 */
X(mov_l_disp_pc_rn)
{
	reg(ic->arg[1]) = ic->arg[0] + (cpu->pc &
	    ~((SH_IC_ENTRIES_PER_PAGE-1) << SH_INSTR_ALIGNMENT_SHIFT));
}


/*
 *  shll_rn: Shift rn left by 1
 *
 *  arg[0] = ptr to rn
 */
X(shll_rn)
{
	uint32_t rn = reg(ic->arg[0]);
	if (rn >> 31)
		cpu->cd.sh.sr |= SH_SR_T;
	else
		cpu->cd.sh.sr &= ~SH_SR_T;
	reg(ic->arg[0]) = rn << 1;
}


/*
 *  stc_sr_rn: Store SR into Rn
 *
 *  arg[0] = ptr to rn
 */
X(stc_sr_rn)
{
	if (!(cpu->cd.sh.sr & SH_SR_MD)) {
		fatal("TODO: Throw RESINST exception, if MD = 0.\n");
		exit(1);
	}

	reg(ic->arg[0]) = cpu->cd.sh.sr;
}


/*****************************************************************************/


X(end_of_page)
{
	/*  Update the PC:  (offset 0, but on the next page)  */
	cpu->pc &= ~((SH_IC_ENTRIES_PER_PAGE-1) <<
	    SH_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (SH_IC_ENTRIES_PER_PAGE <<
	    SH_INSTR_ALIGNMENT_SHIFT);

	/*  Find the new physical page and update the translation pointers:  */
	DYNTRANS_PC_TO_POINTERS(cpu);

	/*  end_of_page doesn't count as an executed instruction:  */
	cpu->n_translated_instrs --;
}


/*****************************************************************************/


/*
 *  sh_instr_to_be_translated():
 *
 *  Translate an instruction word into an sh_instr_call. ic is filled in with
 *  valid data for the translated instruction, or a "nothing" instruction if
 *  there was a translation failure. The newly translated instruction is then
 *  executed.
 */
X(to_be_translated)
{
	uint64_t addr, low_pc;
	uint32_t iword;
	unsigned char *page;
	unsigned char ib[4];
	int main_opcode, isize = cpu->cd.sh.compact? 2 : sizeof(ib);
	int in_crosspage_delayslot = 0, r8, r4, lo4, lo8;
	/*  void (*samepage_function)(struct cpu *, struct sh_instr_call *);  */

	/*  Figure out the (virtual) address of the instruction:  */
	low_pc = ((size_t)ic - (size_t)cpu->cd.sh.cur_ic_page)
	    / sizeof(struct sh_instr_call);

	/*  Special case for branch with delayslot on the next page:  */
	if (cpu->delay_slot == TO_BE_DELAYED && low_pc == 0) {
		/*  fatal("[ delay-slot translation across page "
		    "boundary ]\n");  */
		in_crosspage_delayslot = 1;
	}

	addr = cpu->pc & ~((SH_IC_ENTRIES_PER_PAGE-1)
	    << SH_INSTR_ALIGNMENT_SHIFT);
	addr += (low_pc << SH_INSTR_ALIGNMENT_SHIFT);
	cpu->pc = (MODE_int_t)addr;
	addr &= ~((1 << SH_INSTR_ALIGNMENT_SHIFT) - 1);

	/*  Read the instruction word from memory:  */
#ifdef MODE32
	page = cpu->cd.sh.host_load[(uint32_t)addr >> 12];
#else
	{
		const uint32_t mask1 = (1 << DYNTRANS_L1N) - 1;
		const uint32_t mask2 = (1 << DYNTRANS_L2N) - 1;
		const uint32_t mask3 = (1 << DYNTRANS_L3N) - 1;
		uint32_t x1 = (addr >> (64-DYNTRANS_L1N)) & mask1;
		uint32_t x2 = (addr >> (64-DYNTRANS_L1N-DYNTRANS_L2N)) & mask2;
		uint32_t x3 = (addr >> (64-DYNTRANS_L1N-DYNTRANS_L2N-
		    DYNTRANS_L3N)) & mask3;
		struct DYNTRANS_L2_64_TABLE *l2 = cpu->cd.sh.l1_64[x1];
		struct DYNTRANS_L3_64_TABLE *l3 = l2->l3[x2];
		page = l3->host_load[x3];
	}
#endif

	if (page != NULL) {
		/*  fatal("TRANSLATION HIT!\n");  */
		memcpy(ib, page + (addr & 0xfff), isize);
	} else {
		/*  fatal("TRANSLATION MISS!\n");  */
		if (!cpu->memory_rw(cpu, cpu->mem, addr, ib,
		    isize, MEM_READ, CACHE_INSTRUCTION)) {
			fatal("to_be_translated(): read failed: TODO\n");
			goto bad;
		}
	}

	iword = *((uint32_t *)&ib[0]);

	if (cpu->cd.sh.compact) {
		if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
			iword = LE16_TO_HOST(iword);
		else
			iword = BE16_TO_HOST(iword);
		main_opcode = iword >> 12;
		r8 = (iword >> 8) & 0xf;
		r4 = (iword >> 4) & 0xf;
		lo8 = iword & 0xff;
		lo4 = iword & 0xf;
	} else {
		if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
			iword = LE32_TO_HOST(iword);
		else
			iword = BE32_TO_HOST(iword);
		main_opcode = -1;	/*  TODO  */
		fatal("SH5/SH64 isn't implemented yet. Sorry.\n");
		goto bad;
	}


#define DYNTRANS_TO_BE_TRANSLATED_HEAD
#include "cpu_dyntrans.c"
#undef  DYNTRANS_TO_BE_TRANSLATED_HEAD


	/*
	 *  Translate the instruction:
	 */

	switch (main_opcode) {

	case 0x0:
		switch (lo8) {
		case 0x02:	/*  STC SR,Rn  */
			ic->f = instr(stc_sr_rn);
			ic->arg[0] = (size_t)&cpu->cd.sh.r[r8];	/* n */
			break;
		case 0x09:	/*  NOP  */
			ic->f = instr(nop);
			if (iword & 0x0f00) {
				fatal("Unimplemented NOP variant?\n");
				goto bad;
			}
			break;
		default:fatal("Unimplemented opcode 0x%x,0x03%x\n",
			    main_opcode, iword & 0xfff);
			goto bad;
		}
		break;

	case 0x4:	/*  SHLL Rn  */
		switch (lo8) {
		case 0x00:
			ic->f = instr(shll_rn);
			ic->arg[0] = (size_t)&cpu->cd.sh.r[r8];	/* n */
			break;
		default:fatal("Unimplemented opcode 0x%x,0x02%x\n",
			    main_opcode, lo8);
			goto bad;
		}
		break;

	case 0x6:
		switch (lo4) {
		case 0x3:	/*  MOV Rm,Rn  */
			ic->f = instr(mov_rm_rn);
			ic->arg[0] = (size_t)&cpu->cd.sh.r[r4];	/* m */
			ic->arg[1] = (size_t)&cpu->cd.sh.r[r8];	/* n */
			break;
		default:fatal("Unimplemented opcode 0x%x,0x%x\n",
			    main_opcode, lo4);
			goto bad;
		}
		break;

	case 0xd:	/*  MOV.L @(disp,PC),Rn  */
		ic->f = instr(mov_l_disp_pc_rn);
		ic->arg[0] = lo8 * 4 + (addr & ((SH_IC_ENTRIES_PER_PAGE-1)
		    << SH_INSTR_ALIGNMENT_SHIFT) & ~3) + 4;
		ic->arg[1] = (size_t)&cpu->cd.sh.r[r8];	/* n */
		break;

	case 0xe:	/*  MOV #imm,Rn  */
		ic->f = instr(mov_imm_rn);
		ic->arg[0] = (int8_t)lo8;
		ic->arg[1] = (size_t)&cpu->cd.sh.r[r8];	/* n */
		break;

	default:fatal("Unimplemented main opcode 0x%x\n", main_opcode);
		goto bad;
	}


#define	DYNTRANS_TO_BE_TRANSLATED_TAIL
#include "cpu_dyntrans.c" 
#undef	DYNTRANS_TO_BE_TRANSLATED_TAIL
}

