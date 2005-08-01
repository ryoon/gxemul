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
 *  $Id: cpu_alpha.c,v 1.31 2005-08-01 05:10:30 debug Exp $
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
 *  Create a new Alpha CPU object by filling the CPU struct.
 *  Return 1 on success, 0 if cpu_type_name isn't a valid Alpha processor.
 */
int alpha_cpu_new(struct cpu *cpu, struct memory *mem,
	struct machine *machine, int cpu_id, char *cpu_type_name)
{
	int i;

	if (strcasecmp(cpu_type_name, "Alpha") != 0)
		return 0;

	memset(&cpu->cd.alpha, 0, sizeof(struct alpha_cpu));

	cpu->memory_rw = alpha_memory_rw;
	cpu->update_translation_table = alpha_update_translation_table;
	cpu->invalidate_translation_caches_paddr =
	    alpha_invalidate_translation_caches_paddr;
	cpu->is_32bit = 0;

	/*  Only show name and caches etc for CPU nr 0:  */
	if (cpu_id == 0) {
		debug("%s", cpu->name);
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
		cpu->cd.alpha.vph_table0[i] = cpu->cd.alpha.vph_table0_kernel[i]
		    = cpu->cd.alpha.vph_default_page;

	return 1;
}


/*
 *  alpha_cpu_dumpinfo():
 */
void alpha_cpu_dumpinfo(struct cpu *cpu)
{
	/*  TODO  */
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
	uint64_t offset, tmp;
	int opcode, ra, rb, func, rc, imm, floating;
	char *symbol, *mnem = NULL;
	char palcode_name[30];

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
	case 0x00:
		alpha_palcode_name(iw & 0x3ffffff, palcode_name,
		    sizeof(palcode_name));
		debug("call_pal %s\n", palcode_name);
		break;
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
	case 0x0f:
	case 0x20:
	case 0x21:
	case 0x22:
	case 0x23:
	case 0x24:
	case 0x25:
	case 0x26:
	case 0x27:
	case 0x28:
	case 0x29:
	case 0x2c:
	case 0x2d:
		floating = 0;
		switch (opcode) {
		case 0x0a: mnem = "ldbu"; break;
		case 0x0b: mnem = "ldq_u"; break;
		case 0x0c: mnem = "ldwu"; break;
		case 0x0d: mnem = "stw"; break;
		case 0x0e: mnem = "stb"; break;
		case 0x0f: mnem = "stq_u"; break;
		case 0x20: mnem = "ldf"; floating = 1; break;
		case 0x21: mnem = "ldg"; floating = 1; break;
		case 0x22: mnem = "lds"; floating = 1; break;
		case 0x23: mnem = "ldt"; floating = 1; break;
		case 0x24: mnem = "stf"; floating = 1; break;
		case 0x25: mnem = "stg"; floating = 1; break;
		case 0x26: mnem = "sts"; floating = 1; break;
		case 0x27: mnem = "stt"; floating = 1; break;
		case 0x28: mnem = "ldl"; break;
		case 0x29: mnem = "ldq"; break;
		case 0x2c: mnem = "stl"; break;
		case 0x2d: mnem = "stq"; break;
		}
		if (opcode == 0x0b && ra == ALPHA_ZERO) {
			debug("unop");
		} else {
			debug("%s\t", mnem);
			if (floating)
				debug("f%i,", ra);
			else
				debug("%s,", alpha_regname[ra]);
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
		case 0x008: mnem = "andnot"; break;
		case 0x014: mnem = "cmovlbs"; break;
		case 0x016: mnem = "cmovlbc"; break;
		case 0x020: mnem = "or"; break;
		case 0x024: mnem = "cmoveq"; break;
		case 0x026: mnem = "cmovne"; break;
		case 0x028: mnem = "ornot"; break;
		case 0x040: mnem = "xor"; break;
		case 0x044: mnem = "cmovlt"; break;
		case 0x046: mnem = "cmovge"; break;
		case 0x048: mnem = "eqv"; break;
		case 0x061: mnem = "amask"; break;
		case 0x064: mnem = "cmovle"; break;
		case 0x066: mnem = "cmovgt"; break;
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
		case 0x02: mnem = "mskbl"; break;
		case 0x06: mnem = "extbl"; break;
		case 0x0b: mnem = "insbl"; break;
		case 0x12: mnem = "mskwl"; break;
		case 0x16: mnem = "extwl"; break;
		case 0x1b: mnem = "inswl"; break;
		case 0x22: mnem = "mskll"; break;
		case 0x26: mnem = "extll"; break;
		case 0x2b: mnem = "insll"; break;
		case 0x30: mnem = "zap"; break;
		case 0x31: mnem = "zapnot"; break;
		case 0x32: mnem = "mskql"; break;
		case 0x34: mnem = "srl"; break;
		case 0x36: mnem = "extql"; break;
		case 0x39: mnem = "sll"; break;
		case 0x3b: mnem = "insql"; break;
		case 0x3c: mnem = "sra"; break;
		case 0x52: mnem = "mskwh"; break;
		case 0x57: mnem = "inswh"; break;
		case 0x5a: mnem = "extwh"; break;
		case 0x62: mnem = "msklh"; break;
		case 0x67: mnem = "inslh"; break;
		case 0x6a: mnem = "extlh"; break;
		case 0x72: mnem = "mskqh"; break;
		case 0x77: mnem = "insqh"; break;
		case 0x7a: mnem = "extqh"; break;
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
	case 0x13:
		switch (func & 0x7f) {
		case 0x00: mnem = "mull"; break;
		case 0x20: mnem = "mulq"; break;
		case 0x30: mnem = "umulh"; break;
		case 0x40: mnem = "mull/v"; break;
		case 0x60: mnem = "mulq/v"; break;
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
	case 0x34:
		tmp = iw & 0x1fffff;
		if (tmp & 0x100000)
			tmp |= 0xffffffffffe00000ULL;
		tmp <<= 2;
		tmp += dumpaddr + sizeof(uint32_t);
		debug("%s\t", opcode==0x30? "br" : "bsr");
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


#define	DYNTRANS_TC_ALLOCATE_DEFAULT_PAGE	alpha_tc_allocate_default_page
#define DYNTRANS_IC             		alpha_instr_call
#define DYNTRANS_ARCH           		alpha
#define DYNTRANS_ALPHA
#define DYNTRANS_IC_ENTRIES_PER_PAGE		ALPHA_IC_ENTRIES_PER_PAGE
#define	DYNTRANS_TC_PHYSPAGE			alpha_tc_physpage
#include "cpu_dyntrans.c"
#undef  DYNTRANS_IC_ENTRIES_PER_PAGE
#undef  DYNTRANS_ALPHA
#undef	DYNTRANS_TC_PHYSPAGE
#undef  DYNTRANS_IC
#undef  DYNTRANS_ARCH
#undef	DYNTRANS_TC_ALLOCATE_DEFAULT_PAGE


/*
 *  alpha_invalidate_tlb_entry():
 *
 *  Invalidate a translation entry (based on virtual address).
 */
void alpha_invalidate_tlb_entry(struct cpu *cpu, uint64_t vaddr_page)
{
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
		if (kernel)
			cpu->cd.alpha.vph_table0_kernel[a] =
			    cpu->cd.alpha.vph_default_page;
		else
			cpu->cd.alpha.vph_table0[a] =
			    cpu->cd.alpha.vph_default_page;
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
	int found, r, lowest_index, kernel = 0;
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
			a = (addr >> ALPHA_LEVEL0_SHIFT) & (ALPHA_LEVEL0 - 1);
			b = (addr >> ALPHA_LEVEL1_SHIFT) & (ALPHA_LEVEL1 - 1);
			if ((addr >> ALPHA_TOPSHIFT) == ALPHA_TOP_KERNEL) {
				vph_p = cpu->cd.alpha.vph_table0_kernel[a];
				kernel = 1;
			} else
				vph_p = cpu->cd.alpha.vph_table0[a];
			vph_p->host_load[b] = NULL;
			vph_p->host_store[b] = NULL;
			vph_p->phys_addr[b] = 0;
			vph_p->refcount --;
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
		}

		cpu->cd.alpha.vph_tlb_entry[r].valid = 1;
		cpu->cd.alpha.vph_tlb_entry[r].host_page = host_page;
		cpu->cd.alpha.vph_tlb_entry[r].paddr_page = paddr_page;
		cpu->cd.alpha.vph_tlb_entry[r].vaddr_page = vaddr_page;
		cpu->cd.alpha.vph_tlb_entry[r].writeflag = writeflag;
		cpu->cd.alpha.vph_tlb_entry[r].timestamp = highest + 1;

		/*  Add the new translation to the table:  */
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
	} else {
		/*
		 *  The translation was already in the TLB.
		 *	Writeflag = 0:  Do nothing.
		 *	Writeflag = 1:  Make sure the page is writable.
		 *	Writeflag = -1: Downgrade to readonly.
		 */
		a = (vaddr_page >> ALPHA_LEVEL0_SHIFT) & (ALPHA_LEVEL0 - 1);
		b = (vaddr_page >> ALPHA_LEVEL1_SHIFT) & (ALPHA_LEVEL1 - 1);
		if ((vaddr_page >> ALPHA_TOPSHIFT) == ALPHA_TOP_KERNEL) {
			vph_p = cpu->cd.alpha.vph_table0_kernel[a];
			kernel = 1;
		} else
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


#define MEMORY_RW       alpha_userland_memory_rw
#define MEM_ALPHA
#define MEM_USERLAND
#include "memory_rw.c"
#undef MEM_USERLAND
#undef MEM_ALPHA
#undef MEMORY_RW


#define DYNTRANS_PC_TO_POINTERS_FUNC    alpha_pc_to_pointers
#define DYNTRANS_ARCH           alpha
#define DYNTRANS_ALPHA
#define DYNTRANS_IC_ENTRIES_PER_PAGE    ALPHA_IC_ENTRIES_PER_PAGE
#define DYNTRANS_ADDR_TO_PAGENR         ALPHA_ADDR_TO_PAGENR
#define DYNTRANS_PC_TO_IC_ENTRY         ALPHA_PC_TO_IC_ENTRY
#define DYNTRANS_TC_ALLOCATE            alpha_tc_allocate_default_page
#define DYNTRANS_TC_PHYSPAGE            alpha_tc_physpage
#include "cpu_dyntrans.c"
#undef  DYNTRANS_PC_TO_IC_ENTRY
#undef  DYNTRANS_TC_ALLOCATE
#undef  DYNTRANS_TC_PHYSPAGE
#undef  DYNTRANS_ADDR_TO_PAGENR
#undef  DYNTRANS_IC_ENTRIES_PER_PAGE
#undef  DYNTRANS_ALPHA
#undef  DYNTRANS_ARCH
#undef  DYNTRANS_PC_TO_POINTERS_FUNC


#include "cpu_alpha_instr.c"


#define DYNTRANS_CPU_RUN_INSTR  alpha_cpu_run_instr
#define DYNTRANS_PC_TO_POINTERS alpha_pc_to_pointers
#define DYNTRANS_IC             alpha_instr_call
#define DYNTRANS_ARCH           alpha
#define DYNTRANS_ALPHA
#define DYNTRANS_IC_ENTRIES_PER_PAGE ALPHA_IC_ENTRIES_PER_PAGE
#include "cpu_dyntrans.c"
#undef  DYNTRANS_IC_ENTRIES_PER_PAGE
#undef  DYNTRANS_ALPHA
#undef  DYNTRANS_IC
#undef  DYNTRANS_ARCH
#undef  DYNTRANS_PC_TO_POINTERS
#undef  DYNTRANS_CPU_RUN_INSTR


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
