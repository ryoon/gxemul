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
 *  $Id: cpu_dyntrans.c,v 1.3 2005-08-01 05:38:41 debug Exp $
 *
 *  Common dyntrans routines. Included from cpu_*.c.
 *
 *
 *  For DYNTRANS_CPU_RUN_INSTR: (Example for ARM)
 *
 *	DYNTRANS_IC		is the name of the instruction call struct, for
 *				example arm_instr_call.
 *	DYNTRANS_PC_TO_POINTERS	points to the correct XXX_pc_to_pointers.
 *	DYNTRANS_ARM (etc)	for special hacks.
 *	DYNTRANS_ARCH		set to "arm" for ARM, etc.
 *	DYNTRANS_IC_ENTRIES_PER_PAGE set to ARM_IC_ENTRIES_PER_PAGE.
 */


#ifdef	DYNTRANS_CPU_RUN_INSTR
/*
 *  XXX_cpu_run_instr():
 *
 *  Execute one or more instructions on a specific CPU, using dyntrans.
 *
 *  Return value is the number of instructions executed during this call,
 *  0 if no instructions were executed.
 */
int DYNTRANS_CPU_RUN_INSTR(struct emul *emul, struct cpu *cpu)
{
#ifdef DYNTRANS_ARM
	uint32_t cached_pc;
#else
	uint64_t cached_pc;
#endif
	int low_pc, n_instrs;

	DYNTRANS_PC_TO_POINTERS(cpu);

#ifdef DYNTRANS_ARM
	cached_pc = cpu->cd.arm.r[ARM_PC] & ~3;
#else
	cached_pc = cpu->pc & ~3;
#endif

	cpu->n_translated_instrs = 0;
	cpu->running_translated = 1;

	if (single_step || cpu->machine->instruction_trace) {
		/*
		 *  Single-step:
		 */
		struct DYNTRANS_IC *ic = cpu->cd.DYNTRANS_ARCH.next_ic ++;
		if (cpu->machine->instruction_trace) {
			unsigned char instr[4];
			if (!cpu->memory_rw(cpu, cpu->mem, cached_pc, &instr[0],
			    sizeof(instr), MEM_READ, CACHE_INSTRUCTION)) {
				fatal("XXX_cpu_run_instr(): could not read "
				    "the instruction\n");
			} else
				cpu_disassemble_instr(cpu->machine, cpu,
				    instr, 1, 0, 0);
		}

		/*  When single-stepping, multiple instruction calls cannot
		    be combined into one. This clears all translations:  */
		if (cpu->cd.DYNTRANS_ARCH.cur_physpage->flags & COMBINATIONS) {
			int i;
			for (i=0; i<DYNTRANS_IC_ENTRIES_PER_PAGE; i++)
				cpu->cd.DYNTRANS_ARCH.cur_physpage->ics[i].f =
				    instr(to_be_translated);
			fatal("[ Note: The translation of physical page 0x%08x"
			    " contained combinations of instructions; these "
			    "are now flushed because we are single-stepping."
			    " ]\n", cpu->cd.DYNTRANS_ARCH.
			    cur_physpage->physaddr);
			cpu->cd.DYNTRANS_ARCH.cur_physpage->flags &=
			    ~(COMBINATIONS | TRANSLATIONS);
		}

		/*  Execute just one instruction:  */
		ic->f(cpu, ic);
		n_instrs = 1;
	} else {
		/*  Execute multiple instructions:  */
		n_instrs = 0;
		for (;;) {
			struct DYNTRANS_IC *ic;

#define I		ic = cpu->cd.DYNTRANS_ARCH.next_ic ++; ic->f(cpu, ic);

			I; I; I; I; I;   I; I; I; I; I;
			I; I; I; I; I;   I; I; I; I; I;
			I; I; I; I; I;   I; I; I; I; I;
			I; I; I; I; I;   I; I; I; I; I;
			I; I; I; I; I;   I; I; I; I; I;

			I; I; I; I; I;   I; I; I; I; I;
			I; I; I; I; I;   I; I; I; I; I;
			I; I; I; I; I;   I; I; I; I; I;
			I; I; I; I; I;   I; I; I; I; I;
			I; I; I; I; I;   I; I; I; I; I;

			I; I; I; I; I;   I; I; I; I; I;
			I; I; I; I; I;   I; I; I; I; I;

			n_instrs += 120;

			if (!cpu->running_translated ||
			    n_instrs + cpu->n_translated_instrs >= 16384)
				break;
		}
	}


	/*
	 *  Update the program counter and return the correct number of
	 *  executed instructions:
	 */
	low_pc = ((size_t)cpu->cd.DYNTRANS_ARCH.next_ic - (size_t)
	    cpu->cd.DYNTRANS_ARCH.cur_ic_page) / sizeof(struct DYNTRANS_IC);

	if (low_pc >= 0 && low_pc < DYNTRANS_IC_ENTRIES_PER_PAGE) {
#ifdef DYNTRANS_ARM
		cpu->cd.arm.r[ARM_PC] &= ~((DYNTRANS_IC_ENTRIES_PER_PAGE-1)<<2);
		cpu->cd.arm.r[ARM_PC] += (low_pc << 2);
		cpu->pc = cpu->cd.arm.r[ARM_PC];
#else
		cpu->pc &= ~((DYNTRANS_IC_ENTRIES_PER_PAGE-1) << 2);
		cpu->pc += (low_pc << 2);
#endif
	} else if (low_pc == DYNTRANS_IC_ENTRIES_PER_PAGE) {
		/*  Switch to next page:  */
#ifdef DYNTRANS_ARM
		cpu->cd.arm.r[ARM_PC] &= ~((ARM_IC_ENTRIES_PER_PAGE-1) << 2);
		cpu->cd.arm.r[ARM_PC] += (ARM_IC_ENTRIES_PER_PAGE << 2);
		cpu->pc = cpu->cd.arm.r[ARM_PC];
#else
		cpu->pc &= ~((DYNTRANS_IC_ENTRIES_PER_PAGE-1) << 2);
		cpu->pc += (DYNTRANS_IC_ENTRIES_PER_PAGE << 2);
#endif
	} else {
		/*  debug("debug: Outside a page (This is actually ok)\n");  */
	}

	return n_instrs + cpu->n_translated_instrs;
}
#endif	/*  DYNTRANS_CPU_RUN_INSTR  */



#ifdef DYNTRANS_TC_ALLOCATE_DEFAULT_PAGE
/*
 *  XXX_tc_allocate_default_page():
 *
 *  Create a default page (with just pointers to instr(to_be_translated)
 *  at cpu->translation_cache_cur_ofs.
 */
/*  forward declaration of to_be_translated and end_of_page:  */
static void instr(to_be_translated)(struct cpu *, struct DYNTRANS_IC *);
static void instr(end_of_page)(struct cpu *,struct DYNTRANS_IC *);
static void DYNTRANS_TC_ALLOCATE_DEFAULT_PAGE(struct cpu *cpu,
	uint64_t physaddr)
{ 
	struct DYNTRANS_TC_PHYSPAGE *ppp;
	int i;

	/*  Create the physpage header:  */
	ppp = (struct DYNTRANS_TC_PHYSPAGE *)(cpu->translation_cache
	    + cpu->translation_cache_cur_ofs);
	ppp->next_ofs = 0;
	ppp->physaddr = physaddr;

	/*  TODO: Is this faster than copying an entire template page?  */

	for (i=0; i<DYNTRANS_IC_ENTRIES_PER_PAGE; i++)
		ppp->ics[i].f = instr(to_be_translated);

	ppp->ics[DYNTRANS_IC_ENTRIES_PER_PAGE].f = instr(end_of_page);

	cpu->translation_cache_cur_ofs += sizeof(struct DYNTRANS_TC_PHYSPAGE);
}
#endif	/*  DYNTRANS_TC_ALLOCATE_DEFAULT_PAGE  */



#ifdef DYNTRANS_PC_TO_POINTERS_FUNC
/*
 *  XXX_pc_to_pointers():
 *
 *  This function uses the current program counter (a virtual address) to
 *  find out which physical translation page to use, and then sets the current
 *  translation page pointers to that page.
 *
 *  If there was no translation page for that physical page, then an empty
 *  one is created.
 */
void DYNTRANS_PC_TO_POINTERS_FUNC(struct cpu *cpu)
{
#ifdef DYNTRANS_ARM
	uint32_t
#else
	uint64_t
#endif
	    cached_pc, physaddr, physpage_ofs;
	int pagenr, table_index;
	uint32_t *physpage_entryp;
	struct DYNTRANS_TC_PHYSPAGE *ppp;

#ifdef DYNTRANS_ARM
	cached_pc = cpu->cd.arm.r[ARM_PC];
#else
	cached_pc = cpu->pc;
#endif

	/*
	 *  TODO: virtual to physical address translation
	 */
	physaddr = cached_pc & ~(((DYNTRANS_IC_ENTRIES_PER_PAGE-1) << 2) | 3);

	if (cpu->translation_cache_cur_ofs >= DYNTRANS_CACHE_SIZE)
		cpu_create_or_reset_tc(cpu);

	pagenr = DYNTRANS_ADDR_TO_PAGENR(physaddr);
	table_index = PAGENR_TO_TABLE_INDEX(pagenr);

	physpage_entryp = &(((uint32_t *)cpu->translation_cache)[table_index]);
	physpage_ofs = *physpage_entryp;
	ppp = NULL;

	/*  Traverse the physical page chain:  */
	while (physpage_ofs != 0) {
		ppp = (struct DYNTRANS_TC_PHYSPAGE *)(cpu->translation_cache
		    + physpage_ofs);
		/*  If we found the page in the cache, then we're done:  */
		if (ppp->physaddr == physaddr)
			break;
		/*  Try the next page in the chain:  */
		physpage_ofs = ppp->next_ofs;
	}

	/*  If the offset is 0 (or ppp is NULL), then we need to create a
	    new "default" empty translation page.  */

	if (ppp == NULL) {
		fatal("CREATING page %i (physaddr 0x%08x), table index = %i\n",
		    pagenr, physaddr, table_index);
		*physpage_entryp = physpage_ofs =
		    cpu->translation_cache_cur_ofs;

		/*  Allocate a default page, with to_be_translated entries:  */
		DYNTRANS_TC_ALLOCATE(cpu, physaddr);

		ppp = (struct DYNTRANS_TC_PHYSPAGE *)(cpu->translation_cache
		    + physpage_ofs);
	}

	cpu->cd.DYNTRANS_ARCH.cur_physpage = ppp;
	cpu->cd.DYNTRANS_ARCH.cur_ic_page = &ppp->ics[0];
	cpu->cd.DYNTRANS_ARCH.next_ic = cpu->cd.DYNTRANS_ARCH.cur_ic_page +
	    DYNTRANS_PC_TO_IC_ENTRY(cached_pc);

	/*  printf("cached_pc=0x%016llx  pagenr=%lli  table_index=%lli, "
	    "physpage_ofs=0x%016llx\n", (long long)cached_pc, (long long)pagenr,
	    (long long)table_index, (long long)physpage_ofs);  */
}
#endif	/*  DYNTRANS_PC_TO_POINTERS_FUNC  */



#ifdef DYNTRANS_INVALIDATE_TC_PADDR
/*
 *  XXX_invalidate_translation_caches_paddr():
 *
 *  Invalidate all entries matching a specific physical address.
 */
void DYNTRANS_INVALIDATE_TC_PADDR(struct cpu *cpu, uint64_t paddr)
{
	int r;
#ifdef DYNTRANS_32
	uint32_t
#else
	uint64_t
#endif
	    paddr_page = paddr &
#ifdef DYNTRANS_8K
	    ~0x1fff
#else
	    ~0xfff
#endif
	    ;

	for (r=0; r<DYNTRANS_MAX_VPH_TLB_ENTRIES; r++) {
		if (cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r].valid &&
		    cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r].paddr_page ==
		    paddr_page) {
			DYNTRANS_INVALIDATE_TLB_ENTRY(cpu,
			    cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r].vaddr_page);
			cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r].valid = 0;
		}
	}
}
#endif	/*  DYNTRANS_INVALIDATE_TC_PADDR  */

