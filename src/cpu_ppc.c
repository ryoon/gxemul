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
 *  $Id: cpu_ppc.c,v 1.21 2005-02-13 23:50:48 debug Exp $
 *
 *  PowerPC/POWER CPU emulation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "../config.h"


#ifndef ENABLE_PPC


#include "cpu_ppc.h"


/*
 *  ppc_cpu_family_init():
 *
 *  Bogus function.
 */
int ppc_cpu_family_init(struct cpu_family *fp)
{
	return 0;
}


#else	/*  ENABLE_PPC  */


#include "cpu.h"
#include "cpu_ppc.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"
#include "opcodes_ppc.h"
#include "symbol.h"


extern volatile int single_step;
extern int old_show_trace_tree;   
extern int old_instruction_trace;
extern int old_quiet_mode;
extern int quiet_mode;


/*
 *  ppc_cpu_new():
 *
 *  Create a new PPC cpu object.
 */
struct cpu *ppc_cpu_new(struct memory *mem, struct machine *machine,
	int cpu_id, char *cpu_type_name)
{
	struct cpu *cpu;
	int any_cache = 0;
	int i, found;
	struct ppc_cpu_type_def cpu_type_defs[] = PPC_CPU_TYPE_DEFS;

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
	cpu->memory_rw          = ppc_memory_rw;
	cpu->cd.ppc.cpu_type    = cpu_type_defs[found];
	cpu->name               = cpu->cd.ppc.cpu_type.name;
	cpu->mem                = mem;
	cpu->machine            = machine;
	cpu->cpu_id             = cpu_id;
	cpu->byte_order         = EMUL_BIG_ENDIAN;
	cpu->bootstrap_cpu_flag = 0;
	cpu->running            = 0;
	cpu->cd.ppc.mode        = MODE_PPC;	/*  TODO  */

	/*  Only show name and caches etc for CPU nr 0 (in SMP machines):  */
	if (cpu_id == 0) {
		debug("%s", cpu->cd.ppc.cpu_type.name);

		if (cpu->cd.ppc.cpu_type.icache_shift != 0)
			any_cache = 1;
		if (cpu->cd.ppc.cpu_type.dcache_shift != 0)
			any_cache = 1;
		if (cpu->cd.ppc.cpu_type.l2cache_shift != 0)
			any_cache = 1;

		if (any_cache) {
			debug(" (I+D = %i+%i KB",
			    (int)(1 << (cpu->cd.ppc.cpu_type.icache_shift-10)),
			    (int)(1 << (cpu->cd.ppc.cpu_type.dcache_shift-10)));
			if (cpu->cd.ppc.cpu_type.l2cache_shift != 0) {
				debug(", L2 = %i KB",
				    (int)(1 << (cpu->cd.ppc.cpu_type.
				    l2cache_shift-10)));
			}
			debug(")");
		}
	}

	return cpu;
}


/*
 *  ppc_cpu_list_available_types():
 *
 *  Print a list of available PPC CPU types.
 */
void ppc_cpu_list_available_types(void)
{
	int i, j;
	struct ppc_cpu_type_def tdefs[] = PPC_CPU_TYPE_DEFS;

	i = 0;
	while (tdefs[i].name != NULL) {
		debug("%s", tdefs[i].name);
		for (j=10 - strlen(tdefs[i].name); j>0; j--)
			debug(" ");
		i++;
		if ((i % 6) == 0 || tdefs[i].name == NULL)
			debug("\n");
	}
}


/*
 *  ppc_cpu_dumpinfo():
 */
void ppc_cpu_dumpinfo(struct cpu *cpu)
{
	struct ppc_cpu_type_def *ct = &cpu->cd.ppc.cpu_type;

	debug(" (%i-bit ", cpu->cd.ppc.cpu_type.bits);

	switch (cpu->cd.ppc.mode) {
	case MODE_PPC:
		debug("PPC");
		break;
	case MODE_POWER:
		debug("POWER");
		break;
	default:
		debug("_INTERNAL ERROR_");
	}

	debug(", I+D = %i+%i KB",
	    (1 << ct->icache_shift) / 1024,
	    (1 << ct->dcache_shift) / 1024);

	if (ct->l2cache_shift) {
		int kb = (1 << ct->l2cache_shift) / 1024;
		debug(", L2 = %i %cB",
		    kb >= 1024? kb / 1024 : kb,
		    kb >= 1024? 'M' : 'K');
	}

	debug(")\n");
}


/*
 *  reg_access_msr():
 */
static void reg_access_msr(struct cpu *cpu, uint64_t *valuep, int writeflag)
{
	if (valuep == NULL) {
		fatal("reg_access_msr(): NULL\n");
		return;
	}

	if (writeflag)
		cpu->cd.ppc.msr = *valuep;

	/*  TODO: Is the little-endian bit writable?  */

	cpu->cd.ppc.msr &= ~PPC_MSR_LE;
	if (cpu->byte_order != EMUL_BIG_ENDIAN)
		cpu->cd.ppc.msr |= PPC_MSR_LE;

	if (!writeflag)
		*valuep = cpu->cd.ppc.msr;
}


/*
 *  ppc_cpu_register_dump():
 *
 *  Dump cpu registers in a relatively readable format.
 *
 *  gprs: set to non-zero to dump GPRs and hi/lo/pc
 *  coprocs: set bit 0..3 to dump registers in coproc 0..3.
 */
void ppc_cpu_register_dump(struct cpu *cpu, int gprs, int coprocs)
{
	char *symbol;
	uint64_t offset, tmp;
	int i, x = cpu->cpu_id;
	int bits32 = cpu->cd.ppc.cpu_type.bits == 32;

	if (gprs) {
		/*  Special registers (pc, ...) first:  */
		symbol = get_symbol_name(&cpu->machine->symbol_context,
		    cpu->cd.ppc.pc, &offset);

		debug("cpu%i: pc  = 0x", x);
		if (bits32)
			debug("%08x", x, (int)cpu->cd.ppc.pc);
		else
			debug("%016llx", x, (long long)cpu->cd.ppc.pc);
		debug("  <%s>\n", symbol != NULL? symbol : " no symbol ");

		debug("cpu%i: lr  = 0x", x);
		if (bits32)
			debug("%08x", (int)cpu->cd.ppc.lr);
		else
			debug("%016llx", (long long)cpu->cd.ppc.lr);
		debug("  cr = 0x%08x\n", (int)cpu->cd.ppc.cr);

		debug("cpu%i: ctr = 0x", x);
		if (bits32)
			debug("%08x\n", (int)cpu->cd.ppc.ctr);
		else
			debug("%016llx\n", (long long)cpu->cd.ppc.ctr);

		if (bits32) {
			/*  32-bit:  */
			for (i=0; i<PPC_NGPRS; i++) {
				if ((i % 4) == 0)
					debug("cpu%i:", x);
				debug(" r%02i = 0x%08x ", i,
				    (int)cpu->cd.ppc.gpr[i]);
				if ((i % 4) == 3)
					debug("\n");
			}
		} else {
			/*  64-bit:  */
			for (i=0; i<PPC_NGPRS; i++) {
				if ((i % 2) == 0)
					debug("cpu%i:", x);
				debug(" r%02i = 0x%016llx ", i,
				    (long long)cpu->cd.ppc.gpr[i]);
				if ((i % 2) == 1)
					debug("\n");
			}
		}

		/*  Other special registers:  */
		reg_access_msr(cpu, &tmp, 0);
		debug("cpu%i: msr = 0x%016llx\n", x, (long long)tmp);
	}

	if (coprocs) {
		debug("cpu%i: xer = 0x%016llx  fpscr = 0x%08x\n", x,
		    (long long)cpu->cd.ppc.xer, (int)cpu->cd.ppc.fpscr);

		/*  TODO: show floating-point values :-)  */

		/*  TODO: 32-bit fprs on 32-bit PPC cpus?  */

		for (i=0; i<PPC_NFPRS; i++) {
			if ((i % 2) == 0)
				debug("cpu%i:", x);
			debug(" f%02i = 0x%016llx ", i,
			    (long long)cpu->cd.ppc.fpr[i]);
			if ((i % 2) == 1)
				debug("\n");
		}
	}
}


/*
 *  ppc_cpu_register_match():
 */
void ppc_cpu_register_match(struct machine *m, char *name,
	int writeflag, uint64_t *valuep, int *match_register)
{
	int cpunr = 0;

	/*  CPU number:  */

	/*  TODO  */

	/*  Register name:  */
	if (strcasecmp(name, "pc") == 0) {
		if (writeflag) {
			m->cpus[cpunr]->cd.ppc.pc = *valuep;
		} else
			*valuep = m->cpus[cpunr]->cd.ppc.pc;
		*match_register = 1;
	} else if (strcasecmp(name, "lr") == 0) {
		if (writeflag)
			m->cpus[cpunr]->cd.ppc.lr = *valuep;
		else
			*valuep = m->cpus[cpunr]->cd.ppc.lr;
		*match_register = 1;
	} else if (strcasecmp(name, "cr") == 0) {
		if (writeflag)
			m->cpus[cpunr]->cd.ppc.cr = *valuep;
		else
			*valuep = m->cpus[cpunr]->cd.ppc.cr;
		*match_register = 1;
	} else if (strcasecmp(name, "ctr") == 0) {
		if (writeflag)
			m->cpus[cpunr]->cd.ppc.ctr = *valuep;
		else
			*valuep = m->cpus[cpunr]->cd.ppc.ctr;
		*match_register = 1;
	} else if (name[0] == 'r' && isdigit((int)name[1])) {
		int nr = atoi(name + 1);
		if (nr >= 0 && nr < PPC_NGPRS) {
			if (writeflag) {
				m->cpus[cpunr]->cd.ppc.gpr[nr] = *valuep;
			} else
				*valuep = m->cpus[cpunr]->cd.ppc.gpr[nr];
			*match_register = 1;
		}
	} else if (strcasecmp(name, "xer") == 0) {
		if (writeflag)
			m->cpus[cpunr]->cd.ppc.xer = *valuep;
		else
			*valuep = m->cpus[cpunr]->cd.ppc.xer;
		*match_register = 1;
	} else if (strcasecmp(name, "fpscr") == 0) {
		if (writeflag)
			m->cpus[cpunr]->cd.ppc.fpscr = *valuep;
		else
			*valuep = m->cpus[cpunr]->cd.ppc.fpscr;
		*match_register = 1;
	} else if (name[0] == 'f' && isdigit((int)name[1])) {
		int nr = atoi(name + 1);
		if (nr >= 0 && nr < PPC_NFPRS) {
			if (writeflag) {
				m->cpus[cpunr]->cd.ppc.fpr[nr] = *valuep;
			} else
				*valuep = m->cpus[cpunr]->cd.ppc.fpr[nr];
			*match_register = 1;
		}
	}
}


/*
 *  ppc_cpu_disassemble_instr():
 *
 *  Convert an instruction word into human readable format, for instruction
 *  tracing.
 *
 *  If running is 1, cpu->cd.ppc.pc should be the address of the
 *  instruction.
 *
 *  If running is 0, things that depend on the runtime environment (eg.
 *  register contents) will not be shown, and addr will be used instead of
 *  cpu->cd.ppc.pc for relative addresses.
 */
int ppc_cpu_disassemble_instr(struct cpu *cpu, unsigned char *instr,
	int running, uint64_t dumpaddr, int bintrans)
{
	int hi6, xo, lev, rt, rs, ra, rb, imm, sh, me, rc, l_bit, oe_bit;
	int spr, aa_bit, lk_bit, bf, bh, bi, bo;
	uint64_t offset, addr;
	uint32_t iword;
	char *symbol, *mnem = "ERROR";
	int power = cpu->cd.ppc.mode == MODE_POWER;

	if (running)
		dumpaddr = cpu->cd.ppc.pc;

	symbol = get_symbol_name(&cpu->machine->symbol_context,
	    dumpaddr, &offset);
	if (symbol != NULL && offset==0)
		debug("<%s>\n", symbol);

	if (cpu->machine->ncpus > 1 && running)
		debug("cpu%i: ", cpu->cpu_id);

	if (cpu->cd.ppc.mode == 32)
		debug("%08x", (int)dumpaddr);
	else
		debug("%016llx", (long long)dumpaddr);

	/*  NOTE: Fixed to big-endian.  */
	iword = (instr[0] << 24) + (instr[1] << 16) + (instr[2] << 8)
	    + instr[3];

	debug(": %08x\t", iword);

	if (bintrans && !running) {
		debug("(bintrans)");
		goto disasm_ret;
	}

	/*
	 *  Decode the instruction:
	 */

	hi6 = iword >> 26;

	switch (hi6) {
	case PPC_HI6_MULLI:
	case PPC_HI6_SUBFIC:
		rt = (iword >> 21) & 31;
		ra = (iword >> 16) & 31;
		imm = (int16_t)(iword & 0xffff);
		switch (hi6) {
		case PPC_HI6_MULLI:
			mnem = power? "muli":"mulli";
			break;
		case PPC_HI6_SUBFIC:
			mnem = power? "sfi":"subfic";
			break;
		}
		debug("%s\tr%i,r%i,%i", mnem, rt, ra, imm);
		break;
	case PPC_HI6_CMPLI:
		bf = (iword >> 23) & 7;
		l_bit = (iword >> 21) & 1;
		ra = (iword >> 16) & 31;
		imm = iword & 0xffff;
		debug("cmpl%si\t", l_bit? "d" : "w");
		if (bf != 0)
			debug("cr%i,", bf);
		debug("r%i,%i", ra, imm);
		break;
	case PPC_HI6_ADDIC:
	case PPC_HI6_ADDIC_DOT:
		rt = (iword >> 21) & 31;
		ra = (iword >> 16) & 31;
		rc = hi6 == PPC_HI6_ADDIC_DOT;
		imm = (int16_t)(iword & 0xffff);
		mnem = power? "ai":"addic";
		if (imm < 0 && !power) {
			mnem = "subic";
			imm = -imm;
		}
		debug("%s%s\tr%i,r%i,%i", mnem, rc?".":"", rt, ra, imm);
		break;
	case PPC_HI6_ADDI:
		rt = (iword >> 21) & 31;
		ra = (iword >> 16) & 31;
		imm = (int16_t)(iword & 0xffff);
		if (ra == 0)
			debug("li\tr%i,%i", rt, imm);
		else {
			mnem = power? "cal":"addi";
			if (imm < 0 && !power) {
				mnem = "subi";
				imm = -imm;
			}
			debug("%s\tr%i,r%i,%i", mnem, rt, ra, imm);
		}
		break;
	case PPC_HI6_ADDIS:
		rt = (iword >> 21) & 31;
		ra = (iword >> 16) & 31;
		imm = (int16_t)(iword & 0xffff);
		if (ra == 0)
			debug("lis\tr%i,%i", rt, imm);
		else
			debug("%s\tr%i,r%i,%i",
			    power? "cau":"addis", rt, ra, imm);
		break;
	case PPC_HI6_SC:
		lev = (iword >> 5) & 0x7f;
		debug("sc");
		if (lev != 0) {
			debug("\t%i", lev);
			if (lev > 1)
				debug(" (WARNING! reserved value)");
		}
		break;
	case PPC_HI6_B:
		aa_bit = (iword & 2) >> 1;
		lk_bit = iword & 1;
		/*  Sign-extend addr:  */
		addr = (int64_t)(int32_t)((iword & 0x03fffffc) << 6);
		addr = (int64_t)addr >> 6;
		debug("b");
		if (lk_bit)
			debug("l");
		if (aa_bit)
			debug("a");
		else
			addr += dumpaddr;
		if (cpu->cd.ppc.cpu_type.bits == 32)
			addr &= 0xffffffff;
		if (cpu->cd.ppc.cpu_type.bits == 32)
			debug("\t0x%x", (int)addr);
		else
			debug("\t0x%llx", (long long)addr);
		symbol = get_symbol_name(&cpu->machine->symbol_context,
		    addr, &offset);
		if (symbol != NULL)
			debug("\t<%s>", symbol);
		break;
	case PPC_HI6_19:
		xo = (iword >> 1) & 1023;
		switch (xo) {
		case PPC_19_BCLR:
			bo = (iword >> 21) & 31;
			bi = (iword >> 16) & 31;
			bh = (iword >> 11) & 3;
			lk_bit = iword & 1;
			mnem = power? "bcr" : "bclr";
			debug("%s%s%s\t%i,%i,%i", mnem, lk_bit? "l" : "",
			    bh? (bh==3? "+" : (bh==2? "-" : "?")) : "",
			    bo, bi, bh);
			break;
		case PPC_19_ISYNC:
			debug("%s", power? "ics" : "isync");
			break;
		default:
			debug("unimplemented hi6_19, xo = 0x%x", xo);
		}
		break;
	case PPC_HI6_ORI:
	case PPC_HI6_ORIS:
	case PPC_HI6_XORI:
	case PPC_HI6_XORIS:
	case PPC_HI6_ANDI_DOT:
	case PPC_HI6_ANDIS_DOT:
		rs = (iword >> 21) & 31;
		ra = (iword >> 16) & 31;
		imm = iword & 0xffff;
		switch (hi6) {
		case PPC_HI6_ORI:
			mnem = power? "oril":"ori";
			break;
		case PPC_HI6_ORIS:
			mnem = power? "oriu":"oris";
			break;
		case PPC_HI6_XORI:
			mnem = power? "xoril":"xori";
			break;
		case PPC_HI6_XORIS:
			mnem = power? "xoriu":"xoris";
			break;
		case PPC_HI6_ANDI_DOT:
			mnem = power? "andil.":"andi.";
			break;
		case PPC_HI6_ANDIS_DOT:
			mnem = power? "andiu.":"andis.";
			break;
		}
		if (hi6 == PPC_HI6_ORI && rs == 0 && ra == 0 && imm == 0)
			debug("nop");
		else
			debug("%s\tr%i,r%i,0x%04x", mnem, ra, rs, imm);
		break;
	case PPC_HI6_30:
		xo = (iword >> 2) & 7;
		switch (xo) {
		case PPC_30_RLDICR:
			rs = (iword >> 21) & 31;
			ra = (iword >> 16) & 31;
			sh = ((iword >> 11) & 31) | ((iword & 2) << 4);
			me = ((iword >> 6) & 31) | (iword & 0x20);
			rc = iword & 1;
			debug("rldicr%s\tr%i,r%i,%i,%i",
			    rc?".":"", ra, rs, sh, me);
			break;
		default:
			debug("unimplemented hi6_30, xo = 0x%x", xo);
		}
		break;
	case PPC_HI6_31:
		xo = (iword >> 1) & 1023;
		switch (xo) {
		case PPC_31_SUBF:
		case PPC_31_SUBFO:
			rt = (iword >> 21) & 31;
			ra = (iword >> 16) & 31;
			rb = (iword >> 11) & 31;
			oe_bit = (iword >> 10) & 1;
			rc = iword & 1;
			switch (xo) {
			case PPC_31_SUBF:   mnem = "subf"; break;
			case PPC_31_SUBFO:  mnem = "subfo"; break;
			}
			debug("%s%s\tr%i,r%i,r%i", mnem, rc? "." : "",
			    rt, ra, rb);
			break;
		case PPC_31_ANDC:
			rs = (iword >> 21) & 31;
			ra = (iword >> 16) & 31;
			rb = (iword >> 11) & 31;
			rc = iword & 1;
			debug("andc%s\tr%i,r%i,r%i", rc?".":"", ra, rs, rb);
			break;
		case PPC_31_MFMSR:
			rt = (iword >> 21) & 31;
			debug("mfmsr\tr%i", rt);
			break;
		case PPC_31_MTMSR:
			rs = (iword >> 21) & 31;
			l_bit = (iword >> 16) & 1;
			debug("mtmsr\tr%i", rs);
			if (l_bit)
				debug(",%i", l_bit);
			break;
		case PPC_31_ADD:
		case PPC_31_ADDO:
			rt = (iword >> 21) & 31;
			ra = (iword >> 16) & 31;
			rb = (iword >> 11) & 31;
			oe_bit = (iword >> 10) & 1;
			rc = iword & 1;
			switch (xo) {
			case PPC_31_ADD:
				mnem = power? "cax" : "add";
				break;
			case PPC_31_ADDO:
				mnem = power? "caxo" : "addo";
				break;
			}
			debug("%s%s\tr%i,r%i,r%i", mnem, rc? "." : "",
			    rt, ra, rb);
			break;
		case PPC_31_XOR:
			rs = (iword >> 21) & 31;
			ra = (iword >> 16) & 31;
			rb = (iword >> 11) & 31;
			rc = iword & 1;
			debug("xor%s\tr%i,r%i,r%i", rc? "." : "", ra, rs, rb);
			break;
		case PPC_31_MFSPR:
			rt = (iword >> 21) & 31;
			spr = ((iword >> 6) & 0x3e0) + ((iword >> 16) & 31);
			debug("mfspr\tr%i,spr%i", rt, spr);
			break;
		case PPC_31_OR:
			rs = (iword >> 21) & 31;
			ra = (iword >> 16) & 31;
			rb = (iword >> 11) & 31;
			rc = iword & 1;
			if (rs == rb)
				debug("mr%s\tr%i,r%i", rc? "." : "", ra, rs);
			else
				debug("or%s\tr%i,r%i,r%i",
				    rc? "." : "", ra, rs, rb);
			break;
		case PPC_31_MTSPR:
			rs = (iword >> 21) & 31;
			spr = ((iword >> 6) & 0x3e0) + ((iword >> 16) & 31);
			debug("mtspr\tspr%i,r%i", spr, rs);
			break;
		case PPC_31_SYNC:
			debug("%s", power? "dcs" : "sync");
			break;
		default:
			debug("unimplemented hi6_31, xo = 0x%x", xo);
		}
		break;
	case PPC_HI6_STW:
	case PPC_HI6_STWU:
		rs = (iword >> 21) & 31;
		ra = (iword >> 16) & 31;
		imm = (int16_t)(iword & 0xffff);
		switch (hi6) {
		case PPC_HI6_STW:
			mnem = power? "st" : "stw";
			break;
		case PPC_HI6_STWU:
			mnem = power? "stu" : "stwu";
			break;
		}
		debug("%s\tr%i,%i(r%i)", mnem, rs, imm, ra);
		if (running)
			goto disasm_ret_nonewline;
		break;
	default:
		/*  TODO  */
		debug("unimplemented hi6 = 0x%02x", hi6);
	}

disasm_ret:
	debug("\n");
disasm_ret_nonewline:
	return sizeof(iword);
}


/*
 *  ppc_cpu_run_instr():
 *  
 *  Execute one instruction on a specific CPU.
 *
 *  Return value is the number of instructions executed during this call,
 *  0 if no instruction was executed.
 */
int ppc_cpu_run_instr(struct emul *emul, struct cpu *cpu)
{
	uint32_t iword;
	unsigned char buf[4];
	unsigned char tmp_data[8];
	size_t tmp_data_len;
	int r, hi6, rt, rs, ra, rb, xo, lev, sh, me, rc, imm, l_bit, oe_bit;
	int c, m, i, spr, aa_bit, bo, bi, bh, lk_bit, bf, ctr_ok, cond_ok;
	uint64_t tmp, addr;
	uint64_t cached_pc;

	cached_pc = cpu->cd.ppc.pc_last = cpu->cd.ppc.pc & ~3;

	r = cpu->memory_rw(cpu, cpu->mem, cached_pc, &buf[0], sizeof(buf),
	    MEM_READ, CACHE_INSTRUCTION | PHYSICAL);
	if (!r)
		return 0;

	iword = (buf[0] << 24) + (buf[1] << 16) + (buf[2] << 8) + buf[3];

	if (cpu->machine->instruction_trace)
		ppc_cpu_disassemble_instr(cpu, buf, 1, 0, 0);

	cpu->cd.ppc.pc += sizeof(iword);
	cached_pc += sizeof(iword);

	hi6 = iword >> 26;

	switch (hi6) {

	case PPC_HI6_CMPLI:
		bf = (iword >> 23) & 7;
		l_bit = (iword >> 21) & 1;
		ra = (iword >> 16) & 31;
		imm = iword & 0xffff;
		tmp = cpu->cd.ppc.gpr[ra];
		if (!l_bit)
			tmp &= 0xffffffff;
		if ((uint64_t)tmp < (uint64_t)imm)
			c = 8;
		else if ((uint64_t)tmp > (uint64_t)imm)
			c = 4;
		else
			c = 2;
		/*  TODO: SO bit  */
		cpu->cd.ppc.cr &= ~(0xf << (31 - 4*bf));
		cpu->cd.ppc.cr |= (c << (31 - 4*bf));
		break;

	case PPC_HI6_ADDI:
	case PPC_HI6_ADDIS:
		rt = (iword >> 21) & 31;
		ra = (iword >> 16) & 31;
		if (hi6 == PPC_HI6_ADDI)
			imm = (int16_t)(iword & 0xffff);
		else
			imm = (int32_t)((iword & 0xffff) << 16);
		if (ra == 0)
			tmp = 0;
		else
			tmp = cpu->cd.ppc.gpr[ra];
		cpu->cd.ppc.gpr[rt] = tmp + imm;
		break;

	case PPC_HI6_SC:
		lev = (iword >> 5) & 0x7f;
		if (cpu->machine->userland_emul != NULL) {
			useremul_syscall(cpu, lev);
		} else {
			fatal("[ PPC: pc = 0x%016llx, sc not yet "
			    "implemented ]\n", (long long)cached_pc);
			cpu->running = 0;
			return 0;
		}
		break;

	case PPC_HI6_B:
		aa_bit = (iword & 2) >> 1;
		lk_bit = iword & 1;
		/*  Sign-extend addr:  */
		addr = (int64_t)(int32_t)((iword & 0x03fffffc) << 6);
		addr = (int64_t)addr >> 6;

		if (!aa_bit)
			addr += cpu->cd.ppc.pc_last;

		if (cpu->cd.ppc.cpu_type.bits == 32)
			addr &= 0xffffffff;

		if (lk_bit)
			cpu->cd.ppc.lr = cpu->cd.ppc.pc;
		cpu->cd.ppc.pc = addr;
		break;

	case PPC_HI6_19:
		xo = (iword >> 1) & 1023;
		switch (xo) {
		case PPC_19_BCLR:
			bo = (iword >> 21) & 31;
			bi = (iword >> 16) & 31;
			bh = (iword >> 11) & 3;
			lk_bit = iword & 1;
			if (!(bo & 4))
				cpu->cd.ppc.ctr --;
			addr = cpu->cd.ppc.lr;
			ctr_ok = (bo >> 2) & 1;
			tmp = cpu->cd.ppc.ctr;
			if (cpu->cd.ppc.cpu_type.bits == 32)
				tmp &= 0xffffffff;
			ctr_ok |= ( (tmp == 0) ^ ((bo >> 1) & 1) );
			cond_ok = (bo >> 4) & 1;
			cond_ok |= ( ((bo >> 3) & 1) ==
			    (cpu->cd.ppc.cr & (1 << (31-bi))) );
			if (lk_bit)
				cpu->cd.ppc.lr = cpu->cd.ppc.pc;
			if (ctr_ok && cond_ok) {
				cpu->cd.ppc.pc = addr & ~3;
				if (cpu->cd.ppc.cpu_type.bits == 32)
					cpu->cd.ppc.pc &= 0xffffffff;
			}
			break;
		case PPC_19_ISYNC:
			/*  TODO: actually sync  */
			break;
		default:
			fatal("[ unimplemented PPC hi6_19, xo = 0x%04x, "
			    "pc = 0x%016llx ]\n",
			    xo, (long long) (cpu->cd.ppc.pc_last));
			cpu->running = 0;
			return 0;
		}
		break;

	case PPC_HI6_ORI:
	case PPC_HI6_ORIS:
		rs = (iword >> 21) & 31;
		ra = (iword >> 16) & 31;
		if (hi6 == PPC_HI6_ORI)
			imm = (iword & 0xffff);
		else
			imm = (iword & 0xffff) << 16;
		tmp = cpu->cd.ppc.gpr[rs];
		cpu->cd.ppc.gpr[ra] = tmp | (uint32_t)imm;
		break;

	case PPC_HI6_30:
		xo = (iword >> 2) & 7;
		switch (xo) {
		case PPC_30_RLDICR:
			if (cpu->cd.ppc.cpu_type.bits == 32) {
				/*  TODO: Illegal instruction.  */
				break;
			}
			rs = (iword >> 21) & 31;
			ra = (iword >> 16) & 31;
			sh = ((iword >> 11) & 31) | ((iword & 2) << 4);
			me = ((iword >> 6) & 31) | (iword & 0x20);
			rc = iword & 1;
			if (rc) {
				fatal("[ PPC rc not yet implemeted ]\n");
				cpu->running = 0;
				return 0;
			}
			tmp = cpu->cd.ppc.gpr[rs];
			/*  TODO: Fix this, its performance is awful:  */
			while (sh-- != 0) {
				int b = (tmp >> 63) & 1;
				tmp = (tmp << 1) | b;
			}
			while (me++ < 63)
				tmp &= ~((uint64_t)1 << (63-me));
			cpu->cd.ppc.gpr[ra] = tmp;
			break;
		default:
			fatal("[ unimplemented PPC hi6_30, xo = 0x%04x, "
			    "pc = 0x%016llx ]\n",
			    xo, (long long) (cpu->cd.ppc.pc_last));
			cpu->running = 0;
			return 0;
		}
		break;

	case PPC_HI6_31:
		xo = (iword >> 1) & 1023;
		switch (xo) {
		case PPC_31_SUBF:
		case PPC_31_SUBFO:
			rt = (iword >> 21) & 31;
			ra = (iword >> 16) & 31;
			rb = (iword >> 11) & 31;
			oe_bit = (iword >> 10) & 1;
			rc = iword & 1;
			if (rc) {
				fatal("[ subf: PPC rc not yet implemeted ]\n");
				cpu->running = 0;
				return 0;
			}
			if (oe_bit) {
				fatal("[ subf: PPC oe not yet implemeted ]\n");
				cpu->running = 0;
				return 0;
			}
			cpu->cd.ppc.gpr[rt] = ~cpu->cd.ppc.gpr[ra] +
			    cpu->cd.ppc.gpr[rb] + 1;
			break;
		case PPC_31_ANDC:
			rs = (iword >> 21) & 31;
			ra = (iword >> 16) & 31;
			rb = (iword >> 11) & 31;
			rc = iword & 1;
			if (rc) {
				fatal("[ andc: PPC rc not yet implemeted ]\n");
				cpu->running = 0;
				return 0;
			}
			cpu->cd.ppc.gpr[ra] = cpu->cd.ppc.gpr[rs]
			    & ~cpu->cd.ppc.gpr[rb];
			break;
		case PPC_31_MFMSR:
			rt = (iword >> 21) & 31;
			/*  TODO: check pr  */
			reg_access_msr(cpu, &cpu->cd.ppc.gpr[rt], 0);
			break;
		case PPC_31_MTMSR:
			rs = (iword >> 21) & 31;
			l_bit = (iword >> 16) & 1;
			/*  TODO: the l_bit  */
			reg_access_msr(cpu, &cpu->cd.ppc.gpr[rs], 1);
			break;
		case PPC_31_ADD:
		case PPC_31_ADDO:
			rt = (iword >> 21) & 31;
			ra = (iword >> 16) & 31;
			rb = (iword >> 11) & 31;
			oe_bit = (iword >> 10) & 1;
			rc = iword & 1;
			if (rc) {
				fatal("[ add: PPC rc not yet implemeted ]\n");
				cpu->running = 0;
				return 0;
			}
			if (oe_bit) {
				fatal("[ add: PPC oe not yet implemeted ]\n");
				cpu->running = 0;
				return 0;
			}
			cpu->cd.ppc.gpr[rt] = cpu->cd.ppc.gpr[ra] +
			    cpu->cd.ppc.gpr[rb];
			break;
		case PPC_31_XOR:
			rs = (iword >> 21) & 31;
			ra = (iword >> 16) & 31;
			rb = (iword >> 11) & 31;
			rc = iword & 1;
			if (rc) {
				fatal("[ xor: PPC rc not yet implemeted ]\n");
				cpu->running = 0;
				return 0;
			}
			cpu->cd.ppc.gpr[ra] = cpu->cd.ppc.gpr[rs] ^
			    cpu->cd.ppc.gpr[rb];
			break;
		case PPC_31_MFSPR:
			rt = (iword >> 21) & 31;
			spr = ((iword >> 6) & 0x3e0) + ((iword >> 16) & 31);
			switch (spr) {
			case 1:	cpu->cd.ppc.gpr[rt] = cpu->cd.ppc.xer;
				break;
			case 8:	cpu->cd.ppc.gpr[rt] = cpu->cd.ppc.lr;
				break;
			case 9:	cpu->cd.ppc.gpr[rt] = cpu->cd.ppc.ctr;
				break;
			case 259:	/*  NOTE: no pr check  */
				cpu->cd.ppc.gpr[rt] = cpu->cd.ppc.sprg3;
				break;
			case 272:
				/*  TODO: check pr  */
				cpu->cd.ppc.gpr[rt] = cpu->cd.ppc.sprg0;
				break;
			case 273:
				/*  TODO: check pr  */
				cpu->cd.ppc.gpr[rt] = cpu->cd.ppc.sprg1;
				break;
			case 274:
				/*  TODO: check pr  */
				cpu->cd.ppc.gpr[rt] = cpu->cd.ppc.sprg2;
				break;
			case 275:
				/*  TODO: check pr  */
				cpu->cd.ppc.gpr[rt] = cpu->cd.ppc.sprg3;
				break;
			case 287:
				/*  TODO: check pr  */
				cpu->cd.ppc.gpr[rt] = cpu->cd.ppc.pvr;
				break;
			case 1023:
				/*  TODO: check pr  */
				cpu->cd.ppc.gpr[rt] = cpu->cd.ppc.pir;
				break;
			default:
				fatal("[ unimplemented PPC spr 0x%04x, "
				    "pc = 0x%016llx ]\n",
				    spr, (long long) (cpu->cd.ppc.pc_last));
				cpu->running = 0;
				return 0;
			}
			if (cpu->cd.ppc.cpu_type.bits == 32)
				cpu->cd.ppc.gpr[rt] &= 0xffffffffULL;
			break;
		case PPC_31_OR:
			rs = (iword >> 21) & 31;
			ra = (iword >> 16) & 31;
			rb = (iword >> 11) & 31;
			rc = iword & 1;
			if (rc) {
				fatal("[ or: PPC rc not yet implemeted ]\n");
				cpu->running = 0;
				return 0;
			}
			cpu->cd.ppc.gpr[ra] = cpu->cd.ppc.gpr[rs] |
			    cpu->cd.ppc.gpr[rb];
			break;
		case PPC_31_MTSPR:
			rs = (iword >> 21) & 31;
			spr = ((iword >> 6) & 0x3e0) + ((iword >> 16) & 31);
			switch (spr) {
			case 1:	cpu->cd.ppc.xer = cpu->cd.ppc.gpr[rs];
				break;
			case 8:	cpu->cd.ppc.lr = cpu->cd.ppc.gpr[rs];
				break;
			case 9:	cpu->cd.ppc.ctr = cpu->cd.ppc.gpr[rs];
				break;
			case 272:
				/*  TODO: check hypv  */
				cpu->cd.ppc.sprg0 = cpu->cd.ppc.gpr[rs];
				break;
			case 273:
				/*  TODO: check pr  */
				cpu->cd.ppc.sprg1 = cpu->cd.ppc.gpr[rs];
				break;
			case 274:
				/*  TODO: check pr  */
				cpu->cd.ppc.sprg2 = cpu->cd.ppc.gpr[rs];
				break;
			case 275:
				/*  TODO: check pr  */
				cpu->cd.ppc.sprg3 = cpu->cd.ppc.gpr[rs];
				break;
			case 287:
				fatal("[ PPC: attempt to write to PVR ]\n");
				break;
			case 1023:
				/*  TODO: check pr  */
				cpu->cd.ppc.pir = cpu->cd.ppc.gpr[rs];
				break;
			default:
				fatal("[ unimplemented PPC spr 0x%04x, "
				    "pc = 0x%016llx ]\n",
				    spr, (long long) (cpu->cd.ppc.pc_last));
				cpu->running = 0;
				return 0;
			}
			break;
		case PPC_31_SYNC:
			/*  TODO: actually sync  */
			break;
		default:
			fatal("[ unimplemented PPC hi6_31, xo = 0x%04x, "
			    "pc = 0x%016llx ]\n",
			    xo, (long long) (cpu->cd.ppc.pc_last));
			cpu->running = 0;
			return 0;
		}
		break;

	case PPC_HI6_STW:
	case PPC_HI6_STWU:
		rs = (iword >> 21) & 31;
		ra = (iword >> 16) & 31;
		imm = (int16_t)(iword & 0xffff);

		if (ra == 0) {
			if (hi6 == PPC_HI6_STWU)
				fatal("[ PPC WARNING: invalid STWU form ]\n");
			addr = 0;
		} else
			addr = cpu->cd.ppc.gpr[ra];

		tmp = cpu->cd.ppc.gpr[rs];

		if (cpu->machine->instruction_trace) {
			if (cpu->cd.ppc.cpu_type.bits == 32)
				debug("\t\t[0x%08llx", (long long)addr);
			else
				debug("\t\t[0x%016llx", (long long)addr);
		}

		tmp_data_len = 4;

		if (cpu->byte_order == EMUL_BIG_ENDIAN) {
			for (i=0; i<tmp_data_len; i++)
				tmp_data[tmp_data_len-1-i] = (tmp >> 8) & 255;
		} else {
			for (i=0; i<tmp_data_len; i++)
				tmp_data[i] = (tmp >> 8) & 255;
		}

		/*  TODO  */
		r = cpu->memory_rw(cpu, cpu->mem, addr, tmp_data,
		    tmp_data_len, MEM_WRITE, CACHE_DATA);

		if (cpu->machine->instruction_trace) {
			if (r == MEMORY_ACCESS_OK)
				debug(", data = 0x%08x]\n", (int)tmp);
			else
				debug(", FAILED]\n");
		}

		if (r != MEMORY_ACCESS_OK) {
			/*  TODO: exception?  */
			return 0;
		}

		if (hi6 == PPC_HI6_STWU)
			cpu->cd.ppc.gpr[ra] = addr;
		break;

	default:
		fatal("[ unimplemented PPC hi6 = 0x%02x, pc = 0x%016llx ]\n",
		    hi6, (long long) (cpu->cd.ppc.pc_last));
		cpu->running = 0;
		return 0;
	}

	return 1;
}


#define CPU_RUN		ppc_cpu_run
#define CPU_RINSTR	ppc_cpu_run_instr
#define CPU_RUN_PPC
#include "cpu_run.c"
#undef CPU_RINSTR
#undef CPU_RUN_PPC
#undef CPU_RUN


#define MEMORY_RW	ppc_memory_rw
#define MEM_PPC
#include "memory_rw.c"
#undef MEM_PPC
#undef MEMORY_RW


/*
 *  ppc_cpu_family_init():
 *
 *  Fill in the cpu_family struct for PPC.
 */
int ppc_cpu_family_init(struct cpu_family *fp)
{
	fp->name = "PPC";
	fp->cpu_new = ppc_cpu_new;
	fp->list_available_types = ppc_cpu_list_available_types;
	fp->register_match = ppc_cpu_register_match;
	fp->disassemble_instr = ppc_cpu_disassemble_instr;
	fp->register_dump = ppc_cpu_register_dump;
	fp->run = ppc_cpu_run;
	fp->dumpinfo = ppc_cpu_dumpinfo;
	/*  fp->show_full_statistics = ppc_cpu_show_full_statistics;  */
	/*  fp->tlbdump = ppc_cpu_tlbdump;  */
	/*  fp->interrupt = ppc_cpu_interrupt;  */
	/*  fp->interrupt_ack = ppc_cpu_interrupt_ack;  */
	return 1;
}


#endif	/*  ENABLE_PPC  */
