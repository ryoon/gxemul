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
 *  $Id: cpu_alpha.c,v 1.15 2005-07-18 22:00:06 debug Exp $
 *
 *  Alpha CPU emulation.
 *
 *  TODO: Many things.
 *
 *  See http://www.eecs.harvard.edu/~nr/toolkit/specs/alpha.html for info
 *  on instruction formats etc.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "misc.h"


#ifndef	ENABLE_ALPHA


#include "cpu_alpha.h"


/*
 *  alpha_cpu_family_init():
 *
 *  Bogus, when ENABLE_ALPHA isn't defined.
 */
int alpha_cpu_family_init(struct cpu_family *fp)
{
	return 0;
}


#else	/*  ENABLE_ALPHA  */


#include "cpu.h"
#include "cpu_alpha.h"
#include "machine.h"
#include "memory.h"
#include "symbol.h"


/*  instr uses the same names as in cpu_alpha_instr.c  */
#define instr(n) alpha_instr_ ## n

/*  Alpha symbolic register names:  */
static char *alpha_regname[N_ALPHA_REGS] = ALPHA_REG_NAMES; 


extern volatile int single_step;
extern int old_show_trace_tree;   
extern int old_instruction_trace;
extern int old_quiet_mode;
extern int quiet_mode;


/*
 *  alpha_cpu_new():
 *
 *  Create a new Alpha CPU object by filling in the CPU struct.
 *  Return 1 on success, 0 if cpu_type_name isn't a valid Alpha processor.
 */
int alpha_cpu_new(struct cpu *cpu, struct memory *mem,
	struct machine *machine, int cpu_id, char *cpu_type_name)
{
	int i;

	if (strcasecmp(cpu_type_name, "Alpha") != 0)
		return 0;

	cpu->memory_rw = alpha_memory_rw;
	cpu->update_translation_table = alpha_update_translation_table;
	cpu->invalidate_translation_caches_paddr =
	    alpha_invalidate_translation_caches_paddr;
	cpu->is_32bit = 0;

	/*  Only show name and caches etc for CPU nr 0:  */
	if (cpu_id == 0) {
		debug("%s", cpu->name);
		debug(" (host: %i MB code cache, %.2f MB addr"
		    " cache)", (int)(ALPHA_TRANSLATION_CACHE_SIZE/1048576),
		    (float)(sizeof(struct alpha_vph_page) *
		    ALPHA_MAX_VPH_TLB_ENTRIES / 1048576.0));
	}

	/*  Create the default virtual->physical->host translation:  */
	cpu->cd.alpha.vph_default_page = malloc(sizeof(struct alpha_vph_page));
	if (cpu->cd.alpha.vph_default_page == NULL) {
		fprintf(stderr, "out of memory in alpha_cpu_new()\n");
		exit(1);
	}
	memset(cpu->cd.alpha.vph_default_page, 0,
	    sizeof(struct alpha_vph_page));
	for (i=0; i<ALPHA_LEVEL0; i++)
		cpu->cd.alpha.vph_table0[i] = cpu->cd.alpha.vph_default_page;

	return 1;
}


/*
 *  alpha_cpu_dumpinfo():
 */
void alpha_cpu_dumpinfo(struct cpu *cpu)
{
	debug(" (host: %i MB code cache, %.2f MB addr"
	    " cache)", (int)(ALPHA_TRANSLATION_CACHE_SIZE/1048576),
	    (float)(sizeof(struct alpha_vph_page) * ALPHA_MAX_VPH_TLB_ENTRIES
	    / 1048576.0));
	debug("\n");
}


/*
 *  alpha_cpu_list_available_types():
 *
 *  Print a list of available Alpha CPU types.
 */
void alpha_cpu_list_available_types(void)
{
	/*  TODO  */

	debug("Alpha\n");
}


/*
 *  alpha_cpu_register_match():
 */
void alpha_cpu_register_match(struct machine *m, char *name,
	int writeflag, uint64_t *valuep, int *match_register)
{
	int i, cpunr = 0;

	/*  CPU number:  */

	/*  TODO  */

	if (strcasecmp(name, "pc") == 0) {
		if (writeflag) {
			m->cpus[cpunr]->pc = *valuep;
		} else
			*valuep = m->cpus[cpunr]->pc;
		*match_register = 1;
	}

	/*  Register names:  */
	for (i=0; i<N_ALPHA_REGS; i++) {
		if (strcasecmp(name, alpha_regname[i]) == 0) {
			if (writeflag)
				m->cpus[cpunr]->cd.alpha.r[i] = *valuep;
			else
				*valuep = m->cpus[cpunr]->cd.alpha.r[i];
			*match_register = 1;
		}
	}
}


/*
 *  alpha_cpu_register_dump():
 *  
 *  Dump cpu registers in a relatively readable format.
 *  
 *  gprs: set to non-zero to dump GPRs and some special-purpose registers.
 *  coprocs: set bit 0..3 to dump registers in coproc 0..3.
 */
void alpha_cpu_register_dump(struct cpu *cpu, int gprs, int coprocs)
{ 
	char *symbol;
	uint64_t offset;
	int i, x = cpu->cpu_id;

	if (gprs) {
		symbol = get_symbol_name(&cpu->machine->symbol_context,
		    cpu->pc, &offset);
		debug("cpu%i:\t pc = 0x%016llx", x, (long long)cpu->pc);
		debug("  <%s>\n", symbol != NULL? symbol : " no symbol ");
		for (i=0; i<N_ALPHA_REGS; i++) {
			int r = (i >> 1) + ((i & 1) << 4);
			if ((i % 2) == 0)
				debug("cpu%i:\t", x);
			if (r != ALPHA_ZERO)
				debug("%3s = 0x%016llx", alpha_regname[r],
				    (long long)cpu->cd.alpha.r[r]);
			debug((i % 2) == 1? "\n" : "   ");
		}
	}
}


/*
 *  alpha_print_imm16_disp():
 *
 *  Used internally by alpha_cpu_disassemble_instr().
 */
static void alpha_print_imm16_disp(int imm, int rb)
{
	imm = (int16_t)imm;

	if (imm < 0) {
		debug("-");
		imm = -imm;
	}
	if (imm <= 256)
		debug("%i", imm);
	else
		debug("0x%x", imm);
	if (rb != ALPHA_ZERO)
		debug("(%s)", alpha_regname[rb]);
}


/*
 *  alpha_cpu_disassemble_instr():
 *
 *  Convert an instruction word into human readable format, for instruction
 *  tracing.
 *              
 *  If running is 1, cpu->pc should be the address of the instruction.
 *
 *  If running is 0, things that depend on the runtime environment (eg.
 *  register contents) will not be shown, and addr will be used instead of
 *  cpu->pc for relative addresses.
 */                     
int alpha_cpu_disassemble_instr(struct cpu *cpu, unsigned char *ib,
        int running, uint64_t dumpaddr, int bintrans)
{
	uint32_t iw;
	char *symbol, *condition;
	uint64_t offset, tmp;
	int opcode, ra, rb, func, rc, imm;
	char *mnem = NULL;

	if (running)
		dumpaddr = cpu->pc;

	symbol = get_symbol_name(&cpu->machine->symbol_context,
	    dumpaddr, &offset);
	if (symbol != NULL && offset == 0)
		debug("<%s>\n", symbol);

	if (cpu->machine->ncpus > 1 && running)
		debug("cpu%i:\t", cpu->cpu_id);

	debug("%016llx:  ", (long long)dumpaddr);

	iw = ib[0] + (ib[1]<<8) + (ib[2]<<16) + (ib[3]<<24);
	debug("%08x\t", (int)iw);

	opcode = iw >> 26;
	ra = (iw >> 21) & 31;
	rb = (iw >> 16) & 31;
	func = (iw >> 5) & 0x7ff;
	rc = iw & 31;
	imm = iw & 0xffff;

	switch (opcode) {
	case 0x08:
	case 0x09:
		debug("lda%s\t%s,", opcode == 9? "h" : "", alpha_regname[ra]);
		alpha_print_imm16_disp(imm, rb);
		debug("\n");
		break;
	case 0x0a:
	case 0x0b:
	case 0x0c:
	case 0x0d:
	case 0x0e:
	case 0x28:
	case 0x29:
	case 0x2c:
	case 0x2d:
		switch (opcode) {
		case 0x0a: mnem = "ldbu"; break;
		case 0x0b: mnem = "ldq_u"; break;
		case 0x0c: mnem = "ldwu"; break;
		case 0x0d: mnem = "stw"; break;
		case 0x0e: mnem = "stb"; break;
		case 0x28: mnem = "ldl"; break;
		case 0x29: mnem = "ldq"; break;
		case 0x2c: mnem = "stl"; break;
		case 0x2d: mnem = "stq"; break;
		}
		if (opcode == 0x0b && ra == ALPHA_ZERO) {
			debug("unop");
		} else {
			debug("%s\t%s,", mnem, alpha_regname[ra]);
			alpha_print_imm16_disp(imm, rb);
		}
		debug("\n");
		break;
	case 0x10:
		switch (func & 0x7f) {
		case 0x00: mnem = "addl"; break;
		case 0x02: mnem = "s4addl"; break;
		case 0x09: mnem = "subl"; break;
		case 0x0b: mnem = "s4subl"; break;
		case 0x0f: mnem = "cmpbge"; break;
		case 0x12: mnem = "s8addl"; break;
		case 0x1b: mnem = "s8subl"; break;
		case 0x1d: mnem = "cmpult"; break;
		case 0x20: mnem = "addq"; break;
		case 0x22: mnem = "s4addq"; break;
		case 0x29: mnem = "subq"; break;
		case 0x2b: mnem = "s4subq"; break;
		case 0x2d: mnem = "cmpeq"; break;
		case 0x32: mnem = "s8addq"; break;
		case 0x3b: mnem = "s8subq"; break;
		case 0x3d: mnem = "cmpule"; break;
		case 0x4d: mnem = "cmplt"; break;
		case 0x6d: mnem = "cmple"; break;
		default:debug("UNIMPLEMENTED opcode 0x%x func 0x%x\n",
			    opcode, func);
		}
		if (mnem == NULL)
			break;
		if (func & 0x80)
			debug("%s\t%s,0x%x,%s\n", mnem,
			    alpha_regname[ra], (rb << 3) + (func >> 8),
			    alpha_regname[rc]);
		else
			debug("%s\t%s,%s,%s\n", mnem, alpha_regname[ra],
			    alpha_regname[rb], alpha_regname[rc]);
		break;
	case 0x11:
		switch (func & 0x7f) {
		case 0x000: mnem = "and"; break;
		case 0x020: mnem = "or"; break;
		case 0x040: mnem = "xor"; break;
		default:debug("UNIMPLEMENTED opcode 0x%x func 0x%x\n",
			    opcode, func);
		}
		if (mnem == NULL)
			break;
		/*  Special cases: "nop" etc:  */
		if (func == 0x020 && rc == ALPHA_ZERO)
			debug("nop\n");
		else if (func == 0x020 && ra == ALPHA_ZERO) {
			if (rb == ALPHA_ZERO)
				debug("clr\t%s\n", alpha_regname[rc]);
			else
				debug("mov\t%s,%s\n", alpha_regname[rb],
				    alpha_regname[rc]);
		} else if (func & 0x80)
			debug("%s\t%s,0x%x,%s\n", mnem,
			    alpha_regname[ra], (rb << 3) + (func >> 8),
			    alpha_regname[rc]);
		else
			debug("%s\t%s,%s,%s\n", mnem, alpha_regname[ra],
			    alpha_regname[rb], alpha_regname[rc]);
		break;
	case 0x12:
		switch (func & 0x7f) {
		case 0x030: mnem = "zap"; break;
		case 0x031: mnem = "zapnot"; break;
		case 0x039: mnem = "sll"; break;
		default:debug("UNIMPLEMENTED opcode 0x%x func 0x%x\n",
			    opcode, func);
		}
		if (mnem == NULL)
			break;
		if (func & 0x80)
			debug("%s\t%s,0x%x,%s\n", mnem,
			    alpha_regname[ra], (rb << 3) + (func >> 8),
			    alpha_regname[rc]);
		else
			debug("%s\t%s,%s,%s\n", mnem, alpha_regname[ra],
			    alpha_regname[rb], alpha_regname[rc]);
		break;
	case 0x1a:
		tmp = iw & 0x3fff;
		if (tmp & 0x2000)
			tmp |= 0xffffffffffffc000ULL;
		tmp <<= 2;
		tmp += dumpaddr + sizeof(uint32_t);
		switch ((iw >> 14) & 3) {
		case 0:
		case 1:	if (((iw >> 14) & 3) == 0)
				debug("jmp");
			else
				debug("jsr");
			debug("\t%s,", alpha_regname[ra]);
			debug("(%s),", alpha_regname[rb]);
			debug("0x%llx", (long long)tmp);
			symbol = get_symbol_name(&cpu->machine->symbol_context,
			    tmp, &offset);
			if (symbol != NULL)
				debug("\t<%s>", symbol);
			break;
		case 2:	debug("ret");
			break;
		default:fatal("unimpl JSR!");
		}
		debug("\n");
		break;
	case 0x30:
		tmp = iw & 0x1fffff;
		if (tmp & 0x100000)
			tmp |= 0xffffffffffe00000ULL;
		tmp <<= 2;
		tmp += dumpaddr + sizeof(uint32_t);
		debug("br\t");
		if (ra != ALPHA_ZERO)
			debug("%s,", alpha_regname[ra]);
		debug("0x%llx", (long long)tmp);
		symbol = get_symbol_name(&cpu->machine->symbol_context,
		    tmp, &offset);
		if (symbol != NULL)
			debug("\t<%s>", symbol);
		debug("\n");
		break;
	case 0x38:
	case 0x39:
	case 0x3a:
	case 0x3b:
	case 0x3c:
	case 0x3d:
	case 0x3e:
	case 0x3f:
		switch (opcode) {
		case 0x38: mnem = "blbc"; break;
		case 0x39: mnem = "beq"; break;
		case 0x3a: mnem = "blt"; break;
		case 0x3b: mnem = "ble"; break;
		case 0x3c: mnem = "blbs"; break;
		case 0x3d: mnem = "bne"; break;
		case 0x3e: mnem = "bge"; break;
		case 0x3f: mnem = "bgt"; break;
		}
		tmp = iw & 0x1fffff;
		if (tmp & 0x100000)
			tmp |= 0xffffffffffe00000ULL;
		tmp <<= 2;
		tmp += dumpaddr + sizeof(uint32_t);
		debug("%s\t%s,", mnem, alpha_regname[ra]);
		debug("0x%llx", (long long)tmp);
		symbol = get_symbol_name(&cpu->machine->symbol_context,
		    tmp, &offset);
		if (symbol != NULL)
			debug("\t<%s>", symbol);
		debug("\n");
		break;
	default:debug("UNIMPLEMENTED opcode 0x%x\n", opcode);
	}

	return sizeof(uint32_t);
}


/*
 *  alpha_create_or_reset_tc():
 *
 *  Create the translation cache in memory (ie allocate memory for it), if
 *  necessary, and then reset it to an initial state.
 */
static void alpha_create_or_reset_tc(struct cpu *cpu)
{
	if (cpu->cd.alpha.translation_cache == NULL) {
		cpu->cd.alpha.translation_cache = malloc(
		    ALPHA_TRANSLATION_CACHE_SIZE + 
		    ALPHA_TRANSLATION_CACHE_MARGIN);
		if (cpu->cd.alpha.translation_cache == NULL) {
			fprintf(stderr, "alpha_create_or_reset_tc(): out of "
			    "memory when allocating the translation cache\n");
			exit(1);
		}
	}

	/*  Create an empty table at the beginning of the translation cache:  */
	memset(cpu->cd.alpha.translation_cache, 0, sizeof(uint32_t) *
	    ALPHA_N_BASE_TABLE_ENTRIES);

	cpu->cd.alpha.translation_cache_cur_ofs =
	    ALPHA_N_BASE_TABLE_ENTRIES * sizeof(uint32_t);
}


/*
 *  alpha_tc_allocate_default_page():
 *
 *  Create a default page (with just pointers to instr(to_be_translated)
 *  at cpu->cd.alpha.translation_cache_cur_ofs.
 */
/*  forward declaration of to_be_translated and end_of_page:  */
static void instr(to_be_translated)(struct cpu *,struct alpha_instr_call *);
static void instr(end_of_page)(struct cpu *,struct alpha_instr_call *);
static void alpha_tc_allocate_default_page(struct cpu *cpu, uint32_t physaddr)
{
	struct alpha_tc_physpage *ppp;
	int i;

	/*  Create the physpage header:  */
	ppp = (struct alpha_tc_physpage *)(cpu->cd.alpha.translation_cache
	    + cpu->cd.alpha.translation_cache_cur_ofs);
	ppp->next_ofs = 0;
	ppp->physaddr = physaddr;

	for (i=0; i<ALPHA_IC_ENTRIES_PER_PAGE; i++)
		ppp->ics[i].f = instr(to_be_translated);

	ppp->ics[ALPHA_IC_ENTRIES_PER_PAGE].f = instr(end_of_page);

	cpu->cd.alpha.translation_cache_cur_ofs +=
	    sizeof(struct alpha_tc_physpage);
}


/*
 *  alpha_invalidate_tlb_entry():
 *
 *  Invalidate a translation entry (based on virtual address).
 */
void alpha_invalidate_tlb_entry(struct cpu *cpu, uint32_t vaddr_page)
{
	struct alpha_vph_page *vph_p;
	uint32_t a, b;

	a = (vaddr_page >> 26) & (ALPHA_LEVEL0 - 1);
	b = (vaddr_page >> 13) & (ALPHA_LEVEL1 - 1);
	vph_p = cpu->cd.alpha.vph_table0[a];

	if (vph_p == cpu->cd.alpha.vph_default_page) {
		fatal("alpha_invalidate_tlb_entry(): huh? Problem 1.\n");
		exit(1);
	}

	vph_p->host_load[b] = NULL;
	vph_p->host_store[b] = NULL;
	vph_p->phys_addr[b] = 0;
	vph_p->refcount --;
	if (vph_p->refcount < 0) {
		fatal("alpha_invalidate_tlb_entry(): huh? Problem 2.\n");
		exit(1);
	}
	if (vph_p->refcount == 0) {
		vph_p->next = cpu->cd.alpha.vph_next_free_page;
		cpu->cd.alpha.vph_next_free_page = vph_p;
		cpu->cd.alpha.vph_table0[a] = cpu->cd.alpha.vph_default_page;
	}
}


/*
 *  alpha_invalidate_translation_caches_paddr():
 *
 *  Invalidate all entries matching a specific physical address.
 */
void alpha_invalidate_translation_caches_paddr(struct cpu *cpu, uint64_t paddr)
{
	int r;
	uint64_t paddr_page = paddr & ~0x1fff;

	for (r=0; r<ALPHA_MAX_VPH_TLB_ENTRIES; r++) {
		if (cpu->cd.alpha.vph_tlb_entry[r].valid &&
		    cpu->cd.alpha.vph_tlb_entry[r].paddr_page == paddr_page) {
			alpha_invalidate_tlb_entry(cpu,
			    cpu->cd.alpha.vph_tlb_entry[r].vaddr_page);
			cpu->cd.alpha.vph_tlb_entry[r].valid = 0;
		}
	}
}


/*
 *  alpha_update_translation_table():
 *
 *  Update the memory translation tables.
 */
void alpha_update_translation_table(struct cpu *cpu, uint64_t vaddr_page,
	unsigned char *host_page, int writeflag, uint64_t paddr_page)
{
	uint32_t a, b;
	struct alpha_vph_page *vph_p;
	int found, r, lowest_index;
	int64_t lowest, highest = -1;

	/*  fatal("alpha_update_translation_table(): v=0x%llx, h=%p w=%i"
	    " p=0x%llx\n", (long long)vaddr_page, host_page, writeflag,
	    (long long)paddr_page);  */

	/*  Scan the current TLB entries:  */
	found = -1;
	lowest_index = 0; lowest = cpu->cd.alpha.vph_tlb_entry[0].timestamp;
	for (r=0; r<ALPHA_MAX_VPH_TLB_ENTRIES; r++) {
		if (cpu->cd.alpha.vph_tlb_entry[r].timestamp < lowest) {
			lowest = cpu->cd.alpha.vph_tlb_entry[r].timestamp;
			lowest_index = r;
		}
		if (cpu->cd.alpha.vph_tlb_entry[r].timestamp > highest)
			highest = cpu->cd.alpha.vph_tlb_entry[r].timestamp;
		if (cpu->cd.alpha.vph_tlb_entry[r].valid &&
		    cpu->cd.alpha.vph_tlb_entry[r].vaddr_page == vaddr_page) {
			found = r;
			break;
		}
	}

	if (found < 0) {
		/*  Create the new TLB entry, overwriting the oldest one:  */
		r = lowest_index;
		if (cpu->cd.alpha.vph_tlb_entry[r].valid) {
			/*  This one has to be invalidated first:  */
			uint64_t addr = cpu->cd.alpha.
			    vph_tlb_entry[r].vaddr_page;
			a = (addr >> 26) & (ALPHA_LEVEL0 - 1);
			b = (addr >> 13) & (ALPHA_LEVEL1 - 1);
			vph_p = cpu->cd.alpha.vph_table0[a];
			vph_p->host_load[b] = NULL;
			vph_p->host_store[b] = NULL;
			vph_p->phys_addr[b] = 0;
			vph_p->refcount --;
			if (vph_p->refcount == 0) {
				vph_p->next = cpu->cd.alpha.vph_next_free_page;
				cpu->cd.alpha.vph_next_free_page = vph_p;
				cpu->cd.alpha.vph_table0[a] =
				    cpu->cd.alpha.vph_default_page;
			}
		}

		cpu->cd.alpha.vph_tlb_entry[r].valid = 1;
		cpu->cd.alpha.vph_tlb_entry[r].host_page = host_page;
		cpu->cd.alpha.vph_tlb_entry[r].paddr_page = paddr_page;
		cpu->cd.alpha.vph_tlb_entry[r].vaddr_page = vaddr_page;
		cpu->cd.alpha.vph_tlb_entry[r].writeflag = writeflag;
		cpu->cd.alpha.vph_tlb_entry[r].timestamp = highest + 1;

		/*  Add the new translation to the table:  */
		a = (vaddr_page >> 26) & (ALPHA_LEVEL0 - 1);
		b = (vaddr_page >> 13) & (ALPHA_LEVEL1 - 1);
		vph_p = cpu->cd.alpha.vph_table0[a];
		if (vph_p == cpu->cd.alpha.vph_default_page) {
			if (cpu->cd.alpha.vph_next_free_page != NULL) {
				vph_p = cpu->cd.alpha.vph_table0[a] =
				    cpu->cd.alpha.vph_next_free_page;
				cpu->cd.alpha.vph_next_free_page = vph_p->next;
			} else {
				vph_p = cpu->cd.alpha.vph_table0[a] =
				    malloc(sizeof(struct alpha_vph_page));
				memset(vph_p, 0, sizeof(struct alpha_vph_page));
			}
		}
		vph_p->refcount ++;
		vph_p->host_load[b] = host_page;
		vph_p->host_store[b] = writeflag? host_page : NULL;
		vph_p->phys_addr[b] = paddr_page;
	} else {
		/*
		 *  The translation was already in the TLB.
		 *	Writeflag = 0:  Do nothing.
		 *	Writeflag = 1:  Make sure the page is writable.
		 *	Writeflag = -1: Downgrade to readonly.
		 */
		a = (vaddr_page >> 26) & (ALPHA_LEVEL0 - 1);
		b = (vaddr_page >> 13) & (ALPHA_LEVEL1 - 1);
		vph_p = cpu->cd.alpha.vph_table0[a];
		cpu->cd.alpha.vph_tlb_entry[found].timestamp = highest + 1;
		if (vph_p->phys_addr[b] == paddr_page) {
			if (writeflag == 1)
				vph_p->host_store[b] = host_page;
			if (writeflag == -1)
				vph_p->host_store[b] = NULL;
		} else {
			/*  Change the entire physical/host mapping:  */
			vph_p->host_load[b] = host_page;
			vph_p->host_store[b] = writeflag? host_page : NULL;
			vph_p->phys_addr[b] = paddr_page;
		}
	}
}


#define MEMORY_RW	alpha_memory_rw
#define MEM_ALPHA
#include "memory_rw.c"
#undef MEM_ALPHA
#undef MEMORY_RW


/*
 *  alpha_pc_to_pointers():
 *
 *  This function uses the current program counter (a virtual address) to
 *  find out which physical translation page to use, and then sets the current
 *  translation page pointers to that page.
 *
 *  If there was no translation page for that physical page, then an empty
 *  one is created.
 */
void alpha_pc_to_pointers(struct cpu *cpu)
{
	uint64_t cached_pc, physaddr;
	uint32_t physpage_ofs;
	uint64_t pagenr, table_index;
	uint32_t *physpage_entryp;
	struct alpha_tc_physpage *ppp;

	cached_pc = cpu->pc;

	/*
	 *  TODO: virtual to physical address translation
	 */

	physaddr = cached_pc & ~(((ALPHA_IC_ENTRIES_PER_PAGE-1) << 2) | 3);

	if (cpu->cd.alpha.translation_cache == NULL || cpu->cd.
	    alpha.translation_cache_cur_ofs >= ALPHA_TRANSLATION_CACHE_SIZE)
		alpha_create_or_reset_tc(cpu);

	pagenr = ALPHA_ADDR_TO_PAGENR(physaddr);
	table_index = ALPHA_PAGENR_TO_TABLE_INDEX(pagenr);

	physpage_entryp = &(((uint32_t *)
	    cpu->cd.alpha.translation_cache)[table_index]);
	physpage_ofs = *physpage_entryp;
	ppp = NULL;

	/*  Traverse the physical page chain:  */
	while (physpage_ofs != 0) {
		ppp = (struct alpha_tc_physpage *)
		    (cpu->cd.alpha.translation_cache + physpage_ofs);
		/*  If we found the page in the cache, then we're done:  */
		if (ppp->physaddr == physaddr)
			break;
		/*  Try the next page in the chain:  */
		physpage_ofs = ppp->next_ofs;
	}

	/*  If the offset is 0 (or ppp is NULL), then we need to create a
	    new "default" empty translation page.  */

	if (ppp == NULL) {
		fatal("CREATING page %i (physaddr 0x%016llx), table "
		    "index = %i\n", pagenr, (uint64_t)physaddr, table_index);
		*physpage_entryp = physpage_ofs =
		    cpu->cd.alpha.translation_cache_cur_ofs;

		alpha_tc_allocate_default_page(cpu, physaddr);

		ppp = (struct alpha_tc_physpage *)
		    (cpu->cd.alpha.translation_cache + physpage_ofs);
	}

	cpu->cd.alpha.cur_physpage = ppp;
	cpu->cd.alpha.cur_ic_page = &ppp->ics[0];
	cpu->cd.alpha.next_ic = cpu->cd.alpha.cur_ic_page +
	    ALPHA_PC_TO_IC_ENTRY(cached_pc);

	/*  printf("cached_pc = 0x%08x  pagenr = %i  table_index = %i, "
	    "physpage_ofs = 0x%08x\n", (int)cached_pc, (int)pagenr,
	    (int)table_index, (int)physpage_ofs);  */
}


#include "cpu_alpha_instr.c"


/*
 *  alpha_cpu_run_instr():
 *
 *  Execute one or more instructions on a specific CPU.
 *
 *  Return value is the number of instructions executed during this call,
 *  0 if no instructions were executed.
 */
int alpha_cpu_run_instr(struct emul *emul, struct cpu *cpu)
{
	uint32_t cached_pc;
	ssize_t low_pc;
	int n_instrs;

	alpha_pc_to_pointers(cpu);

	cached_pc = cpu->pc;
	cpu->cd.alpha.n_translated_instrs = 0;
	cpu->cd.alpha.running_translated = 1;

	if (single_step || cpu->machine->instruction_trace) {
		/*
		 *  Single-step:
		 */
		struct alpha_instr_call *ic = cpu->cd.alpha.next_ic ++;
		if (cpu->machine->instruction_trace) {
			unsigned char instr[4];
			if (!cpu->memory_rw(cpu, cpu->mem, cached_pc, &instr[0],
			    sizeof(instr), MEM_READ, CACHE_INSTRUCTION)) {
				fatal("alpha_cpu_run_instr(): could not read "
				    "the instruction\n");
			} else
				alpha_cpu_disassemble_instr(cpu, instr, 1,0,0);
		}

		/*  When single-stepping, multiple instruction calls cannot
		    be combined into one. This clears all translations:  */
		if (cpu->cd.alpha.cur_physpage->flags & COMBINATIONS) {
			int i;
			for (i=0; i<ALPHA_IC_ENTRIES_PER_PAGE; i++)
				cpu->cd.alpha.cur_physpage->ics[i].f =
				    instr(to_be_translated);
			fatal("[ Note: The translation of physical page 0x%08x"
			    " contained combinations of instructions; these "
			    "are now flushed because we are single-stepping."
			    " ]\n", cpu->cd.alpha.cur_physpage->physaddr);
			cpu->cd.alpha.cur_physpage->flags &=~COMBINATIONS;
			cpu->cd.alpha.cur_physpage->flags &=~TRANSLATIONS;
		}

		/*  Execute just one instruction:  */
		ic->f(cpu, ic);
		n_instrs = 1;
	} else {
		/*  Execute multiple instructions:  */
		n_instrs = 0;
		for (;;) {
			struct alpha_instr_call *ic;
			ic = cpu->cd.alpha.next_ic ++; ic->f(cpu, ic);
			ic = cpu->cd.alpha.next_ic ++; ic->f(cpu, ic);
			ic = cpu->cd.alpha.next_ic ++; ic->f(cpu, ic);
			ic = cpu->cd.alpha.next_ic ++; ic->f(cpu, ic);
			ic = cpu->cd.alpha.next_ic ++; ic->f(cpu, ic);
			ic = cpu->cd.alpha.next_ic ++; ic->f(cpu, ic);
			ic = cpu->cd.alpha.next_ic ++; ic->f(cpu, ic);
			ic = cpu->cd.alpha.next_ic ++; ic->f(cpu, ic);

			ic = cpu->cd.alpha.next_ic ++; ic->f(cpu, ic);
			ic = cpu->cd.alpha.next_ic ++; ic->f(cpu, ic);
			ic = cpu->cd.alpha.next_ic ++; ic->f(cpu, ic);
			ic = cpu->cd.alpha.next_ic ++; ic->f(cpu, ic);
			ic = cpu->cd.alpha.next_ic ++; ic->f(cpu, ic);
			ic = cpu->cd.alpha.next_ic ++; ic->f(cpu, ic);
			ic = cpu->cd.alpha.next_ic ++; ic->f(cpu, ic);
			ic = cpu->cd.alpha.next_ic ++; ic->f(cpu, ic);

			ic = cpu->cd.alpha.next_ic ++; ic->f(cpu, ic);
			ic = cpu->cd.alpha.next_ic ++; ic->f(cpu, ic);
			ic = cpu->cd.alpha.next_ic ++; ic->f(cpu, ic);
			ic = cpu->cd.alpha.next_ic ++; ic->f(cpu, ic);
			ic = cpu->cd.alpha.next_ic ++; ic->f(cpu, ic);
			ic = cpu->cd.alpha.next_ic ++; ic->f(cpu, ic);
			ic = cpu->cd.alpha.next_ic ++; ic->f(cpu, ic);
			ic = cpu->cd.alpha.next_ic ++; ic->f(cpu, ic);

			ic = cpu->cd.alpha.next_ic ++; ic->f(cpu, ic);
			ic = cpu->cd.alpha.next_ic ++; ic->f(cpu, ic);
			ic = cpu->cd.alpha.next_ic ++; ic->f(cpu, ic);
			ic = cpu->cd.alpha.next_ic ++; ic->f(cpu, ic);
			ic = cpu->cd.alpha.next_ic ++; ic->f(cpu, ic);
			ic = cpu->cd.alpha.next_ic ++; ic->f(cpu, ic);
			ic = cpu->cd.alpha.next_ic ++; ic->f(cpu, ic);
			ic = cpu->cd.alpha.next_ic ++; ic->f(cpu, ic);

			n_instrs += 32;
			if (!cpu->cd.alpha.running_translated || single_step ||
			    n_instrs+cpu->cd.alpha.n_translated_instrs >= 16384)
				break;
		}
	}


	/*
	 *  Update the program counter and return the correct number of
	 *  executed instructions:
	 */
	low_pc = ((size_t)cpu->cd.alpha.next_ic - (size_t)
	    cpu->cd.alpha.cur_ic_page) / sizeof(struct alpha_instr_call);

	if (low_pc >= 0 && low_pc < ALPHA_IC_ENTRIES_PER_PAGE) {
		cpu->pc &= ~((ALPHA_IC_ENTRIES_PER_PAGE-1) << 2);
		cpu->pc += (low_pc << 2);
	} else if (low_pc == ALPHA_IC_ENTRIES_PER_PAGE) {
		/*  Switch to next page:  */
		cpu->pc &= ~((ALPHA_IC_ENTRIES_PER_PAGE-1) << 2);
		cpu->pc += (ALPHA_IC_ENTRIES_PER_PAGE << 2);
	} else {
		fatal("Outside a page (This is actually ok)\n");
	}

	return n_instrs + cpu->cd.alpha.n_translated_instrs;
}


#define CPU_RUN         alpha_cpu_run
#define CPU_RINSTR      alpha_cpu_run_instr
#define CPU_RUN_ALPHA
#include "cpu_run.c"
#undef CPU_RINSTR
#undef CPU_RUN_ALPHA
#undef CPU_RUN


/*
 *  alpha_cpu_family_init():
 *
 *  This function fills the cpu_family struct with valid data.
 */
int alpha_cpu_family_init(struct cpu_family *fp)
{
	fp->name = "Alpha";
	fp->cpu_new = alpha_cpu_new;
	fp->list_available_types = alpha_cpu_list_available_types;
	fp->register_match = alpha_cpu_register_match;
	fp->disassemble_instr = alpha_cpu_disassemble_instr;
	fp->register_dump = alpha_cpu_register_dump;
	fp->run = alpha_cpu_run;
	fp->dumpinfo = alpha_cpu_dumpinfo;
	/*  fp->show_full_statistics = alpha_cpu_show_full_statistics;  */
	/*  fp->tlbdump = alpha_cpu_tlbdump;  */
	/*  fp->interrupt = alpha_cpu_interrupt;  */
	/*  fp->interrupt_ack = alpha_cpu_interrupt_ack;  */
	return 1;
}

#endif	/*  ENABLE_ALPHA  */
