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
 *  $Id: cpu_arm.c,v 1.41 2005-07-28 13:29:36 debug Exp $
 *
 *  ARM CPU emulation.
 *
 *  Sources of information refered to in cpu_arm*.c:
 *
 *	(1)  http://www.pinknoise.demon.co.uk/ARMinstrs/ARMinstrs.html
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "misc.h"


#ifndef	ENABLE_ARM


#include "cpu_arm.h"


/*
 *  arm_cpu_family_init():
 *
 *  Bogus, when ENABLE_ARM isn't defined.
 */
int arm_cpu_family_init(struct cpu_family *fp)
{
	return 0;
}


#else	/*  ENABLE_ARM  */


#include "cpu.h"
#include "cpu_arm.h"
#include "machine.h"
#include "memory.h"
#include "symbol.h"


/*  instr uses the same names as in cpu_arm_instr.c  */
#define instr(n) arm_instr_ ## n

static char *arm_condition_string[16] = {
	"eq", "ne", "cs", "cc", "mi", "pl", "vs", "vc",
	"hi", "ls", "ge", "lt", "gt", "le", ""/*Always*/, "(INVALID)" };

/*  ARM symbolic register names:  */
static char *arm_regname[N_ARM_REGS] = ARM_REG_NAMES;

/* Data processing instructions:  */
static char *arm_dpiname[16] = {
	"and", "eor", "sub", "rsb", "add", "adc", "sbc", "rsc",
	"tst", "teq", "cmp", "cmn", "orr", "mov", "bic", "mvn" };
static int arm_dpi_uses_d[16] = { 
	1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1 };
static int arm_dpi_uses_n[16] = { 
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0 };

extern volatile int single_step;
extern int old_show_trace_tree;   
extern int old_instruction_trace;
extern int old_quiet_mode;
extern int quiet_mode;


/*
 *  arm_cpu_new():
 *
 *  Create a new ARM cpu object by filling the CPU struct.
 *  Return 1 on success, 0 if cpu_type_name isn't a valid ARM processor.
 */
int arm_cpu_new(struct cpu *cpu, struct memory *mem,
	struct machine *machine, int cpu_id, char *cpu_type_name)
{
	if (strcmp(cpu_type_name, "ARM") != 0)
		return 0;

	cpu->memory_rw = arm_memory_rw;
	cpu->update_translation_table = arm_update_translation_table;
	cpu->invalidate_translation_caches_paddr =
	    arm_invalidate_translation_caches_paddr;
	cpu->is_32bit = 1;

	memset(&cpu->cd.arm, 0, sizeof(struct arm_cpu));

	cpu->cd.arm.flags = ARM_FLAG_I | ARM_FLAG_F | ARM_MODE_USR32;

	/*  Only show name and caches etc for CPU nr 0:  */
	if (cpu_id == 0) {
		debug("%s", cpu->name);
		debug(" (host: %i MB code cache, %i MB addr"
		    " cache)", (int)(ARM_TRANSLATION_CACHE_SIZE/1048576),
		    (int)(ARM_N_VPH_ENTRIES * (sizeof(unsigned char *) * 2
		    + sizeof(uint32_t))) / 1048576);
	}

	return 1;
}


/*
 *  arm_cpu_dumpinfo():
 */
void arm_cpu_dumpinfo(struct cpu *cpu)
{
	debug(" (host: %i MB code cache, %i MB addr"
	    " cache)", (int)(ARM_TRANSLATION_CACHE_SIZE/1048576),
	    (int)(ARM_N_VPH_ENTRIES * (sizeof(unsigned char *) * 2
	    + sizeof(uint32_t))) / 1048576);
	debug("\n");
}


/*
 *  arm_cpu_list_available_types():
 *
 *  Print a list of available ARM CPU types.
 */
void arm_cpu_list_available_types(void)
{
	/*  TODO  */

	debug("ARM\n");
}


/*
 *  arm_cpu_register_match():
 */
void arm_cpu_register_match(struct machine *m, char *name,
	int writeflag, uint64_t *valuep, int *match_register)
{
	int i, cpunr = 0;

	/*  CPU number:  */

	/*  TODO  */

	/*  Register names:  */
	for (i=0; i<N_ARM_REGS; i++) {
		if (strcasecmp(name, arm_regname[i]) == 0) {
			if (writeflag) {
				m->cpus[cpunr]->cd.arm.r[i] = *valuep;
				if (i == ARM_PC)
					m->cpus[cpunr]->pc = *valuep;
			} else
				*valuep = m->cpus[cpunr]->cd.arm.r[i];
			*match_register = 1;
		}
	}
}


/*
 *  arm_cpu_register_dump():
 *
 *  Dump cpu registers in a relatively readable format.
 *  
 *  gprs: set to non-zero to dump GPRs and some special-purpose registers.
 *  coprocs: set bit 0..3 to dump registers in coproc 0..3.
 */
void arm_cpu_register_dump(struct cpu *cpu, int gprs, int coprocs)
{
	char *symbol;
	uint64_t offset;
	int mode = cpu->cd.arm.flags & ARM_FLAG_MODE;
	int i, x = cpu->cpu_id;

	if (gprs) {
		symbol = get_symbol_name(&cpu->machine->symbol_context,
		    cpu->cd.arm.r[ARM_PC], &offset);
		debug("cpu%i:  flags = ", x);
		debug("%s%s%s%s%s%s",
		    (cpu->cd.arm.flags & ARM_FLAG_N)? "N" : "n",
		    (cpu->cd.arm.flags & ARM_FLAG_Z)? "Z" : "z",
		    (cpu->cd.arm.flags & ARM_FLAG_C)? "C" : "c",
		    (cpu->cd.arm.flags & ARM_FLAG_V)? "V" : "v",
		    (cpu->cd.arm.flags & ARM_FLAG_I)? "I" : "i",
		    (cpu->cd.arm.flags & ARM_FLAG_F)? "F" : "f");
		if (mode < ARM_MODE_USR32)
			debug("   pc =  0x%07x",
			    (int)(cpu->cd.arm.r[ARM_PC] & 0x03ffffff));
		else
			debug("   pc = 0x%08x", (int)cpu->cd.arm.r[ARM_PC]);

		/*  TODO: Flags  */

		debug("  <%s>\n", symbol != NULL? symbol : " no symbol ");

		for (i=0; i<N_ARM_REGS; i++) {
			if ((i % 4) == 0)
				debug("cpu%i:", x);
			if (i != ARM_PC)
				debug("  %s = 0x%08x", arm_regname[i],
				    (int)cpu->cd.arm.r[i]);
			if ((i % 4) == 3)
				debug("\n");
		}
	}
}


/*
 *  arm_cpu_disassemble_instr():
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
int arm_cpu_disassemble_instr(struct cpu *cpu, unsigned char *ib,
        int running, uint64_t dumpaddr, int bintrans)
{
	uint32_t iw, tmp;
	int main_opcode, secondary_opcode, s_bit, r16, r12, r8;
	int i, n, p_bit, u_bit, b_bit, w_bit, l_bit;
	char *symbol, *condition;
	uint64_t offset;

	if (running)
		dumpaddr = cpu->pc;

	symbol = get_symbol_name(&cpu->machine->symbol_context,
	    dumpaddr, &offset);
	if (symbol != NULL && offset == 0)
		debug("<%s>\n", symbol);

	if (cpu->machine->ncpus > 1 && running)
		debug("cpu%i:\t", cpu->cpu_id);

	debug("%08x:  ", (int)dumpaddr);

	if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
		iw = ib[0] + (ib[1]<<8) + (ib[2]<<16) + (ib[3]<<24);
	else
		iw = ib[3] + (ib[2]<<8) + (ib[1]<<16) + (ib[0]<<24);
	debug("%08x\t", (int)iw);

	condition = arm_condition_string[iw >> 28];
	main_opcode = (iw >> 24) & 15;
	secondary_opcode = (iw >> 21) & 15;
	u_bit = (iw >> 23) & 1;
	b_bit = (iw >> 22) & 1;
	w_bit = (iw >> 21) & 1;
	s_bit = l_bit = (iw >> 20) & 1;
	r16 = (iw >> 16) & 15;
	r12 = (iw >> 12) & 15;
	r8 = (iw >> 8) & 15;

	switch (main_opcode) {
	case 0x0:
	case 0x1:
	case 0x2:
	case 0x3:
		/*
		 *  See (1):
		 *  xxxx000a aaaSnnnn ddddcccc ctttmmmm  Register form
		 *  xxxx001a aaaSnnnn ddddrrrr bbbbbbbb  Immediate form
		 */
		if (iw & 0x80 && !(main_opcode & 2)) {
			debug("UNIMPLEMENTED reg (c!=0)\n");
			break;
		}

		debug("%s%s%s\t", arm_dpiname[secondary_opcode],
		    condition, s_bit? "s" : "");
		if (arm_dpi_uses_d[secondary_opcode])
			debug("%s,", arm_regname[r12]);
		if (arm_dpi_uses_n[secondary_opcode])
			debug("%s,", arm_regname[r16]);

		if (main_opcode & 2) {
			/*  Immediate form:  */
			int r = (iw >> 7) & 30;
			uint32_t b = iw & 0xff;
			while (r-- > 0)
				b = (b >> 1) | ((b & 1) << 31);
			if (b < 15)
				debug("#%i", b);
			else
				debug("#0x%x", b);
		} else {
			/*  Register form:  */
			int t = (iw >> 4) & 7;
			int c = (iw >> 7) & 31;
			debug("%s", arm_regname[iw & 15]);
			switch (t) {
			case 0:	if (c != 0)
					debug(" LSL #%i", c);
				break;
			case 1:	debug(" LSL %s", arm_regname[c >> 1]);
				break;
			case 2:	debug(" LSR #%i", c? c : 32);
				break;
			case 3:	debug(" LSR %s", arm_regname[c >> 1]);
				break;
			case 4:	debug(" ASR #%i", c? c : 32);
				break;
			case 5:	debug(" ASR %s", arm_regname[c >> 1]);
				break;
			case 6:	if (c != 0)
					debug(" ROR #%i", c);
				else
					debug(" RRX");
				break;
			case 7:	debug(" ROR %s", arm_regname[c >> 1]);
				break;
			}
		}
		debug("\n");
		break;
	case 0x4:				/*  Single Data Transfer  */
	case 0x5:
	case 0x6:
	case 0x7:
		/*
		 *  See (1):
		 *  xxxx010P UBWLnnnn ddddoooo oooooooo  Immediate form
		 *  xxxx011P UBWLnnnn ddddcccc ctt0mmmm  Register form
		 */
		p_bit = main_opcode & 1;
		if (main_opcode >= 6 && iw & 0x10) {
			debug("TODO: single data transf. but 0x10\n");
			break;
		}
		debug("%s%s%s", l_bit? "ldr" : "str",
		    condition, b_bit? "b" : "");
		if (!p_bit && w_bit)
			debug("t");
		debug("\t%s,[%s", arm_regname[r12], arm_regname[r16]);
		if (main_opcode < 6) {
			/*  Immediate form:  */
			uint32_t imm = iw & 0xfff;
			if (!p_bit)
				debug("]");
			if (imm != 0)
				debug(",#%s%i", u_bit? "" : "-", imm);
			if (p_bit)
				debug("]");
		} else {
			debug(" TODO: REG-form]");
		}
		debug("%s\n", (p_bit && w_bit)? "!" : "");
		break;
	case 0x8:				/*  Block Data Transfer  */
	case 0x9:
		/*  See (1):  xxxx100P USWLnnnn llllllll llllllll  */
		p_bit = main_opcode & 1;
		s_bit = b_bit;
		debug("%s%s", l_bit? "ldm" : "stm", condition);
		switch (u_bit * 2 + p_bit) {
		case 0:	debug("da"); break;
		case 1:	debug("db"); break;
		case 2:	debug("ia"); break;
		case 3:	debug("ib"); break;
		}
		debug("\t%s", arm_regname[r16]);
		if (w_bit)
			debug("!");
		debug(",{");
		n = 0;
		for (i=0; i<16; i++)
			if ((iw >> i) & 1) {
				debug("%s%s", (n > 0)? ",":"", arm_regname[i]);
				n++;
			}
		debug("}");
		if (s_bit)
			debug("^");
		debug("\n");
		break;
	case 0xa:				/*  B: branch  */
	case 0xb:				/*  BL: branch and link  */
		debug("b%s%s\t", main_opcode == 0xa? "" : "l", condition);
		tmp = (iw & 0x00ffffff) << 2;
		if (tmp & 0x02000000)
			tmp |= 0xfc000000;
		tmp = (int32_t)(dumpaddr + tmp + 8);
		debug("0x%x", (int)tmp);
		symbol = get_symbol_name(&cpu->machine->symbol_context,
		    tmp, &offset);
		if (symbol != NULL)
			debug("\t\t<%s>", symbol);
		debug("\n");
		break;
	case 0xc:				/*  Coprocessor  */
	case 0xd:				/*  LDC/STC  */
		/*  xxxx110P UNWLnnnn DDDDpppp oooooooo LDC/STC  */
		debug("TODO: coprocessor LDC/STC\n");
		break;
	case 0xe:				/*  CDP (Coprocessor Op)  */
		/*				    or MRC/MCR!
		 *  According to (1):
		 *  xxxx1110 oooonnnn ddddpppp qqq0mmmm		CDP
		 *  xxxx1110 oooLNNNN ddddpppp qqq1MMMM		MRC/MCR
		 */
		if (iw & 0x10) {
			debug("%s%s\t",
			    (iw & 0x00100000)? "mrc" : "mcr", condition);
			debug("%i,%i,r%i,cr%i,cr%i,%i",
			    (int)((iw >> 8) & 15), (int)((iw >>21) & 7),
			    (int)((iw >>12) & 15), (int)((iw >>16) & 15),
			    (int)((iw >> 0) & 15), (int)((iw >> 5) & 7));
		} else {
			debug("cdp%s\t", condition);
			debug("%i,%i,cr%i,cr%i,cr%i",
			    (int)((iw >> 8) & 15),
			    (int)((iw >>20) & 15),
			    (int)((iw >>12) & 15),
			    (int)((iw >>16) & 15),
			    (int)((iw >> 0) & 15));
			if ((iw >> 5) & 7)
				debug(",0x%x", (int)((iw >> 5) & 7));
		}
		debug("\n");
		break;
	case 0xf:				/*  SWI  */
		debug("swi%s\t", condition);
		debug("0x%x\n", (int)(iw & 0x00ffffff));
		break;
	default:debug("UNIMPLEMENTED\n");
	}

	return sizeof(uint32_t);
}


/*
 *  arm_create_or_reset_tc():
 *
 *  Create the translation cache in memory (ie allocate memory for it), if
 *  necessary, and then reset it to an initial state.
 */
static void arm_create_or_reset_tc(struct cpu *cpu)
{
	if (cpu->translation_cache == NULL) {
		cpu->translation_cache = malloc(
		    ARM_TRANSLATION_CACHE_SIZE + ARM_TRANSLATION_CACHE_MARGIN);
		if (cpu->translation_cache == NULL) {
			fprintf(stderr, "arm_create_or_reset_tc(): out of "
			    "memory when allocating the translation cache\n");
			exit(1);
		}
	}

	/*  Create an empty table at the beginning of the translation cache:  */
	memset(cpu->translation_cache, 0, sizeof(uint32_t) *
	    ARM_N_BASE_TABLE_ENTRIES);

	cpu->translation_cache_cur_ofs =
	    ARM_N_BASE_TABLE_ENTRIES * sizeof(uint32_t);
}


/*
 *  arm_tc_allocate_default_page():
 *
 *  Create a default page (with just pointers to instr(to_be_translated)
 *  at cpu->translation_cache_cur_ofs.
 */
/*  forward declaration of to_be_translated and end_of_page:  */
static void instr(to_be_translated)(struct cpu *,struct arm_instr_call *);
static void instr(end_of_page)(struct cpu *,struct arm_instr_call *);
static void arm_tc_allocate_default_page(struct cpu *cpu, uint32_t physaddr)
{
	struct arm_tc_physpage *ppp;
	int i;

	/*  Create the physpage header:  */
	ppp = (struct arm_tc_physpage *)(cpu->translation_cache
	    + cpu->translation_cache_cur_ofs);
	ppp->next_ofs = 0;
	ppp->physaddr = physaddr;

	/*  TODO: Is this faster than copying an entire template page?  */

	for (i=0; i<ARM_IC_ENTRIES_PER_PAGE; i++)
		ppp->ics[i].f = instr(to_be_translated);

	ppp->ics[ARM_IC_ENTRIES_PER_PAGE].f = instr(end_of_page);

	cpu->translation_cache_cur_ofs += sizeof(struct arm_tc_physpage);
}


/*
 *  arm_invalidate_tlb_entry():
 *
 *  Invalidate a translation entry (based on virtual address).
 */
void arm_invalidate_tlb_entry(struct cpu *cpu, uint32_t vaddr_page)
{
	uint32_t index = vaddr_page >> 12;
	cpu->cd.arm.host_load[index] = NULL;
	cpu->cd.arm.host_store[index] = NULL;
	cpu->cd.arm.phys_addr[index] = 0;
}


/*
 *  arm_invalidate_translation_caches_paddr():
 *
 *  Invalidate all entries matching a specific physical address.
 */
void arm_invalidate_translation_caches_paddr(struct cpu *cpu, uint64_t paddr)
{
	int r;
	uint32_t paddr_page = paddr & 0xfffff000;

	for (r=0; r<ARM_MAX_VPH_TLB_ENTRIES; r++) {
		if (cpu->cd.arm.vph_tlb_entry[r].valid &&
		    cpu->cd.arm.vph_tlb_entry[r].paddr_page == paddr_page) {
			arm_invalidate_tlb_entry(cpu,
			    cpu->cd.arm.vph_tlb_entry[r].vaddr_page);
			cpu->cd.arm.vph_tlb_entry[r].valid = 0;
		}
	}
}


/*
 *  arm_update_translation_table():
 *
 *  Update the memory translation tables.
 */
void arm_update_translation_table(struct cpu *cpu, uint64_t vaddr_page,
	unsigned char *host_page, int writeflag, uint64_t paddr_page)
{
	uint32_t index;
	int found, r, lowest_index;
	int64_t lowest, highest = -1;

	/*  fatal("arm_update_translation_table(): v=0x%08x, h=%p w=%i"
	    " p=0x%08x\n", (int)vaddr_page, host_page, writeflag,
	    (int)paddr_page);  */

	/*  Scan the current TLB entries:  */
	found = -1;
	lowest_index = 0; lowest = cpu->cd.arm.vph_tlb_entry[0].timestamp;
	for (r=0; r<ARM_MAX_VPH_TLB_ENTRIES; r++) {
		if (cpu->cd.arm.vph_tlb_entry[r].timestamp < lowest) {
			lowest = cpu->cd.arm.vph_tlb_entry[r].timestamp;
			lowest_index = r;
		}
		if (cpu->cd.arm.vph_tlb_entry[r].timestamp > highest)
			highest = cpu->cd.arm.vph_tlb_entry[r].timestamp;
		if (cpu->cd.arm.vph_tlb_entry[r].valid &&
		    cpu->cd.arm.vph_tlb_entry[r].vaddr_page == vaddr_page) {
			found = r;
			break;
		}
	}

	if (found < 0) {
		/*  Create the new TLB entry, overwriting the oldest one:  */
		r = lowest_index;
		if (cpu->cd.arm.vph_tlb_entry[r].valid) {
			/*  This one has to be invalidated first:  */
			arm_invalidate_tlb_entry(cpu,
			    cpu->cd.arm.vph_tlb_entry[r].vaddr_page);
		}

		cpu->cd.arm.vph_tlb_entry[r].valid = 1;
		cpu->cd.arm.vph_tlb_entry[r].host_page = host_page;
		cpu->cd.arm.vph_tlb_entry[r].paddr_page = paddr_page;
		cpu->cd.arm.vph_tlb_entry[r].vaddr_page = vaddr_page;
		cpu->cd.arm.vph_tlb_entry[r].writeflag = writeflag;
		cpu->cd.arm.vph_tlb_entry[r].timestamp = highest + 1;

		/*  Add the new translation to the table:  */
		index = vaddr_page >> 12;
		cpu->cd.arm.host_load[index] = host_page;
		cpu->cd.arm.host_store[index] = writeflag? host_page : NULL;
		cpu->cd.arm.phys_addr[index] = paddr_page;
	} else {
		/*
		 *  The translation was already in the TLB.
		 *	Writeflag = 0:  Do nothing.
		 *	Writeflag = 1:  Make sure the page is writable.
		 *	Writeflag = -1: Downgrade to readonly.
		 */
		index = vaddr_page >> 12;
		cpu->cd.arm.vph_tlb_entry[found].timestamp = highest + 1;
		if (cpu->cd.arm.phys_addr[index] == paddr_page) {
			if (writeflag == 1)
				cpu->cd.arm.host_store[index] = host_page;
			if (writeflag == -1)
				cpu->cd.arm.host_store[index] = NULL;
		} else {
			/*  Change the entire physical/host mapping:  */
			cpu->cd.arm.host_load[index] = host_page;
			cpu->cd.arm.host_store[index] =
			    writeflag? host_page : NULL;
			cpu->cd.arm.phys_addr[index] = paddr_page;
		}
	}
}


#define MEMORY_RW	arm_memory_rw
#define MEM_ARM
#include "memory_rw.c"
#undef MEM_ARM
#undef MEMORY_RW


/*
 *  arm_pc_to_pointers():
 *
 *  This function uses the current program counter (a virtual address) to
 *  find out which physical translation page to use, and then sets the current
 *  translation page pointers to that page.
 *
 *  If there was no translation page for that physical page, then an empty
 *  one is created.
 */
void arm_pc_to_pointers(struct cpu *cpu)
{
	uint32_t cached_pc, physaddr, physpage_ofs;
	int pagenr, table_index;
	uint32_t *physpage_entryp;
	struct arm_tc_physpage *ppp;

	cached_pc = cpu->cd.arm.r[ARM_PC];

	/*
	 *  TODO: virtual to physical address translation
	 */

	physaddr = cached_pc & ~(((ARM_IC_ENTRIES_PER_PAGE-1) << 2) | 3);

	if (cpu->translation_cache == NULL || cpu->
	    translation_cache_cur_ofs >= ARM_TRANSLATION_CACHE_SIZE)
		arm_create_or_reset_tc(cpu);

	pagenr = ARM_ADDR_TO_PAGENR(physaddr);
	table_index = ARM_PAGENR_TO_TABLE_INDEX(pagenr);

	physpage_entryp = &(((uint32_t *)
	    cpu->translation_cache)[table_index]);
	physpage_ofs = *physpage_entryp;
	ppp = NULL;

	/*  Traverse the physical page chain:  */
	while (physpage_ofs != 0) {
		ppp = (struct arm_tc_physpage *)(cpu->translation_cache
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

		arm_tc_allocate_default_page(cpu, physaddr);

		ppp = (struct arm_tc_physpage *)(cpu->translation_cache
		    + physpage_ofs);
	}

	cpu->cd.arm.cur_physpage = ppp;
	cpu->cd.arm.cur_ic_page = &ppp->ics[0];
	cpu->cd.arm.next_ic = cpu->cd.arm.cur_ic_page +
	    ARM_PC_TO_IC_ENTRY(cached_pc);

	/*  printf("cached_pc = 0x%08x  pagenr = %i  table_index = %i, "
	    "physpage_ofs = 0x%08x\n", (int)cached_pc, (int)pagenr,
	    (int)table_index, (int)physpage_ofs);  */
}


#include "cpu_arm_instr.c"


/*
 *  arm_cpu_run_instr():
 *
 *  Execute one or more instructions on a specific CPU.
 *
 *  Return value is the number of instructions executed during this call,
 *  0 if no instructions were executed.
 */
int arm_cpu_run_instr(struct emul *emul, struct cpu *cpu)
{
	uint32_t cached_pc;
	ssize_t low_pc;
	int n_instrs;

	arm_pc_to_pointers(cpu);

	cached_pc = cpu->cd.arm.r[ARM_PC] & ~3;
	cpu->n_translated_instrs = 0;
	cpu->running_translated = 1;

	if (single_step || cpu->machine->instruction_trace) {
		/*
		 *  Single-step:
		 */
		struct arm_instr_call *ic = cpu->cd.arm.next_ic ++;
		if (cpu->machine->instruction_trace) {
			unsigned char instr[4];
			if (!cpu->memory_rw(cpu, cpu->mem, cached_pc, &instr[0],
			    sizeof(instr), MEM_READ, CACHE_INSTRUCTION)) {
				fatal("arm_cpu_run_instr(): could not read "
				    "the instruction\n");
			} else
				arm_cpu_disassemble_instr(cpu, instr, 1, 0, 0);
		}

		/*  When single-stepping, multiple instruction calls cannot
		    be combined into one. This clears all translations:  */
		if (cpu->cd.arm.cur_physpage->flags & COMBINATIONS) {
			int i;
			for (i=0; i<ARM_IC_ENTRIES_PER_PAGE; i++)
				cpu->cd.arm.cur_physpage->ics[i].f =
				    instr(to_be_translated);
			fatal("[ Note: The translation of physical page 0x%08x"
			    " contained combinations of instructions; these "
			    "are now flushed because we are single-stepping."
			    " ]\n", cpu->cd.arm.cur_physpage->physaddr);
			cpu->cd.arm.cur_physpage->flags &= ~COMBINATIONS;
			cpu->cd.arm.cur_physpage->flags &= ~TRANSLATIONS;
		}

		/*  Execute just one instruction:  */
		ic->f(cpu, ic);
		n_instrs = 1;
	} else {
		/*  Execute multiple instructions:  */
		n_instrs = 0;
		for (;;) {
			struct arm_instr_call *ic;

#define I		ic = cpu->cd.arm.next_ic ++; ic->f(cpu, ic);

			I; I; I; I; I;   I; I; I; I; I;
			I; I; I; I; I;   I; I; I; I; I;
			I; I; I; I; I;   I; I; I; I; I;
			I; I; I; I; I;   I; I; I; I; I;
			I; I; I; I; I;   I; I; I; I; I;
			I; I; I; I; I;   I; I; I; I; I;

			n_instrs += 60;
			if (!cpu->running_translated || single_step ||
			    n_instrs + cpu->n_translated_instrs >= 16384)
				break;
		}
	}


	/*
	 *  Update the program counter and return the correct number of
	 *  executed instructions:
	 */
	low_pc = ((size_t)cpu->cd.arm.next_ic - (size_t)
	    cpu->cd.arm.cur_ic_page) / sizeof(struct arm_instr_call);

	if (low_pc >= 0 && low_pc < ARM_IC_ENTRIES_PER_PAGE) {
		cpu->cd.arm.r[ARM_PC] &= ~((ARM_IC_ENTRIES_PER_PAGE-1) << 2);
		cpu->cd.arm.r[ARM_PC] += (low_pc << 2);
		cpu->pc = cpu->cd.arm.r[ARM_PC];
	} else if (low_pc == ARM_IC_ENTRIES_PER_PAGE) {
		/*  Switch to next page:  */
		cpu->cd.arm.r[ARM_PC] &= ~((ARM_IC_ENTRIES_PER_PAGE-1) << 2);
		cpu->cd.arm.r[ARM_PC] += (ARM_IC_ENTRIES_PER_PAGE << 2);
		cpu->pc = cpu->cd.arm.r[ARM_PC];
	} else {
		/*  debug("debug: Outside a page (This is actually ok)\n");  */
	}

	return n_instrs + cpu->n_translated_instrs;
}


#define CPU_RUN         arm_cpu_run
#define CPU_RINSTR      arm_cpu_run_instr
#define CPU_RUN_ARM
#include "cpu_run.c"
#undef CPU_RINSTR
#undef CPU_RUN_ARM
#undef CPU_RUN


/*
 *  arm_cpu_family_init():
 *
 *  This function fills the cpu_family struct with valid data.
 */
int arm_cpu_family_init(struct cpu_family *fp)
{
	fp->name = "ARM";
	fp->cpu_new = arm_cpu_new;
	fp->list_available_types = arm_cpu_list_available_types;
	fp->register_match = arm_cpu_register_match;
	fp->disassemble_instr = arm_cpu_disassemble_instr;
	fp->register_dump = arm_cpu_register_dump;
	fp->run = arm_cpu_run;
	fp->dumpinfo = arm_cpu_dumpinfo;
	/*  fp->show_full_statistics = arm_cpu_show_full_statistics;  */
	/*  fp->tlbdump = arm_cpu_tlbdump;  */
	/*  fp->interrupt = arm_cpu_interrupt;  */
	/*  fp->interrupt_ack = arm_cpu_interrupt_ack;  */
	return 1;
}

#endif	/*  ENABLE_ARM  */
