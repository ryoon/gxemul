/*
 *  Copyright (C) 2003-2005  Anders Gavare.  All rights reserved.
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
 *  $Id: cpu_mips.c,v 1.17 2005-02-03 22:33:15 debug Exp $
 *
 *  MIPS core CPU emulation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <ctype.h>

#include "../config.h"


#ifndef ENABLE_MIPS


#include "cpu_mips.h"

/*
 *  mips_cpu_family_init():
 *
 *  Bogus function.
 */
int mips_cpu_family_init(struct cpu_family *fp)
{
	return 0;
}


#else   /*  ENABLE_MIPS  */


#include "arcbios.h"
#include "bintrans.h"
#include "cop0.h"
#include "cpu.h"
#include "cpu_mips.h"
#include "debugger.h"
#include "devices.h"
#include "emul.h"
#include "machine.h"
#include "memory.h"
#include "mips_cpu_types.h"
#include "opcodes_mips.h"
#include "symbol.h"


extern volatile int single_step;
extern int show_opcode_statistics;
extern int old_show_trace_tree;
extern int old_instruction_trace;
extern int old_quiet_mode;
extern int quiet_mode;

static char *exception_names[] = EXCEPTION_NAMES;

static char *hi6_names[] = HI6_NAMES;
static char *regimm_names[] = REGIMM_NAMES;
static char *special_names[] = SPECIAL_NAMES;
static char *special2_names[] = SPECIAL2_NAMES;

static char *regnames[] = MIPS_REGISTER_NAMES;
static char *cop0_names[] = COP0_NAMES;


#include "cpu_mips16.c"


/*
 *  regname():
 *
 *  Convert a register number into either 'r0', 'r31' etc, or a symbolic
 *  name, depending on machine->show_symbolic_register_names.
 *
 *  NOTE: _NOT_ reentrant.
 */
static char *regname(struct machine *machine, int r)
{
	static char ch[4];
	ch[3] = ch[2] = '\0';

	if (r<0 || r>=32)
		strcpy(ch, "xx");
	else if (machine->show_symbolic_register_names)
		strcpy(ch, regnames[r]);
	else
		sprintf(ch, "r%i", r);

	return ch;
}


/*
 *  mips_cpu_new():
 *
 *  Create a new MIPS cpu object.
 */
struct cpu *mips_cpu_new(struct memory *mem, struct machine *machine,
	int cpu_id, char *cpu_type_name)
{
	struct cpu *cpu;
	int i, found, j, tags_size, n_cache_lines, size_per_cache_line;
	struct mips_cpu_type_def cpu_type_defs[] = MIPS_CPU_TYPE_DEFS;
	int64_t secondary_cache_size;
	int x, linesize;

	/*  Scan the cpu_type_defs list for this cpu type:  */
	i = 0;
	found = -1;
	while (i >= 0 && cpu_type_defs[i].name != NULL) {
		if (strcasecmp(cpu_type_defs[i].name, cpu_type_name) == 0) {
			found = i;
			break;
		}
		i++;
	}

	if (found == -1)
		return NULL;

	cpu = malloc(sizeof(struct cpu));
	if (cpu == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}

	memset(cpu, 0, sizeof(struct cpu));
	cpu->cd.mips.cpu_type   = cpu_type_defs[found];
	cpu->name               = cpu->cd.mips.cpu_type.name;
	cpu->mem                = mem;
	cpu->machine            = machine;
	cpu->cpu_id             = cpu_id;
	cpu->byte_order         = EMUL_LITTLE_ENDIAN;
	cpu->bootstrap_cpu_flag = 0;
	cpu->running            = 0;
	cpu->cd.mips.gpr[MIPS_GPR_SP]	= INITIAL_STACK_POINTER;

	if (cpu_id == 0)
		debug("%s", cpu->cd.mips.cpu_type.name);

	/*
	 *  CACHES:
	 *
	 *  1) Use DEFAULT_PCACHE_SIZE and DEFAULT_PCACHE_LINESIZE etc.
	 *  2) If there are specific values defined for this type of cpu,
	 *     in its cpu_type substruct, then let's use those.
	 *  3) Values in the emul struct override both of the above.
	 *
	 *  Once we've decided which values to use, they are stored in
	 *  the emul struct so they can be used from src/machine.c etc.
	 */

	x = DEFAULT_PCACHE_SIZE;
	if (cpu->cd.mips.cpu_type.default_pdcache)
		x = cpu->cd.mips.cpu_type.default_pdcache;
	if (machine->cache_pdcache == 0)
		machine->cache_pdcache = x;

	x = DEFAULT_PCACHE_SIZE;
	if (cpu->cd.mips.cpu_type.default_picache)
		x = cpu->cd.mips.cpu_type.default_picache;
	if (machine->cache_picache == 0)
		machine->cache_picache = x;

	if (machine->cache_secondary == 0)
		machine->cache_secondary = cpu->cd.mips.cpu_type.default_scache;

	linesize = DEFAULT_PCACHE_LINESIZE;
	if (cpu->cd.mips.cpu_type.default_pdlinesize)
		linesize = cpu->cd.mips.cpu_type.default_pdlinesize;
	if (machine->cache_pdcache_linesize == 0)
		machine->cache_pdcache_linesize = linesize;

	linesize = DEFAULT_PCACHE_LINESIZE;
	if (cpu->cd.mips.cpu_type.default_pilinesize)
		linesize = cpu->cd.mips.cpu_type.default_pilinesize;
	if (machine->cache_picache_linesize == 0)
		machine->cache_picache_linesize = linesize;

	linesize = 0;
	if (cpu->cd.mips.cpu_type.default_slinesize)
		linesize = cpu->cd.mips.cpu_type.default_slinesize;
	if (machine->cache_secondary_linesize == 0)
		machine->cache_secondary_linesize = linesize;


	/*
	 *  Primary Data and Instruction caches:
	 */
	for (i=CACHE_DATA; i<=CACHE_INSTRUCTION; i++) {
		switch (i) {
		case CACHE_DATA:
			x = 1 << machine->cache_pdcache;
			linesize = 1 << machine->cache_pdcache_linesize;
			break;
		case CACHE_INSTRUCTION:
			x = 1 << machine->cache_picache;
			linesize = 1 << machine->cache_picache_linesize;
			break;
		}

		/*  Primary cache size and linesize:  */
		cpu->cd.mips.cache_size[i] = x;
		cpu->cd.mips.cache_linesize[i] = linesize;

		switch (cpu->cd.mips.cpu_type.rev) {
		case MIPS_R2000:
		case MIPS_R3000:
			size_per_cache_line = sizeof(struct r3000_cache_line);
			break;
		default:
			size_per_cache_line = sizeof(struct r4000_cache_line);
		}

		cpu->cd.mips.cache_mask[i] = cpu->cd.mips.cache_size[i] - 1;
		cpu->cd.mips.cache_miss_penalty[i] = 10;	/*  TODO ?  */

		cpu->cd.mips.cache[i] = malloc(cpu->cd.mips.cache_size[i]);
		if (cpu->cd.mips.cache[i] == NULL) {
			fprintf(stderr, "out of memory\n");
		}

		n_cache_lines = cpu->cd.mips.cache_size[i] / cpu->cd.mips.cache_linesize[i];
		tags_size = n_cache_lines * size_per_cache_line;

		cpu->cd.mips.cache_tags[i] = malloc(tags_size);
		if (cpu->cd.mips.cache_tags[i] == NULL) {
			fprintf(stderr, "out of memory\n");
		}

		/*  Initialize the cache tags:  */
		switch (cpu->cd.mips.cpu_type.rev) {
		case MIPS_R2000:
		case MIPS_R3000:
			for (j=0; j<n_cache_lines; j++) {
				struct r3000_cache_line *rp;
				rp = (struct r3000_cache_line *)
				    cpu->cd.mips.cache_tags[i];
				rp[j].tag_paddr = 0;
				rp[j].tag_valid = 0;
			}
			break;
		default:
			;
		}

		/*  Set cache_last_paddr to something "impossible":  */
		cpu->cd.mips.cache_last_paddr[i] = IMPOSSIBLE_PADDR;
	}

	/*
	 *  Secondary cache:
	 */
	secondary_cache_size = 0;
	if (machine->cache_secondary)
		secondary_cache_size = 1 << machine->cache_secondary;
	/*  TODO: linesize...  */

	if (cpu_id == 0) {
		debug(" (I+D = %i+%i KB",
		    (int)(cpu->cd.mips.cache_size[CACHE_INSTRUCTION] / 1024),
		    (int)(cpu->cd.mips.cache_size[CACHE_DATA] / 1024));

		if (secondary_cache_size != 0) {
			debug(", L2 = ");
			if (secondary_cache_size >= 1048576)
				debug("%i MB", (int)
				    (secondary_cache_size / 1048576));
			else
				debug("%i KB", (int)
				    (secondary_cache_size / 1024));
		}

		debug(")");
	}

	cpu->cd.mips.coproc[0] = coproc_new(cpu, 0);	/*  System control, MMU  */
	cpu->cd.mips.coproc[1] = coproc_new(cpu, 1);	/*  FPU  */

	/*
	 *  Initialize the cpu->cd.mips.pc_last_* cache (a 1-entry cache of the
	 *  last program counter value).  For pc_last_virtual_page, any
	 *  "impossible" value will do.  The pc should never ever get this
	 *  value.  (The other pc_last* variables do not need initialization,
	 *  as they are not used before pc_last_virtual_page.)
	 */
	cpu->cd.mips.pc_last_virtual_page = PC_LAST_PAGE_IMPOSSIBLE_VALUE;

	switch (cpu->cd.mips.cpu_type.mmu_model) {
	case MMU3K:
		cpu->translate_address = translate_address_mmu3k;
		break;
	case MMU8K:
		cpu->translate_address = translate_address_mmu8k;
		break;
	case MMU10K:
		cpu->translate_address = translate_address_mmu10k;
		break;
	default:
		if (cpu->cd.mips.cpu_type.rev == MIPS_R4100)
			cpu->translate_address = translate_address_mmu4100;
		else
			cpu->translate_address = translate_address_generic;
	}

	return cpu;
}


/*
 *  mips_cpu_show_full_statistics():
 *
 *  Show detailed statistics on opcode usage on each cpu.
 */
void mips_cpu_show_full_statistics(struct machine *m)
{
	int i, s1, s2, iadd = 4;

	if (m->bintrans_enable)
		fatal("NOTE: Dynamic binary translation is used; this list"
		    " of opcode usage\n      only includes instructions that"
		    " were interpreted manually!\n");

	for (i=0; i<m->ncpus; i++) {
		fatal("cpu%i opcode statistics:\n", i);
		debug_indentation(iadd);

		for (s1=0; s1<N_HI6; s1++) {
			if (m->cpus[i]->cd.mips.stats_opcode[s1] > 0)
				fatal("opcode %02x (%7s): %li\n", s1,
				    hi6_names[s1],
				    m->cpus[i]->cd.mips.stats_opcode[s1]);

			debug_indentation(iadd);
			if (s1 == HI6_SPECIAL)
				for (s2=0; s2<N_SPECIAL; s2++)
					if (m->cpus[i]->cd.mips.stats__special[s2] > 0)
						fatal("special %02x (%7s): %li\n",
						    s2, special_names[s2], m->cpus[i]->cd.mips.stats__special[s2]);
			if (s1 == HI6_REGIMM)
				for (s2=0; s2<N_REGIMM; s2++)
					if (m->cpus[i]->cd.mips.stats__regimm[s2] > 0)
						fatal("regimm %02x (%7s): %li\n",
						    s2, regimm_names[s2], m->cpus[i]->cd.mips.stats__regimm[s2]);
			if (s1 == HI6_SPECIAL2)
				for (s2=0; s2<N_SPECIAL; s2++)
					if (m->cpus[i]->cd.mips.stats__special2[s2] > 0)
						fatal("special2 %02x (%7s): %li\n",
						    s2, special2_names[s2], m->cpus[i]->cd.mips.stats__special2[s2]);
			debug_indentation(-iadd);
		}

		debug_indentation(-iadd);
	}
}


/*
 *  mips_cpu_tlbdump():
 *
 *  Called from the debugger to dump the TLB in a readable format.
 *  x is the cpu number to dump, or -1 to dump all CPUs.
 *
 *  If rawflag is nonzero, then the TLB contents isn't formated nicely,
 *  just dumped.
 */
void mips_cpu_tlbdump(struct machine *m, int x, int rawflag)
{
	int i, j;

	/*  Nicely formatted output:  */
	if (!rawflag) {
		for (i=0; i<m->ncpus; i++) {
			int pageshift = 12;

			if (x >= 0 && i != x)
				continue;

			if (m->cpus[i]->cd.mips.cpu_type.rev == MIPS_R4100)
				pageshift = 10;

			/*  Print index, random, and wired:  */
			printf("cpu%i: (", i);
			switch (m->cpus[i]->cd.mips.cpu_type.isa_level) {
			case 1:
			case 2:
				printf("index=0x%x random=0x%x",
				    (int) ((m->cpus[i]->cd.mips.coproc[0]->
				    reg[COP0_INDEX] & R2K3K_INDEX_MASK)
				    >> R2K3K_INDEX_SHIFT),
				    (int) ((m->cpus[i]->cd.mips.coproc[0]->
				    reg[COP0_RANDOM] & R2K3K_RANDOM_MASK)
				    >> R2K3K_RANDOM_SHIFT));
				break;
			default:
				printf("index=0x%x random=0x%x",
				    (int) (m->cpus[i]->cd.mips.coproc[0]->
				    reg[COP0_INDEX] & INDEX_MASK),
				    (int) (m->cpus[i]->cd.mips.coproc[0]->
				    reg[COP0_RANDOM] & RANDOM_MASK));
				printf(" wired=0x%llx", (long long)
				    m->cpus[i]->cd.mips.coproc[0]->reg[COP0_WIRED]);
			}

			printf(")\n");

			for (j=0; j<m->cpus[i]->cd.mips.cpu_type.nr_of_tlb_entries;
			    j++) {
				uint64_t hi,lo0,lo1,mask;
				hi = m->cpus[i]->cd.mips.coproc[0]->tlbs[j].hi;
				lo0 = m->cpus[i]->cd.mips.coproc[0]->tlbs[j].lo0;
				lo1 = m->cpus[i]->cd.mips.coproc[0]->tlbs[j].lo1;
				mask = m->cpus[i]->cd.mips.coproc[0]->tlbs[j].mask;

				printf("%3i: ", j);
				switch (m->cpus[i]->cd.mips.cpu_type.mmu_model) {
				case MMU3K:
					if (!(lo0 & R2K3K_ENTRYLO_V)) {
						printf("(invalid)\n");
						continue;
					}
					printf("vaddr=0x%08x ",
					    (int) (hi&R2K3K_ENTRYHI_VPN_MASK));
					if (lo0 & R2K3K_ENTRYLO_G)
						printf("(global), ");
					else
						printf("(asid %02x),",
						    (int) ((hi & R2K3K_ENTRYHI_ASID_MASK)
						    >> R2K3K_ENTRYHI_ASID_SHIFT));
					printf(" paddr=0x%08x ",
					    (int) (lo0&R2K3K_ENTRYLO_PFN_MASK));
					if (lo0 & R2K3K_ENTRYLO_N)
						printf("N");
					if (lo0 & R2K3K_ENTRYLO_D)
						printf("D");
					printf("\n");
					break;
				default:
					/*  TODO: MIPS32 doesn't need 0x16llx  */
					if (m->cpus[i]->cd.mips.cpu_type.mmu_model == MMU10K)
						printf("vaddr=0x%1x..%011llx ",
						    (int) (hi >> 60),
						    (long long) (hi&ENTRYHI_VPN2_MASK_R10K));
					else
						printf("vaddr=0x%1x..%010llx ",
						    (int) (hi >> 60),
						    (long long) (hi&ENTRYHI_VPN2_MASK));
					if (hi & TLB_G)
						printf("(global): ");
					else
						printf("(asid %02x):",
						    (int) (hi & ENTRYHI_ASID));

					/*  TODO: Coherency bits  */

					if (!(lo0 & ENTRYLO_V))
						printf(" p0=(invalid)   ");
					else
						printf(" p0=0x%09llx ", (long long)
						    (((lo0&ENTRYLO_PFN_MASK) >> ENTRYLO_PFN_SHIFT) << pageshift));
					printf(lo0 & ENTRYLO_D? "D" : " ");

					if (!(lo1 & ENTRYLO_V))
						printf(" p1=(invalid)   ");
					else
						printf(" p1=0x%09llx ", (long long)
						    (((lo1&ENTRYLO_PFN_MASK) >> ENTRYLO_PFN_SHIFT) << pageshift));
					printf(lo1 & ENTRYLO_D? "D" : " ");
					mask |= (1 << (pageshift+1)) - 1;
					switch (mask) {
					case 0x7ff:	printf(" (1KB)"); break;
					case 0x1fff:	printf(" (4KB)"); break;
					case 0x7fff:	printf(" (16KB)"); break;
					case 0x1ffff:	printf(" (64KB)"); break;
					case 0x7ffff:	printf(" (256KB)"); break;
					case 0x1fffff:	printf(" (1MB)"); break;
					case 0x7fffff:	printf(" (4MB)"); break;
					case 0x1ffffff:	printf(" (16MB)"); break;
					case 0x7ffffff:	printf(" (64MB)"); break;
					default:
						printf(" (mask=%08x?)", (int)mask);
					}
					printf("\n");
				}
			}
		}

		return;
	}

	/*  Raw output:  */
	for (i=0; i<m->ncpus; i++) {
		if (x >= 0 && i != x)
			continue;

		/*  Print index, random, and wired:  */
		printf("cpu%i: (", i);

		if (m->cpus[i]->cd.mips.cpu_type.isa_level < 3 ||
		    m->cpus[i]->cd.mips.cpu_type.isa_level == 32)
			printf("index=0x%08x random=0x%08x",
			    (int)m->cpus[i]->cd.mips.coproc[0]->reg[COP0_INDEX],
			    (int)m->cpus[i]->cd.mips.coproc[0]->reg[COP0_RANDOM]);
		else
			printf("index=0x%016llx random=0x%016llx", (long long)
			    m->cpus[i]->cd.mips.coproc[0]->reg[COP0_INDEX],
			    (long long)m->cpus[i]->cd.mips.coproc[0]->reg
			    [COP0_RANDOM]);

		if (m->cpus[i]->cd.mips.cpu_type.isa_level >= 3)
			printf(" wired=0x%llx", (long long)
			    m->cpus[i]->cd.mips.coproc[0]->reg[COP0_WIRED]);

		printf(")\n");

		for (j=0; j<m->cpus[i]->cd.mips.cpu_type.nr_of_tlb_entries; j++) {
			if (m->cpus[i]->cd.mips.cpu_type.mmu_model == MMU3K)
				printf("%3i: hi=0x%08x lo=0x%08x\n",
				    j,
				    (int)m->cpus[i]->cd.mips.coproc[0]->tlbs[j].hi,
				    (int)m->cpus[i]->cd.mips.coproc[0]->tlbs[j].lo0);
			else if (m->cpus[i]->cd.mips.cpu_type.isa_level < 3 ||
			    m->cpus[i]->cd.mips.cpu_type.isa_level == 32)
				printf("%3i: hi=0x%08x mask=0x%08x "
				    "lo0=0x%08x lo1=0x%08x\n", j,
				    (int)m->cpus[i]->cd.mips.coproc[0]->tlbs[j].hi,
				    (int)m->cpus[i]->cd.mips.coproc[0]->tlbs[j].mask,
				    (int)m->cpus[i]->cd.mips.coproc[0]->tlbs[j].lo0,
				    (int)m->cpus[i]->cd.mips.coproc[0]->tlbs[j].lo1);
			else
				printf("%3i: hi=0x%016llx mask=0x%016llx "
				    "lo0=0x%016llx lo1=0x%016llx\n", j,
				    (long long)m->cpus[i]->cd.mips.coproc[0]->tlbs[j].hi,
				    (long long)m->cpus[i]->cd.mips.coproc[0]->tlbs[j].mask,
				    (long long)m->cpus[i]->cd.mips.coproc[0]->tlbs[j].lo0,
				    (long long)m->cpus[i]->cd.mips.coproc[0]->tlbs[j].lo1);
		}
	}
}


/*
 *  mips_cpu_register_match():
 */
void mips_cpu_register_match(struct machine *m, char *name,
	int writeflag, uint64_t *valuep, int *match_register)
{
	int cpunr = 0;

	/*  CPU number:  */

	/*  TODO  */

	/*  Register name:  */
	if (strcasecmp(name, "pc") == 0) {
		if (writeflag) {
			m->cpus[cpunr]->cd.mips.pc = *valuep;
			if (m->cpus[cpunr]->cd.mips.delay_slot) {
				printf("NOTE: Clearing the delay slot"
				    " flag! (It was set before.)\n");
				m->cpus[cpunr]->cd.mips.delay_slot = 0;
			}
			if (m->cpus[cpunr]->cd.mips.nullify_next) {
				printf("NOTE: Clearing the nullify-ne"
				    "xt flag! (It was set before.)\n");
				m->cpus[cpunr]->cd.mips.nullify_next = 0;
			}
		} else
			*valuep = m->cpus[cpunr]->cd.mips.pc;
		*match_register = 1;
	} else if (strcasecmp(name, "hi") == 0) {
		if (writeflag)
			m->cpus[cpunr]->cd.mips.hi = *valuep;
		else
			*valuep = m->cpus[cpunr]->cd.mips.hi;
		*match_register = 1;
	} else if (strcasecmp(name, "lo") == 0) {
		if (writeflag)
			m->cpus[cpunr]->cd.mips.lo = *valuep;
		else
			*valuep = m->cpus[cpunr]->cd.mips.lo;
		*match_register = 1;
	} else if (name[0] == 'r' && isdigit((int)name[1])) {
		int nr = atoi(name + 1);
		if (nr >= 0 && nr < N_MIPS_GPRS) {
			if (writeflag) {
				if (nr != 0)
					m->cpus[cpunr]->cd.mips.gpr[nr] = *valuep;
				else
					printf("WARNING: Attempt to modify r0.\n");
			} else
				*valuep = m->cpus[cpunr]->cd.mips.gpr[nr];
			*match_register = 1;
		}
	} else {
		/*  Check for a symbolic name such as "t6" or "at":  */
		int nr;
		for (nr=0; nr<N_MIPS_GPRS; nr++)
			if (strcmp(name, regnames[nr]) == 0) {
				if (writeflag) {
					if (nr != 0)
						m->cpus[cpunr]->cd.mips.gpr[nr] = *valuep;
					else
						printf("WARNING: Attempt to modify r0.\n");
				} else
					*valuep = m->cpus[cpunr]->cd.mips.gpr[nr];
				*match_register = 1;
			}
	}

	if (!(*match_register)) {
		/*  Check for a symbolic coproc0 name:  */
		int nr;
		for (nr=0; nr<32; nr++)
			if (strcmp(name, cop0_names[nr]) == 0) {
				if (writeflag) {
					coproc_register_write(m->cpus[cpunr],
					    m->cpus[cpunr]->cd.mips.coproc[0], nr,
					    valuep, 1);
				} else {
					/*  TODO: Use coproc_register_read instead?  */
					*valuep = m->cpus[cpunr]->cd.mips.coproc[0]->reg[nr];
				}
				*match_register = 1;
			}
	}

	/*  TODO: Coprocessor 1,2,3 registers.  */
}


/*
 *  cpu_flags():
 *
 *  Returns a pointer to a string containing "(d)" "(j)" "(dj)" or "",
 *  depending on the cpu's current delay_slot and last_was_jumptoself
 *  flags.
 */
static const char *cpu_flags(struct cpu *cpu)
{
	if (cpu->cd.mips.delay_slot) {
		if (cpu->cd.mips.last_was_jumptoself)
			return " (dj)";
		else
			return " (d)";
	} else {
		if (cpu->cd.mips.last_was_jumptoself)
			return " (j)";
		else
			return "";
	}
}


/*
 *  mips_cpu_disassemble_instr():
 *
 *  Convert an instruction word into human readable format, for instruction
 *  tracing.
 *
 *  If running is 1, cpu->cd.mips.pc should be the address of the
 *  instruction.
 *
 *  If running is 0, things that depend on the runtime environment (eg.
 *  register contents) will not be shown, and addr will be used instead of
 *  cpu->cd.mips.pc for relative addresses.
 *
 *  NOTE 2:  coprocessor instructions are not decoded nicely yet  (TODO)
 */
int mips_cpu_disassemble_instr(struct cpu *cpu, unsigned char *originstr,
	int running, uint64_t dumpaddr, int bintrans)
{
	int hi6, special6, regimm5;
	int rt, rd, rs, sa, imm, copz, cache_op, which_cache, showtag;
	uint64_t addr, offset;
	uint32_t instrword;
	unsigned char instr[4];
	char *symbol;

	if (running)
		dumpaddr = cpu->cd.mips.pc;

	symbol = get_symbol_name(&cpu->machine->symbol_context,
	    dumpaddr, &offset);
	if (symbol != NULL && offset==0)
		debug("<%s>\n", symbol);

	if (cpu->machine->ncpus > 1 && running)
		debug("cpu%i: ", cpu->cpu_id);

	if (cpu->cd.mips.cpu_type.isa_level < 3 ||
	    cpu->cd.mips.cpu_type.isa_level == 32)
		debug("%08x", (int)dumpaddr);
	else
		debug("%016llx", (long long)dumpaddr);

	*((uint32_t *)&instr[0]) = *((uint32_t *)&originstr[0]);

	/*
	 *  The rest of the code is written for little endian,
	 *  so swap if necessary:
	 */
	if (cpu->byte_order == EMUL_BIG_ENDIAN) {
		int tmp = instr[0]; instr[0] = instr[3];
		    instr[3] = tmp;
		tmp = instr[1]; instr[1] = instr[2];
		    instr[2] = tmp;
	}

	debug(": %02x%02x%02x%02x",
	    instr[3], instr[2], instr[1], instr[0]);

	if (running)
		debug("%s", cpu_flags(cpu));

	debug("\t");

	if (bintrans && !running) {
		debug("(bintrans)");
		goto disasm_ret;
	}

	/*
	 *  Decode the instruction:
	 */

	if (cpu->cd.mips.nullify_next && running) {
		debug("(nullified)");
		goto disasm_ret;
	}

	hi6 = (instr[3] >> 2) & 0x3f;

	switch (hi6) {
	case HI6_SPECIAL:
		special6 = instr[0] & 0x3f;
		switch (special6) {
		case SPECIAL_SLL:
		case SPECIAL_SRL:
		case SPECIAL_SRA:
		case SPECIAL_DSLL:
		case SPECIAL_DSRL:
		case SPECIAL_DSRA:
		case SPECIAL_DSLL32:
		case SPECIAL_DSRL32:
		case SPECIAL_DSRA32:
			rt = instr[2] & 31;
			rd = (instr[1] >> 3) & 31;
			sa = ((instr[1] & 7) << 2) + ((instr[0] >> 6) & 3);

			if (rd == 0 && special6 == SPECIAL_SLL) {
				if (sa == 0)
					debug("nop");
				else if (sa == 1)
					debug("ssnop");
				else
					debug("nop (weird, sa=%i)", sa);
				goto disasm_ret;
			} else
				debug("%s\t%s,",
				    special_names[special6],
				    regname(cpu->machine, rd));
				debug("%s,%i", regname(cpu->machine, rt), sa);
			break;
		case SPECIAL_DSRLV:
		case SPECIAL_DSRAV:
		case SPECIAL_DSLLV:
		case SPECIAL_SLLV:
		case SPECIAL_SRAV:
		case SPECIAL_SRLV:
			rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
			rt = instr[2] & 31;
			rd = (instr[1] >> 3) & 31;
			debug("%s\t%s",
			    special_names[special6], regname(cpu->machine, rd));
			debug(",%s", regname(cpu->machine, rt));
			debug(",%s", regname(cpu->machine, rs));
			break;
		case SPECIAL_JR:
			rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
			symbol = get_symbol_name(&cpu->machine->symbol_context,
			    cpu->cd.mips.gpr[rs], &offset);
			debug("jr\t%s", regname(cpu->machine, rs));
			if (running && symbol != NULL)
				debug("\t\t<%s>", symbol);
			break;
		case SPECIAL_JALR:
			rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
			rd = (instr[1] >> 3) & 31;
			symbol = get_symbol_name(&cpu->machine->symbol_context,
			    cpu->cd.mips.gpr[rs], &offset);
			debug("jalr\t%s", regname(cpu->machine, rd));
			debug(",%s", regname(cpu->machine, rs));
			if (running && symbol != NULL)
				debug("<%s>", symbol);
			break;
		case SPECIAL_MFHI:
		case SPECIAL_MFLO:
			rd = (instr[1] >> 3) & 31;
			debug("%s\t%s", special_names[special6],
			    regname(cpu->machine, rd));
			break;
		case SPECIAL_MTLO:
		case SPECIAL_MTHI:
			rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
			debug("%s\t%s", special_names[special6],
			    regname(cpu->machine, rs));
			break;
		case SPECIAL_ADD:
		case SPECIAL_ADDU:
		case SPECIAL_SUB:
		case SPECIAL_SUBU:
		case SPECIAL_AND:
		case SPECIAL_OR:
		case SPECIAL_XOR:
		case SPECIAL_NOR:
		case SPECIAL_SLT:
		case SPECIAL_SLTU: 
		case SPECIAL_DADD:
		case SPECIAL_DADDU:
		case SPECIAL_DSUB:
		case SPECIAL_DSUBU:
		case SPECIAL_MOVZ:
		case SPECIAL_MOVN:
			rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
			rt = instr[2] & 31;
			rd = (instr[1] >> 3) & 31;
			debug("%s\t%s", special_names[special6],
			    regname(cpu->machine, rd));
			debug(",%s", regname(cpu->machine, rs));
			debug(",%s", regname(cpu->machine, rt));
			break;
		case SPECIAL_MULT:
		case SPECIAL_MULTU:
		case SPECIAL_DMULT:
		case SPECIAL_DMULTU:
		case SPECIAL_DIV:
		case SPECIAL_DIVU:
		case SPECIAL_DDIV:  
		case SPECIAL_DDIVU:
		case SPECIAL_TGE:                
		case SPECIAL_TGEU:
		case SPECIAL_TLT:
		case SPECIAL_TLTU:
		case SPECIAL_TEQ:
		case SPECIAL_TNE:
			rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
			rt = instr[2] & 31;
			rd = (instr[1] >> 3) & 31;
			if (special6 == SPECIAL_MULT) {
				if (rd != 0) {
					debug("mult_xx\t%s",
					    regname(cpu->machine, rd));
					debug(",%s", regname(cpu->machine, rs));
					debug(",%s", regname(cpu->machine, rt));
					goto disasm_ret;
				}
			}
			debug("%s\t%s", special_names[special6],
			    regname(cpu->machine, rs));
			debug(",%s", regname(cpu->machine, rt));
			break;
		case SPECIAL_SYNC:
			imm = ((instr[1] & 7) << 2) + (instr[0] >> 6);
			debug("sync\t0x%02x", imm);
			break;
		case SPECIAL_SYSCALL:
			imm = (((instr[3] << 24) + (instr[2] << 16) +
			    (instr[1] << 8) + instr[0]) >> 6) & 0xfffff;
			if (imm != 0)
				debug("syscall\t0x%05x", imm);
			else
				debug("syscall");
			break;
		case SPECIAL_BREAK:
			/*  TODO: imm, as in 'syscall'?  */
			debug("break");
			break;
		case SPECIAL_MFSA:
			rd = (instr[1] >> 3) & 31;
			debug("mfsa\t%s", regname(cpu->machine, rd));
			break;
		case SPECIAL_MTSA:
			rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
			debug("mtsa\t%s", regname(cpu->machine, rs));
			break;
		default:
			debug("unimplemented special6 = 0x%02x", special6);
		}
		break;
	case HI6_BEQ:
	case HI6_BEQL:
	case HI6_BNE:
	case HI6_BGTZ:
	case HI6_BGTZL:
	case HI6_BLEZ:
	case HI6_BLEZL:
	case HI6_BNEL:
		rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
		rt = instr[2] & 31;
		imm = (instr[1] << 8) + instr[0];
		if (imm >= 32768)
			imm -= 65536;
		addr = (dumpaddr + 4) + (imm << 2);
		debug("%s\t%s,", hi6_names[hi6], regname(cpu->machine, rt));

		debug("%s,", regname(cpu->machine, rs));

		if (cpu->cd.mips.cpu_type.isa_level < 3 ||
		    cpu->cd.mips.cpu_type.isa_level == 32)
			debug("0x%08x", (int)addr);
		else
			debug("0x%016llx", (long long)addr);

		symbol = get_symbol_name(&cpu->machine->symbol_context,
		    addr, &offset);
		if (symbol != NULL && offset != addr)
			debug("\t<%s>", symbol);
		break;
	case HI6_ADDI:
	case HI6_ADDIU:
	case HI6_DADDI:
	case HI6_DADDIU:
	case HI6_SLTI:
	case HI6_SLTIU:
	case HI6_ANDI:
	case HI6_ORI:
	case HI6_XORI:
		rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
		rt = instr[2] & 31;
		imm = (instr[1] << 8) + instr[0];
		if (imm >= 32768)
			imm -= 65536;
		debug("%s\t%s,", hi6_names[hi6], regname(cpu->machine, rt));
		debug("%s,", regname(cpu->machine, rs));
		if (hi6 == HI6_ANDI || hi6 == HI6_ORI || hi6 == HI6_XORI)
			debug("0x%04x", imm & 0xffff);
		else
			debug("%i", imm);
		break;
	case HI6_LUI:
		rt = instr[2] & 31;
		imm = (instr[1] << 8) + instr[0];
		debug("lui\t%s,0x%x", regname(cpu->machine, rt), imm);
		break;
	case HI6_LB:
	case HI6_LBU:
	case HI6_LH:
	case HI6_LHU:
	case HI6_LW:
	case HI6_LWU:
	case HI6_LD:
	case HI6_LQ_MDMX:
	case HI6_LWC1:
	case HI6_LWC2:
	case HI6_LWC3:
	case HI6_LDC1:
	case HI6_LDC2:
	case HI6_LL:
	case HI6_LLD:
	case HI6_SB:
	case HI6_SH:
	case HI6_SW:
	case HI6_SD:
	case HI6_SQ:
	case HI6_SC:
	case HI6_SCD:
	case HI6_SWC1:
	case HI6_SWC2:
	case HI6_SWC3:
	case HI6_SDC1:
	case HI6_SDC2:
	case HI6_LWL:   
	case HI6_LWR:
	case HI6_LDL:
	case HI6_LDR:
	case HI6_SWL:
	case HI6_SWR:
	case HI6_SDL:
	case HI6_SDR:
		rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
		rt = instr[2] & 31;
		imm = (instr[1] << 8) + instr[0];
		if (imm >= 32768)
			imm -= 65536;
		symbol = get_symbol_name(&cpu->machine->symbol_context,
		    cpu->cd.mips.gpr[rs] + imm, &offset);

		/*  LWC3 is PREF in the newer ISA levels:  */
		/*  TODO: Which ISAs? cpu->cd.mips.cpu_type.isa_level >= 4?  */
		if (hi6 == HI6_LWC3) {
			debug("pref\t0x%x,%i(%s)",
			    rt, imm, regname(cpu->machine, rs));

			if (running) {
				debug("\t\t[0x%016llx = %s]",
				    (long long)(cpu->cd.mips.gpr[rs] + imm));
				if (symbol != NULL)
					debug(" = %s", symbol);
				debug("]");
			}
			goto disasm_ret;
		}

		debug("%s\t", hi6_names[hi6]);

		if (hi6 == HI6_SWC1 || hi6 == HI6_SWC2 || hi6 == HI6_SWC3 ||
		    hi6 == HI6_SDC1 || hi6 == HI6_SDC2 ||
		    hi6 == HI6_LWC1 || hi6 == HI6_LWC2 || hi6 == HI6_LWC3 ||
		    hi6 == HI6_LDC1 || hi6 == HI6_LDC2)
			debug("r%i", rt);
		else
			debug("%s", regname(cpu->machine, rt));

		debug(",%i(%s)", imm, regname(cpu->machine, rs));

		if (running) {
			debug("\t\t[");

			if (cpu->cd.mips.cpu_type.isa_level < 3 ||
			    cpu->cd.mips.cpu_type.isa_level == 32)
				debug("0x%08x", (int)(cpu->cd.mips.gpr[rs] + imm));
			else
				debug("0x%016llx",
				    (long long)(cpu->cd.mips.gpr[rs] + imm));

			if (symbol != NULL)
				debug(" = %s", symbol);

			debug(", data=");
		} else
			break;
		/*  NOTE: No break here (if we are running) as it is up
		    to the caller to print 'data'.  */
		return sizeof(instrword);
	case HI6_J:
	case HI6_JAL:
		imm = (((instr[3] & 3) << 24) + (instr[2] << 16) +
		    (instr[1] << 8) + instr[0]) << 2;
		addr = (dumpaddr + 4) & ~((1 << 28) - 1);
		addr |= imm;
		symbol = get_symbol_name(&cpu->machine->symbol_context,
		    addr, &offset);
		debug("%s\t0x", hi6_names[hi6]);
		if (cpu->cd.mips.cpu_type.isa_level < 3 ||
		    cpu->cd.mips.cpu_type.isa_level == 32)
			debug("%08x", (int)addr);
		else
			debug("%016llx", (long long)addr);
		if (symbol != NULL)
			debug("\t<%s>", symbol);
		break;
	case HI6_COP0:
	case HI6_COP1:
	case HI6_COP2:
	case HI6_COP3:
		imm = (instr[3] << 24) + (instr[2] << 16) +
		     (instr[1] << 8) + instr[0];
		imm &= ((1 << 26) - 1);

		/*  Call coproc_function(), but ONLY disassembly, no exec:  */
		coproc_function(cpu, cpu->cd.mips.coproc[hi6 - HI6_COP0],
		    hi6 - HI6_COP0, imm, 1, running);
		return sizeof(instrword);
	case HI6_CACHE:
		rt   = ((instr[3] & 3) << 3) + (instr[2] >> 5); /*  base  */
		copz = instr[2] & 31;
		imm  = (instr[1] << 8) + instr[0];
		cache_op    = copz >> 2;
		which_cache = copz & 3;
		showtag = 0;
		debug("cache\t0x%02x,0x%04x(%s)", copz, imm,
		    regname(cpu->machine, rt));
		if (which_cache==0)	debug("  [ primary I-cache");
		if (which_cache==1)	debug("  [ primary D-cache");
		if (which_cache==2)	debug("  [ secondary I-cache");
		if (which_cache==3)	debug("  [ secondary D-cache");
		debug(", ");
		if (cache_op==0)	debug("index invalidate");
		if (cache_op==1)	debug("index load tag");
		if (cache_op==2)	debug("index store tag"), showtag=1;
		if (cache_op==3)	debug("create dirty exclusive");
		if (cache_op==4)	debug("hit invalidate");
		if (cache_op==5)	debug("fill OR hit writeback invalidate");
		if (cache_op==6)	debug("hit writeback");
		if (cache_op==7)	debug("hit set virtual");
		if (running)
			debug(", addr 0x%016llx",
			    (long long)(cpu->cd.mips.gpr[rt] + imm));
		if (showtag)
		debug(", taghi=%08lx lo=%08lx",
		    (long)cpu->cd.mips.coproc[0]->reg[COP0_TAGDATA_HI],
		    (long)cpu->cd.mips.coproc[0]->reg[COP0_TAGDATA_LO]);
		debug(" ]");
		break;
	case HI6_SPECIAL2:
		special6 = instr[0] & 0x3f;
		instrword = (instr[3] << 24) + (instr[2] << 16) +
		    (instr[1] << 8) + instr[0];
		rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
		rt = instr[2] & 31;
		rd = (instr[1] >> 3) & 31;
		if ((instrword & 0xfc0007ffULL) == 0x70000000) {
			debug("madd\t%s", regname(cpu->machine, rd));
			debug(",%s", regname(cpu->machine, rs));
			debug(",%s", regname(cpu->machine, rt));
		} else if (special6 == SPECIAL2_MUL) {
			/*  TODO: this is just a guess, I don't have the
				docs in front of me  */
			debug("mul\t%s", regname(cpu->machine, rd));
			debug(",%s", regname(cpu->machine, rs));
			debug(",%s", regname(cpu->machine, rt));
		} else if (special6 == SPECIAL2_CLZ) {
			debug("clz\t%s", regname(cpu->machine, rd));
			debug(",%s", regname(cpu->machine, rs));
		} else if (special6 == SPECIAL2_CLO) {
			debug("clo\t%s", regname(cpu->machine, rd));
			debug(",%s", regname(cpu->machine, rs));
		} else if (special6 == SPECIAL2_DCLZ) {
			debug("dclz\t%s", regname(cpu->machine, rd));
			debug(",%s", regname(cpu->machine, rs));
		} else if (special6 == SPECIAL2_DCLO) {
			debug("dclo\t%s", regname(cpu->machine, rd));
			debug(",%s", regname(cpu->machine, rs));
		} else if ((instrword & 0xffff07ffULL) == 0x70000209
		    || (instrword & 0xffff07ffULL) == 0x70000249) {
			if (instr[0] == 0x49) {
				debug("pmflo\t%s", regname(cpu->machine, rd));
				debug("  (rs=%s)", regname(cpu->machine, rs));
			} else {
				debug("pmfhi\t%s", regname(cpu->machine, rd));
				debug("  (rs=%s)", regname(cpu->machine, rs));
			}
		} else if ((instrword & 0xfc1fffff) == 0x70000269 
		    || (instrword & 0xfc1fffff) == 0x70000229) {
			if (instr[0] == 0x69) {
				debug("pmtlo\t%s", regname(cpu->machine, rs));
			} else {
				debug("pmthi\t%s", regname(cpu->machine, rs));
			} 
		} else if ((instrword & 0xfc0007ff) == 0x700004a9) {
			debug("por\t%s", regname(cpu->machine, rd));
			debug(",%s", regname(cpu->machine, rs));
			debug(",%s", regname(cpu->machine, rt));
		} else if ((instrword & 0xfc0007ff) == 0x70000488) {
			debug("pextlw\t%s", regname(cpu->machine, rd));
			debug(",%s", regname(cpu->machine, rs));
			debug(",%s", regname(cpu->machine, rt));
		} else {
			debug("unimplemented special2 = 0x%02x", special6);
		}
		break;
	case HI6_REGIMM:
		regimm5 = instr[2] & 0x1f;
		switch (regimm5) {
		case REGIMM_BLTZ:
		case REGIMM_BGEZ:
		case REGIMM_BLTZL:
		case REGIMM_BGEZL:
		case REGIMM_BLTZAL:
		case REGIMM_BLTZALL:
		case REGIMM_BGEZAL:
		case REGIMM_BGEZALL:
			rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
			imm = (instr[1] << 8) + instr[0];
			if (imm >= 32768)               
				imm -= 65536;

			debug("%s\t%s,", regimm_names[regimm5],
			    regname(cpu->machine, rs));

			addr = (dumpaddr + 4) + (imm << 2);

			if (cpu->cd.mips.cpu_type.isa_level < 3 ||
			    cpu->cd.mips.cpu_type.isa_level == 32)
				debug("0x%08x", (int)addr);
			else
				debug("0x%016llx", (long long)addr);
			break;
		default:
			debug("unimplemented regimm5 = 0x%02x", regimm5);
		}
		break;
	default:
		debug("unimplemented hi6 = 0x%02x", hi6);
	}

disasm_ret:
	debug("\n");
	return sizeof(instrword);
}


/*
 *  mips_cpu_register_dump():
 *
 *  Dump cpu registers in a relatively readable format.
 *
 *  gprs: set to non-zero to dump GPRs and hi/lo/pc
 *  coprocs: set bit 0..3 to dump registers in coproc 0..3.
 */
void mips_cpu_register_dump(struct cpu *cpu, int gprs, int coprocs)
{
	int coprocnr, i, bits32;
	uint64_t offset;
	char *symbol;

	bits32 = (cpu->cd.mips.cpu_type.isa_level < 3 ||
	    cpu->cd.mips.cpu_type.isa_level == 32)? 1 : 0;

	if (gprs) {
		/*  Special registers (pc, hi/lo) first:  */
		symbol = get_symbol_name(&cpu->machine->symbol_context,
		    cpu->cd.mips.pc, &offset);

		if (bits32)
			debug("cpu%i:  pc = %08x", cpu->cpu_id, (int)cpu->cd.mips.pc);
		else
			debug("cpu%i:    pc = %016llx",
			    cpu->cpu_id, (long long)cpu->cd.mips.pc);

		debug("    <%s>\n", symbol != NULL? symbol :
		    " no symbol ");

		if (bits32)
			debug("cpu%i:  hi = %08x  lo = %08x\n",
			    cpu->cpu_id, (int)cpu->cd.mips.hi, (int)cpu->cd.mips.lo);
		else
			debug("cpu%i:    hi = %016llx    lo = %016llx\n",
			    cpu->cpu_id, (long long)cpu->cd.mips.hi,
			    (long long)cpu->cd.mips.lo);

		/*  General registers:  */
		if (cpu->cd.mips.cpu_type.rev == MIPS_R5900) {
			/*  128-bit:  */
			for (i=0; i<32; i++) {
				if ((i & 1) == 0)
					debug("cpu%i:", cpu->cpu_id);
				debug(" %3s=%016llx%016llx",
				    regname(cpu->machine, i),
				    (long long)cpu->cd.mips.gpr_quadhi[i],
				    (long long)cpu->cd.mips.gpr[i]);
				if ((i & 1) == 1)
					debug("\n");
			}
		} else if (bits32) {
			/*  32-bit:  */
			for (i=0; i<32; i++) {
				if ((i & 3) == 0)
					debug("cpu%i:", cpu->cpu_id);
				debug(" %3s = %08x", regname(cpu->machine, i),
				    (int)cpu->cd.mips.gpr[i]);
				if ((i & 3) == 3)
					debug("\n");
			}
		} else {
			/*  64-bit:  */
			for (i=0; i<32; i++) {
				if ((i & 1) == 0)
					debug("cpu%i:", cpu->cpu_id);
				debug("   %3s = %016llx",
				    regname(cpu->machine, i),
				    (long long)cpu->cd.mips.gpr[i]);
				if ((i & 1) == 1)
					debug("\n");
			}
		}
	}

	for (coprocnr=0; coprocnr<4; coprocnr++) {
		int nm1 = 1;

		if (bits32)
			nm1 = 3;

		if (!(coprocs & (1<<coprocnr)))
			continue;
		if (cpu->cd.mips.coproc[coprocnr] == NULL) {
			debug("cpu%i: no coprocessor %i\n",
			    cpu->cpu_id, coprocnr);
			continue;
		}

		/*  Coprocessor registers:  */
		/*  TODO: multiple selections per register?  */
		for (i=0; i<32; i++) {
			/*  32-bit:  */
			if ((i & nm1) == 0)
				debug("cpu%i:", cpu->cpu_id);

			if (cpu->machine->show_symbolic_register_names &&
			    coprocnr == 0)
				debug(" %8s", cop0_names[i]);
			else
				debug(" c%i,%02i", coprocnr, i);

			if (bits32)
				debug("=%08x", (int)cpu->cd.mips.coproc[coprocnr]->reg[i]);
			else
				debug(" = 0x%016llx", (long long)
				    cpu->cd.mips.coproc[coprocnr]->reg[i]);

			if ((i & nm1) == nm1)
				debug("\n");

			/*  Skip the last 16 cop0 registers on R3000 etc.  */
			if (coprocnr == 0 && cpu->cd.mips.cpu_type.isa_level < 3
			    && i == 15)
				i = 31;
		}

		/*  Floating point control registers:  */
		if (coprocnr == 1) {
			for (i=0; i<32; i++)
				switch (i) {
				case 0:	printf("cpu%i: fcr0  (fcir) = 0x%08x\n",
					    cpu->cpu_id, (int)cpu->cd.mips.coproc[coprocnr]->fcr[i]);
					break;
				case 25:printf("cpu%i: fcr25 (fccr) = 0x%08x\n",
					    cpu->cpu_id, (int)cpu->cd.mips.coproc[coprocnr]->fcr[i]);
					break;
				case 31:printf("cpu%i: fcr31 (fcsr) = 0x%08x\n",
					    cpu->cpu_id, (int)cpu->cd.mips.coproc[coprocnr]->fcr[i]);
					break;
				}
		}
	}
}


/*
 *  show_trace():
 *
 *  Show trace tree.   This function should be called every time
 *  a function is called.  cpu->cd.mips.trace_tree_depth is increased here
 *  and should not be increased by the caller.
 *
 *  Note:  This function should not be called if show_trace_tree == 0.
 */
static void show_trace(struct cpu *cpu, uint64_t addr)
{
	uint64_t offset;
	int x, n_args_to_print;
	char strbuf[50];
	char *symbol;

	cpu->cd.mips.trace_tree_depth ++;

	if (cpu->machine->ncpus > 1)
		debug("cpu%i:", cpu->cpu_id);

	symbol = get_symbol_name(&cpu->machine->symbol_context, addr, &offset);

	for (x=0; x<cpu->cd.mips.trace_tree_depth; x++)
		debug("  ");

	/*  debug("<%s>\n", symbol!=NULL? symbol : "no symbol");  */

	if (symbol != NULL)
		debug("<%s(", symbol);
	else {
		debug("<0x");
		if (cpu->cd.mips.cpu_type.isa_level < 3 ||
		    cpu->cd.mips.cpu_type.isa_level == 32)
			debug("%08x", (int)addr);
		else
			debug("%016llx", (long long)addr);
		debug("(");
	}

	/*
	 *  TODO:  The number of arguments and the symbol type of each
	 *  argument should be taken from the symbol table, in some way.
	 *
	 *  The MIPS binary calling convention is that the first 4
	 *  arguments are in registers a0..a3.
	 *
	 *  Choose a value greater than 4 (eg 5) to print all values in
	 *  the A0..A3 registers and then add a ".." to indicate that
	 *  there might be more arguments.
	 */
	n_args_to_print = 5;

	for (x=0; x<n_args_to_print; x++) {
		int64_t d = cpu->cd.mips.gpr[x + MIPS_GPR_A0];

		if (d > -256 && d < 256)
			debug("%i", (int)d);
		else if (memory_points_to_string(cpu, cpu->mem, d, 1))
			debug("\"%s\"", memory_conv_to_string(cpu,
			    cpu->mem, d, strbuf, sizeof(strbuf)));
		else {
			if (cpu->cd.mips.cpu_type.isa_level < 3 ||
			    cpu->cd.mips.cpu_type.isa_level == 32)
				debug("0x%x", (int)d);
			else
				debug("0x%llx", (long long)d);
		}

		if (x < n_args_to_print - 1)
			debug(",");

		/*  Cannot go beyound MIPS_GPR_A3:  */
		if (x == 3)
			break;
	}

	if (n_args_to_print > 4)
		debug("..");

	debug(")>\n");
}


/*
 *  mips_cpu_interrupt():
 *
 *  Cause an interrupt. If irq_nr is 2..7, then it is a MIPS hardware
 *  interrupt. 0 and 1 are ignored (software interrupts).
 *
 *  If irq_nr is >= 8, then this function calls md_interrupt().
 */
int mips_cpu_interrupt(struct cpu *cpu, uint64_t irq_nr)
{
	if (irq_nr >= 8) {
		if (cpu->machine->md_interrupt != NULL)
			cpu->machine->md_interrupt(cpu->machine, cpu, irq_nr, 1);
		else
			fatal("mips_cpu_interrupt(): irq_nr = %i, but md_interrupt = NULL ?\n", irq_nr);
		return 1;
	}

	if (irq_nr < 2)
		return 0;

	cpu->cd.mips.coproc[0]->reg[COP0_CAUSE] |= ((1 << irq_nr) << STATUS_IM_SHIFT);
	cpu->cd.mips.cached_interrupt_is_possible = 1;
	return 1;
}


/*
 *  mips_cpu_interrupt_ack():
 *
 *  Acknowledge an interrupt. If irq_nr is 2..7, then it is a MIPS hardware
 *  interrupt.  Interrupts 0..1 are ignored (software interrupts).
 *
 *  If irq_nr is >= 8, then it is machine dependant, and md_interrupt() is
 *  called.
 */
int mips_cpu_interrupt_ack(struct cpu *cpu, uint64_t irq_nr)
{
	if (irq_nr >= 8) {
		if (cpu->machine->md_interrupt != NULL)
			cpu->machine->md_interrupt(cpu->machine, cpu, irq_nr, 0);
		else
			fatal("mips_cpu_interrupt_ack(): irq_nr = %i, but md_interrupt = NULL ?\n", irq_nr);
		return 1;
	}

	if (irq_nr < 2)
		return 0;

	cpu->cd.mips.coproc[0]->reg[COP0_CAUSE] &= ~((1 << irq_nr) << STATUS_IM_SHIFT);
	if (!(cpu->cd.mips.coproc[0]->reg[COP0_CAUSE] & STATUS_IM_MASK))
		cpu->cd.mips.cached_interrupt_is_possible = 0;

	return 1;
}


/*
 *  mips_cpu_exception():
 *
 *  Cause an exception in a CPU.  This sets a couple of coprocessor 0
 *  registers, and the program counter.
 *
 *	exccode		the exception code
 *	tlb		set to non-zero if the exception handler at
 *			0x80000000 should be used. (normal = 0x80000180)
 *	vaddr		virtual address (for some exceptions)
 *	coproc_nr	coprocessor number (for some exceptions)
 *	vaddr_vpn2	vpn2 (for some exceptions)
 *	vaddr_asid	asid (for some exceptions)
 *	x_64		non-zero for 64-bit mode for R4000-style tlb misses
 */
void mips_cpu_exception(struct cpu *cpu, int exccode, int tlb, uint64_t vaddr,
	int coproc_nr, uint64_t vaddr_vpn2, int vaddr_asid, int x_64)
{
	uint64_t base;
	uint64_t *reg = &cpu->cd.mips.coproc[0]->reg[0];
	int exc_model = cpu->cd.mips.cpu_type.exc_model;

	if (!quiet_mode) {
		uint64_t offset;
		int x;
		char *symbol = get_symbol_name(
		    &cpu->machine->symbol_context, cpu->cd.mips.pc_last, &offset);

		debug("[ ");
		if (cpu->machine->ncpus > 1)
			debug("cpu%i: ", cpu->cpu_id);

		debug("exception %s%s",
		    exception_names[exccode], tlb? " <tlb>" : "");

		switch (exccode) {
		case EXCEPTION_INT:
			debug(" cause_im=0x%02x", (int)((reg[COP0_CAUSE] & CAUSE_IP_MASK) >> CAUSE_IP_SHIFT));
			break;
		case EXCEPTION_SYS:
			debug(" v0=%i", (int)cpu->cd.mips.gpr[MIPS_GPR_V0]);
			for (x=0; x<4; x++) {
				int64_t d = cpu->cd.mips.gpr[MIPS_GPR_A0 + x];
				char strbuf[30];

				if (d > -256 && d < 256)
					debug(" a%i=%i", x, (int)d);
				else if (memory_points_to_string(cpu, cpu->mem, d, 1))
					debug(" a%i=\"%s\"", x, memory_conv_to_string(cpu, cpu->mem, d, strbuf, sizeof(strbuf)));
				else
					debug(" a%i=0x%llx", x, (long long)d);
			}
			break;
		default:
			debug(" vaddr=0x%016llx", (long long)vaddr);
		}

		debug(" pc=%08llx ", (long long)cpu->cd.mips.pc_last);

		if (symbol != NULL)
			debug("<%s> ]\n", symbol);
		else
			debug("]\n");
	}

	if (tlb && vaddr < 0x1000) {
		uint64_t offset;
		char *symbol = get_symbol_name(
		    &cpu->machine->symbol_context, cpu->cd.mips.pc_last, &offset);
		fatal("warning: LOW reference vaddr=0x%08x, exception %s, pc=%08llx <%s>\n",
		    (int)vaddr, exception_names[exccode], (long long)cpu->cd.mips.pc_last, symbol? symbol : "(no symbol)");
	}

	/*  Clear the exception code bits of the cause register...  */
	if (exc_model == EXC3K) {
		reg[COP0_CAUSE] &= ~R2K3K_CAUSE_EXCCODE_MASK;
#if 0
		if (exccode >= 16) {
			fatal("exccode = %i  (there are only 16 exceptions on R3000 and lower)\n", exccode);
			cpu->running = 0;
			return;
		}
#endif
	} else
		reg[COP0_CAUSE] &= ~CAUSE_EXCCODE_MASK;

	/*  ... and OR in the exception code:  */
	reg[COP0_CAUSE] |= (exccode << CAUSE_EXCCODE_SHIFT);

	/*  Always set CE (according to the R5000 manual):  */
	reg[COP0_CAUSE] &= ~CAUSE_CE_MASK;
	reg[COP0_CAUSE] |= (coproc_nr << CAUSE_CE_SHIFT);

	/*  TODO:  On R4000, vaddr should NOT be set on bus errors!!!  */
#if 0
	if (exccode == EXCEPTION_DBE) {
		reg[COP0_BADVADDR] = vaddr;
		/*  sign-extend vaddr, if it is 32-bit  */
		if ((vaddr >> 32) == 0 && (vaddr & 0x80000000ULL))
			reg[COP0_BADVADDR] |=
			    0xffffffff00000000ULL;
	}
#endif

	if (tlb || (exccode >= EXCEPTION_MOD && exccode <= EXCEPTION_ADES) ||
	    exccode == EXCEPTION_VCEI || exccode == EXCEPTION_VCED) {
		reg[COP0_BADVADDR] = vaddr;
#if 1
/*  TODO: This should be removed.  */
		/*  sign-extend vaddr, if it is 32-bit  */
		if ((vaddr >> 32) == 0 && (vaddr & 0x80000000ULL))
			reg[COP0_BADVADDR] |=
			    0xffffffff00000000ULL;
#endif
		if (exc_model == EXC3K) {
			reg[COP0_CONTEXT] &= ~R2K3K_CONTEXT_BADVPN_MASK;
			reg[COP0_CONTEXT] |= ((vaddr_vpn2 << R2K3K_CONTEXT_BADVPN_SHIFT) & R2K3K_CONTEXT_BADVPN_MASK);

			reg[COP0_ENTRYHI] = (vaddr & R2K3K_ENTRYHI_VPN_MASK)
			    | (vaddr_asid << R2K3K_ENTRYHI_ASID_SHIFT);

			/*  Sign-extend:  */
			reg[COP0_CONTEXT] = (int64_t)(int32_t)reg[COP0_CONTEXT];
			reg[COP0_ENTRYHI] = (int64_t)(int32_t)reg[COP0_ENTRYHI];
		} else {
			if (cpu->cd.mips.cpu_type.rev == MIPS_R4100) {
				reg[COP0_CONTEXT] &= ~CONTEXT_BADVPN2_MASK_R4100;
				reg[COP0_CONTEXT] |= ((vaddr_vpn2 << CONTEXT_BADVPN2_SHIFT) & CONTEXT_BADVPN2_MASK_R4100);

				/*  TODO:  fix these  */
				reg[COP0_XCONTEXT] &= ~XCONTEXT_R_MASK;
				reg[COP0_XCONTEXT] &= ~XCONTEXT_BADVPN2_MASK;
				reg[COP0_XCONTEXT] |= (vaddr_vpn2 << XCONTEXT_BADVPN2_SHIFT) & XCONTEXT_BADVPN2_MASK;
				reg[COP0_XCONTEXT] |= ((vaddr >> 62) & 0x3) << XCONTEXT_R_SHIFT;

				/*  reg[COP0_PAGEMASK] = cpu->cd.mips.coproc[0]->tlbs[0].mask & PAGEMASK_MASK;  */

				reg[COP0_ENTRYHI] = (vaddr & (ENTRYHI_R_MASK | ENTRYHI_VPN2_MASK | 0x1800)) | vaddr_asid;
			} else {
				reg[COP0_CONTEXT] &= ~CONTEXT_BADVPN2_MASK;
				reg[COP0_CONTEXT] |= ((vaddr_vpn2 << CONTEXT_BADVPN2_SHIFT) & CONTEXT_BADVPN2_MASK);

				reg[COP0_XCONTEXT] &= ~XCONTEXT_R_MASK;
				reg[COP0_XCONTEXT] &= ~XCONTEXT_BADVPN2_MASK;
				reg[COP0_XCONTEXT] |= (vaddr_vpn2 << XCONTEXT_BADVPN2_SHIFT) & XCONTEXT_BADVPN2_MASK;
				reg[COP0_XCONTEXT] |= ((vaddr >> 62) & 0x3) << XCONTEXT_R_SHIFT;

				/*  reg[COP0_PAGEMASK] = cpu->cd.mips.coproc[0]->tlbs[0].mask & PAGEMASK_MASK;  */

				if (cpu->cd.mips.cpu_type.mmu_model == MMU10K)
					reg[COP0_ENTRYHI] = (vaddr & (ENTRYHI_R_MASK | ENTRYHI_VPN2_MASK_R10K)) | vaddr_asid;
				else
					reg[COP0_ENTRYHI] = (vaddr & (ENTRYHI_R_MASK | ENTRYHI_VPN2_MASK)) | vaddr_asid;
			}
		}
	}

	if (exc_model == EXC4K && reg[COP0_STATUS] & STATUS_EXL) {
		/*
		 *  Don't set EPC if STATUS_EXL is set, for R4000 and up.
		 *  This actually happens when running IRIX and Ultrix, when
		 *  they handle interrupts and/or tlb updates, I think, so
		 *  printing this with debug() looks better than with fatal().
		 */
		/*  debug("[ warning: cpu%i exception while EXL is set, not setting EPC ]\n", cpu->cpu_id);  */
	} else {
		if (cpu->cd.mips.delay_slot || cpu->cd.mips.nullify_next) {
			reg[COP0_EPC] = cpu->cd.mips.pc_last - 4;
			reg[COP0_CAUSE] |= CAUSE_BD;

			/*  TODO: Should the BD flag actually be set
			    on nullified slots?  */
		} else {
			reg[COP0_EPC] = cpu->cd.mips.pc_last;
			reg[COP0_CAUSE] &= ~CAUSE_BD;
		}
	}

	cpu->cd.mips.delay_slot = NOT_DELAYED;
	cpu->cd.mips.nullify_next = 0;

	/*  TODO: This is true for MIPS64, but how about others?  */
	if (reg[COP0_STATUS] & STATUS_BEV)
		base = 0xffffffffbfc00200ULL;
	else
		base = 0xffffffff80000000ULL;

	switch (exc_model) {
	case EXC3K:
		/*  Userspace tlb, vs others:  */
		if (tlb && !(vaddr & 0x80000000ULL) &&
		    (exccode == EXCEPTION_TLBL || exccode == EXCEPTION_TLBS) )
			cpu->cd.mips.pc = base + 0x000;
		else
			cpu->cd.mips.pc = base + 0x080;
		break;
	default:
		/*
		 *  These offsets are according to the MIPS64 manual, but
		 *  should work with R4000 and the rest too (I hope).
		 *
		 *  0x000  TLB refill, if EXL=0
		 *  0x080  64-bit XTLB refill, if EXL=0
		 *  0x100  cache error  (not implemented yet)
		 *  0x180  general exception
		 *  0x200  interrupt (if CAUSE_IV is set)
		 */
		if (tlb && (exccode == EXCEPTION_TLBL ||
		    exccode == EXCEPTION_TLBS) &&
		    !(reg[COP0_STATUS] & STATUS_EXL)) {
			if (x_64)
				cpu->cd.mips.pc = base + 0x080;
			else
				cpu->cd.mips.pc = base + 0x000;
		} else {
			if (exccode == EXCEPTION_INT &&
			    (reg[COP0_CAUSE] & CAUSE_IV))
				cpu->cd.mips.pc = base + 0x200;
			else
				cpu->cd.mips.pc = base + 0x180;
		}
	}

	if (exc_model == EXC3K) {
		/*  R2000/R3000:  Shift the lowest 6 bits to the left two steps:  */
		reg[COP0_STATUS] =
		    (reg[COP0_STATUS] & ~0x3f) +
		    ((reg[COP0_STATUS] & 0xf) << 2);
	} else {
		/*  R4000:  */
		reg[COP0_STATUS] |= STATUS_EXL;
	}

	/*  Sign-extend:  */
	reg[COP0_CAUSE] = (int64_t)(int32_t)reg[COP0_CAUSE];
	reg[COP0_STATUS] = (int64_t)(int32_t)reg[COP0_STATUS];
}


#ifdef BINTRANS
/*
 *  mips_cpu_cause_simple_exception():
 *
 *  Useful for causing raw exceptions from bintrans, for example
 *  SYSCALL or BREAK.
 */
void mips_cpu_cause_simple_exception(struct cpu *cpu, int exc_code)
{
	mips_cpu_exception(cpu, exc_code, 0, 0, 0, 0, 0, 0);
}
#endif


/*  Included here for better cache characteristics:  */
#include "memory.c"


/*
 *  mips_cpu_run_instr():
 *
 *  Execute one instruction on a cpu.
 *
 *  If we are in a delay slot, set cpu->cd.mips.pc to
 *  cpu->cd.mips.delay_jmpaddr after the instruction is executed.
 *
 *  Return value is the number of instructions executed during this call,
 *  0 if no instruction was executed.
 */
int mips_cpu_run_instr(struct emul *emul, struct cpu *cpu)
{
	int quiet_mode_cached = quiet_mode;
	int instruction_trace_cached = cpu->machine->instruction_trace;
	struct mips_coproc *cp0 = cpu->cd.mips.coproc[0];
	int i, tmp, ninstrs_executed;
	unsigned char instr[4];
	uint32_t instrword;
	uint64_t cached_pc;
	int hi6, special6, regimm5, rd, rs, rt, sa, imm;
	int copz, which_cache, cache_op;

	int cond, likely, and_link;

	/*  for unaligned load/store  */
	uint64_t dir, is_left, reg_ofs, reg_dir;

	uint64_t tmpvalue, tmpaddr;

	int cpnr;			/*  coprocessor nr  */

	/*  for load/store  */
	uint64_t addr, value, value_hi, result_value;
	int wlen, st, signd, linked;
	unsigned char d[16];		/*  room for at most 128 bits  */


	/*
	 *  Update Coprocessor 0 registers:
	 *
	 *  The COUNT register needs to be updated on every [other] instruction.
	 *  The RANDOM register should decrease for every instruction.
	 */

	if (cpu->cd.mips.cpu_type.exc_model == EXC3K) {
		int r = (cp0->reg[COP0_RANDOM] & R2K3K_RANDOM_MASK) >> R2K3K_RANDOM_SHIFT;
		r --;
		if (r >= cp0->nr_of_tlbs || r < 8)
			r = cp0->nr_of_tlbs-1;
		cp0->reg[COP0_RANDOM] = r << R2K3K_RANDOM_SHIFT;
	} else {
		cp0->reg[COP0_RANDOM] --;
		if ((int64_t)cp0->reg[COP0_RANDOM] >= cp0->nr_of_tlbs ||
		    (int64_t)cp0->reg[COP0_RANDOM] < (int64_t) cp0->reg[COP0_WIRED])
			cp0->reg[COP0_RANDOM] = cp0->nr_of_tlbs-1;

		/*
		 *  TODO: only increase count every other instruction,
		 *  according to the R4000 manual. But according to the
		 *  R5000 manual: increment every other clock cycle.
		 *  Which one is it? :-)
		 */
		cp0->reg[COP0_COUNT] = (int64_t)(int32_t)(cp0->reg[COP0_COUNT] + 1);

		if (cpu->cd.mips.compare_register_set &&
		    cp0->reg[COP0_COUNT] == cp0->reg[COP0_COMPARE]) {
			mips_cpu_interrupt(cpu, 7);
			cpu->cd.mips.compare_register_set = 0;
		}
	}


#ifdef ENABLE_INSTRUCTION_DELAYS
	if (cpu->cd.mips.instruction_delay > 0) {
		cpu->cd.mips.instruction_delay --;
		return 1;
	}
#endif

	/*  Cache the program counter in a local variable:  */
	cached_pc = cpu->cd.mips.pc;

	/*  Hardwire the zero register to 0:  */
	cpu->cd.mips.gpr[MIPS_GPR_ZERO] = 0;

	if (cpu->cd.mips.delay_slot) {
		if (cpu->cd.mips.delay_slot == DELAYED) {
			cached_pc = cpu->cd.mips.pc = cpu->cd.mips.delay_jmpaddr;
			cpu->cd.mips.delay_slot = NOT_DELAYED;
		} else /* if (cpu->cd.mips.delay_slot == TO_BE_DELAYED) */ {
			/*  next instruction will be delayed  */
			cpu->cd.mips.delay_slot = DELAYED;
		}
	}

	if (cpu->cd.mips.last_was_jumptoself > 0)
		cpu->cd.mips.last_was_jumptoself --;

	/*  Check PC against breakpoints:  */
	if (!single_step)
		for (i=0; i<cpu->machine->n_breakpoints; i++)
			if (cached_pc == cpu->machine->breakpoint_addr[i]) {
				fatal("Breakpoint reached, pc=0x");
				if (cpu->cd.mips.cpu_type.isa_level < 3 ||
				    cpu->cd.mips.cpu_type.isa_level == 32)
					fatal("%08x", (int)cached_pc);
				else
					fatal("%016llx", (long long)cached_pc);
				fatal("\n");
				single_step = 1;
				return 0;
			}


	/*  Remember where we are, in case of interrupt or exception:  */
	cpu->cd.mips.pc_last = cached_pc;

	/*
	 *  Any pending interrupts?
	 *
	 *  If interrupts are enabled, and any interrupt has arrived (ie its
	 *  bit in the cause register is set) and corresponding enable bits
	 *  in the status register are set, then cause an interrupt exception
	 *  instead of executing the current instruction.
	 *
	 *  NOTE: cached_interrupt_is_possible is set to 1 whenever an
	 *  interrupt bit in the cause register is set to one (in
	 *  mips_cpu_interrupt()) and set to 0 whenever all interrupt bits are
	 *  cleared (in mips_cpu_interrupt_ack()), so we don't need to do a
	 *  full check each time.
	 */
	if (cpu->cd.mips.cached_interrupt_is_possible && !cpu->cd.mips.nullify_next) {
		if (cpu->cd.mips.cpu_type.exc_model == EXC3K) {
			/*  R3000:  */
			int enabled, mask;
			int status = cp0->reg[COP0_STATUS];

			enabled = status & MIPS_SR_INT_IE;
			mask  = status & cp0->reg[COP0_CAUSE] & STATUS_IM_MASK;
			if (enabled && mask) {
				mips_cpu_exception(cpu, EXCEPTION_INT, 0, 0, 0, 0, 0, 0);
				return 0;
			}
		} else {
			/*  R4000 and others:  */
			int enabled, mask;
			int status = cp0->reg[COP0_STATUS];

			enabled = (status & STATUS_IE)
			    && !(status & STATUS_EXL)
			    && !(status & STATUS_ERL);

			mask = status & cp0->reg[COP0_CAUSE] & STATUS_IM_MASK;
			if (enabled && mask) {
				mips_cpu_exception(cpu, EXCEPTION_INT, 0, 0, 0, 0, 0, 0);
				return 0;
			}
		}
	}


	/*
	 *  ROM emulation:
	 *
	 *  This assumes that a jal was made to a ROM address,
	 *  and we should return via gpr ra.
	 */
	if ((cached_pc & 0xfff00000) == 0xbfc00000 &&
	    cpu->machine->prom_emulation) {
		int rom_jal, res = 1;
		switch (cpu->machine->machine_type) {
		case MACHINE_DEC:
			res = decstation_prom_emul(cpu);
			rom_jal = 1;
			break;
		case MACHINE_PS2:
			res = playstation2_sifbios_emul(cpu);
			rom_jal = 1;
			break;
		case MACHINE_ARC:
		case MACHINE_SGI:
			res = arcbios_emul(cpu);
			rom_jal = 1;
			break;
		default:
			rom_jal = 0;
		}

		if (rom_jal) {
			/*
			 *  Special hack:  If the PROM emulation layer needs
			 *  to loop (for example when emulating blocking
			 *  console input) then we should simply return, so
			 *  that the same PROM routine is called on the next
			 *  round as well.
			 *
			 *  This still has to count as one or more
			 *  instructions, so 100 is returned. (Ugly.)
			 */
			if (!res)
				return 100;

			cpu->cd.mips.pc = cpu->cd.mips.gpr[MIPS_GPR_RA];
			/*  no need to update cached_pc, as we're returning  */
			cpu->cd.mips.delay_slot = NOT_DELAYED;

			if (!quiet_mode_cached &&
			    cpu->machine->show_trace_tree)
				cpu->cd.mips.trace_tree_depth --;

			/*  TODO: how many instrs should this count as?  */
			return 100;
		}
	}

#ifdef ALWAYS_SIGNEXTEND_32
	/*
	 *  An extra check for 32-bit mode to make sure that all
	 *  registers are sign-extended:   (Slow, but might be useful
	 *  to detect bugs that have to do with sign-extension.)
	 */
	if (cpu->cd.mips.cpu_type.isa_level < 3 || cpu->cd.mips.cpu_type.isa_level == 32) {
		int warning = 0;
		uint64_t x;

		if (cpu->cd.mips.gpr[0] != 0) {
			fatal("\nWARNING: r0 was not zero! (%016llx)\n\n",
			    (long long)cpu->cd.mips.gpr[0]);
			cpu->cd.mips.gpr[0] = 0;
			warning = 1;
		}

		if (cpu->cd.mips.pc != (int64_t)(int32_t)cpu->cd.mips.pc) {
			fatal("\nWARNING: pc was not sign-extended correctly"
			    " (%016llx)\n\n", (long long)cpu->cd.mips.pc);
			cpu->cd.mips.pc = (int64_t)(int32_t)cpu->cd.mips.pc;
			warning = 1;
		}

		if (cpu->cd.mips.pc_last != (int64_t)(int32_t)cpu->cd.mips.pc_last) {
			fatal("\nWARNING: pc_last was not sign-extended correc"
			    "tly (%016llx)\n\n", (long long)cpu->cd.mips.pc_last);
			cpu->cd.mips.pc_last = (int64_t)(int32_t)cpu->cd.mips.pc_last;
			warning = 1;
		}

		/*  Sign-extend ALL registers, including coprocessor registers and tlbs:  */
		for (i=1; i<32; i++) {
			x = cpu->cd.mips.gpr[i];
			cpu->cd.mips.gpr[i] &= 0xffffffff;
			if (cpu->cd.mips.gpr[i] & 0x80000000ULL)
				cpu->cd.mips.gpr[i] |= 0xffffffff00000000ULL;
			if (x != cpu->cd.mips.gpr[i]) {
				fatal("\nWARNING: r%i (%s) was not sign-"
				    "extended correctly (%016llx != "
				    "%016llx)\n\n", i, regname(cpu->machine, i),
				    (long long)x, (long long)cpu->cd.mips.gpr[i]);
				warning = 1;
			}
		}
		for (i=0; i<32; i++) {
			x = cpu->cd.mips.coproc[0]->reg[i];
			cpu->cd.mips.coproc[0]->reg[i] &= 0xffffffffULL;
			if (cpu->cd.mips.coproc[0]->reg[i] & 0x80000000ULL)
				cpu->cd.mips.coproc[0]->reg[i] |=
				    0xffffffff00000000ULL;
			if (x != cpu->cd.mips.coproc[0]->reg[i]) {
				fatal("\nWARNING: cop0,r%i was not sign-extended correctly (%016llx != %016llx)\n\n",
				    i, (long long)x, (long long)cpu->cd.mips.coproc[0]->reg[i]);
				warning = 1;
			}
		}
		for (i=0; i<cpu->cd.mips.coproc[0]->nr_of_tlbs; i++) {
			x = cpu->cd.mips.coproc[0]->tlbs[i].hi;
			cpu->cd.mips.coproc[0]->tlbs[i].hi &= 0xffffffffULL;
			if (cpu->cd.mips.coproc[0]->tlbs[i].hi & 0x80000000ULL)
				cpu->cd.mips.coproc[0]->tlbs[i].hi |=
				    0xffffffff00000000ULL;
			if (x != cpu->cd.mips.coproc[0]->tlbs[i].hi) {
				fatal("\nWARNING: tlb[%i].hi was not sign-extended correctly (%016llx != %016llx)\n\n",
				    i, (long long)x, (long long)cpu->cd.mips.coproc[0]->tlbs[i].hi);
				warning = 1;
			}

			x = cpu->cd.mips.coproc[0]->tlbs[i].lo0;
			cpu->cd.mips.coproc[0]->tlbs[i].lo0 &= 0xffffffffULL;
			if (cpu->cd.mips.coproc[0]->tlbs[i].lo0 & 0x80000000ULL)
				cpu->cd.mips.coproc[0]->tlbs[i].lo0 |=
				    0xffffffff00000000ULL;
			if (x != cpu->cd.mips.coproc[0]->tlbs[i].lo0) {
				fatal("\nWARNING: tlb[%i].lo0 was not sign-extended correctly (%016llx != %016llx)\n\n",
				    i, (long long)x, (long long)cpu->cd.mips.coproc[0]->tlbs[i].lo0);
				warning = 1;
			}
		}

		if (warning) {
			fatal("Halting. pc = %016llx\n", (long long)cpu->cd.mips.pc);
			cpu->running = 0;
		}
	}
#endif

	PREFETCH(cpu->cd.mips.pc_last_host_4k_page + (cached_pc & 0xfff));

#ifdef HALT_IF_PC_ZERO
	/*  Halt if PC = 0:  */
	if (cached_pc == 0) {
		debug("cpu%i: pc=0, halting\n", cpu->cpu_id);
		cpu->running = 0;
		return 0;
	}
#endif



#ifdef BINTRANS
        /*  Caches are not very coozy to handle in bintrans:  */
        switch (cpu->cd.mips.cpu_type.mmu_model) {
        case MMU3K:
                if (cpu->cd.mips.coproc[0]->reg[COP0_STATUS] & MIPS1_ISOL_CACHES) {
			/*  cpu->cd.mips.dont_run_next_bintrans = 1;  */
			cpu->cd.mips.vaddr_to_hostaddr_table0 =
			    cpu->cd.mips.coproc[0]->reg[COP0_STATUS] & MIPS1_SWAP_CACHES?
			       cpu->cd.mips.vaddr_to_hostaddr_table0_cacheisol_i
			     : cpu->cd.mips.vaddr_to_hostaddr_table0_cacheisol_d;
		} else {
			cpu->cd.mips.vaddr_to_hostaddr_table0 =
			    cpu->cd.mips.vaddr_to_hostaddr_table0_kernel;

			/*  TODO: cpu->cd.mips.vaddr_to_hostaddr_table0_user;  */
		}
                break;
	default:
		cpu->cd.mips.vaddr_to_hostaddr_table0 =
		    cpu->cd.mips.vaddr_to_hostaddr_table0_kernel;
		/*  TODO: cpu->cd.mips.vaddr_to_hostaddr_table0_user;  */
        }
#endif


	if (!quiet_mode_cached) {
		/*  Dump CPU registers for debugging:  */
		if (cpu->machine->register_dump) {
			debug("\n");
			mips_cpu_register_dump(cpu, 1, 0x1);
		}

		/*  Trace tree:  */
		if (cpu->machine->show_trace_tree && cpu->cd.mips.show_trace_delay > 0) {
			cpu->cd.mips.show_trace_delay --;
			if (cpu->cd.mips.show_trace_delay == 0)
				show_trace(cpu, cpu->cd.mips.show_trace_addr);
		}
	}

#ifdef MFHILO_DELAY
	/*  Decrease the MFHI/MFLO delays:  */
	if (cpu->mfhi_delay > 0)
		cpu->mfhi_delay--;
	if (cpu->mflo_delay > 0)
		cpu->mflo_delay--;
#endif

	/*  Read an instruction from memory:  */
#ifdef ENABLE_MIPS16
	if (cpu->cd.mips.mips16 && (cached_pc & 1)) {
		/*  16-bit instruction word:  */
		unsigned char instr16[2];
		int mips16_offset = 0;

		if (!memory_rw(cpu, cpu->mem, cached_pc ^ 1, &instr16[0], sizeof(instr16), MEM_READ, CACHE_INSTRUCTION))
			return 0;

		/*  TODO:  If Reverse-endian is set in the status cop0 register, and
			we are in usermode, then reverse endianness!  */

		/*  The rest of the code is written for little endian, so swap if necessary:  */
		if (cpu->byte_order == EMUL_BIG_ENDIAN) {
			int tmp;
			tmp  = instr16[0]; instr16[0] = instr16[1]; instr16[1] = tmp;
		}

		cpu->cd.mips.mips16_extend = 0;

		/*
		 *  Translate into 32-bit instruction, little endian (instr[3..0]):
		 *
		 *  This ugly loop is necessary because if we would get an exception between
		 *  reading an extend instruction and the next instruction, and execution
		 *  continues on the second instruction, the extend data would be lost. So the
		 *  entire instruction (the two parts) need to be read in. If an exception is
		 *  caused, it will appear as if it was caused when reading the extend instruction.
		 */
		while (mips16_to_32(cpu, instr16, instr) == 0) {
			if (instruction_trace_cached)
				debug("cpu%i @ %016llx: %02x%02x\t\t\textend\n",
				    cpu->cpu_id, (cpu->cd.mips.pc_last ^ 1) + mips16_offset,
				    instr16[1], instr16[0]);

			/*  instruction with extend:  */
			mips16_offset += 2;
			if (!memory_rw(cpu, cpu->mem, (cached_pc ^ 1) + mips16_offset, &instr16[0], sizeof(instr16), MEM_READ, CACHE_INSTRUCTION))
				return 0;

			if (cpu->byte_order == EMUL_BIG_ENDIAN) {
				int tmp;
				tmp  = instr16[0]; instr16[0] = instr16[1]; instr16[1] = tmp;
			}
		}

		/*  TODO: bintrans like in 32-bit mode?  */

		/*  Advance the program counter:  */
		cpu->cd.mips.pc += sizeof(instr16) + mips16_offset;
		cached_pc = cpu->cd.mips.pc;

		if (instruction_trace_cached) {
			uint64_t offset;
			char *symbol = get_symbol_name(
			    &cpu->machine->symbol_context, cpu->cd.mips.pc_last ^ 1,
			    &offset);
			if (symbol != NULL && offset==0)
				debug("<%s>\n", symbol);

			debug("cpu%i @ %016llx: %02x%02x => %02x%02x%02x%02x%s\t",
			    cpu->cpu_id, (cpu->cd.mips.pc_last ^ 1) + mips16_offset,
			    instr16[1], instr16[0],
			    instr[3], instr[2], instr[1], instr[0],
			    cpu_flags(cpu));
		}
	} else
#endif
	    {
		/*
		 *  Fetch a 32-bit instruction word from memory:
		 *
		 *  1)  The special case of reading an instruction from the
		 *      same host RAM page as the last one is handled here,
		 *      to gain a little bit performance.
		 *
		 *  2)  Fallback to reading from memory the usual way.
		 */
		if (cpu->cd.mips.pc_last_host_4k_page != NULL &&
		    (cached_pc & ~0xfff) == cpu->cd.mips.pc_last_virtual_page) {
			/*  NOTE: This only works on the host if offset is
			    aligned correctly!  (TODO)  */
			*(uint32_t *)instr = *(uint32_t *)
			    (cpu->cd.mips.pc_last_host_4k_page + (cached_pc & 0xfff));
#ifdef BINTRANS
			cpu->cd.mips.pc_bintrans_paddr_valid = 1;
			cpu->cd.mips.pc_bintrans_paddr =
			    cpu->cd.mips.pc_last_physical_page | (cached_pc & 0xfff);
			cpu->cd.mips.pc_bintrans_host_4kpage = cpu->cd.mips.pc_last_host_4k_page;
#endif
                } else {
			if (!memory_rw(cpu, cpu->mem, cached_pc, &instr[0],
			    sizeof(instr), MEM_READ, CACHE_INSTRUCTION))
				return 0;
		}

#ifdef BINTRANS
		if (cpu->cd.mips.dont_run_next_bintrans) {
			cpu->cd.mips.dont_run_next_bintrans = 0;
		} else if (cpu->machine->bintrans_enable &&
		    cpu->cd.mips.pc_bintrans_paddr_valid) {
			int res;
			cpu->cd.mips.bintrans_instructions_executed = 0;
			res = bintrans_attempt_translate(cpu,
			    cpu->cd.mips.pc_bintrans_paddr);

			if (res >= 0) {
				/*  debug("BINTRANS translation + hit,"
				    " pc = %016llx\n", (long long)cached_pc);  */
				if (res > 0 || cpu->cd.mips.pc != cached_pc) {
					if (instruction_trace_cached)
						mips_cpu_disassemble_instr(cpu, instr, 1, 0, 1);
					if (res & BINTRANS_DONT_RUN_NEXT)
						cpu->cd.mips.dont_run_next_bintrans = 1;
					res &= BINTRANS_N_MASK;

					if (cpu->cd.mips.cpu_type.exc_model != EXC3K) {
						/*  TODO: 32-bit or 64-bit?  */
						int x = cp0->reg[COP0_COUNT], y = cp0->reg[COP0_COMPARE];
						int diff = x - y;
						if (diff < 0 && diff + (res-1) >= 0
						    && cpu->cd.mips.compare_register_set) {
							mips_cpu_interrupt(cpu, 7);
							cpu->cd.mips.compare_register_set = 0;
						}

						cp0->reg[COP0_COUNT] = (int64_t)
						    (int32_t)(cp0->reg[COP0_COUNT] + res-1);
					}

					return res;
				}
			}
		}
#endif

		if (instruction_trace_cached)
			mips_cpu_disassemble_instr(cpu, instr, 1, 0, 0);

		/*  Advance the program counter:  */
		cpu->cd.mips.pc += sizeof(instr);
		cached_pc = cpu->cd.mips.pc;

		/*
		 *  TODO:  If Reverse-endian is set in the status cop0 register
		 *  and we are in usermode, then reverse endianness!
		 */

		/*
		 *  The rest of the code is written for little endian, so
		 *  swap if necessary:
		 */
		if (cpu->byte_order == EMUL_BIG_ENDIAN) {
			instrword = instr[0]; instr[0] = instr[3];
			    instr[3] = instrword;
			instrword = instr[1]; instr[1] = instr[2];
			    instr[2] = instrword;
		}
	}


	/*
	 *  Nullify this instruction?  (Set by a previous branch-likely
	 *  instruction.)
	 *
	 *  Note: The return value is 1, even if no instruction was actually
	 *  executed.
	 */
	if (cpu->cd.mips.nullify_next) {
		cpu->cd.mips.nullify_next = 0;
		return 1;
	}


	/*
	 *  Execute the instruction:
	 */

	/*  Get the top 6 bits of the instruction:  */
	hi6 = instr[3] >> 2;  	/*  & 0x3f  */

	if (show_opcode_statistics)
		cpu->cd.mips.stats_opcode[hi6] ++;

	switch (hi6) {
	case HI6_SPECIAL:
		special6 = instr[0] & 0x3f;

		if (show_opcode_statistics)
			cpu->cd.mips.stats__special[special6] ++;

		switch (special6) {
		case SPECIAL_SLL:
		case SPECIAL_SRL:
		case SPECIAL_SRA:
		case SPECIAL_DSLL:
		case SPECIAL_DSRL:
		case SPECIAL_DSRA:
		case SPECIAL_DSLL32:
		case SPECIAL_DSRL32:
		case SPECIAL_DSRA32:
			rt = instr[2] & 31;
			rd = (instr[1] >> 3) & 31;
			sa = ((instr[1] & 7) << 2) + ((instr[0] >> 6) & 3);

			/*
			 *  Check for NOP:
			 *
			 *  The R4000 manual says that a shift amount of zero
			 *  is treated as a nop by some assemblers. Checking
			 *  for sa == 0 here would not be correct, though,
			 *  because instructions such as sll r3,r4,0 are
			 *  possible, and are definitely not a nop.
			 *  Instead, check if the destination register is r0.
			 *
			 *  TODO:  ssnop should wait until the _next_
			 *  cycle boundary, or something like that. The
			 *  code here is incorrect.
			 */
			if (rd == 0 && special6 == SPECIAL_SLL) {
				if (sa == 1) {
					/*  ssnop  */
#ifdef ENABLE_INSTRUCTION_DELAYS
					cpu->cd.mips.instruction_delay +=
					    cpu->cd.mips.cpu_type.
					    instrs_per_cycle - 1;
#endif
				}
				return 1;
			}

			if (special6 == SPECIAL_SLL) {
				switch (sa) {
				case 8:	cpu->cd.mips.gpr[rd] = cpu->cd.mips.gpr[rt] << 8; break;
				case 16:cpu->cd.mips.gpr[rd] = cpu->cd.mips.gpr[rt] << 16; break;
				default:cpu->cd.mips.gpr[rd] = cpu->cd.mips.gpr[rt] << sa;
				}
				/*  Sign-extend rd:  */
				cpu->cd.mips.gpr[rd] = (int64_t) (int32_t) cpu->cd.mips.gpr[rd];
			}
			if (special6 == SPECIAL_DSLL) {
				cpu->cd.mips.gpr[rd] = cpu->cd.mips.gpr[rt] << sa;
			}
			if (special6 == SPECIAL_DSRL) {
				cpu->cd.mips.gpr[rd] = cpu->cd.mips.gpr[rt] >> sa;
			}
			if (special6 == SPECIAL_DSLL32) {
				cpu->cd.mips.gpr[rd] = cpu->cd.mips.gpr[rt] << (sa + 32);
			}
			if (special6 == SPECIAL_SRL) {
				/*
				 *  Three cases:
				 *	shift amount = zero:  just copy
				 *	high bit of rt zero:  plain shift right (of all bits)
				 *	high bit of rt one:   plain shift right (of lowest 32 bits)
				 */
				if (sa == 0)
					cpu->cd.mips.gpr[rd] = cpu->cd.mips.gpr[rt];
				else if (!(cpu->cd.mips.gpr[rt] & 0x80000000ULL)) {
					cpu->cd.mips.gpr[rd] = cpu->cd.mips.gpr[rt] >> sa;
				} else
					cpu->cd.mips.gpr[rd] = (cpu->cd.mips.gpr[rt] & 0xffffffffULL) >> sa;
			}
			if (special6 == SPECIAL_SRA) {
				int topbit = cpu->cd.mips.gpr[rt] & 0x80000000ULL;
				switch (sa) {
				case 8:	cpu->cd.mips.gpr[rd] = cpu->cd.mips.gpr[rt] >> 8; break;
				case 16:cpu->cd.mips.gpr[rd] = cpu->cd.mips.gpr[rt] >> 16; break;
				default:cpu->cd.mips.gpr[rd] = cpu->cd.mips.gpr[rt] >> sa;
				}
				if (topbit)
					cpu->cd.mips.gpr[rd] |= 0xffffffff00000000ULL;
			}
			if (special6 == SPECIAL_DSRL32) {
				cpu->cd.mips.gpr[rd] = cpu->cd.mips.gpr[rt] >> (sa + 32);
			}
			if (special6 == SPECIAL_DSRA32 || special6 == SPECIAL_DSRA) {
				if (special6 == SPECIAL_DSRA32)
					sa += 32;
				cpu->cd.mips.gpr[rd] = cpu->cd.mips.gpr[rt];
				while (sa > 0) {
					cpu->cd.mips.gpr[rd] = cpu->cd.mips.gpr[rd] >> 1;
					sa--;
					if (cpu->cd.mips.gpr[rd] & ((uint64_t)1 << 62))		/*  old signbit  */
						cpu->cd.mips.gpr[rd] |= ((uint64_t)1 << 63);
				}
			}
			return 1;
		case SPECIAL_DSRLV:
		case SPECIAL_DSRAV:
		case SPECIAL_DSLLV:
		case SPECIAL_SLLV:
		case SPECIAL_SRAV:
		case SPECIAL_SRLV:
			rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
			rt = instr[2] & 31;
			rd = (instr[1] >> 3) & 31;

			if (special6 == SPECIAL_DSRLV) {
				sa = cpu->cd.mips.gpr[rs] & 63;
				cpu->cd.mips.gpr[rd] = cpu->cd.mips.gpr[rt] >> sa;
			}
			if (special6 == SPECIAL_DSRAV) {
				sa = cpu->cd.mips.gpr[rs] & 63;
				cpu->cd.mips.gpr[rd] = cpu->cd.mips.gpr[rt];
				while (sa > 0) {
					cpu->cd.mips.gpr[rd] = cpu->cd.mips.gpr[rd] >> 1;
					sa--;
					if (cpu->cd.mips.gpr[rd] & ((uint64_t)1 << 62))		/*  old sign-bit  */
						cpu->cd.mips.gpr[rd] |= ((uint64_t)1 << 63);
				}
			}
			if (special6 == SPECIAL_DSLLV) {
				sa = cpu->cd.mips.gpr[rs] & 63;
				cpu->cd.mips.gpr[rd] = cpu->cd.mips.gpr[rt];
				cpu->cd.mips.gpr[rd] = cpu->cd.mips.gpr[rd] << sa;
			}
			if (special6 == SPECIAL_SLLV) {
				sa = cpu->cd.mips.gpr[rs] & 31;
				cpu->cd.mips.gpr[rd] = cpu->cd.mips.gpr[rt];
				cpu->cd.mips.gpr[rd] = cpu->cd.mips.gpr[rd] << sa;
				/*  Sign-extend rd:  */
				cpu->cd.mips.gpr[rd] &= 0xffffffffULL;
				if (cpu->cd.mips.gpr[rd] & 0x80000000ULL)
					cpu->cd.mips.gpr[rd] |= 0xffffffff00000000ULL;
			}
			if (special6 == SPECIAL_SRAV) {
				sa = cpu->cd.mips.gpr[rs] & 31;
				cpu->cd.mips.gpr[rd] = cpu->cd.mips.gpr[rt];
				/*  Sign-extend rd:  */
				cpu->cd.mips.gpr[rd] &= 0xffffffffULL;
				if (cpu->cd.mips.gpr[rd] & 0x80000000ULL)
					cpu->cd.mips.gpr[rd] |= 0xffffffff00000000ULL;
				while (sa > 0) {
					cpu->cd.mips.gpr[rd] = cpu->cd.mips.gpr[rd] >> 1;
					sa--;
				}
				if (cpu->cd.mips.gpr[rd] & 0x80000000ULL)
					cpu->cd.mips.gpr[rd] |= 0xffffffff00000000ULL;
			}
			if (special6 == SPECIAL_SRLV) {
				sa = cpu->cd.mips.gpr[rs] & 31;
				cpu->cd.mips.gpr[rd] = cpu->cd.mips.gpr[rt];
				cpu->cd.mips.gpr[rd] &= 0xffffffffULL;
				cpu->cd.mips.gpr[rd] = cpu->cd.mips.gpr[rd] >> sa;
				/*  And finally sign-extend rd:  */
				if (cpu->cd.mips.gpr[rd] & 0x80000000ULL)
					cpu->cd.mips.gpr[rd] |= 0xffffffff00000000ULL;
			}
			return 1;
		case SPECIAL_JR:
			if (cpu->cd.mips.delay_slot) {
				fatal("jr: jump inside a jump's delay slot, or similar. TODO\n");
				cpu->running = 0;
				return 1;
			}

			rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);

			cpu->cd.mips.delay_slot = TO_BE_DELAYED;
			cpu->cd.mips.delay_jmpaddr = cpu->cd.mips.gpr[rs];

			if (!quiet_mode_cached && cpu->machine->show_trace_tree
			    && rs == 31) {
				cpu->cd.mips.trace_tree_depth --;
			}

			return 1;
		case SPECIAL_JALR:
			if (cpu->cd.mips.delay_slot) {
				fatal("jalr: jump inside a jump's delay slot, or similar. TODO\n");
				cpu->running = 0;
				return 1;
			}

			rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
			rd = (instr[1] >> 3) & 31;

			tmpvalue = cpu->cd.mips.gpr[rs];
			cpu->cd.mips.gpr[rd] = cached_pc + 4;
			    /*  already increased by 4 earlier  */

			if (!quiet_mode_cached && cpu->machine->show_trace_tree
			    && rd == 31) {
				cpu->cd.mips.show_trace_delay = 2;
				cpu->cd.mips.show_trace_addr = tmpvalue;
			}

			cpu->cd.mips.delay_slot = TO_BE_DELAYED;
			cpu->cd.mips.delay_jmpaddr = tmpvalue;
			return 1;
		case SPECIAL_MFHI:
		case SPECIAL_MFLO:
			rd = (instr[1] >> 3) & 31;

			if (special6 == SPECIAL_MFHI) {
				cpu->cd.mips.gpr[rd] = cpu->cd.mips.hi;
#ifdef MFHILO_DELAY
				cpu->mfhi_delay = 3;
#endif
			}
			if (special6 == SPECIAL_MFLO) {
				cpu->cd.mips.gpr[rd] = cpu->cd.mips.lo;
#ifdef MFHILO_DELAY
				cpu->mflo_delay = 3;
#endif
			}
			return 1;
		case SPECIAL_ADD:
		case SPECIAL_ADDU:
		case SPECIAL_SUB:
		case SPECIAL_SUBU:
		case SPECIAL_AND:
		case SPECIAL_OR:
		case SPECIAL_XOR:
		case SPECIAL_NOR:
		case SPECIAL_SLT:
		case SPECIAL_SLTU:
		case SPECIAL_MTLO:
		case SPECIAL_MTHI:
		case SPECIAL_MULT:
		case SPECIAL_MULTU:
		case SPECIAL_DMULT:
		case SPECIAL_DMULTU:
		case SPECIAL_DIV:
		case SPECIAL_DIVU:
		case SPECIAL_DDIV:
		case SPECIAL_DDIVU:
		case SPECIAL_TGE:
		case SPECIAL_TGEU:
		case SPECIAL_TLT:
		case SPECIAL_TLTU:
		case SPECIAL_TEQ:
		case SPECIAL_TNE:
		case SPECIAL_DADD:
		case SPECIAL_DADDU:
		case SPECIAL_DSUB:
		case SPECIAL_DSUBU:
		case SPECIAL_MOVZ:
		case SPECIAL_MOVN:
			rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
			rt = instr[2] & 31;
			rd = (instr[1] >> 3) & 31;

#ifdef MFHILO_DELAY
			if (cpu->mflo_delay > 0 && (
			    special6 == SPECIAL_DDIV ||   special6 == SPECIAL_DDIVU ||
			    special6 == SPECIAL_DIV ||    special6 == SPECIAL_DIVU ||
			    special6 == SPECIAL_DMULT ||  special6 == SPECIAL_DMULTU ||
			    special6 == SPECIAL_MTLO ||   special6 == SPECIAL_MULT
			    || special6 == SPECIAL_MULTU
			    ) )
				debug("warning: instruction modifying LO too early after mflo!\n");

			if (cpu->mfhi_delay > 0 && (
			    special6 == SPECIAL_DDIV ||  special6 == SPECIAL_DDIVU ||
			    special6 == SPECIAL_DIV ||   special6 == SPECIAL_DIVU ||
			    special6 == SPECIAL_DMULT || special6 == SPECIAL_DMULTU ||
			    special6 == SPECIAL_MTHI ||  special6 == SPECIAL_MULT
			    || special6 == SPECIAL_MULTU
			    ) )
				debug("warning: instruction modifying HI too early after mfhi!\n");
#endif

			if (special6 == SPECIAL_ADDU) {
				cpu->cd.mips.gpr[rd] = cpu->cd.mips.gpr[rs] + cpu->cd.mips.gpr[rt];
				cpu->cd.mips.gpr[rd] &= 0xffffffffULL;
				if (cpu->cd.mips.gpr[rd] & 0x80000000ULL)
					cpu->cd.mips.gpr[rd] |= 0xffffffff00000000ULL;
				break;
			}
			if (special6 == SPECIAL_ADD) {
				/*  According to the MIPS64 manual:  */
				uint64_t temp, temp1, temp2;
				temp1 = cpu->cd.mips.gpr[rs] + ((cpu->cd.mips.gpr[rs] & 0x80000000ULL) << 1);
				temp2 = cpu->cd.mips.gpr[rt] + ((cpu->cd.mips.gpr[rt] & 0x80000000ULL) << 1);
				temp = temp1 + temp2;
#if 0
	/*  TODO: apparently this doesn't work (an example of
	something that breaks is NetBSD/sgimips' mips3_TBIA()  */
				/*  If bits 32 and 31 of temp differ, then it's an overflow  */
				temp1 = temp & 0x100000000ULL;
				temp2 = temp & 0x80000000ULL;
				if ((temp1 && !temp2) || (!temp1 && temp2)) {
					mips_cpu_exception(cpu, EXCEPTION_OV, 0, 0, 0, 0, 0, 0);
					break;
				}
#endif
				cpu->cd.mips.gpr[rd] = temp & 0xffffffffULL;
				if (cpu->cd.mips.gpr[rd] & 0x80000000ULL)
					cpu->cd.mips.gpr[rd] |= 0xffffffff00000000ULL;
				break;
			}
			if (special6 == SPECIAL_SUBU) {
				cpu->cd.mips.gpr[rd] = cpu->cd.mips.gpr[rs] - cpu->cd.mips.gpr[rt];
				cpu->cd.mips.gpr[rd] &= 0xffffffffULL;
				if (cpu->cd.mips.gpr[rd] & 0x80000000ULL)
					cpu->cd.mips.gpr[rd] |= 0xffffffff00000000ULL;
				break;
			}
			if (special6 == SPECIAL_SUB) {
				/*  According to the MIPS64 manual:  */
				uint64_t temp, temp1, temp2;
				temp1 = cpu->cd.mips.gpr[rs] + ((cpu->cd.mips.gpr[rs] & 0x80000000ULL) << 1);
				temp2 = cpu->cd.mips.gpr[rt] + ((cpu->cd.mips.gpr[rt] & 0x80000000ULL) << 1);
				temp = temp1 - temp2;
#if 0
				/*  If bits 32 and 31 of temp differ, then it's an overflow  */
				temp1 = temp & 0x100000000ULL;
				temp2 = temp & 0x80000000ULL;
				if ((temp1 && !temp2) || (!temp1 && temp2)) {
					mips_cpu_exception(cpu, EXCEPTION_OV, 0, 0, 0, 0, 0, 0);
					break;
				}
#endif
				cpu->cd.mips.gpr[rd] = temp & 0xffffffffULL;
				if (cpu->cd.mips.gpr[rd] & 0x80000000ULL)
					cpu->cd.mips.gpr[rd] |= 0xffffffff00000000ULL;
				break;
			}

			if (special6 == SPECIAL_AND) {
				cpu->cd.mips.gpr[rd] = cpu->cd.mips.gpr[rs] & cpu->cd.mips.gpr[rt];
				break;
			}
			if (special6 == SPECIAL_OR) {
				cpu->cd.mips.gpr[rd] = cpu->cd.mips.gpr[rs] | cpu->cd.mips.gpr[rt];
				break;
			}
			if (special6 == SPECIAL_XOR) {
				cpu->cd.mips.gpr[rd] = cpu->cd.mips.gpr[rs] ^ cpu->cd.mips.gpr[rt];
				break;
			}
			if (special6 == SPECIAL_NOR) {
				cpu->cd.mips.gpr[rd] = ~(cpu->cd.mips.gpr[rs] | cpu->cd.mips.gpr[rt]);
				break;
			}
			if (special6 == SPECIAL_SLT) {
				cpu->cd.mips.gpr[rd] = (int64_t)cpu->cd.mips.gpr[rs] < (int64_t)cpu->cd.mips.gpr[rt];
				break;
			}
			if (special6 == SPECIAL_SLTU) {
				cpu->cd.mips.gpr[rd] = cpu->cd.mips.gpr[rs] < cpu->cd.mips.gpr[rt];
				break;
			}
			if (special6 == SPECIAL_MTLO) {
				cpu->cd.mips.lo = cpu->cd.mips.gpr[rs];
				break;
			}
			if (special6 == SPECIAL_MTHI) {
				cpu->cd.mips.hi = cpu->cd.mips.gpr[rs];
				break;
			}
			if (special6 == SPECIAL_MULT) {
				int64_t f1, f2, sum;
				f1 = cpu->cd.mips.gpr[rs] & 0xffffffffULL;
				/*  sign extend f1  */
				if (f1 & 0x80000000ULL)
					f1 |= 0xffffffff00000000ULL;
				f2 = cpu->cd.mips.gpr[rt] & 0xffffffffULL;
				/*  sign extend f2  */
				if (f2 & 0x80000000ULL)
					f2 |= 0xffffffff00000000ULL;
				sum = f1 * f2;

				cpu->cd.mips.lo = sum & 0xffffffffULL;
				cpu->cd.mips.hi = ((uint64_t)sum >> 32) & 0xffffffffULL;

				/*  sign-extend:  */
				if (cpu->cd.mips.lo & 0x80000000ULL)
					cpu->cd.mips.lo |= 0xffffffff00000000ULL;
				if (cpu->cd.mips.hi & 0x80000000ULL)
					cpu->cd.mips.hi |= 0xffffffff00000000ULL;

				/*
				 *  NOTE:  The stuff about rd!=0 is just a
				 *  guess, judging from how some NetBSD code
				 *  seems to execute.  It is not documented in
				 *  the MIPS64 ISA docs :-/
				 */

				if (rd != 0) {
					if (cpu->cd.mips.cpu_type.rev != MIPS_R5900)
						debug("WARNING! mult_xx is an undocumented instruction!");
					cpu->cd.mips.gpr[rd] = cpu->cd.mips.lo;
				}
				break;
			}
			if (special6 == SPECIAL_MULTU) {
				uint64_t f1, f2, sum;
				/*  zero extend f1 and f2  */
				f1 = cpu->cd.mips.gpr[rs] & 0xffffffffULL;
				f2 = cpu->cd.mips.gpr[rt] & 0xffffffffULL;
				sum = f1 * f2;
				cpu->cd.mips.lo = sum & 0xffffffffULL;
				cpu->cd.mips.hi = (sum >> 32) & 0xffffffffULL;

				/*  sign-extend:  */
				if (cpu->cd.mips.lo & 0x80000000ULL)
					cpu->cd.mips.lo |= 0xffffffff00000000ULL;
				if (cpu->cd.mips.hi & 0x80000000ULL)
					cpu->cd.mips.hi |= 0xffffffff00000000ULL;
				break;
			}
			/*
			 *  TODO:  I'm too tired to think now.  DMULT is probably
			 *  correct, but is DMULTU?  (Unsigned 64x64 multiply.)
			 *  Or, hm, perhaps it is dmult which is incorrect.
			 */
			if (special6 == SPECIAL_DMULT || special6 == SPECIAL_DMULTU) {
				/*  64x64 = 128 bit multiplication:  SLOW!!!  TODO  */
				uint64_t i, low_add, high_add;

				cpu->cd.mips.lo = cpu->cd.mips.hi = 0;
				for (i=0; i<64; i++) {
					uint64_t bit = cpu->cd.mips.gpr[rt] & ((uint64_t)1 << i);
					if (bit) {
						/*  Add cpu->cd.mips.gpr[rs] to hi and lo:  */
						low_add = (cpu->cd.mips.gpr[rs] << i);
						high_add = (cpu->cd.mips.gpr[rs] >> (64-i));
						if (i==0)			/*  WEIRD BUG in the compiler? Or maybe I'm just stupid  */
							high_add = 0;		/*  these lines are necessary, a >> 64 doesn't seem to do anything  */
						if (cpu->cd.mips.lo + low_add < cpu->cd.mips.lo)
							cpu->cd.mips.hi ++;
						cpu->cd.mips.lo += low_add;
						cpu->cd.mips.hi += high_add;
					}
				}
				break;
			}
			if (special6 == SPECIAL_DIV) {
				int64_t a, b;
				/*  Signextend rs and rt:  */
				a = cpu->cd.mips.gpr[rs] & 0xffffffffULL;
				if (a & 0x80000000ULL)
					a |= 0xffffffff00000000ULL;
				b = cpu->cd.mips.gpr[rt] & 0xffffffffULL;
				if (b & 0x80000000ULL)
					b |= 0xffffffff00000000ULL;

				if (b == 0) {
					/*  undefined  */
					cpu->cd.mips.lo = cpu->cd.mips.hi = 0;
				} else {
					cpu->cd.mips.lo = a / b;
					cpu->cd.mips.hi = a % b;
				}
				/*  Sign-extend lo and hi:  */
				cpu->cd.mips.lo &= 0xffffffffULL;
				if (cpu->cd.mips.lo & 0x80000000ULL)
					cpu->cd.mips.lo |= 0xffffffff00000000ULL;
				cpu->cd.mips.hi &= 0xffffffffULL;
				if (cpu->cd.mips.hi & 0x80000000ULL)
					cpu->cd.mips.hi |= 0xffffffff00000000ULL;
				break;
			}
			if (special6 == SPECIAL_DIVU) {
				int64_t a, b;
				/*  Zero-extend rs and rt:  */
				a = cpu->cd.mips.gpr[rs] & 0xffffffffULL;
				b = cpu->cd.mips.gpr[rt] & 0xffffffffULL;
				if (b == 0) {
					/*  undefined  */
					cpu->cd.mips.lo = cpu->cd.mips.hi = 0;
				} else {
					cpu->cd.mips.lo = a / b;
					cpu->cd.mips.hi = a % b;
				}
				/*  Sign-extend lo and hi:  */
				cpu->cd.mips.lo &= 0xffffffffULL;
				if (cpu->cd.mips.lo & 0x80000000ULL)
					cpu->cd.mips.lo |= 0xffffffff00000000ULL;
				cpu->cd.mips.hi &= 0xffffffffULL;
				if (cpu->cd.mips.hi & 0x80000000ULL)
					cpu->cd.mips.hi |= 0xffffffff00000000ULL;
				break;
			}
			if (special6 == SPECIAL_DDIV) {
				if (cpu->cd.mips.gpr[rt] == 0) {
					cpu->cd.mips.lo = cpu->cd.mips.hi = 0;		/*  undefined  */
				} else {
					cpu->cd.mips.lo = (int64_t)cpu->cd.mips.gpr[rs] / (int64_t)cpu->cd.mips.gpr[rt];
					cpu->cd.mips.hi = (int64_t)cpu->cd.mips.gpr[rs] % (int64_t)cpu->cd.mips.gpr[rt];
				}
				break;
			}
			if (special6 == SPECIAL_DDIVU) {
				if (cpu->cd.mips.gpr[rt] == 0) {
					cpu->cd.mips.lo = cpu->cd.mips.hi = 0;		/*  undefined  */
				} else {
					cpu->cd.mips.lo = cpu->cd.mips.gpr[rs] / cpu->cd.mips.gpr[rt];
					cpu->cd.mips.hi = cpu->cd.mips.gpr[rs] % cpu->cd.mips.gpr[rt];
				}
				break;
			}
			if (special6 == SPECIAL_TGE) {
				if ((int64_t)cpu->cd.mips.gpr[rs] >= (int64_t)cpu->cd.mips.gpr[rt])
					mips_cpu_exception(cpu, EXCEPTION_TR, 0, 0, 0, 0, 0, 0);
				break;
			}
			if (special6 == SPECIAL_TGEU) {
				if (cpu->cd.mips.gpr[rs] >= cpu->cd.mips.gpr[rt])
					mips_cpu_exception(cpu, EXCEPTION_TR, 0, 0, 0, 0, 0, 0);
				break;
			}
			if (special6 == SPECIAL_TLT) {
				if ((int64_t)cpu->cd.mips.gpr[rs] < (int64_t)cpu->cd.mips.gpr[rt])
					mips_cpu_exception(cpu, EXCEPTION_TR, 0, 0, 0, 0, 0, 0);
				break;
			}
			if (special6 == SPECIAL_TLTU) {
				if (cpu->cd.mips.gpr[rs] < cpu->cd.mips.gpr[rt])
					mips_cpu_exception(cpu, EXCEPTION_TR, 0, 0, 0, 0, 0, 0);
				break;
			}
			if (special6 == SPECIAL_TEQ) {
				if (cpu->cd.mips.gpr[rs] == cpu->cd.mips.gpr[rt])
					mips_cpu_exception(cpu, EXCEPTION_TR, 0, 0, 0, 0, 0, 0);
				break;
			}
			if (special6 == SPECIAL_TNE) {
				if (cpu->cd.mips.gpr[rs] != cpu->cd.mips.gpr[rt])
					mips_cpu_exception(cpu, EXCEPTION_TR, 0, 0, 0, 0, 0, 0);
				break;
			}
			if (special6 == SPECIAL_DADD) {
				cpu->cd.mips.gpr[rd] = cpu->cd.mips.gpr[rs] + cpu->cd.mips.gpr[rt];
				/*  TODO:  exception on overflow  */
				break;
			}
			if (special6 == SPECIAL_DADDU) {
				cpu->cd.mips.gpr[rd] = cpu->cd.mips.gpr[rs] + cpu->cd.mips.gpr[rt];
				break;
			}
			if (special6 == SPECIAL_DSUB) {
				cpu->cd.mips.gpr[rd] = cpu->cd.mips.gpr[rs] - cpu->cd.mips.gpr[rt];
				/*  TODO:  exception on overflow  */
				break;
			}
			if (special6 == SPECIAL_DSUBU) {
				cpu->cd.mips.gpr[rd] = cpu->cd.mips.gpr[rs] - cpu->cd.mips.gpr[rt];
				break;
			}
			if (special6 == SPECIAL_MOVZ) {
				if (cpu->cd.mips.gpr[rt] == 0)
					cpu->cd.mips.gpr[rd] = cpu->cd.mips.gpr[rs];
				break;
			}
			if (special6 == SPECIAL_MOVN) {
				if (cpu->cd.mips.gpr[rt] != 0)
					cpu->cd.mips.gpr[rd] = cpu->cd.mips.gpr[rs];
				return 1;
			}
			return 1;
		case SPECIAL_SYNC:
			imm = ((instr[1] & 7) << 2) + (instr[0] >> 6);
			/*  TODO: actually sync  */

			/*  Clear the LLbit (at least on R10000):  */
			cpu->cd.mips.rmw = 0;
			return 1;
		case SPECIAL_SYSCALL:
			imm = ((instr[3] << 24) + (instr[2] << 16) +
			    (instr[1] << 8) + instr[0]) >> 6;
			imm &= 0xfffff;

			if (cpu->machine->userland_emul)
				useremul_syscall(cpu, imm);
			else
				mips_cpu_exception(cpu, EXCEPTION_SYS,
				    0, 0, 0, 0, 0, 0);
			return 1;
		case SPECIAL_BREAK:
			mips_cpu_exception(cpu, EXCEPTION_BP, 0, 0, 0, 0, 0, 0);
			return 1;
		case SPECIAL_MFSA:
			/*  R5900? What on earth does this thing do?  */
			rd = (instr[1] >> 3) & 31;
			/*  TODO  */
			return 1;
		case SPECIAL_MTSA:
			/*  R5900? What on earth does this thing do?  */
			rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
			/*  TODO  */
			return 1;
		default:
			if (!instruction_trace_cached) {
				fatal("cpu%i @ %016llx: %02x%02x%02x%02x%s\t",
				    cpu->cpu_id, cpu->cd.mips.pc_last,
				    instr[3], instr[2], instr[1], instr[0], cpu_flags(cpu));
			}
			fatal("unimplemented special6 = 0x%02x\n", special6);
			cpu->running = 0;
			return 1;
		}
		return 1;
	case HI6_BEQ:
	case HI6_BEQL:
	case HI6_BNE:
	case HI6_BGTZ:
	case HI6_BGTZL:
	case HI6_BLEZ:
	case HI6_BLEZL:
	case HI6_BNEL:
	case HI6_ADDI:
	case HI6_ADDIU:
	case HI6_DADDI:
	case HI6_DADDIU:
	case HI6_SLTI:
	case HI6_SLTIU:
	case HI6_ANDI:
	case HI6_ORI:
	case HI6_XORI:
	case HI6_LUI:
	case HI6_LB:
	case HI6_LBU:
	case HI6_LH:
	case HI6_LHU:
	case HI6_LW:
	case HI6_LWU:
	case HI6_LD:
	case HI6_LQ_MDMX:
	case HI6_LWC1:
	case HI6_LWC2:
	case HI6_LWC3:
	case HI6_LDC1:
	case HI6_LDC2:
	case HI6_LL:
	case HI6_LLD:
	case HI6_SB:
	case HI6_SH:
	case HI6_SW:
	case HI6_SD:
	case HI6_SQ:
	case HI6_SC:
	case HI6_SCD:
	case HI6_SWC1:
	case HI6_SWC2:
	case HI6_SWC3:
	case HI6_SDC1:
	case HI6_SDC2:
	case HI6_LWL:	/*  Unaligned load/store  */
	case HI6_LWR:
	case HI6_LDL:
	case HI6_LDR:
	case HI6_SWL:
	case HI6_SWR:
	case HI6_SDL:
	case HI6_SDR:
		rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
		rt = instr[2] & 31;
		imm = (instr[1] << 8) + instr[0];
		if (imm >= 32768)		/*  signed 16-bit  */
			imm -= 65536;

		tmpvalue = imm;		/*  used later in several cases  */

		switch (hi6) {
		case HI6_ADDI:
		case HI6_ADDIU:
		case HI6_DADDI:
		case HI6_DADDIU:
			tmpvalue = cpu->cd.mips.gpr[rs];
			result_value = cpu->cd.mips.gpr[rs] + imm;

			if (hi6 == HI6_ADDI || hi6 == HI6_DADDI) {
				/*
				 *  addi and daddi should trap on overflow:
				 *
				 *  TODO:  This is incorrect? The R4000 manual
				 *  says that overflow occurs if the carry bits
				 *  out of bit 62 and 63 differ. The
				 *  destination register should not be modified
				 *  on overflow.
				 */
				if (imm >= 0) {
					/*  Turn around from 0x7fff.. to 0x800 ?  Then overflow.  */
					if (   ((hi6 == HI6_ADDI && (result_value &
					    0x80000000ULL) && (tmpvalue &
					    0x80000000ULL)==0))
					    || ((hi6 == HI6_DADDI && (result_value &
					    0x8000000000000000ULL) && (tmpvalue &
					    0x8000000000000000ULL)==0)) ) {
						mips_cpu_exception(cpu, EXCEPTION_OV, 0, 0, 0, 0, 0, 0);
						break;
					}
				} else {
					/*  Turn around from 0x8000.. to 0x7fff.. ?  Then overflow.  */
					if (   ((hi6 == HI6_ADDI && (result_value &
					    0x80000000ULL)==0 && (tmpvalue &
					    0x80000000ULL)))
					    || ((hi6 == HI6_DADDI && (result_value &
					    0x8000000000000000ULL)==0 && (tmpvalue &
					    0x8000000000000000ULL))) ) {
						mips_cpu_exception(cpu, EXCEPTION_OV, 0, 0, 0, 0, 0, 0);
						break;
					}
				}
			}

			cpu->cd.mips.gpr[rt] = result_value;

			/*
			 *  Super-ugly speed-hack:  (only if speed_tricks != 0)
			 *  NOTE: This makes the emulation less correct.
			 *
			 *  If we encounter a loop such as:
			 *
			 *	8012f5f4: 1c40ffff      bgtz r0,r2,ffffffff8012f5f4
			 *	8012f5f8: 2442ffff (d)  addiu r2,r2,-1
			 *
			 *  then it is a small loop which simply waits for r2
			 *  to become zero.
			 *
			 *  TODO:  increaste the count register, and cause
			 *  interrupts!!!  For now: return as if we just
			 *  executed 1 instruction.
			 */
			ninstrs_executed = 1;
			if (cpu->machine->speed_tricks && cpu->cd.mips.delay_slot &&
			    cpu->cd.mips.last_was_jumptoself &&
			    cpu->cd.mips.jump_to_self_reg == rt &&
			    cpu->cd.mips.jump_to_self_reg == rs) {
				if ((int64_t)cpu->cd.mips.gpr[rt] > 1 && (int64_t)cpu->cd.mips.gpr[rt] < 0x70000000
				    && (imm >= -30000 && imm <= -1)) {
					if (instruction_trace_cached)
						debug("changing r%i from %016llx to", rt, (long long)cpu->cd.mips.gpr[rt]);

					while ((int64_t)cpu->cd.mips.gpr[rt] > 0 && ninstrs_executed < 1000
					    && ((int64_t)cpu->cd.mips.gpr[rt] + (int64_t)imm) > 0) {
						cpu->cd.mips.gpr[rt] += (int64_t)imm;
						ninstrs_executed += 2;
					}

					if (instruction_trace_cached)
						debug(" %016llx\n", (long long)cpu->cd.mips.gpr[rt]);

					/*  TODO: return value, cpu->cd.mips.gpr[rt] * 2;  */
				}
				if ((int64_t)cpu->cd.mips.gpr[rt] > -0x70000000 && (int64_t)cpu->cd.mips.gpr[rt] < -1
				     && (imm >= 1 && imm <= 30000)) {
					if (instruction_trace_cached)
						debug("changing r%i from %016llx to", rt, (long long)cpu->cd.mips.gpr[rt]);

					while ((int64_t)cpu->cd.mips.gpr[rt] < 0 && ninstrs_executed < 1000
					    && ((int64_t)cpu->cd.mips.gpr[rt] + (int64_t)imm) < 0) {
						cpu->cd.mips.gpr[rt] += (int64_t)imm;
						ninstrs_executed += 2;
					}

					if (instruction_trace_cached)
						debug(" %016llx\n", (long long)cpu->cd.mips.gpr[rt]);
				}
			}

			if (hi6 == HI6_ADDI || hi6 == HI6_ADDIU) {
				/*  Sign-extend:  */
				cpu->cd.mips.gpr[rt] &= 0xffffffffULL;
				if (cpu->cd.mips.gpr[rt] & 0x80000000ULL)
					cpu->cd.mips.gpr[rt] |= 0xffffffff00000000ULL;
			}
			return ninstrs_executed;
		case HI6_BEQ:
		case HI6_BNE:
		case HI6_BGTZ:
		case HI6_BGTZL:
		case HI6_BLEZ:
		case HI6_BLEZL:
		case HI6_BEQL:
		case HI6_BNEL:
			if (cpu->cd.mips.delay_slot) {
				fatal("b*: jump inside a jump's delay slot, or similar. TODO\n");
				cpu->running = 0;
				return 1;
			}
			likely = cond = 0;
			switch (hi6) {
			case HI6_BNEL:	likely = 1;
			case HI6_BNE:	cond = (cpu->cd.mips.gpr[rt] != cpu->cd.mips.gpr[rs]);
					break;
			case HI6_BEQL:	likely = 1;
			case HI6_BEQ:	cond = (cpu->cd.mips.gpr[rt] == cpu->cd.mips.gpr[rs]);
					break;
			case HI6_BLEZL:	likely = 1;
			case HI6_BLEZ:	cond = ((int64_t)cpu->cd.mips.gpr[rs] <= 0);
					break;
			case HI6_BGTZL:	likely = 1;
			case HI6_BGTZ:	cond = ((int64_t)cpu->cd.mips.gpr[rs] > 0);
					break;
			}

			if (cond) {
				cpu->cd.mips.delay_slot = TO_BE_DELAYED;
				cpu->cd.mips.delay_jmpaddr = cached_pc + (imm << 2);
			} else {
				if (likely)
					cpu->cd.mips.nullify_next = 1;		/*  nullify delay slot  */
			}

			if (imm==-1 && (hi6 == HI6_BGTZ || hi6 == HI6_BLEZ ||
			    (hi6 == HI6_BGTZL && cond) ||
			    (hi6 == HI6_BLEZL && cond) ||
			    (hi6 == HI6_BNE && (rt==0 || rs==0)) ||
			    (hi6 == HI6_BEQ && (rt==0 || rs==0)))) {
				cpu->cd.mips.last_was_jumptoself = 2;
				if (rs == 0)
					cpu->cd.mips.jump_to_self_reg = rt;
				else
					cpu->cd.mips.jump_to_self_reg = rs;
			}
			return 1;
		case HI6_LUI:
			cpu->cd.mips.gpr[rt] = (imm << 16);
			/*  No sign-extending necessary, as imm already
			    was sign-extended if it was negative.  */
			break;
		case HI6_SLTI:
			cpu->cd.mips.gpr[rt] = (int64_t)cpu->cd.mips.gpr[rs] < (int64_t)tmpvalue;
			break;
		case HI6_SLTIU:
			cpu->cd.mips.gpr[rt] = cpu->cd.mips.gpr[rs] < (uint64_t)imm;
			break;
		case HI6_ANDI:
			cpu->cd.mips.gpr[rt] = cpu->cd.mips.gpr[rs] & (tmpvalue & 0xffff);
			break;
		case HI6_ORI:
			cpu->cd.mips.gpr[rt] = cpu->cd.mips.gpr[rs] | (tmpvalue & 0xffff);
			break;
		case HI6_XORI:
			cpu->cd.mips.gpr[rt] = cpu->cd.mips.gpr[rs] ^ (tmpvalue & 0xffff);
			break;
		case HI6_LB:
		case HI6_LBU:
		case HI6_LH:
		case HI6_LHU:
		case HI6_LW:
		case HI6_LWU:
		case HI6_LD:
		case HI6_LQ_MDMX:
		case HI6_LWC1:
		case HI6_LWC2:
		case HI6_LWC3:	/*  pref  */
		case HI6_LDC1:
		case HI6_LDC2:
		case HI6_LL:
		case HI6_LLD:
		case HI6_SB:
		case HI6_SH:
		case HI6_SW:
		case HI6_SD:
		case HI6_SQ:
		case HI6_SC:
		case HI6_SCD:
		case HI6_SWC1:
		case HI6_SWC2:
		case HI6_SWC3:
		case HI6_SDC1:
		case HI6_SDC2:
			/*  These are the default "assumptions".  */
			linked = 0;
			st = 1;
			signd = 1;
			wlen = 4;

			switch (hi6) {
			/*  The most common ones:  */
			case HI6_LW:	{           st = 0;            }  break;
			case HI6_SW:	{                   signd = 0; }  break;

			case HI6_LB:	{ wlen = 1; st = 0;            }  break;
			case HI6_LBU:	{ wlen = 1; st = 0; signd = 0; }  break;
			case HI6_SB:	{ wlen = 1;         signd = 0; }  break;

			case HI6_LD:	{ wlen = 8; st = 0; signd = 0; }  break;
			case HI6_SD:	{ wlen = 8;         signd = 0; }  break;

			case HI6_LQ_MDMX:	{ wlen = 16; st = 0; signd = 0; }  break;	/*  R5900, otherwise MDMX (TODO)  */
			case HI6_SQ:		{ wlen = 16;         signd = 0; }  break;	/*  R5900 ?  */

			/*  The rest:  */
			case HI6_LH:	{ wlen = 2; st = 0;            }  break;
			case HI6_LHU:	{ wlen = 2; st = 0; signd = 0; }  break;
			case HI6_LWU:	{           st = 0; signd = 0; }  break;
			case HI6_LWC1:	{           st = 0;            }  break;
			case HI6_LWC2:	{           st = 0;            }  break;
			case HI6_LWC3:	{           st = 0;            }  break;
			case HI6_LDC1:	{ wlen = 8; st = 0; signd = 0; }  break;
			case HI6_LDC2:	{ wlen = 8; st = 0; signd = 0; }  break;

			case HI6_SH:	{ wlen = 2;         signd = 0; }  break;
			case HI6_SDC1:
			case HI6_SDC2:	wlen = 8;
			case HI6_SWC1:
			case HI6_SWC2:
			case HI6_SWC3:	{                   signd = 0; }  break;

			case HI6_LL:	{           st = 0; signd = 1; linked = 1; }  break;
			case HI6_LLD:	{ wlen = 8; st = 0; signd = 0; linked = 1; }  break;

			case HI6_SC:	{                   signd = 1; linked = 1; }  break;
			case HI6_SCD:	{ wlen = 8;         signd = 0; linked = 1; }  break;

			default:
				fatal("cannot be here\n");
				wlen = 4; st = 0; signd = 0;
			}

			/*
			 *  In the MIPS IV ISA, the 'lwc3' instruction is changed into 'pref'.
			 *  The pref instruction is emulated by not doing anything. :-)  TODO
			 */
			if (hi6 == HI6_LWC3 && cpu->cd.mips.cpu_type.isa_level >= 4) {
				/*  Clear the LLbit (at least on R10000):  */
				cpu->cd.mips.rmw = 0;
				break;
			}

			addr = cpu->cd.mips.gpr[rs] + imm;

			/*  Check for natural alignment:  */
			if ((addr & (wlen - 1)) != 0) {
				mips_cpu_exception(cpu, st? EXCEPTION_ADES : EXCEPTION_ADEL,
				    0, addr, 0, 0, 0, 0);
				break;
			}

#if 0
			if (cpu->cd.mips.cpu_type.isa_level == 4 && (imm & (wlen - 1)) != 0)
				debug("WARNING: low bits of imm value not zero! (MIPS IV) "
				    "pc=%016llx", (long long)cpu->cd.mips.pc_last);
#endif

			/*
			 *  Load Linked: This initiates a Read-Modify-Write
			 *  sequence.
			 */
			if (linked) {
				if (st==0) {
					/*  st == 0:  Load  */
					cpu->cd.mips.rmw      = 1;
					cpu->cd.mips.rmw_addr = addr;
					cpu->cd.mips.rmw_len  = wlen;

					/*
					 *  COP0_LLADDR is updated for
					 *  diagnostic purposes, except for
					 *  CPUs in the R10000 family.
					 */
					if (cpu->cd.mips.cpu_type.exc_model != MMU10K)
						cp0->reg[COP0_LLADDR] =
						    (addr >> 4) & 0xffffffffULL;
				} else {
					/*
					 *  st == 1:  Store
					 *  If rmw is 0, then the store failed.
					 *  (This cache-line was written to by
					 *  someone else.)
					 */
					if (cpu->cd.mips.rmw == 0) {
						/*  The store failed:  */
						cpu->cd.mips.gpr[rt] = 0;
						if (instruction_trace_cached)
							debug(" [COLLISION] ");
						break;
					}
				}
			} else {
				/*
				 *  If any kind of load or store occurs between
				 *  an ll and an sc, then the ll-sc sequence
				 *  should fail.  (This is local to each cpu.)
				 */
				cpu->cd.mips.rmw = 0;
			}

			value_hi = 0;

			if (st) {
				/*  store:  */
				int cpnr, success;

				if (hi6 == HI6_SWC3 || hi6 == HI6_SWC2 ||
				    hi6 == HI6_SDC1 || hi6 == HI6_SWC1) {
					cpnr = 1;
					switch (hi6) {
					case HI6_SWC3:	cpnr++;		/*  fallthrough  */
					case HI6_SWC2:	cpnr++;
					case HI6_SDC1:
					case HI6_SWC1:	if (cpu->cd.mips.coproc[cpnr] == NULL ||
							    (!(cp0->reg[COP0_STATUS] & ((1 << cpnr) << STATUS_CU_SHIFT))) ) {
								mips_cpu_exception(cpu, EXCEPTION_CPU, 0, 0, cpnr, 0, 0, 0);
								cpnr = -1;
								break;
							} else {
								/*  Special handling of 64-bit stores
								    on 32-bit CPUs, and on newer CPUs
								    in 32-bit compatiblity mode:  */
								if ((hi6==HI6_SDC1 || hi6==HI6_SDC2) &&
								    (cpu->cd.mips.cpu_type.isa_level <= 2 ||
								    !(cp0->reg[COP0_STATUS] & STATUS_FR))) {
									uint64_t a, b;
									coproc_register_read(cpu,
									    cpu->cd.mips.coproc[cpnr], rt, &a);
									coproc_register_read(cpu,
									    cpu->cd.mips.coproc[cpnr], rt^1, &b);
									if (rt & 1)
										fatal("WARNING: SDCx in 32-bit mode from odd register!\n");
									value = (a & 0xffffffffULL)
									    | (b << 32);
								} else
									coproc_register_read(cpu, cpu->cd.mips.coproc[cpnr], rt, &value);
							}
							break;
					default:
							;
					}
					if (cpnr < 0)
						break;
				} else
					value = cpu->cd.mips.gpr[rt];

				if (wlen == 4) {
					/*  Special case for 32-bit stores... (perhaps not worth it)  */
					if (cpu->byte_order == EMUL_LITTLE_ENDIAN) {
						d[0] = value & 0xff;         d[1] = (value >> 8) & 0xff;
						d[2] = (value >> 16) & 0xff; d[3] = (value >> 24) & 0xff;
					} else {
						d[3] = value & 0xff;         d[2] = (value >> 8) & 0xff;
						d[1] = (value >> 16) & 0xff; d[0] = (value >> 24) & 0xff;
					}
				} else if (wlen == 16) {
					value_hi = cpu->cd.mips.gpr_quadhi[rt];
					/*  Special case for R5900 128-bit stores:  */
					if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
						for (i=0; i<8; i++) {
							d[i] = (value >> (i*8)) & 255;
							d[i+8] = (value_hi >> (i*8)) & 255;
						}
					else
						for (i=0; i<8; i++) {
							d[i] = (value >> ((wlen-1-i)*8)) & 255;
							d[i + 8] = (value_hi >> ((wlen-1-i)*8)) & 255;
						}
				} else if (wlen == 1) {
					d[0] = value & 0xff;
				} else {
					/*  General case:  */
					uint64_t v = value;
					if (cpu->byte_order ==
					    EMUL_LITTLE_ENDIAN)
						for (i=0; i<wlen; i++) {
							d[i] = v & 255;
							v >>= 8;
						}
					else
						for (i=0; i<wlen; i++) {
							d[wlen-1-i] = v & 255;
							v >>= 8;
						}
				}

				success = memory_rw(cpu, cpu->mem, addr, d, wlen, MEM_WRITE, CACHE_DATA);
				if (!success) {
					/*  The store failed, and might have caused an exception.  */
					if (instruction_trace_cached)
						debug("(failed)]\n");
					break;
				}
			} else {
				/*  load:  */
				int cpnr = 1;
				int success;

				success = memory_rw(cpu, cpu->mem, addr, d, wlen, MEM_READ, CACHE_DATA);
				if (!success) {
					/*  The load failed, and might have caused an exception.  */
					if (instruction_trace_cached)
						debug("(failed)]\n");
					break;
				}

				if (wlen == 1)
					value = d[0] | (signd && (d[0]&128)? (-1 << 8) : 0);
				else if (wlen != 16) {
					/*  General case (except for 128-bit):  */
					int i;
					value = 0;
					if (cpu->byte_order == EMUL_LITTLE_ENDIAN) {
						if (signd && (d[wlen-1] & 128)!=0)	/*  sign extend  */
							value = -1;
						for (i=wlen-1; i>=0; i--) {
							value <<= 8;
							value += d[i];
						}
					} else {
						if (signd && (d[0] & 128)!=0)		/*  sign extend  */
							value = -1;
						for (i=0; i<wlen; i++) {
							value <<= 8;
							value += d[i];
						}
					}
				} else {
					/*  R5900 128-bit quadword:  */
					int i;
					value_hi = 0;
					value = 0;
					if (cpu->byte_order == EMUL_LITTLE_ENDIAN) {
						for (i=wlen-1; i>=0; i--) {
							value_hi <<= 8;
							value_hi += (value >> 56) & 255;
							value <<= 8;
							value += d[i];
						}
					} else {
						for (i=0; i<wlen; i++) {
							value_hi <<= 8;
							value_hi += (value >> 56) & 255;
							value <<= 8;
							value += d[i];
						}
					}
					cpu->cd.mips.gpr_quadhi[rt] = value_hi;
				}

				switch (hi6) {
				case HI6_LWC3:	cpnr++;		/*  fallthrough  */
				case HI6_LDC2:
				case HI6_LWC2:	cpnr++;
				case HI6_LDC1:
				case HI6_LWC1:	if (cpu->cd.mips.coproc[cpnr] == NULL ||
						    (!(cp0->reg[COP0_STATUS] & ((1 << cpnr) << STATUS_CU_SHIFT))) ) {
							mips_cpu_exception(cpu, EXCEPTION_CPU, 0, 0, cpnr, 0, 0, 0);
						} else {
							/*  Special handling of 64-bit loads
							    on 32-bit CPUs, and on newer CPUs
							    in 32-bit compatiblity mode:  */
							if ((hi6==HI6_LDC1 || hi6==HI6_LDC2) &&
							    (cpu->cd.mips.cpu_type.isa_level <= 2 ||
							    !(cp0->reg[COP0_STATUS] & STATUS_FR))) {
								uint64_t a, b;
								a = (int64_t)(int32_t) (value & 0xffffffffULL);
								b = (int64_t)(int32_t) (value >> 32);
								coproc_register_write(cpu,
								    cpu->cd.mips.coproc[cpnr], rt, &a,
								    hi6==HI6_LDC1 || hi6==HI6_LDC2);
								coproc_register_write(cpu,
								    cpu->cd.mips.coproc[cpnr], rt ^ 1, &b,
								    hi6==HI6_LDC1 || hi6==HI6_LDC2);
								if (rt & 1)
									fatal("WARNING: LDCx in 32-bit mode to odd register!\n");
							} else {
								coproc_register_write(cpu,
								    cpu->cd.mips.coproc[cpnr], rt, &value,
								    hi6==HI6_LDC1 || hi6==HI6_LDC2);
							}
						}
						break;
				default:	if (rt != 0)
							cpu->cd.mips.gpr[rt] = value;
				}
			}

			if (linked && st==1) {
				/*
				 *  The store succeeded. Invalidate any other
				 *  cpu's store to this cache line, and then
				 *  return 1 in gpr rt:
				 *
				 *  (this is a semi-ugly hack using global
				 * 'cpus')
				 *
				 *  TODO: How about invalidating other CPUs
				 *  stores to this cache line, even if this
				 *  was _NOT_ a linked store?
				 */
				for (i=0; i<cpu->machine->ncpus; i++) {
					if (cpu->machine->cpus[i]->cd.mips.rmw) {
						uint64_t yaddr = addr;
						uint64_t xaddr =
						    cpu->machine->cpus[i]->cd.mips.rmw_addr;
						uint64_t mask;
						mask = ~(cpu->machine->cpus[i]->
						    cd.mips.cache_linesize[CACHE_DATA]
						    - 1);
						xaddr &= mask;
						yaddr &= mask;
						if (xaddr == yaddr) {
							cpu->machine->cpus[i]->cd.mips.rmw = 0;
							cpu->machine->cpus[i]->cd.mips.rmw_addr = 0;
						}
					}
				}

				if (rt != 0)
					cpu->cd.mips.gpr[rt] = 1;

				if (instruction_trace_cached)
					debug(" [no collision] ");
				cpu->cd.mips.rmw = 0;
			}

			if (instruction_trace_cached) {
				switch (wlen) {
				case 2:	debug("0x%04x", (int)value); break;
				case 4:	debug("0x%08x", (int)value); break;
				case 8:	debug("0x%016llx", (long long)value);
					break;
				case 16:debug("0x%016llx", (long long)value_hi);
					debug("%016llx", (long long)value); 
					break;
				default:debug("0x%02x", (int)value);
				}
				debug("]\n");
			}
			return 1;
		case HI6_LWL:	/*  Unaligned load/store  */
		case HI6_LWR:
		case HI6_LDL:
		case HI6_LDR:
		case HI6_SWL:
		case HI6_SWR:
		case HI6_SDL:
		case HI6_SDR:
			/*  For L (Left):   address is the most significant byte  */
			/*  For R (Right):  address is the least significant byte  */
			addr = cpu->cd.mips.gpr[rs] + imm;

			is_left = 0;
			if (hi6 == HI6_SWL || hi6 == HI6_LWL ||
			    hi6 == HI6_SDL || hi6 == HI6_LDL)
				is_left = 1;

			wlen = 0; st = 0;
			signd = 0;
			if (hi6 == HI6_LWL || hi6 == HI6_LWR)
				signd = 1;

			if (hi6 == HI6_LWL || hi6 == HI6_LWR)	{ wlen = 4; st = 0; }
			if (hi6 == HI6_SWL || hi6 == HI6_SWR)	{ wlen = 4; st = 1; }
			if (hi6 == HI6_LDL || hi6 == HI6_LDR)	{ wlen = 8; st = 0; }
			if (hi6 == HI6_SDL || hi6 == HI6_SDR)	{ wlen = 8; st = 1; }

			dir = 1;		/*  big endian, Left  */
			reg_dir = -1;
			reg_ofs = wlen - 1;	/*  byte offset in the register  */
			if (!is_left) {
				dir = -dir;
				reg_ofs = 0;
				reg_dir = 1;
			}
			if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
				dir = -dir;

			result_value = cpu->cd.mips.gpr[rt];

			if (st) {
				/*  Store:  */
				uint64_t aligned_addr = addr & ~(wlen-1);
				unsigned char aligned_word[8];
				uint64_t oldpc = cpu->cd.mips.pc;
				/*
				 *  NOTE (this is ugly): The memory_rw()
				 *  generates a TLBL exception, if there
				 *  is a tlb refill exception. However, since
				 *  this is a Store, the exception is converted
				 *  to a TLBS:
				 */
				int ok = memory_rw(cpu, cpu->mem, aligned_addr,
				    &aligned_word[0], wlen, MEM_READ, CACHE_DATA);
				if (!ok) {
					if (cpu->cd.mips.pc != oldpc) {
						cp0->reg[COP0_CAUSE] &= ~CAUSE_EXCCODE_MASK;
						cp0->reg[COP0_CAUSE] |= (EXCEPTION_TLBS << CAUSE_EXCCODE_SHIFT);
					}
					return 1;
				}

				for (i=0; i<wlen; i++) {
					tmpaddr = addr + i*dir;
					/*  Have we moved into another word/dword? Then stop:  */
					if ( (tmpaddr & ~(wlen-1)) != (addr & ~(wlen-1)) )
						break;

					/*  debug("unaligned byte at %016llx, reg_ofs=%i reg=0x%016llx\n",
					    tmpaddr, reg_ofs, (long long)result_value);  */

					/*  Store one byte:  */
					aligned_word[tmpaddr & (wlen-1)] = (result_value >> (reg_ofs * 8)) & 255;

					reg_ofs += reg_dir;
				}

				ok = memory_rw(cpu, cpu->mem, aligned_addr, &aligned_word[0], wlen, MEM_WRITE, CACHE_DATA);
				if (!ok)
					return 1;
			} else {
				/*  Load:  */
				uint64_t aligned_addr = addr & ~(wlen-1);
				unsigned char aligned_word[8], databyte;
				int ok = memory_rw(cpu, cpu->mem, aligned_addr, &aligned_word[0], wlen, MEM_READ, CACHE_DATA);
				if (!ok)
					return 1;

				for (i=0; i<wlen; i++) {
					tmpaddr = addr + i*dir;
					/*  Have we moved into another word/dword? Then stop:  */
					if ( (tmpaddr & ~(wlen-1)) != (addr & ~(wlen-1)) )
						break;

					/*  debug("unaligned byte at %016llx, reg_ofs=%i reg=0x%016llx\n",
					    tmpaddr, reg_ofs, (long long)result_value);  */

					/*  Load one byte:  */
					databyte = aligned_word[tmpaddr & (wlen-1)];
					result_value &= ~((uint64_t)0xff << (reg_ofs * 8));
					result_value |= (uint64_t)databyte << (reg_ofs * 8);

					reg_ofs += reg_dir;
				}

				if (rt != 0)
					cpu->cd.mips.gpr[rt] = result_value;
			}

			/*  Sign extend for 32-bit load lefts:  */
			if (!st && signd && wlen == 4) {
				cpu->cd.mips.gpr[rt] &= 0xffffffffULL;
				if (cpu->cd.mips.gpr[rt] & 0x80000000ULL)
					cpu->cd.mips.gpr[rt] |= 0xffffffff00000000ULL;
			}

			if (instruction_trace_cached) {
				char *t;
				switch (wlen) {
				case 2:		t = "0x%04llx"; break;
				case 4:		t = "0x%08llx"; break;
				case 8:		t = "0x%016llx"; break;
				default:	t = "0x%02llx";
				}
				debug(t, (long long)cpu->cd.mips.gpr[rt]);
				debug("]\n");
			}

			return 1;
		}
		return 1;
	case HI6_REGIMM:
		regimm5 = instr[2] & 0x1f;

		if (show_opcode_statistics)
			cpu->cd.mips.stats__regimm[regimm5] ++;

		switch (regimm5) {
		case REGIMM_BLTZ:
		case REGIMM_BGEZ:
		case REGIMM_BLTZL:
		case REGIMM_BGEZL:
		case REGIMM_BLTZAL:
		case REGIMM_BLTZALL:
		case REGIMM_BGEZAL:
		case REGIMM_BGEZALL:
			rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
			imm = (instr[1] << 8) + instr[0];
			if (imm >= 32768)		/*  signed 16-bit  */
				imm -= 65536;

			cond = and_link = likely = 0;

			switch (regimm5) {
			case REGIMM_BLTZL:	likely = 1;
			case REGIMM_BLTZ:	cond = (cpu->cd.mips.gpr[rs] & ((uint64_t)1 << 63)) != 0;
						break;
			case REGIMM_BGEZL:	likely = 1;
			case REGIMM_BGEZ:	cond = (cpu->cd.mips.gpr[rs] & ((uint64_t)1 << 63)) == 0;
						break;

			case REGIMM_BLTZALL:	likely = 1;
			case REGIMM_BLTZAL:	and_link = 1;
						cond = (cpu->cd.mips.gpr[rs] & ((uint64_t)1 << 63)) != 0;
						break;
			case REGIMM_BGEZALL:	likely = 1;
			case REGIMM_BGEZAL:	and_link = 1;
						cond = (cpu->cd.mips.gpr[rs] & ((uint64_t)1 << 63)) == 0;
						break;
			}

			if (and_link)
				cpu->cd.mips.gpr[31] = cached_pc + 4;

			if (cond) {
				cpu->cd.mips.delay_slot = TO_BE_DELAYED;
				cpu->cd.mips.delay_jmpaddr = cached_pc + (imm << 2);
			} else {
				if (likely)
					cpu->cd.mips.nullify_next = 1;		/*  nullify delay slot  */
			}

			return 1;
		default:
			if (!instruction_trace_cached) {
				fatal("cpu%i @ %016llx: %02x%02x%02x%02x%s\t",
				    cpu->cpu_id, cpu->cd.mips.pc_last,
				    instr[3], instr[2], instr[1], instr[0], cpu_flags(cpu));
			}
			fatal("unimplemented regimm5 = 0x%02x\n", regimm5);
			cpu->running = 0;
			return 1;
		}
		/*  NOT REACHED  */
	case HI6_J:
	case HI6_JAL:
		if (cpu->cd.mips.delay_slot) {
			fatal("j/jal: jump inside a jump's delay slot, or similar. TODO\n");
			cpu->running = 0;
			return 1;
		}
		imm = ((instr[3] & 3) << 24) + (instr[2] << 16) + (instr[1] << 8) + instr[0];
		imm <<= 2;

		if (hi6 == HI6_JAL)
			cpu->cd.mips.gpr[31] = cached_pc + 4;		/*  pc already increased by 4 earlier  */

		addr = cached_pc & ~((1 << 28) - 1);
		addr |= imm;

		cpu->cd.mips.delay_slot = TO_BE_DELAYED;
		cpu->cd.mips.delay_jmpaddr = addr;

		if (!quiet_mode_cached && cpu->machine->show_trace_tree &&
		    hi6 == HI6_JAL) {
			cpu->cd.mips.show_trace_delay = 2;
			cpu->cd.mips.show_trace_addr = addr;
		}

		return 1;
	case HI6_COP0:
	case HI6_COP1:
	case HI6_COP2:
	case HI6_COP3:
		imm = (instr[3] << 24) + (instr[2] << 16) + (instr[1] << 8) + instr[0];
		imm &= ((1 << 26) - 1);

		cpnr = 0;
		if (hi6 == HI6_COP0)	cpnr = 0;
		if (hi6 == HI6_COP1)	cpnr = 1;
		if (hi6 == HI6_COP2)	cpnr = 2;
		if (hi6 == HI6_COP3)	cpnr = 3;

		/*
		 *  If there is no coprocessor nr cpnr, or we are running in
		 *  userland and the coprocessor is not marked as Useable in
		 *  the status register of CP0, then we get an exception.
		 *
		 *  An exception (hehe) to this rule is that the kernel should
		 *  always be able to access CP0.
		 */
		/*  Set tmp = 1 if we're in user mode.  */
		tmp = 0;
		switch (cpu->cd.mips.cpu_type.exc_model) {
		case EXC3K:
			/*
			 *  NOTE: If the KU bit is checked, Linux crashes.
			 *  It is the PC that counts.  TODO: Check whether
			 *  this is true or not for R4000 as well.
			 */
			if (cached_pc <= 0x7fffffff) /* if (cp0->reg[COP0_STATUS] & MIPS1_SR_KU_CUR) */
				tmp = 1;
			break;
		default:
			/*  R4000 etc:  (TODO: How about supervisor mode?)  */
			if (((cp0->reg[COP0_STATUS] & STATUS_KSU_MASK) >> STATUS_KSU_SHIFT) != KSU_KERNEL)
				tmp = 1;
			if (cp0->reg[COP0_STATUS] & STATUS_ERL)
				tmp = 0;
			if (cp0->reg[COP0_STATUS] & STATUS_EXL)
				tmp = 0;
			break;
		}
		if (cpu->cd.mips.coproc[cpnr] == NULL ||
		    (tmp && !(cp0->reg[COP0_STATUS] & ((1 << cpnr) << STATUS_CU_SHIFT))) ||
		    (!tmp && cpnr >= 1 && !(cp0->reg[COP0_STATUS] & ((1 << cpnr) << STATUS_CU_SHIFT)))
		    ) {
			if (instruction_trace_cached)
				debug("cop%i\t0x%08x => coprocessor unusable\n", cpnr, (int)imm);

			mips_cpu_exception(cpu, EXCEPTION_CPU, 0, 0, cpnr, 0, 0, 0);
		} else {
			/*
			 *  Execute the coprocessor function. The
			 *  coproc_function code outputs instruction
			 *  trace, if necessary.
			 */
			coproc_function(cpu, cpu->cd.mips.coproc[cpnr],
			    cpnr, imm, 0, 1);
		}
		return 1;
	case HI6_CACHE:
		rt   = ((instr[3] & 3) << 3) + (instr[2] >> 5);	/*  base  */
		copz = instr[2] & 31;
		imm  = (instr[1] << 8) + instr[0];

		cache_op    = copz >> 2;
		which_cache = copz & 3;

		/*
		 *  TODO:  The cache instruction is implementation dependant.
		 *  This is really ugly.
		 */

#if 0
Remove this...

/*		if (cpu->cd.mips.cpu_type.mmu_model == MMU10K) {  */
/*			printf("taghi=%08lx taglo=%08lx\n",
			    (long)cp0->reg[COP0_TAGDATA_HI],
			    (long)cp0->reg[COP0_TAGDATA_LO]);
*/
			if (cp0->reg[COP0_TAGDATA_HI] == 0 &&
			    cp0->reg[COP0_TAGDATA_LO] == 0) {
				/*  Normal cache operation:  */
				cpu->r10k_cache_disable_TODO = 0;
			} else {
				/*  Dislocate the cache:  */
				cpu->r10k_cache_disable_TODO = 1;
			}
/*		}  */
#endif

		/*
		 *  Clear the LLbit (at least on R10000):
		 *  TODO: How about R4000?
		 */
		cpu->cd.mips.rmw = 0;

		return 1;
	case HI6_SPECIAL2:
		special6 = instr[0] & 0x3f;

		if (show_opcode_statistics)
			cpu->cd.mips.stats__special2[special6] ++;

		instrword = (instr[3] << 24) + (instr[2] << 16) + (instr[1] << 8) + instr[0];

		rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
		rt = instr[2] & 31;
		rd = (instr[1] >> 3) & 31;

		/*  printf("special2 %08x  rs=0x%02x rt=0x%02x rd=0x%02x\n", instrword, rs,rt,rd);  */

		/*
		 *  Many of these can be found in the R5000 docs, or figured out
		 *  by studying binutils source code for MIPS instructions.
		 */

		if ((instrword & 0xfc0007ffULL) == 0x70000000) {
			{
				int32_t a, b;
				int64_t c;
				a = (int32_t)cpu->cd.mips.gpr[rs];
				b = (int32_t)cpu->cd.mips.gpr[rt];
				c = a * b;
				c += (cpu->cd.mips.lo & 0xffffffffULL)
				    + (cpu->cd.mips.hi << 32);
				cpu->cd.mips.lo = (int64_t)((int32_t)c);
				cpu->cd.mips.hi = (int64_t)((int32_t)(c >> 32));

				/*
				 *  The R5000 manual says that rd should be all zeros,
				 *  but it isn't on R5900.   I'm just guessing here that
				 *  it stores the value in register rd, in addition to hi/lo.
				 *  TODO
				 */
				if (rd != 0)
					cpu->cd.mips.gpr[rd] = cpu->cd.mips.lo;
			}
		} else if ((instrword & 0xffff07ffULL) == 0x70000209
		    || (instrword & 0xffff07ffULL) == 0x70000249) {
			/*
			 *  This is just a guess for R5900, I've not found any docs on this one yet.
			 *
			 *	pmfhi/pmflo rd
			 *
			 *  If the lowest 8 bits of the instruction word are 0x09, it's a pmfhi.
			 *  If the lowest bits are 0x49, it's a pmflo.
			 *
			 *  A wild guess is that this is a 128-bit version of mfhi/mflo.
			 *  For now, this is implemented as 64-bit only.  (TODO)
			 */
			if (instr[0] == 0x49) {
				cpu->cd.mips.gpr[rd] = cpu->cd.mips.lo;
			} else {
				cpu->cd.mips.gpr[rd] = cpu->cd.mips.hi;
			}
		} else if ((instrword & 0xfc1fffff) == 0x70000269 || (instrword & 0xfc1fffff) == 0x70000229) {
			/*
			 *  This is just a guess for R5900, I've not found any docs on this one yet.
			 *
			 *	pmthi/pmtlo rs		(pmtlo = 269, pmthi = 229)
			 *
			 *  A wild guess is that this is a 128-bit version of mthi/mtlo.
			 *  For now, this is implemented as 64-bit only.  (TODO)
			 */
			if (instr[0] == 0x69) {
				cpu->cd.mips.lo = cpu->cd.mips.gpr[rs];
			} else {
				cpu->cd.mips.hi = cpu->cd.mips.gpr[rs];
			}
		} else if ((instrword & 0xfc0007ff) == 0x700004a9) {
			/*
			 *  This is just a guess for R5900, I've not found any docs on this one yet.
			 *
			 *	por dst,src,src2  ==> rs=src rt=src2 rd=dst
			 *
			 *  A wild guess is that this is a 128-bit "or" between two registers.
			 *  For now, let's just or using 64-bits.  (TODO)
			 */
			cpu->cd.mips.gpr[rd] = cpu->cd.mips.gpr[rs] | cpu->cd.mips.gpr[rt];
		} else if ((instrword & 0xfc0007ff) == 0x70000488) {
			/*
			 *  R5900 "undocumented" pextlw. TODO: find out if this is correct.
			 *  It seems that this instruction is used to combine two 32-bit
			 *  words into a 64-bit dword, typically before a sd (store dword).
			 */
			cpu->cd.mips.gpr[rd] =
			    ((cpu->cd.mips.gpr[rs] & 0xffffffffULL) << 32)		/*  TODO: switch rt and rs?  */
			    | (cpu->cd.mips.gpr[rt] & 0xffffffffULL);
		} else if (special6 == SPECIAL2_MUL) {
			cpu->cd.mips.gpr[rd] = (int64_t)cpu->cd.mips.gpr[rt] *
			    (int64_t)cpu->cd.mips.gpr[rs];
		} else if (special6 == SPECIAL2_CLZ) {
			/*  clz: count leading zeroes  */
			int i, n=0;
			for (i=31; i>=0; i--) {
				if (cpu->cd.mips.gpr[rs] & ((uint32_t)1 << i))
					break;
				else
					n++;
			}
			cpu->cd.mips.gpr[rd] = n;
		} else if (special6 == SPECIAL2_CLO) {
			/*  clo: count leading ones  */
			int i, n=0;
			for (i=31; i>=0; i--) {
				if (cpu->cd.mips.gpr[rs] & ((uint32_t)1 << i))
					n++;
				else
					break;
			}
			cpu->cd.mips.gpr[rd] = n;
		} else if (special6 == SPECIAL2_DCLZ) {
			/*  dclz: count leading zeroes  */
			int i, n=0;
			for (i=63; i>=0; i--) {
				if (cpu->cd.mips.gpr[rs] & ((uint64_t)1 << i))
					break;
				else
					n++;
			}
			cpu->cd.mips.gpr[rd] = n;
		} else if (special6 == SPECIAL2_DCLO) {
			/*  dclo: count leading ones  */
			int i, n=0;
			for (i=63; i>=0; i--) {
				if (cpu->cd.mips.gpr[rs] & ((uint64_t)1 << i))
					n++;
				else
					break;
			}
			cpu->cd.mips.gpr[rd] = n;
		} else {
			if (!instruction_trace_cached) {
				fatal("cpu%i @ %016llx: %02x%02x%02x%02x%s\t",
				    cpu->cpu_id, cpu->cd.mips.pc_last,
				    instr[3], instr[2], instr[1], instr[0], cpu_flags(cpu));
			}
			fatal("unimplemented special_2 = 0x%02x, rs=0x%02x rt=0x%02x rd=0x%02x\n",
			    special6, rs, rt, rd);
			cpu->running = 0;
			return 1;
		}
		return 1;
	default:
		if (!instruction_trace_cached) {
			fatal("cpu%i @ %016llx: %02x%02x%02x%02x%s\t",
			    cpu->cpu_id, cpu->cd.mips.pc_last,
			    instr[3], instr[2], instr[1], instr[0], cpu_flags(cpu));
		}
		fatal("unimplemented hi6 = 0x%02x\n", hi6);
		cpu->running = 0;
		return 1;
	}

	/*  NOTREACHED  */
}


#define CPU_RUN		mips_cpu_run
#define CPU_RUN_MIPS
#define CPU_RINSTR	mips_cpu_run_instr
#include "cpu_run.c"
#undef CPU_RINSTR
#undef CPU_RUN_MIPS
#undef CPU_RUN


/*
 *  mips_cpu_dumpinfo():
 */
void mips_cpu_dumpinfo(struct cpu *cpu)
{
	struct mips_cpu_type_def *ct = &cpu->cd.mips.cpu_type;

	debug(" (%i-bit ", (ct->isa_level < 3 ||
	    ct->isa_level == 32)? 32 : 64);

	debug("%s, ", cpu->byte_order == EMUL_BIG_ENDIAN? "BE" : "LE");

	debug("%i TLB entries", ct->nr_of_tlb_entries);

	if (ct->default_picache || ct->default_pdcache)
		debug(", I+D = %i+%i KB",
		    (1 << ct->default_picache) / 1024,
		    (1 << ct->default_pdcache) / 1024);

	if (ct->default_scache) {
		int kb = (1 << ct->default_scache) / 1024;
		debug(", L2 = %i %cB",
		    kb >= 1024? kb / 1024 : kb,
		    kb >= 1024? 'M' : 'K');
	}

	debug(")\n");
}


/*
 *  mips_cpu_list_available_types():
 *
 *  Print a list of available MIPS CPU types.
 */
void mips_cpu_list_available_types(void)
{
	int i, j;
	struct mips_cpu_type_def cpu_type_defs[] = MIPS_CPU_TYPE_DEFS;

	i = 0;
	while (cpu_type_defs[i].name != NULL) {
		debug("%s", cpu_type_defs[i].name);
		for (j=10 - strlen(cpu_type_defs[i].name); j>0; j--)
			debug(" ");
		i++;
		if ((i % 6) == 0 || cpu_type_defs[i].name == NULL)
			debug("\n");
	}
}


/*
 *  mips_cpu_family_init():
 *
 *  Fill in the cpu_family struct for MIPS.
 */
int mips_cpu_family_init(struct cpu_family *fp)
{
	fp->name = "MIPS";
	fp->cpu_new = mips_cpu_new;
	fp->list_available_types = mips_cpu_list_available_types;
	fp->register_match = mips_cpu_register_match;
	fp->disassemble_instr = mips_cpu_disassemble_instr;
	fp->register_dump = mips_cpu_register_dump;
	fp->run = mips_cpu_run;
	fp->dumpinfo = mips_cpu_dumpinfo;
	fp->show_full_statistics = mips_cpu_show_full_statistics;
	fp->tlbdump = mips_cpu_tlbdump;
	fp->interrupt = mips_cpu_interrupt;
	fp->interrupt_ack = mips_cpu_interrupt_ack;
	return 1;
}


#endif	/*  ENABLE_MIPS  */
