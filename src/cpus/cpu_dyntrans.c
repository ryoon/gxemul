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
 *  $Id: cpu_dyntrans.c,v 1.53 2006-02-09 22:43:56 debug Exp $
 *
 *  Common dyntrans routines. Included from cpu_*.c.
 */


#ifdef	DYNTRANS_CPU_RUN_INSTR
#if 1	/*  IC statistics:  */
static void gather_statistics(struct cpu *cpu)
{
	struct DYNTRANS_IC *ic = cpu->cd.DYNTRANS_ARCH.next_ic;
	static long long n = 0;
	static FILE *f = NULL;

	n++;
	if (n < 100000000)
		return;

	if (f == NULL) {
		f = fopen("instruction_call_statistics.raw", "w");
		if (f == NULL) {
			fatal("Unable to open statistics file for output.\n");
			exit(1);
		}
	}
	fwrite(&ic->f, 1, sizeof(void *), f);
}
#else	/*  PC statistics:  */
static void gather_statistics(struct cpu *cpu)
{
	uint64_t a;
	int low_pc = ((size_t)cpu->cd.DYNTRANS_ARCH.next_ic - (size_t)
	    cpu->cd.DYNTRANS_ARCH.cur_ic_page) / sizeof(struct DYNTRANS_IC);
	if (low_pc < 0 || low_pc >= DYNTRANS_IC_ENTRIES_PER_PAGE)
		return;

#if 0
	/*  Use the physical address:  */
	cpu->cd.DYNTRANS_ARCH.cur_physpage = (void *)
	    cpu->cd.DYNTRANS_ARCH.cur_ic_page;
	a = cpu->cd.DYNTRANS_ARCH.cur_physpage->physaddr;
#else
	/*  Use the PC (virtual address):  */
	a = cpu->pc;
#endif

	a &= ~((DYNTRANS_IC_ENTRIES_PER_PAGE-1) <<
	    DYNTRANS_INSTR_ALIGNMENT_SHIFT);
	a += low_pc << DYNTRANS_INSTR_ALIGNMENT_SHIFT;

	/*
	 *  TODO: Everything below this line should be cleaned up :-)
	 */
a &= 0x03ffffff;
{
	static long long *array = NULL;
	static char *array_16kpage_in_use = NULL;
	static int n = 0;
	a >>= DYNTRANS_INSTR_ALIGNMENT_SHIFT;
	if (array == NULL)
		array = zeroed_alloc(sizeof(long long) * 16384*1024);
	if (array_16kpage_in_use == NULL)
		array_16kpage_in_use = zeroed_alloc(sizeof(char) * 1024);
	a &= (16384*1024-1);
	array[a] ++;
	array_16kpage_in_use[a / 16384] = 1;
	n++;
	if ((n & 0x3fffffff) == 0) {
		FILE *f = fopen("statistics.out", "w");
		int i, j;
		printf("Saving statistics... "); fflush(stdout);
		for (i=0; i<1024; i++)
			if (array_16kpage_in_use[i]) {
				for (j=0; j<16384; j++)
					if (array[i*16384 + j] > 0)
						fprintf(f, "%lli\t0x%016llx\n",
						    (long long)array[i*16384+j],
						    (long long)((i*16384+j) <<
				DYNTRANS_INSTR_ALIGNMENT_SHIFT));
			}
		fclose(f);
		printf("n=0x%08x\n", n);
	}
}
}
#endif	/*  PC statistics  */

#define S		gather_statistics(cpu)

#ifdef DYNTRANS_VARIABLE_INSTRUCTION_LENGTH
#define I		ic = cpu->cd.DYNTRANS_ARCH.next_ic;		\
			cpu->cd.DYNTRANS_ARCH.next_ic += ic->arg[0];	\
			ic->f(cpu, ic);
#else
#define I		ic = cpu->cd.DYNTRANS_ARCH.next_ic ++; ic->f(cpu, ic);
#endif


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
#ifdef MODE32
	uint32_t cached_pc;
#else
	uint64_t cached_pc;
#endif
	int low_pc, n_instrs;

#ifdef DYNTRANS_DUALMODE_32
	if (cpu->is_32bit)
		DYNTRANS_PC_TO_POINTERS32(cpu);
	else
#endif
	DYNTRANS_PC_TO_POINTERS(cpu);

	/*
	 *  Interrupt assertion?  (This is _below_ the initial PC to pointer
	 *  conversion; if the conversion caused an exception of some kind
	 *  then interrupts are probably disabled, and the exception will get
	 *  priority over device interrupts.)
 	 */
#ifdef DYNTRANS_ARM
	if (cpu->cd.arm.irq_asserted && !(cpu->cd.arm.cpsr & ARM_FLAG_I))
		arm_exception(cpu, ARM_EXCEPTION_IRQ);
#endif
#ifdef DYNTRANS_PPC
	if (cpu->cd.ppc.dec_intr_pending && cpu->cd.ppc.msr & PPC_MSR_EE) {
		ppc_exception(cpu, PPC_EXCEPTION_DEC);
		cpu->cd.ppc.dec_intr_pending = 0;
	}
	if (cpu->cd.ppc.irq_asserted && cpu->cd.ppc.msr & PPC_MSR_EE)
		ppc_exception(cpu, PPC_EXCEPTION_EI);
#endif

	cached_pc = cpu->pc;

	cpu->n_translated_instrs = 0;
	cpu->running_translated = 1;

	cpu->cd.DYNTRANS_ARCH.cur_physpage = (void *)
	    cpu->cd.DYNTRANS_ARCH.cur_ic_page;

	if (single_step || cpu->machine->instruction_trace) {
		/*
		 *  Single-step:
		 */
		struct DYNTRANS_IC *ic = cpu->cd.DYNTRANS_ARCH.next_ic
#ifndef DYNTRANS_VARIABLE_INSTRUCTION_LENGTH
		    ++
#endif
		    ;
		if (cpu->machine->instruction_trace) {
#ifdef DYNTRANS_X86
			unsigned char instr[17];
			cpu->cd.x86.cursegment = X86_S_CS;
			cpu->cd.x86.seg_override = 0;
#else
#ifdef DYNTRANS_M68K
			unsigned char instr[16];	/*  TODO: 16?  */
#else
			unsigned char instr[4];		/*  General case...  */
#endif
#endif
			if (!cpu->memory_rw(cpu, cpu->mem, cached_pc, &instr[0],
			    sizeof(instr), MEM_READ, CACHE_INSTRUCTION)) {
				fatal("XXX_cpu_run_instr(): could not read "
				    "the instruction\n");
			} else {
				cpu_disassemble_instr(cpu->machine, cpu,
				    instr, 1, 0, 0);
#ifdef DYNTRANS_MIPS
/*  TODO: generalize, not just MIPS  */
				/*  Show the instruction in the delay slot,
				    if any:  */
				fatal("TODO: check for delay slot!\n");
#endif
			}
		}

		/*  When single-stepping, multiple instruction calls cannot
		    be combined into one. This clears all translations:  */
		if (cpu->cd.DYNTRANS_ARCH.cur_physpage->flags & COMBINATIONS) {
			int i;
			for (i=0; i<DYNTRANS_IC_ENTRIES_PER_PAGE; i++)
				cpu->cd.DYNTRANS_ARCH.cur_physpage->ics[i].f =
#ifdef DYNTRANS_DUALMODE_32
				    cpu->is_32bit?
				        instr32(to_be_translated) :
#endif
				        instr(to_be_translated);
			fatal("[ Note: The translation of physical page 0x%llx"
			    " contained combinations of instructions; these "
			    "are now flushed because we are single-stepping."
			    " ]\n", (long long)cpu->cd.DYNTRANS_ARCH.
			    cur_physpage->physaddr);
			cpu->cd.DYNTRANS_ARCH.cur_physpage->flags &=
			    ~(COMBINATIONS | TRANSLATIONS);
		}

		if (show_opcode_statistics)
			S;

		/*  Execute just one instruction:  */
		ic->f(cpu, ic);
		n_instrs = 1;
	} else if (show_opcode_statistics) {
		/*  Gather statistics while executing multiple instructions:  */
		n_instrs = 0;
		for (;;) {
			struct DYNTRANS_IC *ic;

			S; I; S; I; S; I; S; I; S; I; S; I;
			S; I; S; I; S; I; S; I; S; I; S; I;
			S; I; S; I; S; I; S; I; S; I; S; I;
			S; I; S; I; S; I; S; I; S; I; S; I;

			n_instrs += 24;

			if (!cpu->running_translated ||
			    n_instrs + cpu->n_translated_instrs >= 16384)
				break;
		}
	} else {
		/*  Execute multiple instructions:  */
		n_instrs = 0;
		for (;;) {
			struct DYNTRANS_IC *ic;

			I; I; I; I; I;   I; I; I; I; I;
			I; I; I; I; I;   I; I; I; I; I;
			I; I; I; I; I;   I; I; I; I; I;
			I; I; I; I; I;   I; I; I; I; I;
			I; I; I; I; I;   I; I; I; I; I;

			I; I; I; I; I;   I; I; I; I; I;

			n_instrs += 60;

			if (!cpu->running_translated ||
			    n_instrs + cpu->n_translated_instrs >= 16384)
				break;
		}
	}

	n_instrs += cpu->n_translated_instrs;

	/*  Synchronize the program counter:  */
	low_pc = ((size_t)cpu->cd.DYNTRANS_ARCH.next_ic - (size_t)
	    cpu->cd.DYNTRANS_ARCH.cur_ic_page) / sizeof(struct DYNTRANS_IC);
	if (low_pc >= 0 && low_pc < DYNTRANS_IC_ENTRIES_PER_PAGE) {
		cpu->pc &= ~((DYNTRANS_IC_ENTRIES_PER_PAGE-1) <<
		    DYNTRANS_INSTR_ALIGNMENT_SHIFT);
		cpu->pc += (low_pc << DYNTRANS_INSTR_ALIGNMENT_SHIFT);
	} else if (low_pc == DYNTRANS_IC_ENTRIES_PER_PAGE) {
		/*  Switch to next page:  */
		cpu->pc &= ~((DYNTRANS_IC_ENTRIES_PER_PAGE-1) <<
		    DYNTRANS_INSTR_ALIGNMENT_SHIFT);
		cpu->pc += (DYNTRANS_IC_ENTRIES_PER_PAGE <<
		    DYNTRANS_INSTR_ALIGNMENT_SHIFT);
	} else if (low_pc == DYNTRANS_IC_ENTRIES_PER_PAGE + 1) {
		/*  Switch to next page and skip an instruction which was
		    already executed (in a delay slot):  */
		cpu->pc &= ~((DYNTRANS_IC_ENTRIES_PER_PAGE-1) <<
		    DYNTRANS_INSTR_ALIGNMENT_SHIFT);
		cpu->pc += ((DYNTRANS_IC_ENTRIES_PER_PAGE + 1) <<
		    DYNTRANS_INSTR_ALIGNMENT_SHIFT);
	}

#ifdef DYNTRANS_PPC
	/*  Update the Decrementer and Time base registers:  */
	{
		uint32_t old = cpu->cd.ppc.spr[SPR_DEC];
		cpu->cd.ppc.spr[SPR_DEC] = (uint32_t) (old - n_instrs);
		if ((old >> 31) == 0 && (cpu->cd.ppc.spr[SPR_DEC] >> 31) == 1
		    && !(cpu->cd.ppc.cpu_type.flags & PPC_NO_DEC))
			cpu->cd.ppc.dec_intr_pending = 1;
		old = cpu->cd.ppc.spr[SPR_TBL];
		cpu->cd.ppc.spr[SPR_TBL] += n_instrs;
		if ((old >> 31) == 1 && (cpu->cd.ppc.spr[SPR_TBL] >> 31) == 0)
			cpu->cd.ppc.spr[SPR_TBU] ++;
	}
#endif

	/*  Return the nr of instructions executed:  */
	return n_instrs;
}
#endif	/*  DYNTRANS_CPU_RUN_INSTR  */



#ifdef DYNTRANS_FUNCTION_TRACE
/*
 *  XXX_cpu_functioncall_trace():
 *
 *  Without this function, the main trace tree function prints something
 *  like    <f()>  or  <0x1234()>   on a function call. It is up to this
 *  function to print the arguments passed.
 */
void DYNTRANS_FUNCTION_TRACE(struct cpu *cpu, uint64_t f, int n_args)
{
        char strbuf[50];
	char *symbol;
	uint64_t ot;
	int x, print_dots = 1, n_args_to_print =
#ifdef DYNTRANS_ALPHA
	    6
#else
#ifdef DYNTRANS_SH
	    8
#else
	    4	/*  Default value for most archs  */
#endif
#endif
	    ;

	if (n_args >= 0 && n_args <= n_args_to_print) {
		print_dots = 0;
		n_args_to_print = n_args;
	}

	/*
	 *  TODO: The type of each argument should be taken from the symbol
	 *  table, in some way.
	 *
	 *  The code here does a kind of "heuristic guess" regarding what the
	 *  argument values might mean. Sometimes the output looks weird, but
	 *  usually it looks good enough.
	 *
	 *  Print ".." afterwards to show that there might be more arguments
	 *  than were passed in register.
	 */
	for (x=0; x<n_args_to_print; x++) {
		int64_t d;
#ifdef DYNTRANS_X86
		d = 0;		/*  TODO  */
#else
		/*  Args in registers:  */
		d = cpu->cd.DYNTRANS_ARCH.
#ifdef DYNTRANS_ALPHA
		    r[ALPHA_A0
#endif
#ifdef DYNTRANS_ARM
		    r[0
#endif
#ifdef DYNTRANS_AVR
		    /*  TODO: 24,25 = first register, but then
			they go downwards, ie. 22,23 and so on  */
		    r[24
#endif
#ifdef DYNTRANS_HPPA
		    r[0		/*  TODO  */
#endif
#ifdef DYNTRANS_I960
		    r[0		/*  TODO  */
#endif
#ifdef DYNTRANS_IA64
		    r[0		/*  TODO  */
#endif
#ifdef DYNTRANS_M68K
		    d[0		/*  TODO  */
#endif
#ifdef DYNTRANS_MIPS
		    gpr[MIPS_GPR_A0
#endif
#ifdef DYNTRANS_PPC
		    gpr[3
#endif
#ifdef DYNTRANS_SH
		    r[2
#endif
#ifdef DYNTRANS_SPARC
		    r[24
#endif
		    + x];
#endif
		symbol = get_symbol_name(&cpu->machine->symbol_context, d, &ot);

		if (d > -256 && d < 256)
			fatal("%i", (int)d);
		else if (memory_points_to_string(cpu, cpu->mem, d, 1))
			fatal("\"%s\"", memory_conv_to_string(cpu,
			    cpu->mem, d, strbuf, sizeof(strbuf)));
		else if (symbol != NULL && ot == 0)
			fatal("&%s", symbol);
		else {
			if (cpu->is_32bit)
				fatal("0x%x", (int)d);
			else
				fatal("0x%llx", (long long)d);
		}

		if (x < n_args_to_print - 1)
			fatal(",");
	}

	if (print_dots)
		fatal(",..");
}
#endif



#ifdef DYNTRANS_TC_ALLOCATE_DEFAULT_PAGE

/*  forward declaration of to_be_translated and end_of_page:  */
static void instr(to_be_translated)(struct cpu *, struct DYNTRANS_IC *);
static void instr(end_of_page)(struct cpu *,struct DYNTRANS_IC *);
#ifdef DYNTRANS_DUALMODE_32
static void instr32(to_be_translated)(struct cpu *, struct DYNTRANS_IC *);
static void instr32(end_of_page)(struct cpu *,struct DYNTRANS_IC *);
#endif

#ifdef DYNTRANS_DELAYSLOT
static void instr(end_of_page2)(struct cpu *,struct DYNTRANS_IC *);
#ifdef DYNTRANS_DUALMODE_32
static void instr32(end_of_page2)(struct cpu *,struct DYNTRANS_IC *);
#endif
#endif

/*
 *  XXX_tc_allocate_default_page():
 *
 *  Create a default page (with just pointers to instr(to_be_translated)
 *  at cpu->translation_cache_cur_ofs.
 */
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
		ppp->ics[i].f =
#ifdef DYNTRANS_DUALMODE_32
		    cpu->is_32bit? instr32(to_be_translated) :
#endif
		    instr(to_be_translated);

	ppp->ics[DYNTRANS_IC_ENTRIES_PER_PAGE + 0].f =
#ifdef DYNTRANS_DUALMODE_32
	    cpu->is_32bit? instr32(end_of_page) :
#endif
	    instr(end_of_page);

#ifdef DYNTRANS_DELAYSLOT
	ppp->ics[DYNTRANS_IC_ENTRIES_PER_PAGE + 1].f =
#ifdef DYNTRANS_DUALMODE_32
	    cpu->is_32bit? instr32(end_of_page2) :
#endif
	    instr(end_of_page2);
#endif

	cpu->translation_cache_cur_ofs += sizeof(struct DYNTRANS_TC_PHYSPAGE);

	cpu->translation_cache_cur_ofs --;
	cpu->translation_cache_cur_ofs |= 63;
	cpu->translation_cache_cur_ofs ++;
}
#endif	/*  DYNTRANS_TC_ALLOCATE_DEFAULT_PAGE  */



#ifdef DYNTRANS_PC_TO_POINTERS_FUNC
/*
 *  XXX_pc_to_pointers_generic():
 *
 *  Generic case. See DYNTRANS_PC_TO_POINTERS_FUNC below.
 */
void DYNTRANS_PC_TO_POINTERS_GENERIC(struct cpu *cpu)
{
#ifdef MODE32
	uint32_t
#else
	uint64_t
#endif
	    cached_pc, physaddr = 0;
	uint32_t physpage_ofs;
	int ok, pagenr, table_index;
	uint32_t *physpage_entryp;
	struct DYNTRANS_TC_PHYSPAGE *ppp;

#ifdef MODE32
	int index;
	cached_pc = cpu->pc;
	index = DYNTRANS_ADDR_TO_PAGENR(cached_pc);
#else
#ifdef DYNTRANS_ALPHA
	uint32_t a, b;
	int kernel = 0;
	struct alpha_vph_page *vph_p;
	cached_pc = cpu->pc;
	a = (cached_pc >> ALPHA_LEVEL0_SHIFT) & (ALPHA_LEVEL0 - 1);
	b = (cached_pc >> ALPHA_LEVEL1_SHIFT) & (ALPHA_LEVEL1 - 1);
	if ((cached_pc >> ALPHA_TOPSHIFT) == ALPHA_TOP_KERNEL) {
		vph_p = cpu->cd.alpha.vph_table0_kernel[a];
		kernel = 1;
	} else
		vph_p = cpu->cd.alpha.vph_table0[a];
#else
	fatal("Neither alpha nor 32-bit? 3\n");
	exit(1);
#endif
#endif

	/*  Virtual to physical address translation:  */
	ok = 0;
#ifdef MODE32
	if (cpu->cd.DYNTRANS_ARCH.host_load[index] != NULL) {
		physaddr = cpu->cd.DYNTRANS_ARCH.phys_addr[index];
		ok = 1;
	}
#else
#ifdef DYNTRANS_ALPHA
	if (vph_p->host_load[b] != NULL) {
		physaddr = vph_p->phys_addr[b];
		ok = 1;
	}
#else
	fatal("Neither alpha nor 32-bit? 4\n");
	exit(1);
#endif
#endif

	if (!ok) {
		uint64_t paddr;
		if (cpu->translate_address != NULL)
			ok = cpu->translate_address(cpu, cached_pc,
			    &paddr, FLAG_INSTR);
		else {
			paddr = cached_pc;
			ok = 1;
		}
		if (!ok) {
			/*  fatal("TODO: instruction vaddr=>paddr translation"
			    " failed. vaddr=0x%llx\n", (long long)cached_pc);
			fatal("!! cpu->pc=0x%llx\n", (long long)cpu->pc);  */

			ok = cpu->translate_address(cpu, cpu->pc, &paddr,
			    FLAG_INSTR);

			/*  printf("EXCEPTION HANDLER: vaddr = 0x%x ==> "
			    "paddr = 0x%x\n", (int)cpu->pc, (int)paddr);
			fatal("!? cpu->pc=0x%llx\n", (long long)cpu->pc);  */

			if (!ok) {
				fatal("FATAL: could not find physical"
				    " address of the exception handler?");
				exit(1);
			}
		}
		cached_pc = cpu->pc;
#ifdef MODE32
		index = DYNTRANS_ADDR_TO_PAGENR(cached_pc);
#endif
		physaddr = paddr;
	}

#ifdef MODE32
	if (cpu->cd.DYNTRANS_ARCH.host_load[index] == NULL) {
		unsigned char *host_page = memory_paddr_to_hostaddr(cpu->mem,
		    physaddr, MEM_READ);
		if (host_page != NULL) {
			int q = DYNTRANS_PAGESIZE - 1;
			host_page += (physaddr &
			    ((1 << BITS_PER_MEMBLOCK) - 1) & ~q);
			cpu->update_translation_table(cpu, cached_pc & ~q,
			    host_page, TLB_CODE, physaddr & ~q);
		}
	}
#endif

	if (cpu->translation_cache_cur_ofs >= DYNTRANS_CACHE_SIZE) {
		debug("[ dyntrans: resetting the translation cache ]\n");
		cpu_create_or_reset_tc(cpu);
	}

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
		/*  fatal("CREATING page %lli (physaddr 0x%llx), table index "
		    "%i\n", (long long)pagenr, (long long)physaddr,
		    (int)table_index);  */
		*physpage_entryp = physpage_ofs =
		    cpu->translation_cache_cur_ofs;

		/*  Allocate a default page, with to_be_translated entries:  */
		DYNTRANS_TC_ALLOCATE(cpu, physaddr);

		ppp = (struct DYNTRANS_TC_PHYSPAGE *)(cpu->translation_cache
		    + physpage_ofs);
	}

#ifdef MODE32
	if (cpu->cd.DYNTRANS_ARCH.host_load[index] != NULL)
		cpu->cd.DYNTRANS_ARCH.phys_page[index] = ppp;
#endif

#ifdef DYNTRANS_ALPHA
	if (vph_p->host_load[b] != NULL)
		vph_p->phys_page[b] = ppp;
#endif

#ifdef MODE32
	/*  Small optimization: only mark the physical page as non-writable
	    if it did not contain translations. (Because if it does contain
	    translations, it is already non-writable.)  */
	if (!cpu->cd.DYNTRANS_ARCH.phystranslation[pagenr >> 5] &
	    (1 << (pagenr & 31)))
#endif
	cpu->invalidate_translation_caches(cpu, physaddr,
	    JUST_MARK_AS_NON_WRITABLE | INVALIDATE_PADDR);

	cpu->cd.DYNTRANS_ARCH.cur_ic_page = &ppp->ics[0];

	cpu->cd.DYNTRANS_ARCH.next_ic = cpu->cd.DYNTRANS_ARCH.cur_ic_page +
	    DYNTRANS_PC_TO_IC_ENTRY(cached_pc);

	/*  printf("cached_pc=0x%016llx  pagenr=%lli  table_index=%lli, "
	    "physpage_ofs=0x%016llx\n", (long long)cached_pc, (long long)pagenr,
	    (long long)table_index, (long long)physpage_ofs);  */
}


/*
 *  XXX_pc_to_pointers():
 *
 *  This function uses the current program counter (a virtual address) to
 *  find out which physical translation page to use, and then sets the current
 *  translation page pointers to that page.
 *
 *  If there was no translation page for that physical page, then an empty
 *  one is created.
 *
 *  NOTE: This is the quick lookup version. See
 *  DYNTRANS_PC_TO_POINTERS_GENERIC above for the generic case.
 */
void DYNTRANS_PC_TO_POINTERS_FUNC(struct cpu *cpu)
{
#ifdef MODE32
	uint32_t
#else
	uint64_t
#endif
	    cached_pc;
	struct DYNTRANS_TC_PHYSPAGE *ppp;

#ifdef MODE32
	int index;
	cached_pc = cpu->pc;
	index = DYNTRANS_ADDR_TO_PAGENR(cached_pc);
	ppp = cpu->cd.DYNTRANS_ARCH.phys_page[index];
	if (ppp != NULL)
		goto have_it;
#else
#ifdef DYNTRANS_ALPHA
	uint32_t a, b;
	int kernel = 0;
	struct alpha_vph_page *vph_p;
	cached_pc = cpu->pc;
	a = (cached_pc >> ALPHA_LEVEL0_SHIFT) & (ALPHA_LEVEL0 - 1);
	b = (cached_pc >> ALPHA_LEVEL1_SHIFT) & (ALPHA_LEVEL1 - 1);
	if ((cached_pc >> ALPHA_TOPSHIFT) == ALPHA_TOP_KERNEL) {
		vph_p = cpu->cd.alpha.vph_table0_kernel[a];
		kernel = 1;
	} else
		vph_p = cpu->cd.alpha.vph_table0[a];
	if (vph_p != cpu->cd.alpha.vph_default_page) {
		ppp = vph_p->phys_page[b];
		if (ppp != NULL)
			goto have_it;
	}
#else
	/*  Temporary, to avoid a compiler warning:  */
	cached_pc = 0;
	ppp = NULL;
	fatal("Neither alpha nor 32-bit? 1\n");
	exit(1);
#endif
#endif

	DYNTRANS_PC_TO_POINTERS_GENERIC(cpu);
	return;

	/*  Quick return path:  */
#if defined(MODE32) || defined(DYNTRANS_ALPHA)
have_it:
	cpu->cd.DYNTRANS_ARCH.cur_ic_page = &ppp->ics[0];
	cpu->cd.DYNTRANS_ARCH.next_ic = cpu->cd.DYNTRANS_ARCH.cur_ic_page +
	    DYNTRANS_PC_TO_IC_ENTRY(cached_pc);

	/*  printf("cached_pc=0x%016llx  pagenr=%lli  table_index=%lli, "
	    "physpage_ofs=0x%016llx\n", (long long)cached_pc, (long long)pagenr,
	    (long long)table_index, (long long)physpage_ofs);  */
#endif
}
#endif	/*  DYNTRANS_PC_TO_POINTERS_FUNC  */



#ifdef DYNTRANS_INVAL_ENTRY
/*
 *  XXX_invalidate_tlb_entry():
 *
 *  Invalidate one translation entry (based on virtual address).
 *
 *  If the JUST_MARK_AS_NON_WRITABLE flag is set, then the translation entry
 *  is just downgraded to non-writable (ie the host store page is set to
 *  NULL). Otherwise, the entire translation is removed.
 */
static void DYNTRANS_INVALIDATE_TLB_ENTRY(struct cpu *cpu,
#ifdef MODE32
	uint32_t
#else
	uint64_t
#endif
	vaddr_page, int flags)
{
#ifdef MODE32
	uint32_t index = DYNTRANS_ADDR_TO_PAGENR(vaddr_page);

#ifdef DYNTRANS_ARM
	cpu->cd.DYNTRANS_ARCH.is_userpage[index >> 5] &= ~(1 << (index & 31));
#endif

	if (flags & JUST_MARK_AS_NON_WRITABLE) {
		/*  printf("JUST MARKING NON-W: vaddr 0x%08x\n",
		    (int)vaddr_page);  */
		cpu->cd.DYNTRANS_ARCH.host_store[index] = NULL;
	} else {
		cpu->cd.DYNTRANS_ARCH.host_load[index] = NULL;
		cpu->cd.DYNTRANS_ARCH.host_store[index] = NULL;
		cpu->cd.DYNTRANS_ARCH.phys_addr[index] = 0;
		cpu->cd.DYNTRANS_ARCH.phys_page[index] = NULL;
		cpu->cd.DYNTRANS_ARCH.vaddr_to_tlbindex[index] = 0;
	}
#else
	/*  2-level:  */
#ifdef DYNTRANS_ALPHA
	struct alpha_vph_page *vph_p;
	uint32_t a, b;
	int kernel = 0;

	a = (vaddr_page >> ALPHA_LEVEL0_SHIFT) & (ALPHA_LEVEL0 - 1);
	b = (vaddr_page >> ALPHA_LEVEL1_SHIFT) & (ALPHA_LEVEL1 - 1);
	if ((vaddr_page >> ALPHA_TOPSHIFT) == ALPHA_TOP_KERNEL) {
		vph_p = cpu->cd.alpha.vph_table0_kernel[a];
		kernel = 1;
	} else
		vph_p = cpu->cd.alpha.vph_table0[a];

	if (vph_p == cpu->cd.alpha.vph_default_page) {
		fatal("alpha_invalidate_tlb_entry(): huh? Problem 1.\n");
		exit(1);
	}

	if (flags & JUST_MARK_AS_NON_WRITABLE) {
		vph_p->host_store[b] = NULL;
		return;
	}
	vph_p->host_load[b] = NULL;
	vph_p->host_store[b] = NULL;
	vph_p->phys_addr[b] = 0;
	vph_p->phys_page[b] = NULL;
	vph_p->refcount --;
	if (vph_p->refcount < 0) {
		fatal("alpha_invalidate_tlb_entry(): huh? Problem 2.\n");
		exit(1);
	}
	if (vph_p->refcount == 0) {
		vph_p->next = cpu->cd.alpha.vph_next_free_page;
		cpu->cd.alpha.vph_next_free_page = vph_p;
		if (kernel)
			cpu->cd.alpha.vph_table0_kernel[a] =
			    cpu->cd.alpha.vph_default_page;
		else
			cpu->cd.alpha.vph_table0[a] =
			    cpu->cd.alpha.vph_default_page;
	}
#else	/*  !DYNTRANS_ALPHA  */
	fatal("Not yet for non-1-level, non-Alpha\n");
#endif	/*  !DYNTRANS_ALPHA  */
#endif
}
#endif


#ifdef DYNTRANS_INVALIDATE_TC
/*
 *  XXX_invalidate_translation_caches():
 *
 *  Invalidate all entries matching a specific physical address, a specific
 *  virtual address, or ALL entries.
 *
 *  flags should be one of
 *	INVALIDATE_PADDR  INVALIDATE_VADDR  or  INVALIDATE_ALL
 *
 *  In addition, for INVALIDATE_ALL, INVALIDATE_VADDR_UPPER4 may be set and
 *  bit 31..28 of addr are used to select the virtual addresses to invalidate.
 *  (This is useful for PowerPC emulation, when segment registers are updated.)
 *
 *  In the case when all translations are invalidated, paddr doesn't need
 *  to be supplied.
 *
 *  NOTE/TODO: When invalidating a virtual address, it is only cleared from
 *             the quick translation array, not from the linear
 *             vph_tlb_entry[] array.  Hopefully this is enough anyway.
 */
void DYNTRANS_INVALIDATE_TC(struct cpu *cpu, uint64_t addr, int flags)
{
	int r;
#ifdef MODE32
	uint32_t
#else
	uint64_t
#endif
	    addr_page = addr & ~(DYNTRANS_PAGESIZE - 1);

	/*  fatal("invalidate(): ");  */

	/*  Quick case for _one_ virtual addresses: see note above.  */
	if (flags & INVALIDATE_VADDR) {
		/*  fatal("vaddr 0x%08x\n", (int)addr_page);  */
		DYNTRANS_INVALIDATE_TLB_ENTRY(cpu, addr_page, flags);
		return;
	}

	/*  Invalidate everything:  */
#ifdef DYNTRANS_PPC
	if (flags & INVALIDATE_ALL && flags & INVALIDATE_VADDR_UPPER4) {
		/*  fatal("all, upper4 (PowerPC segment)\n");  */
		for (r=0; r<DYNTRANS_MAX_VPH_TLB_ENTRIES; r++) {
			if (cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r].valid &&
			    (cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r].vaddr_page
			    & 0xf0000000) == addr_page) {
				DYNTRANS_INVALIDATE_TLB_ENTRY(cpu, cpu->cd.
				    DYNTRANS_ARCH.vph_tlb_entry[r].vaddr_page,
				    0);
				cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r].valid=0;
			}
		}
		return;
	}
#endif
	if (flags & INVALIDATE_ALL) {
		/*  fatal("all\n");  */
		for (r=0; r<DYNTRANS_MAX_VPH_TLB_ENTRIES; r++) {
			if (cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r].valid) {
				DYNTRANS_INVALIDATE_TLB_ENTRY(cpu, cpu->cd.
				    DYNTRANS_ARCH.vph_tlb_entry[r].vaddr_page,
				    0);
				cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r].valid=0;
			}
		}
		return;
	}

	/*  Invalidate a physical page:  */

	if (!(flags & INVALIDATE_PADDR))
		fatal("HUH? Invalidate: Not vaddr, all, or paddr?\n");

	/*  fatal("addr 0x%08x\n", (int)addr_page);  */

	for (r=0; r<DYNTRANS_MAX_VPH_TLB_ENTRIES; r++) {
		if (cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r].valid && addr_page
		    == cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r].paddr_page) {
			DYNTRANS_INVALIDATE_TLB_ENTRY(cpu,
			    cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r].vaddr_page,
			    flags);
			if (flags & JUST_MARK_AS_NON_WRITABLE)
				cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r]
				    .writeflag = 0;
			else
				cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r]
				    .valid = 0;
		}
	}
}
#endif	/*  DYNTRANS_INVALIDATE_TC  */



#ifdef DYNTRANS_INVALIDATE_TC_CODE
/*
 *  XXX_invalidate_code_translation():
 *
 *  Invalidate code translations for a specific physical address, a specific
 *  virtual address, or for all entries in the cache.
 */
void DYNTRANS_INVALIDATE_TC_CODE(struct cpu *cpu, uint64_t addr, int flags)
{
	int r;
#ifdef MODE32
	uint32_t
#else
	uint64_t
#endif
	    vaddr_page, paddr_page;

	addr &= ~(DYNTRANS_PAGESIZE-1);

	/*  printf("DYNTRANS_INVALIDATE_TC_CODE addr=0x%08x flags=%i\n",
	    (int)addr, flags);  */

	if (flags & INVALIDATE_PADDR) {
		int pagenr, table_index;
		uint32_t physpage_ofs, *physpage_entryp;
		struct DYNTRANS_TC_PHYSPAGE *ppp, *prev_ppp;

		pagenr = DYNTRANS_ADDR_TO_PAGENR(addr);

#ifdef MODE32
		/*  If this page isn't marked as having any translations,
		    then return immediately.  */
		if (!(cpu->cd.DYNTRANS_ARCH.phystranslation[pagenr >> 5]
		    & 1 << (pagenr & 31)))
			return;
		/*  Remove the mark:  */
		cpu->cd.DYNTRANS_ARCH.phystranslation[pagenr >> 5] &=
		    ~ (1 << (pagenr & 31));
#endif

		table_index = PAGENR_TO_TABLE_INDEX(pagenr);

		physpage_entryp = &(((uint32_t *)cpu->
		    translation_cache)[table_index]);
		physpage_ofs = *physpage_entryp;
		prev_ppp = ppp = NULL;

		/*  Traverse the physical page chain:  */
		while (physpage_ofs != 0) {
			prev_ppp = ppp;
			ppp = (struct DYNTRANS_TC_PHYSPAGE *)
			    (cpu->translation_cache + physpage_ofs);
			/*  If we found the page in the cache,
			    then we're done:  */
			if (ppp->physaddr == addr)
				break;
			/*  Try the next page in the chain:  */
			physpage_ofs = ppp->next_ofs;
		}

		if (physpage_ofs == 0)
			ppp = NULL;

#if 1
		/*
		 *  "Bypass" the page, removing it from the code cache.
		 *
		 *  NOTE/TODO: This gives _TERRIBLE_ performance with self-
		 *  modifying code, or when a single page is used for both
		 *  code and (writable) data.
		 */
		if (ppp != NULL) {
			if (prev_ppp != NULL)
				prev_ppp->next_ofs = ppp->next_ofs;
			else
				*physpage_entryp = ppp->next_ofs;
		}
#else
		/*
		 *  Instead of removing the page from the code cache, each
		 *  entry can be set to "to_be_translated". This is slow in
		 *  the general case, but in the case of self-modifying code,
		 *  it might be faster since we don't risk wasting cache
		 *  memory as quickly (which would force unnecessary Restarts).
		 */
		if (ppp != NULL) {
			/*  TODO: Is this faster than copying an entire
			    template page?  */
			int i;
			for (i=0; i<DYNTRANS_IC_ENTRIES_PER_PAGE; i++)
				ppp->ics[i].f =
#ifdef DYNTRANS_DUALMODE_32
				    cpu->is_32bit? instr32(to_be_translated) :
#endif
				    instr(to_be_translated);
		}
#endif
	}

	/*  Invalidate entries (NOTE: only code entries) in the VPH table:  */
	for (r = DYNTRANS_MAX_VPH_TLB_ENTRIES/2;
	     r < DYNTRANS_MAX_VPH_TLB_ENTRIES; r ++) {
		if (cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r].valid) {
			vaddr_page = cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r]
			    .vaddr_page & ~(DYNTRANS_PAGESIZE-1);
			paddr_page = cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r]
			    .paddr_page & ~(DYNTRANS_PAGESIZE-1);

			if (flags & INVALIDATE_ALL ||
			    (flags & INVALIDATE_PADDR && paddr_page == addr) ||
			    (flags & INVALIDATE_VADDR && vaddr_page == addr)) {
#ifdef MODE32
				uint32_t index =
				    DYNTRANS_ADDR_TO_PAGENR(vaddr_page);
				cpu->cd.DYNTRANS_ARCH.phys_page[index] = NULL;
				/*  Remove the mark:  */
				index = DYNTRANS_ADDR_TO_PAGENR(paddr_page);
				cpu->cd.DYNTRANS_ARCH.phystranslation[
				    index >> 5] &= ~ (1 << (index & 31));
#else
				/*  2-level:  */
#ifdef DYNTRANS_ALPHA
				struct alpha_vph_page *vph_p;
				uint32_t a, b;
				int kernel = 0;

				a = (vaddr_page >> ALPHA_LEVEL0_SHIFT)
				    & (ALPHA_LEVEL0 - 1);
				b = (vaddr_page >> ALPHA_LEVEL1_SHIFT)
				    & (ALPHA_LEVEL1 - 1);
				if ((vaddr_page >> ALPHA_TOPSHIFT) ==
				    ALPHA_TOP_KERNEL) {
					vph_p = cpu->cd.alpha.
					    vph_table0_kernel[a];
					kernel = 1;
				} else
					vph_p = cpu->cd.alpha.vph_table0[a];
				vph_p->phys_page[b] = NULL;
#else	/*  !DYNTRANS_ALPHA  */
				fatal("Not yet for non-1-level, non-Alpha\n");
#endif	/*  !DYNTRANS_ALPHA  */
#endif
			}
		}
	}
}
#endif	/*  DYNTRANS_INVALIDATE_TC_CODE  */



#ifdef DYNTRANS_UPDATE_TRANSLATION_TABLE
/*
 *  XXX_update_translation_table():
 *
 *  Update the virtual memory translation tables.
 */
void DYNTRANS_UPDATE_TRANSLATION_TABLE(struct cpu *cpu, uint64_t vaddr_page,
	unsigned char *host_page, int writeflag, uint64_t paddr_page)
{
#ifndef MODE32
	int64_t lowest, highest = -1;
#endif
	int found, r, lowest_index, start, end, useraccess = 0;

#ifdef DYNTRANS_ALPHA
	uint32_t a, b;
	struct alpha_vph_page *vph_p;
	int kernel = 0;
	/*  fatal("update_translation_table(): v=0x%llx, h=%p w=%i"
	    " p=0x%llx\n", (long long)vaddr_page, host_page, writeflag,
	    (long long)paddr_page);  */
#else
#ifdef MODE32
	uint32_t index;
	vaddr_page &= 0xffffffffULL;
	paddr_page &= 0xffffffffULL;
	/*  fatal("update_translation_table(): v=0x%x, h=%p w=%i"
	    " p=0x%x\n", (int)vaddr_page, host_page, writeflag,
	    (int)paddr_page);  */
#else	/*  !MODE32  */
	fatal("Neither 32-bit nor Alpha? 2\n");
	exit(1);
#endif
#endif

	if (writeflag & MEMORY_USER_ACCESS) {
		writeflag &= ~MEMORY_USER_ACCESS;
		useraccess = 1;
	}

	start = 0; end = DYNTRANS_MAX_VPH_TLB_ENTRIES / 2;
#if 1
	/*  Half of the TLB used for data, half for code:  */
	if (writeflag & TLB_CODE) {
		writeflag &= ~TLB_CODE;
		start = end; end = DYNTRANS_MAX_VPH_TLB_ENTRIES;
	}
#else
	/*  Data and code entries are mixed.  */
	end = DYNTRANS_MAX_VPH_TLB_ENTRIES;
#endif

	/*  Scan the current TLB entries:  */
	lowest_index = start;

#ifdef MODE32
	/*
	 *  NOTE 1: vaddr_to_tlbindex is one more than the index, so that
	 *          0 becomes -1, which means a miss.
	 *
	 *  NOTE 2: When a miss occurs, instead of scanning the entire tlb
	 *          for the entry with the lowest time stamp, just choosing
	 *          one at random will work as well.
	 */
	found = (int)cpu->cd.DYNTRANS_ARCH.vaddr_to_tlbindex[
	    DYNTRANS_ADDR_TO_PAGENR(vaddr_page)] - 1;
	if (found < 0) {
		static unsigned int x = 0;
		lowest_index = (x % (end-start)) + start;
		x ++;
	}
#else
	lowest = cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[0].timestamp;
	found = -1;
	for (r=start; r<end; r++) {
		if (cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r].timestamp < lowest) {
			lowest = cpu->cd.DYNTRANS_ARCH.
			    vph_tlb_entry[r].timestamp;
			lowest_index = r;
		}
		if (cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r].timestamp > highest)
			highest = cpu->cd.DYNTRANS_ARCH.
			    vph_tlb_entry[r].timestamp;
		if (cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r].valid &&
		    cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r].vaddr_page ==
		    vaddr_page) {
			found = r;
			break;
		}
	}
#endif

	if (found < 0) {
		/*  Create the new TLB entry, overwriting the oldest one:  */
		r = lowest_index;
		if (cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r].valid) {
			/*  This one has to be invalidated first:  */
			DYNTRANS_INVALIDATE_TLB_ENTRY(cpu,
			    cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r].vaddr_page,
			    0);
		}

		cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r].valid = 1;
		cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r].host_page = host_page;
		cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r].paddr_page = paddr_page;
		cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r].vaddr_page = vaddr_page;
		cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r].writeflag =
		    writeflag & MEM_WRITE;
#ifndef MODE32
		cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r].timestamp = highest + 1;
#endif

		/*  Add the new translation to the table:  */
#ifdef DYNTRANS_ALPHA
		a = (vaddr_page >> ALPHA_LEVEL0_SHIFT) & (ALPHA_LEVEL0 - 1);
		b = (vaddr_page >> ALPHA_LEVEL1_SHIFT) & (ALPHA_LEVEL1 - 1);
		if ((vaddr_page >> ALPHA_TOPSHIFT) == ALPHA_TOP_KERNEL) {
			vph_p = cpu->cd.alpha.vph_table0_kernel[a];
			kernel = 1;
		} else
			vph_p = cpu->cd.alpha.vph_table0[a];
		if (vph_p == cpu->cd.alpha.vph_default_page) {
			if (cpu->cd.alpha.vph_next_free_page != NULL) {
				if (kernel)
					vph_p = cpu->cd.alpha.vph_table0_kernel
					    [a] = cpu->cd.alpha.
					    vph_next_free_page;
				else
					vph_p = cpu->cd.alpha.vph_table0[a] =
					    cpu->cd.alpha.vph_next_free_page;
				cpu->cd.alpha.vph_next_free_page = vph_p->next;
			} else {
				if (kernel)
					vph_p = cpu->cd.alpha.vph_table0_kernel
					    [a] = malloc(sizeof(struct
					    alpha_vph_page));
				else
					vph_p = cpu->cd.alpha.vph_table0[a] =
					    malloc(sizeof(struct
					    alpha_vph_page));
				memset(vph_p, 0, sizeof(struct alpha_vph_page));
			}
		}
		vph_p->refcount ++;
		vph_p->host_load[b] = host_page;
		vph_p->host_store[b] = writeflag? host_page : NULL;
		vph_p->phys_addr[b] = paddr_page;
		vph_p->phys_page[b] = NULL;
#else
#ifdef MODE32
		index = DYNTRANS_ADDR_TO_PAGENR(vaddr_page);
		cpu->cd.DYNTRANS_ARCH.host_load[index] = host_page;
		cpu->cd.DYNTRANS_ARCH.host_store[index] =
		    writeflag? host_page : NULL;
		cpu->cd.DYNTRANS_ARCH.phys_addr[index] = paddr_page;
		cpu->cd.DYNTRANS_ARCH.phys_page[index] = NULL;
		cpu->cd.DYNTRANS_ARCH.vaddr_to_tlbindex[index] = r + 1;
#ifdef DYNTRANS_ARM
		if (useraccess)
			cpu->cd.DYNTRANS_ARCH.is_userpage[index >> 5]
			    |= 1 << (index & 31);
#endif
#endif	/*  32  */
#endif	/*  !ALPHA  */
	} else {
		/*
		 *  The translation was already in the TLB.
		 *	Writeflag = 0:  Do nothing.
		 *	Writeflag = 1:  Make sure the page is writable.
		 *	Writeflag = MEM_DOWNGRADE: Downgrade to readonly.
		 */
		r = found;
#ifndef MODE32
		cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r].timestamp = highest + 1;
#endif
		if (writeflag & MEM_WRITE)
			cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r].writeflag = 1;
		if (writeflag & MEM_DOWNGRADE)
			cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r].writeflag = 0;
#ifdef DYNTRANS_ALPHA
		a = (vaddr_page >> ALPHA_LEVEL0_SHIFT) & (ALPHA_LEVEL0 - 1);
		b = (vaddr_page >> ALPHA_LEVEL1_SHIFT) & (ALPHA_LEVEL1 - 1);
		if ((vaddr_page >> ALPHA_TOPSHIFT) == ALPHA_TOP_KERNEL) {
			vph_p = cpu->cd.alpha.vph_table0_kernel[a];
			kernel = 1;
		} else
			vph_p = cpu->cd.alpha.vph_table0[a];
		vph_p->phys_page[b] = NULL;
		if (vph_p->phys_addr[b] == paddr_page) {
			if (writeflag & MEM_WRITE)
				vph_p->host_store[b] = host_page;
			if (writeflag & MEM_DOWNGRADE)
				vph_p->host_store[b] = NULL;
		} else {
			/*  Change the entire physical/host mapping:  */
			vph_p->host_load[b] = host_page;
			vph_p->host_store[b] = writeflag? host_page : NULL;
			vph_p->phys_addr[b] = paddr_page;
		}
#else
#ifdef MODE32
		index = DYNTRANS_ADDR_TO_PAGENR(vaddr_page);
		cpu->cd.DYNTRANS_ARCH.phys_page[index] = NULL;
#ifdef DYNTRANS_ARM
		cpu->cd.DYNTRANS_ARCH.is_userpage[index>>5] &= ~(1<<(index&31));
		if (useraccess)
			cpu->cd.DYNTRANS_ARCH.is_userpage[index >> 5]
			    |= 1 << (index & 31);
#endif
		if (cpu->cd.DYNTRANS_ARCH.phys_addr[index] == paddr_page) {
			if (writeflag & MEM_WRITE)
				cpu->cd.DYNTRANS_ARCH.host_store[index] =
				    host_page;
			if (writeflag & MEM_DOWNGRADE)
				cpu->cd.DYNTRANS_ARCH.host_store[index] = NULL;
		} else {
			/*  Change the entire physical/host mapping:  */
			cpu->cd.DYNTRANS_ARCH.host_load[index] = host_page;
			cpu->cd.DYNTRANS_ARCH.host_store[index] =
			    writeflag? host_page : NULL;
			cpu->cd.DYNTRANS_ARCH.phys_addr[index] = paddr_page;
		}
#endif	/*  32  */
#endif	/*  !ALPHA  */
	}
}
#endif	/*  DYNTRANS_UPDATE_TRANSLATION_TABLE  */


/*****************************************************************************/


#ifdef DYNTRANS_TO_BE_TRANSLATED_HEAD
	/*
	 *  Check for breakpoints.
	 */
	if (!single_step_breakpoint) {
#ifdef MODE32
		uint32_t curpc = cpu->pc;
#else
		uint64_t curpc = cpu->pc;
#endif
		int i;
		for (i=0; i<cpu->machine->n_breakpoints; i++)
			if (curpc ==
#ifdef MODE32
			    (uint32_t)
#endif
			    cpu->machine->breakpoint_addr[i]) {
				if (!cpu->machine->instruction_trace) {
					int old_quiet_mode = quiet_mode;
					quiet_mode = 0;
					DISASSEMBLE(cpu, ib, 1, 0, 0);
					quiet_mode = old_quiet_mode;
				}
				fatal("BREAKPOINT: pc = 0x%llx\n(The "
				    "instruction has not yet executed.)\n",
				    (long long)cpu->pc);
#ifdef DYNTRANS_DELAYSLOT
				if (cpu->cd.DYNTRANS_ARCH.delay_slot !=
				    NOT_DELAYED)
					fatal("ERROR! Breakpoint in a delay"
					    " slot! Not yet supported.\n");
#endif
				single_step_breakpoint = 1;
				single_step = 1;
				goto stop_running_translated;
			}
	}
#endif	/*  DYNTRANS_TO_BE_TRANSLATED_HEAD  */


/*****************************************************************************/


#ifdef DYNTRANS_TO_BE_TRANSLATED_TAIL
	/*
	 *  If we end up here, then an instruction was translated.
	 *  Mark the page as containing a translation.
	 *
	 *  (Special case for 32-bit mode: set the corresponding bit in the
	 *  phystranslation[] array.)
	 */
#ifdef MODE32
	if (!(cpu->cd.DYNTRANS_ARCH.cur_physpage->flags & TRANSLATIONS)) {
		uint32_t index = DYNTRANS_ADDR_TO_PAGENR((uint32_t)addr);
		cpu->cd.DYNTRANS_ARCH.phystranslation[index >> 5] |=
		    (1 << (index & 31));
	}
#endif
	cpu->cd.DYNTRANS_ARCH.cur_physpage->flags |= TRANSLATIONS;


#ifdef DYNTRANS_BACKEND
	/*
	 *  "Empty"/simple native dyntrans backend stuff:
	 *
	 *  1) If no translation is currently being done, but the translated
	 *     instruction was simple enough, then let's start making a new
	 *     native translation block.
	 *
	 *  2) If a native translation block is currently being constructed,
	 *     but this instruction wasn't simple enough, then end the block
	 *     (without including this instruction).
	 *
	 *  3) If a native translation block is currently being constructed,
	 *     and this is a simple instruction, then add it.
	 */
	if (simple && cpu->translation_context.p == NULL &&
	    dyntrans_backend_enable) {
		size_t s = 0;

		if (cpu->translation_context.translation_buffer == NULL) {
			cpu->translation_context.translation_buffer =
			    zeroed_alloc(DTB_TRANSLATION_SIZE_MAX +
			    DTB_TRANSLATION_SIZE_MARGIN);
		}

		cpu->translation_context.p =
		    cpu->translation_context.translation_buffer;

		cpu->translation_context.ic_page =
		    cpu->cd.DYNTRANS_ARCH.cur_ic_page;
		cpu->translation_context.start_instr_call_index =
		    ((size_t)ic - (size_t)cpu->cd.DYNTRANS_ARCH.cur_ic_page)
		    / (sizeof(*ic));

		dtb_function_prologue(&cpu->translation_context, &s);
		cpu->translation_context.p += s;
		cpu->translation_context.n_simple = 0;
	}

	/*  If this is not a continuation of a simple translation, then
	    stop now!  */
	if (cpu->translation_context.ic_page != cpu->cd.DYNTRANS_ARCH.
	    cur_ic_page || ic != &cpu->cd.DYNTRANS_ARCH.cur_ic_page[
	    cpu->translation_context.start_instr_call_index +
	    cpu->translation_context.n_simple])
		simple = 0;

	if (cpu->translation_context.p != NULL && !simple) {
		size_t s = 0, total;

		if (cpu->translation_context.n_simple > 1) {
			dtb_generate_ptr_inc(cpu, &cpu->translation_context,
			    &s, &cpu->cd.DYNTRANS_ARCH.next_ic,
			    (cpu->translation_context.n_simple - 1) *
			    sizeof(*(cpu->cd.DYNTRANS_ARCH.next_ic)));
			cpu->translation_context.p += s;
		}

		dtb_function_epilogue(&cpu->translation_context, &s);
		cpu->translation_context.p += s;

		cpu_dtb_do_fixups(cpu);
#if 0
{
int i;
unsigned char *addr = cpu->translation_context.translation_buffer;
printf("index = %i\n", cpu->translation_context.start_instr_call_index);
quiet_mode = 0;
for (i=0; i<4*32; i+=4)
	alpha_cpu_disassemble_instr(cpu, (unsigned char *)addr + i,
        0, i, 0);
}
#endif
		total = (size_t)cpu->translation_context.p -
		    (size_t)cpu->translation_context.translation_buffer;

		/*  Copy the translated block to the translation cache:  */
		/*  Align first:  */
		cpu->translation_cache_cur_ofs --;
		cpu->translation_cache_cur_ofs |= 31;
		cpu->translation_cache_cur_ofs ++;

		memcpy(cpu->translation_cache + cpu->translation_cache_cur_ofs,
		    cpu->translation_context.translation_buffer, total);

		/*  Set the ic pointer:  */
		((struct DYNTRANS_IC *)cpu->translation_context.ic_page)
		    [cpu->translation_context.start_instr_call_index].f =
		    (void *)
		    (cpu->translation_cache + cpu->translation_cache_cur_ofs);

		/*  Align cur_ofs afterwards as well, just to be safe.  */
		cpu->translation_cache_cur_ofs += total;
		cpu->translation_cache_cur_ofs --;
		cpu->translation_cache_cur_ofs |= 31;
		cpu->translation_cache_cur_ofs ++;

		/*  Set the "combined instruction" flag for this page:  */
		cpu->cd.DYNTRANS_ARCH.cur_physpage = (void *)
		    cpu->cd.DYNTRANS_ARCH.cur_ic_page;
		cpu->cd.DYNTRANS_ARCH.cur_physpage->flags |= COMBINATIONS;

		dtb_host_cacheinvalidate(0,0); /* p , size ... ); */

		cpu->translation_context.p = NULL;
	}
	if (cpu->translation_context.p != NULL) {
		size_t s = 0;
		dtb_generate_fcall(cpu, &cpu->translation_context,
		    &s, (size_t)ic->f, (size_t)ic);
		cpu->translation_context.p += s;
		cpu->translation_context.n_simple ++;
	}
#endif	/*  DYNTRANS_BACKEND  */


	/*
	 *  Now it is time to check for combinations of instructions that can
	 *  be converted into a single function call.
	 *
	 *  Note: Single-stepping or instruction tracing doesn't work with
	 *  instruction combination.
	 */
	if (!single_step && !cpu->machine->instruction_trace) {
		if (cpu->cd.DYNTRANS_ARCH.combination_check != NULL &&
		    cpu->machine->speed_tricks)
			cpu->cd.DYNTRANS_ARCH.combination_check(cpu, ic,
			    addr & (DYNTRANS_PAGESIZE - 1));
		cpu->cd.DYNTRANS_ARCH.combination_check = NULL;
	}

	/*  ... and finally execute the translated instruction:  */
	if (single_step_breakpoint) {
		/*
		 *  Special case when single-stepping: Execute the translated
		 *  instruction, but then replace it with a "to be translated"
		 *  directly afterwards.
		 */
		single_step_breakpoint = 0;
		ic->f(cpu, ic);
		ic->f =
#ifdef DYNTRANS_DUALMODE_32
		    cpu->is_32bit? instr32(to_be_translated) :
#endif
		    instr(to_be_translated);
	} else
		ic->f(cpu, ic);

	return;


bad:	/*
	 *  Nothing was translated. (Unimplemented or illegal instruction.)
	 */

	quiet_mode = 0;
	fatal("to_be_translated(): TODO: unimplemented instruction");

	if (cpu->machine->instruction_trace)
#ifdef MODE32
		fatal(" at 0x%x\n", (int)cpu->pc);
#else
		fatal(" at 0x%llx\n", (long long)cpu->pc);
#endif
	else {
		fatal(":\n");
		DISASSEMBLE(cpu, ib, 1, 0, 0);
	}

	cpu->running = 0;
	cpu->dead = 1;
stop_running_translated:
	debugger_n_steps_left_before_interaction = 0;
	cpu->running_translated = 0;
	ic = cpu->cd.DYNTRANS_ARCH.next_ic = &nothing_call;
	cpu->cd.DYNTRANS_ARCH.next_ic ++;

	/*  Execute the "nothing" instruction:  */
	ic->f(cpu, ic);
#endif	/*  DYNTRANS_TO_BE_TRANSLATED_TAIL  */

