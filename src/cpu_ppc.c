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
 *  $Id: cpu_ppc.c,v 1.57 2005-02-22 20:55:41 debug Exp $
 *
 *  PowerPC/POWER CPU emulation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "misc.h"


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
	cpu->cd.ppc.mode        = MODE_PPC;	/*  TODO  */

	/*  Current operating mode:  */
	cpu->cd.ppc.bits        = cpu->cd.ppc.cpu_type.bits;
	cpu->bootstrap_cpu_flag = 0;
	cpu->running            = 0;

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

	cpu->cd.ppc.pir = cpu_id;

	/*  Some default stack pointer value.  TODO: move this?  */
	cpu->cd.ppc.gpr[1] = machine->physical_ram_in_mb * 1048576 - 4096;

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

	debug(" (%i-bit ", cpu->cd.ppc.bits);

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
	int bits32 = cpu->cd.ppc.bits == 32;

	if (gprs) {
		/*  Special registers (pc, ...) first:  */
		symbol = get_symbol_name(&cpu->machine->symbol_context,
		    cpu->pc, &offset);

		debug("cpu%i: pc  = 0x", x);
		if (bits32)
			debug("%08x", (int)cpu->pc);
		else
			debug("%016llx", (long long)cpu->pc);
		debug("  <%s>\n", symbol != NULL? symbol : " no symbol ");

		debug("cpu%i: lr  = 0x", x);
		if (bits32)
			debug("%08x", (int)cpu->cd.ppc.lr);
		else
			debug("%016llx", (long long)cpu->cd.ppc.lr);
		debug("  cr = 0x%08x\n", (int)cpu->cd.ppc.cr);

		debug("cpu%i: ctr = 0x", x);
		if (bits32)
			debug("%08x", (int)cpu->cd.ppc.ctr);
		else
			debug("%016llx", (long long)cpu->cd.ppc.ctr);

		debug("  xer = 0x", x);
		if (bits32)
			debug("%08x\n", (int)cpu->cd.ppc.xer);
		else
			debug("%016llx\n", (long long)cpu->cd.ppc.xer);

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
		debug("cpu%i: msr = 0x%016llx  ", x, (long long)tmp);
		debug("tb = 0x%08x%08x\n",
		    (int)cpu->cd.ppc.tbu, (int)cpu->cd.ppc.tbl);
		debug("cpu%i: dec = 0x%08x  hdec = 0x%08x\n",
		    x, (int)cpu->cd.ppc.dec, (int)cpu->cd.ppc.hdec);
	}

	if (coprocs) {
		debug("cpu%i: fpscr = 0x%08x\n", x, (int)cpu->cd.ppc.fpscr);

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
			m->cpus[cpunr]->pc = *valuep;
		} else
			*valuep = m->cpus[cpunr]->pc;
		*match_register = 1;
	} else if (strcasecmp(name, "msr") == 0) {
		if (writeflag)
			m->cpus[cpunr]->cd.ppc.msr = *valuep;
		else
			*valuep = m->cpus[cpunr]->cd.ppc.msr;
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
	} else if (strcasecmp(name, "dec") == 0) {
		if (writeflag)
			m->cpus[cpunr]->cd.ppc.dec = *valuep;
		else
			*valuep = m->cpus[cpunr]->cd.ppc.dec;
		*match_register = 1;
	} else if (strcasecmp(name, "hdec") == 0) {
		if (writeflag)
			m->cpus[cpunr]->cd.ppc.hdec = *valuep;
		else
			*valuep = m->cpus[cpunr]->cd.ppc.hdec;
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
 *  If running is 1, cpu->pc should be the address of the instruction.
 *
 *  If running is 0, things that depend on the runtime environment (eg.
 *  register contents) will not be shown, and addr will be used instead of
 *  cpu->pc for relative addresses.
 */
int ppc_cpu_disassemble_instr(struct cpu *cpu, unsigned char *instr,
	int running, uint64_t dumpaddr, int bintrans)
{
	int hi6, xo, lev, rt, rs, ra, rb, imm, sh, me, rc, l_bit, oe_bit;
	int spr, aa_bit, lk_bit, bf, bh, bi, bo, mb, nb, bt, ba, bb, fpreg;
	int bfa;
	uint64_t offset, addr;
	uint32_t iword;
	char *symbol, *mnem = "ERROR";
	int power = cpu->cd.ppc.mode == MODE_POWER;

	if (running)
		dumpaddr = cpu->pc;

	symbol = get_symbol_name(&cpu->machine->symbol_context,
	    dumpaddr, &offset);
	if (symbol != NULL && offset==0)
		debug("<%s>\n", symbol);

	if (cpu->machine->ncpus > 1 && running)
		debug("cpu%i: ", cpu->cpu_id);

	if (cpu->cd.ppc.bits == 32)
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
	case PPC_HI6_CMPI:
		bf = (iword >> 23) & 7;
		l_bit = (iword >> 21) & 1;
		ra = (iword >> 16) & 31;
		if (hi6 == PPC_HI6_CMPLI) {
			imm = iword & 0xffff;
			mnem = "cmpl";
		} else {
			imm = (int16_t)(iword & 0xffff);
			mnem = "cmp";
		}
		debug("%s%si\t", mnem, l_bit? "d" : "w");
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
	case PPC_HI6_BC:
		aa_bit = (iword & 2) >> 1;
		lk_bit = iword & 1;
		bo = (iword >> 21) & 31;
		bi = (iword >> 16) & 31;
		/*  Sign-extend addr:  */
		addr = (int64_t)(int16_t)(iword & 0xfffc);
		debug("bc");
		if (lk_bit)
			debug("l");
		if (aa_bit)
			debug("a");
		else
			addr += dumpaddr;
		debug("\t%i,%i,", bo, bi);
		if (cpu->cd.ppc.bits == 32)
			addr &= 0xffffffff;
		if (cpu->cd.ppc.bits == 32)
			debug("0x%x", (int)addr);
		else
			debug("0x%llx", (long long)addr);
		symbol = get_symbol_name(&cpu->machine->symbol_context,
		    addr, &offset);
		if (symbol != NULL)
			debug("\t<%s>", symbol);
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
		if (cpu->cd.ppc.bits == 32)
			addr &= 0xffffffff;
		if (cpu->cd.ppc.bits == 32)
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
		case PPC_19_MCRF:
			bf = (iword >> 23) & 7;
			bfa = (iword >> 18) & 7;
			debug("mcrf\tcr%i,cr%i", bf, bfa);
			break;
		case PPC_19_BCLR:
		case PPC_19_BCCTR:
			bo = (iword >> 21) & 31;
			bi = (iword >> 16) & 31;
			bh = (iword >> 11) & 3;
			lk_bit = iword & 1;
			switch (xo) {
			case PPC_19_BCLR:
				mnem = power? "bcr" : "bclr"; break;
			case PPC_19_BCCTR:
				mnem = power? "bcc" : "bcctr"; break;
			}
			debug("%s%s%s\t%i,%i,%i", mnem, lk_bit? "l" : "",
			    bh? (bh==3? "+" : (bh==2? "-" : "?")) : "",
			    bo, bi, bh);
			break;
		case PPC_19_ISYNC:
			debug("%s", power? "ics" : "isync");
			break;
		case PPC_19_CRAND:
		case PPC_19_CRXOR:
		case PPC_19_CROR:
		case PPC_19_CRNAND:
		case PPC_19_CRNOR:
		case PPC_19_CRANDC:
		case PPC_19_CREQV:
		case PPC_19_CRORC:
			bt = (iword >> 21) & 31;
			ba = (iword >> 16) & 31;
			bb = (iword >> 11) & 31;
			switch (xo) {
			case PPC_19_CRAND:	mnem = "crand"; break;
			case PPC_19_CRXOR:	mnem = "crxor"; break;
			case PPC_19_CROR:	mnem = "cror"; break;
			case PPC_19_CRNAND:	mnem = "crnand"; break;
			case PPC_19_CRNOR:	mnem = "crnor"; break;
			case PPC_19_CRANDC:	mnem = "crandc"; break;
			case PPC_19_CREQV:	mnem = "creqv"; break;
			case PPC_19_CRORC:	mnem = "crorc"; break;
			}
			debug("%s\t%i,%i,%i", mnem, bt, ba, bb);
			break;
		default:
			debug("unimplemented hi6_19, xo = 0x%x", xo);
		}
		break;
	case PPC_HI6_RLWIMI:
	case PPC_HI6_RLWINM:
		rs = (iword >> 21) & 31;
		ra = (iword >> 16) & 31;
		sh = (iword >> 11) & 31;
		mb = (iword >> 6) & 31;
		me = (iword >> 1) & 31;
		rc = iword & 1;
		switch (hi6) {
		case PPC_HI6_RLWIMI:
			mnem = power? "rlimi" : "rlwimi"; break;
		case PPC_HI6_RLWINM:
			mnem = power? "rlinm" : "rlwinm"; break;
		}
		debug("%s%s\tr%i,r%i,%i,%i,%i",
		    mnem, rc?".":"", ra, rs, sh, mb, me);
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

		case PPC_31_CMP:
		case PPC_31_CMPL:
			bf = (iword >> 23) & 7;
			l_bit = (iword >> 21) & 1;
			ra = (iword >> 16) & 31;
			rb = (iword >> 11) & 31;
			if (xo == PPC_31_CMPL)
				mnem = "cmpl";
			else
				mnem = "cmp";
			debug("%s%s\t", mnem, l_bit? "d" : "w");
			if (bf != 0)
				debug("cr%i,", bf);
			debug("r%i,r%i", ra, rb);
			break;
		case PPC_31_MFCR:
			rt = (iword >> 21) & 31;
			debug("mfcr\tr%i", rt);
			break;
		case PPC_31_DCBST:
		case PPC_31_ICBI:
			ra = (iword >> 16) & 31;
			rb = (iword >> 11) & 31;
			switch (xo) {
			case PPC_31_DCBST:  mnem = "dcbst"; break;
			case PPC_31_ICBI:   mnem = "icbi"; break;
			}
			debug("%s\tr%i,r%i", mnem, ra, rb);
			break;
		case PPC_31_MFMSR:
			rt = (iword >> 21) & 31;
			debug("mfmsr\tr%i", rt);
			break;
		case PPC_31_MTCRF:
			rs = (iword >> 21) & 31;
			mb = (iword >> 12) & 255;  /*  actually fxm, not mb  */
			debug("mtcrf\t%i,r%i", mb, rs);
			break;
		case PPC_31_MTMSR:
			rs = (iword >> 21) & 31;
			l_bit = (iword >> 16) & 1;
			debug("mtmsr\tr%i", rs);
			if (l_bit)
				debug(",%i", l_bit);
			break;
		case PPC_31_LBZX:
		case PPC_31_LBZUX:
		case PPC_31_LHZX:
		case PPC_31_LHZUX:
		case PPC_31_LWZX:
		case PPC_31_LWZUX:
		case PPC_31_STBX:
		case PPC_31_STBUX:
		case PPC_31_STHX:
		case PPC_31_STHUX:
		case PPC_31_STWX:
		case PPC_31_STWUX:
			/*  rs for stores, rt for loads, actually  */
			rs = (iword >> 21) & 31;
			ra = (iword >> 16) & 31;
			rb = (iword >> 11) & 31;
			switch (xo) {
			case PPC_31_LBZX:  mnem = "lbzx"; break;
			case PPC_31_LBZUX: mnem = "lbzux"; break;
			case PPC_31_LHZX:  mnem = "lhzx"; break;
			case PPC_31_LHZUX: mnem = "lhzux"; break;
			case PPC_31_LWZX:
				mnem = power? "lx" : "lwzx";
				break;
			case PPC_31_LWZUX:
				mnem = power? "lux" : "lwzux";
				break;
			case PPC_31_STBX:  mnem = "stbx"; break;
			case PPC_31_STBUX: mnem = "stbux"; break;
			case PPC_31_STHX:  mnem = "sthx"; break;
			case PPC_31_STHUX: mnem = "sthux"; break;
			case PPC_31_STWX:
				mnem = power? "stx" : "stwx";
				break;
			case PPC_31_STWUX:
				mnem = power? "stux" : "stwux";
				break;
			}
			debug("%s\tr%i,r%i,r%i", mnem, rs, ra, rb);
			if (running)
				goto disasm_ret_nonewline;
			break;
		case PPC_31_NEG:
		case PPC_31_NEGO:
			rt = (iword >> 21) & 31;
			ra = (iword >> 16) & 31;
			oe_bit = (iword >> 10) & 1;
			rc = iword & 1;
			switch (xo) {
			case PPC_31_NEG:  mnem = "neg"; break;
			case PPC_31_NEGO: mnem = "nego"; break;
			}
			debug("%s%s\tr%i,r%i", mnem, rc? "." : "", rt, ra);
			break;
		case PPC_31_ADDZE:
		case PPC_31_ADDZEO:
			rt = (iword >> 21) & 31;
			ra = (iword >> 16) & 31;
			oe_bit = (iword >> 10) & 1;
			rc = iword & 1;
			switch (xo) {
			case PPC_31_ADDZE:
				mnem = power? "aze" : "addze";
				break;
			case PPC_31_ADDZEO:
				mnem = power? "azeo" : "addzeo";
				break;
			}
			debug("%s%s\tr%i,r%i", mnem, rc? "." : "", rt, ra);
			break;
		case PPC_31_MTSR:
			/*  Move to segment register (?)  */
			/*  TODO  */
			debug("mtsr\tTODO");
			break;
		case PPC_31_MTSRIN:
			/*  Move to segment register indirect (?)  */
			rt = (iword >> 21) & 31;
			rb = (iword >> 11) & 31;
			debug("mtsrin\tr%i,r%i", rt, rb);
			break;
		case PPC_31_ADDC:
		case PPC_31_ADDCO:
		case PPC_31_ADDE:
		case PPC_31_ADDEO:
		case PPC_31_ADD:
		case PPC_31_ADDO:
		case PPC_31_MULHW:
		case PPC_31_MULHWU:
		case PPC_31_MULLW:
		case PPC_31_MULLWO:
		case PPC_31_SUBF:
		case PPC_31_SUBFO:
		case PPC_31_SUBFC:
		case PPC_31_SUBFCO:
		case PPC_31_SUBFE:
		case PPC_31_SUBFEO:
			rt = (iword >> 21) & 31;
			ra = (iword >> 16) & 31;
			rb = (iword >> 11) & 31;
			oe_bit = (iword >> 10) & 1;
			rc = iword & 1;
			switch (xo) {
			case PPC_31_ADDC:
				mnem = power? "a" : "addc";
				break;
			case PPC_31_ADDCO:
				mnem = power? "ao" : "addco";
				break;
			case PPC_31_ADDE:
				mnem = power? "ae" : "adde";
				break;
			case PPC_31_ADDEO:
				mnem = power? "aeo" : "addeo";
				break;
			case PPC_31_ADD:
				mnem = power? "cax" : "add";
				break;
			case PPC_31_ADDO:
				mnem = power? "caxo" : "addo";
				break;
			case PPC_31_MULHW:  mnem = "mulhw"; break;
			case PPC_31_MULHWU: mnem = "mulhwu"; break;
			case PPC_31_MULLW:
				mnem = power? "muls" : "mullw";
				break;
			case PPC_31_MULLWO:
				mnem = power? "mulso" : "mullwo";
				break;
			case PPC_31_SUBF:   mnem = "subf"; break;
			case PPC_31_SUBFO:  mnem = "subfo"; break;
			case PPC_31_SUBFC:
				mnem = power? "sf" : "subfc";
				break;
			case PPC_31_SUBFCO:
				mnem = power? "sfo" : "subfco";
				break;
			case PPC_31_SUBFE:
				mnem = power? "sfe" : "subfe";
				break;
			case PPC_31_SUBFEO:
				mnem = power? "sfeo" : "subfeo";
				break;
			}
			debug("%s%s\tr%i,r%i,r%i", mnem, rc? "." : "",
			    rt, ra, rb);
			break;
		case PPC_31_MFSPR:
			rt = (iword >> 21) & 31;
			spr = ((iword >> 6) & 0x3e0) + ((iword >> 16) & 31);
			debug("mfspr\tr%i,spr%i", rt, spr);
			break;
		case PPC_31_TLBIE:
			/*  TODO: what is ra? The IBM online docs didn't say  */
			ra = 0;
			rb = (iword >> 11) & 31;
			if (power)
				debug("tlbi\tr%i,r%i", ra, rb);
			else
				debug("tlbie\tr%i", rb);
			break;
		case PPC_31_TLBSYNC:
			debug("tlbsync");
			break;
		case PPC_31_MFTB:
			rt = (iword >> 21) & 31;
			spr = ((iword >> 6) & 0x3e0) + ((iword >> 16) & 31);
			debug("mftb%s\tr%i", spr==268? "" :
			    (spr==269? "u" : "?"), rt);
			break;
		case PPC_31_CNTLZW:
			rs = (iword >> 21) & 31;
			ra = (iword >> 16) & 31;
			rc = iword & 1;
			mnem = power? "cntlz" : "cntlzw";
			debug("%s\tr%i,r%i", mnem, rc? "." : "", ra, rs);
			break;
		case PPC_31_SLW:
		case PPC_31_SRW:
		case PPC_31_AND:
		case PPC_31_ANDC:
		case PPC_31_NOR:
		case PPC_31_OR:
		case PPC_31_ORC:
		case PPC_31_XOR:
		case PPC_31_NAND:
			rs = (iword >> 21) & 31;
			ra = (iword >> 16) & 31;
			rb = (iword >> 11) & 31;
			rc = iword & 1;
			if (rs == rb && xo == PPC_31_OR)
				debug("mr%s\tr%i,r%i", rc? "." : "", ra, rs);
			else {
				switch (xo) {
				case PPC_31_SLW:  mnem =
					power? "sl" : "slw"; break;
				case PPC_31_SRW:  mnem =
					power? "sr" : "srw"; break;
				case PPC_31_AND:  mnem = "and"; break;
				case PPC_31_NAND: mnem = "nand"; break;
				case PPC_31_ANDC: mnem = "andc"; break;
				case PPC_31_NOR:  mnem = "nor"; break;
				case PPC_31_OR:   mnem = "or"; break;
				case PPC_31_ORC:  mnem = "orc"; break;
				case PPC_31_XOR:  mnem = "xor"; break;
				}
				debug("%s%s\tr%i,r%i,r%i", mnem,
				    rc? "." : "", ra, rs, rb);
			}
			break;
		case PPC_31_DCCCI:
			ra = (iword >> 16) & 31;
			rb = (iword >> 11) & 31;
			debug("dccci\tr%i,r%i", ra, rb);
			break;
		case PPC_31_ICCCI:
			ra = (iword >> 16) & 31;
			rb = (iword >> 11) & 31;
			debug("iccci\tr%i,r%i", ra, rb);
			break;
		case PPC_31_DIVW:
		case PPC_31_DIVWO:
		case PPC_31_DIVWU:
		case PPC_31_DIVWUO:
			rt = (iword >> 21) & 31;
			ra = (iword >> 16) & 31;
			rb = (iword >> 11) & 31;
			oe_bit = (iword >> 10) & 1;
			rc = iword & 1;
			switch (xo) {
			case PPC_31_DIVWU:  mnem = "divwu"; break;
			case PPC_31_DIVWUO: mnem = "divwuo"; break;
			case PPC_31_DIVW:   mnem = "divw"; break;
			case PPC_31_DIVWO:  mnem = "divwo"; break;
			}
			debug("%s%s\tr%i,r%i,r%i", mnem, rc? "." : "",
			    rt, ra, rb);
			break;
		case PPC_31_MTSPR:
			rs = (iword >> 21) & 31;
			spr = ((iword >> 6) & 0x3e0) + ((iword >> 16) & 31);
			debug("mtspr\tspr%i,r%i", spr, rs);
			break;
		case PPC_31_SYNC:
			debug("%s", power? "dcs" : "sync");
			break;
		case PPC_31_STSWI:
			rs = (iword >> 21) & 31;
			ra = (iword >> 16) & 31;
			nb = (iword >> 11) & 31;
			debug("%s\tr%i,r%i,%i", power? "stsi" : "stswi",
			    rs, ra, nb);
			if (running)
				goto disasm_ret_nonewline;
			break;
		case PPC_31_SRAWI:
			rs = (iword >> 21) & 31;
			ra = (iword >> 16) & 31;
			sh = (iword >> 11) & 31;
			rc = iword & 1;
			mnem = power? "srai" : "srawi";
			debug("%s%s\tr%i,r%i,%i", mnem,
			    rc? "." : "", ra, rs, sh);
			break;
		case PPC_31_EIEIO:
			debug("%s", power? "eieio?" : "eieio");
			break;
		case PPC_31_EXTSB:
		case PPC_31_EXTSH:
		case PPC_31_EXTSW:
			rs = (iword >> 21) & 31;
			ra = (iword >> 16) & 31;
			rc = iword & 1;
			switch (xo) {
			case PPC_31_EXTSB:
				mnem = power? "exts" : "extsb";
				break;
			case PPC_31_EXTSH:
				mnem = "extsh";
				break;
			case PPC_31_EXTSW:
				mnem = "extsw";
				break;
			}
			debug("%s%s\tr%i,r%i", mnem, rc? "." : "", ra, rs);
			break;
		default:
			debug("unimplemented hi6_31, xo = 0x%x", xo);
		}
		break;
	case PPC_HI6_LWZ:
	case PPC_HI6_LWZU:
	case PPC_HI6_LHZ:
	case PPC_HI6_LHZU:
	case PPC_HI6_LHA:
	case PPC_HI6_LHAU:
	case PPC_HI6_LBZ:
	case PPC_HI6_LBZU:
	case PPC_HI6_STW:
	case PPC_HI6_STWU:
	case PPC_HI6_STH:
	case PPC_HI6_STHU:
	case PPC_HI6_STB:
	case PPC_HI6_STBU:
	case PPC_HI6_STMW:
	case PPC_HI6_LFD:
	case PPC_HI6_STFD:
		/*  NOTE: Loads use rt, not rs, but are otherwise similar
		    to stores  */
		rs = (iword >> 21) & 31;
		ra = (iword >> 16) & 31;
		imm = (int16_t)(iword & 0xffff);
		fpreg = 0;
		switch (hi6) {
		case PPC_HI6_LWZ:	mnem = power? "l" : "lwz"; break;
		case PPC_HI6_LWZU:	mnem = power? "lu" : "lwzu"; break;
		case PPC_HI6_LHZ:	mnem = "lhz"; break;
		case PPC_HI6_LHZU:	mnem = "lhzu"; break;
		case PPC_HI6_LHA:	mnem = "lha"; break;
		case PPC_HI6_LHAU:	mnem = "lhau"; break;
		case PPC_HI6_LBZ:	mnem = "lbz"; break;
		case PPC_HI6_LBZU:	mnem = "lbzu"; break;
		case PPC_HI6_STW:	mnem = power? "st" : "stw"; break;
		case PPC_HI6_STWU:	mnem = power? "stu" : "stwu"; break;
		case PPC_HI6_STH:	mnem = "sth"; break;
		case PPC_HI6_STHU:	mnem = "sthu"; break;
		case PPC_HI6_STB:	mnem = "stb"; break;
		case PPC_HI6_STBU:	mnem = "stbu"; break;
		case PPC_HI6_LMW:	mnem = power? "lm" : "lmw"; break;
		case PPC_HI6_STMW:	mnem = power? "stm" : "stmw"; break;
		case PPC_HI6_LFD:	fpreg = 1; mnem = "lfd"; break;
		case PPC_HI6_STFD:	fpreg = 1; mnem = "stfd"; break;
		}
		debug("%s\t", mnem);
		if (fpreg)
			debug("f");
		else
			debug("r");
		debug("%i,%i(r%i)", rs, imm, ra);
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
 *  show_trace():
 *
 *  Show trace tree.   This function should be called every time
 *  a function is called.  cpu->cd.ppc.trace_tree_depth is increased here
 *  and should not be increased by the caller.
 *
 *  Note:  This function should not be called if show_trace_tree == 0.
 */
static void show_trace(struct cpu *cpu)
{
	uint64_t offset, addr = cpu->pc;
	int x, n_args_to_print;
	char strbuf[60];
	char *symbol;

	cpu->cd.ppc.trace_tree_depth ++;

	if (cpu->machine->ncpus > 1)
		debug("cpu%i:", cpu->cpu_id);

	symbol = get_symbol_name(&cpu->machine->symbol_context, addr, &offset);

	for (x=0; x<cpu->cd.ppc.trace_tree_depth; x++)
		debug("  ");

	/*  debug("<%s>\n", symbol!=NULL? symbol : "no symbol");  */

	if (symbol != NULL)
		debug("<%s(", symbol);
	else {
		debug("<0x");
		if (cpu->cd.ppc.bits == 32)
			debug("%08x", (int)addr);
		else
			debug("%016llx", (long long)addr);
		debug("(");
	}

	/*
	 *  TODO:  The number of arguments and the symbol type of each
	 *  argument should be taken from the symbol table, in some way.
	 */
	n_args_to_print = 5;

	for (x=0; x<n_args_to_print; x++) {
		int64_t d = cpu->cd.ppc.gpr[x + 3];

		if (d > -256 && d < 256)
			debug("%i", (int)d);
		else if (memory_points_to_string(cpu, cpu->mem, d, 1)) {
			debug("\"%s\"", memory_conv_to_string(cpu,
			    cpu->mem, d, strbuf, sizeof(strbuf)));
			if (strlen(strbuf) >= sizeof(strbuf)-1)
				debug("..");
		} else {
			if (cpu->cd.ppc.bits == 32)
				debug("0x%x", (int)d);
			else
				debug("0x%llx", (long long)d);
		}

		if (x < n_args_to_print - 1)
			debug(",");

		if (x == n_args_to_print - 1)
			break;
	}

	if (n_args_to_print > 9)
		debug("..");

	debug(")>\n");
}


/*
 *  update_cr0():
 *
 *  Sets the top 4 bits of the CR register.
 */
static void update_cr0(struct cpu *cpu, uint64_t value)
{
	int c;

	if (cpu->cd.ppc.bits == 64) {
		if ((int64_t)value < 0)
			c = 8;
		else if ((int64_t)value > 0)
			c = 4;
		else
			c = 2;
	} else {
		if ((int32_t)value < 0)
			c = 8;
		else if ((int32_t)value > 0)
			c = 4;
		else
			c = 2;
	}

	/*  SO bit, copied from XER:  */
	c |= ((cpu->cd.ppc.xer >> 31) & 1);

	cpu->cd.ppc.cr &= ~((uint32_t)0xf << 28);
	cpu->cd.ppc.cr |= ((uint32_t)c << 28);
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
	char *mnem = NULL;
	int r, hi6, rt, rs, ra, rb, xo, lev, sh, me, rc, imm, l_bit, oe_bit;
	int c, i, spr, aa_bit, bo, bi, bh, lk_bit, bf, ctr_ok, cond_ok;
	int update, load, mb, nb, bt, ba, bb, fpreg, arithflag, old_ca, bfa;
	uint64_t tmp=0, tmp2, addr;
	uint64_t cached_pc;

	cached_pc = cpu->cd.ppc.pc_last = cpu->pc & ~3;

	/*  Check PC against breakpoints:  */
	if (!single_step)
		for (i=0; i<cpu->machine->n_breakpoints; i++)
			if (cached_pc == cpu->machine->breakpoint_addr[i]) {
				fatal("Breakpoint reached, pc=0x");
				if (cpu->cd.ppc.bits == 32)
					fatal("%08x", (int)cached_pc);
				else
					fatal("%016llx", (long long)cached_pc);
				fatal("\n");
				single_step = 1;
				return 0;
			}

	/*  Update the Time Base and Decrementer:  */
	if ((++ cpu->cd.ppc.tbl) == 0)
		cpu->cd.ppc.tbu ++;

	cpu->cd.ppc.dec --;
	/*  TODO: dec interrupt!  */

	/*  TODO: hdec for POWER4+  */

	r = cpu->memory_rw(cpu, cpu->mem, cached_pc, &buf[0], sizeof(buf),
	    MEM_READ, CACHE_INSTRUCTION | PHYSICAL);
	if (!r)
		return 0;

	iword = (buf[0] << 24) + (buf[1] << 16) + (buf[2] << 8) + buf[3];

	if (cpu->machine->instruction_trace)
		ppc_cpu_disassemble_instr(cpu, buf, 1, 0, 0);

	cpu->pc += sizeof(iword);
	cached_pc += sizeof(iword);

	hi6 = iword >> 26;

	switch (hi6) {

	case PPC_HI6_MULLI:
		rt = (iword >> 21) & 31;
		ra = (iword >> 16) & 31;
		imm = (int16_t)(iword & 0xffff);
		cpu->cd.ppc.gpr[rt] = (int64_t)cpu->cd.ppc.gpr[ra]
		    * (int64_t)imm;
		break;

	case PPC_HI6_SUBFIC:
		rt = (iword >> 21) & 31;
		ra = (iword >> 16) & 31;
		imm = (int16_t)(iword & 0xffff);
		cpu->cd.ppc.xer &= ~PPC_XER_CA;
		if (cpu->cd.ppc.bits == 32) {
			tmp = (~cpu->cd.ppc.gpr[ra]) & 0xffffffff;
			cpu->cd.ppc.gpr[rt] = tmp + imm + 1;
			/*  TODO: is this CA correct?  */
			/*  printf("subfic: tmp = %016llx\n", (long long)tmp);
			    printf("subfic:  rt = %016llx\n\n",
			    (long long)cpu->cd.ppc.gpr[rt]);  */
			if ((tmp >> 32) != (cpu->cd.ppc.gpr[rt] >> 32))
				cpu->cd.ppc.xer |= PPC_XER_CA;
			/*  High 32 bits are probably undefined in
			    32-bit mode (I hope)  */
		} else {
			/*
			 *  Ugly, but I can't figure out a way right now how
			 *  to get the carry bit out of a 64-bit addition,
			 *  without access to more-than-64-bit operations in C.
			 */
			tmp = ~cpu->cd.ppc.gpr[ra];
			tmp2 = (tmp >> 32);	/*  High 32 bits  */
			tmp &= 0xffffffff;	/*  Low 32 bits  */

			tmp += imm + 1;
			if ((tmp >> 32) == 0) {
				/*  No change to upper 32 bits  */
			} else if ((tmp >> 32) == 1) {
				/*  Positive change:  */
				tmp2 ++;
			} else {
				/*  Negative change:  */
				tmp2 --;
			}

			tmp &= 0xffffffff;

			/*  TODO: is this CA calculation correct?  */
			if ((tmp2 >> 32) != 0)
				cpu->cd.ppc.xer |= PPC_XER_CA;

			cpu->cd.ppc.gpr[rt] = (tmp2 << 32) + tmp;
		}
		break;

	case PPC_HI6_CMPLI:
	case PPC_HI6_CMPI:
		bf = (iword >> 23) & 7;
		l_bit = (iword >> 21) & 1;
		ra = (iword >> 16) & 31;
		if (hi6 == PPC_HI6_CMPLI)
			imm = iword & 0xffff;
		else
			imm = (int16_t)(iword & 0xffff);
		tmp = cpu->cd.ppc.gpr[ra];

		if (hi6 == PPC_HI6_CMPI) {
			if (!l_bit)
				tmp = (int64_t)(int32_t)tmp;
			if ((int64_t)tmp < (int64_t)imm)
				c = 8;
			else if ((int64_t)tmp > (int64_t)imm)
				c = 4;
			else
				c = 2;
		} else {
			if (!l_bit)
				tmp &= 0xffffffff;
			if ((uint64_t)tmp < (uint64_t)imm)
				c = 8;
			else if ((uint64_t)tmp > (uint64_t)imm)
				c = 4;
			else
				c = 2;
		}

		/*  SO bit, copied from XER:  */
		c |= ((cpu->cd.ppc.xer >> 31) & 1);

		cpu->cd.ppc.cr &= ~(0xf << (28 - 4*bf));
		cpu->cd.ppc.cr |= (c << (28 - 4*bf));
		break;

	case PPC_HI6_ADDIC:
	case PPC_HI6_ADDIC_DOT:
		rt = (iword >> 21) & 31;
		ra = (iword >> 16) & 31;
		rc = hi6 == PPC_HI6_ADDIC_DOT;
		imm = (int16_t)(iword & 0xffff);
		/*  NOTE: Addic doesn't clear CA!  */
		if (cpu->cd.ppc.bits == 32) {
			tmp = cpu->cd.ppc.gpr[ra] & 0xffffffff;
			cpu->cd.ppc.gpr[rt] = tmp + (uint32_t)imm;
			/*  TODO: is this CA correct?  */
			/*  printf("addic: tmp = %016llx\n", (long long)tmp);
			    printf("addic:  rt = %016llx\n\n",
			    (long long)cpu->cd.ppc.gpr[rt]);  */
			if ((tmp >> 32) != (cpu->cd.ppc.gpr[rt] >> 32))
				cpu->cd.ppc.xer |= PPC_XER_CA;
			/*  High 32 bits are probably undefined in
			    32-bit mode (I hope)  */
		} else {
			/*  See comment about ugliness regarding SUBFIC  */
			tmp = cpu->cd.ppc.gpr[ra];
			tmp2 = (tmp >> 32);	/*  High 32 bits  */
			tmp &= 0xffffffff;	/*  Low 32 bits  */

			tmp += (int64_t)imm;
			if ((tmp >> 32) == 0) {
				/*  No change to upper 32 bits  */
			} else if ((tmp >> 32) == 1) {
				/*  Positive change:  */
				tmp2 ++;
			} else {
				/*  Negative change:  */
				tmp2 --;
			}

			tmp &= 0xffffffff;

			/*  TODO: is this CA calculation correct?  */
			if ((tmp2 >> 32) != 0)
				cpu->cd.ppc.xer |= PPC_XER_CA;

			cpu->cd.ppc.gpr[rt] = (tmp2 << 32) + tmp;
		}
		if (rc)
			update_cr0(cpu, cpu->cd.ppc.gpr[rt]);
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

	case PPC_HI6_BC:
		aa_bit = (iword >> 1) & 1;
		lk_bit = iword & 1;
		bo = (iword >> 21) & 31;
		bi = (iword >> 16) & 31;
		/*  Sign-extend addr:  */
		addr = (int64_t)(int16_t)(iword & 0xfffc);

		if (!aa_bit)
			addr += cpu->cd.ppc.pc_last;

		if (cpu->cd.ppc.bits == 32)
			addr &= 0xffffffff;

		if (!(bo & 4))
			cpu->cd.ppc.ctr --;
		ctr_ok = (bo >> 2) & 1;
		tmp = cpu->cd.ppc.ctr;
		if (cpu->cd.ppc.bits == 32)
			tmp &= 0xffffffff;
		ctr_ok |= ( (tmp != 0) ^ ((bo >> 1) & 1) );

		cond_ok = (bo >> 4) & 1;
		cond_ok |= ( ((bo >> 3) & 1) ==
		    ((cpu->cd.ppc.cr >> (31-bi)) & 1)  );

		if (lk_bit)
			cpu->cd.ppc.lr = cpu->pc;
		if (ctr_ok && cond_ok)
			cpu->pc = addr & ~3;
		if (lk_bit && cpu->machine->show_trace_tree)
			show_trace(cpu);
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

		if (cpu->cd.ppc.bits == 32)
			addr &= 0xffffffff;

		if (lk_bit)
			cpu->cd.ppc.lr = cpu->pc;

		cpu->pc = addr;

		if (lk_bit && cpu->machine->show_trace_tree)
			show_trace(cpu);
		break;

	case PPC_HI6_19:
		xo = (iword >> 1) & 1023;
		switch (xo) {

		case PPC_19_MCRF:
			bf = (iword >> 23) & 7;
			bfa = (iword >> 18) & 7;
			tmp = cpu->cd.ppc.cr >> (28 - bfa*4);
			tmp &= 0xf;
			cpu->cd.ppc.cr &= ~(0xf << (28 - bf*4));
			cpu->cd.ppc.cr |= (tmp << (28 - bf*4));
			break;

		case PPC_19_BCLR:
		case PPC_19_BCCTR:
			bo = (iword >> 21) & 31;
			bi = (iword >> 16) & 31;
			bh = (iword >> 11) & 3;
			lk_bit = iword & 1;
			if (xo == PPC_19_BCLR) {
				addr = cpu->cd.ppc.lr;
				if (!(bo & 4))
					cpu->cd.ppc.ctr --;
				ctr_ok = (bo >> 2) & 1;
				tmp = cpu->cd.ppc.ctr;
				if (cpu->cd.ppc.bits == 32)
					tmp &= 0xffffffff;
				ctr_ok |= ( (tmp != 0) ^ ((bo >> 1) & 1) );
				if (!quiet_mode && !lk_bit &&
				    cpu->machine->show_trace_tree) {
					cpu->cd.ppc.trace_tree_depth --;
					/*  TODO: show return value?  */
				}
			} else {
				addr = cpu->cd.ppc.ctr;
				ctr_ok = 1;
			}
			cond_ok = (bo >> 4) & 1;
			cond_ok |= ( ((bo >> 3) & 1) ==
			    ((cpu->cd.ppc.cr >> (31-bi)) & 1) );
			if (lk_bit)
				cpu->cd.ppc.lr = cpu->pc;
			if (ctr_ok && cond_ok) {
				cpu->pc = addr & ~3;
				if (cpu->cd.ppc.bits == 32)
					cpu->pc &= 0xffffffff;
			}
			if (lk_bit && cpu->machine->show_trace_tree)
				show_trace(cpu);
			break;

		case PPC_19_ISYNC:
			/*  TODO: actually sync  */
			break;

		case PPC_19_CRAND:
		case PPC_19_CRXOR:
		case PPC_19_CROR:
		case PPC_19_CRNAND:
		case PPC_19_CRNOR:
		case PPC_19_CRANDC:
		case PPC_19_CREQV:
		case PPC_19_CRORC:
			bt = (iword >> 21) & 31;
			ba = (iword >> 16) & 31;
			bb = (iword >> 11) & 31;
			ba = (cpu->cd.ppc.cr >> (31-ba)) & 1;
			bb = (cpu->cd.ppc.cr >> (31-bb)) & 1;
			cpu->cd.ppc.cr &= ~(1 << (31-bt));
			switch (xo) {
			case PPC_19_CRXOR:
				if (ba ^ bb)
					cpu->cd.ppc.cr |= (1 << (31-bt));
				break;
			case PPC_19_CROR:
				if (ba | bb)
					cpu->cd.ppc.cr |= (1 << (31-bt));
				break;
			default:
				fatal("[ TODO: crXXX, xo = %i, "
				    "pc = 0x%016llx ]\n",
				    xo, (long long) (cpu->cd.ppc.pc_last));
				cpu->running = 0;
				return 0;
			}
			break;

		default:
			fatal("[ unimplemented PPC hi6_19, xo = 0x%04x, "
			    "pc = 0x%016llx ]\n",
			    xo, (long long) (cpu->cd.ppc.pc_last));
			cpu->running = 0;
			return 0;
		}
		break;

	case PPC_HI6_RLWIMI:
	case PPC_HI6_RLWINM:
		rs = (iword >> 21) & 31;
		ra = (iword >> 16) & 31;
		sh = (iword >> 11) & 31;
		mb = (iword >> 6) & 31;
		me = (iword >> 1) & 31;
		rc = iword & 1;
		tmp = cpu->cd.ppc.gpr[rs];
		/*  TODO: Fix this, its performance is awful:  */
		while (sh-- != 0) {
			int b = (tmp >> 31) & 1;
			tmp = (tmp << 1) | b;
		}

		switch (hi6) {
		case PPC_HI6_RLWIMI:
			for (;;) {
				uint64_t mask;
				mask = (uint64_t)1 << (31-mb);
				cpu->cd.ppc.gpr[ra] &= ~mask;
				cpu->cd.ppc.gpr[ra] |= (tmp & mask);
				if (mb == me)
					break;
				mb ++;
				if (mb == 32)
					mb = 0;
			}
			break;
		case PPC_HI6_RLWINM:
			cpu->cd.ppc.gpr[ra] = 0;
			for (;;) {
				uint64_t mask;
				mask = (uint64_t)1 << (31-mb);
				cpu->cd.ppc.gpr[ra] |= (tmp & mask);
				if (mb == me)
					break;
				mb ++;
				if (mb == 32)
					mb = 0;
			}
			break;
		}
		if (rc)
			update_cr0(cpu, cpu->cd.ppc.gpr[ra]);
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

	case PPC_HI6_XORI:
	case PPC_HI6_XORIS:
		rs = (iword >> 21) & 31;
		ra = (iword >> 16) & 31;
		if (hi6 == PPC_HI6_XORI)
			imm = (iword & 0xffff);
		else
			imm = (iword & 0xffff) << 16;
		tmp = cpu->cd.ppc.gpr[rs];
		cpu->cd.ppc.gpr[ra] = tmp ^ (uint32_t)imm;
		break;

	case PPC_HI6_ANDI_DOT:
	case PPC_HI6_ANDIS_DOT:
		rs = (iword >> 21) & 31;
		ra = (iword >> 16) & 31;
		if (hi6 == PPC_HI6_ANDI_DOT)
			imm = (iword & 0xffff);
		else
			imm = (iword & 0xffff) << 16;
		tmp = cpu->cd.ppc.gpr[rs];
		cpu->cd.ppc.gpr[ra] = tmp & (uint32_t)imm;
		update_cr0(cpu, cpu->cd.ppc.gpr[ra]);
		break;

	case PPC_HI6_30:
		xo = (iword >> 2) & 7;
		switch (xo) {
		case PPC_30_RLDICR:
			if (cpu->cd.ppc.bits == 32) {
				/*  TODO: Illegal instruction.  */
				break;
			}
			rs = (iword >> 21) & 31;
			ra = (iword >> 16) & 31;
			sh = ((iword >> 11) & 31) | ((iword & 2) << 4);
			me = ((iword >> 6) & 31) | (iword & 0x20);
			rc = iword & 1;
			tmp = cpu->cd.ppc.gpr[rs];
			/*  TODO: Fix this, its performance is awful:  */
			while (sh-- != 0) {
				int b = (tmp >> 63) & 1;
				tmp = (tmp << 1) | b;
			}
			while (me++ < 63)
				tmp &= ~((uint64_t)1 << (63-me));
			cpu->cd.ppc.gpr[ra] = tmp;
			if (rc)
				update_cr0(cpu, cpu->cd.ppc.gpr[ra]);
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

		case PPC_31_CMPL:
		case PPC_31_CMP:
			bf = (iword >> 23) & 7;
			l_bit = (iword >> 21) & 1;
			ra = (iword >> 16) & 31;
			rb = (iword >> 11) & 31;

			tmp = cpu->cd.ppc.gpr[ra];
			tmp2 = cpu->cd.ppc.gpr[rb];

			if (hi6 == PPC_31_CMP) {
				if (!l_bit) {
					tmp = (int64_t)(int32_t)tmp;
					tmp2 = (int64_t)(int32_t)tmp2;
				}
				if ((int64_t)tmp < (int64_t)tmp2)
					c = 8;
				else if ((int64_t)tmp > (int64_t)tmp2)
					c = 4;
				else
					c = 2;
			} else {
				if (!l_bit) {
					tmp &= 0xffffffff;
					tmp2 &= 0xffffffff;
				}
				if ((uint64_t)tmp < (uint64_t)tmp2)
					c = 8;
				else if ((uint64_t)tmp > (uint64_t)tmp2)
					c = 4;
				else
					c = 2;
			}

			/*  SO bit, copied from XER:  */
			c |= ((cpu->cd.ppc.xer >> 31) & 1);

			cpu->cd.ppc.cr &= ~(0xf << (28 - 4*bf));
			cpu->cd.ppc.cr |= (c << (28 - 4*bf));
			break;

		case PPC_31_MFCR:
			rt = (iword >> 21) & 31;
			cpu->cd.ppc.gpr[rt] = cpu->cd.ppc.cr;
			break;

		case PPC_31_DCBST:
		case PPC_31_ICBI:
			ra = (iword >> 16) & 31;
			rb = (iword >> 11) & 31;
			switch (xo) {
			case PPC_31_DCBST:  mnem = "dcbst"; break;
			case PPC_31_ICBI:   mnem = "icbi"; break;
			}
			/*  debug("[ %s r%i,r%i: TODO ]\n", mnem, ra, rb);  */
			break;

		case PPC_31_MFMSR:
			rt = (iword >> 21) & 31;
			/*  TODO: check pr  */
			reg_access_msr(cpu, &cpu->cd.ppc.gpr[rt], 0);
			break;

		case PPC_31_MTCRF:
			rs = (iword >> 21) & 31;
			mb = (iword >> 12) & 255;  /*  actually fxm, not mb  */
			tmp = 0;
			for (i=0; i<8; i++, mb <<= 1, tmp <<= 4)
				if (mb & 128)
					tmp |= 0xf;
			cpu->cd.ppc.cr &= ~tmp;
			cpu->cd.ppc.cr |= (cpu->cd.ppc.gpr[rs] & tmp);
			break;

		case PPC_31_MTMSR:
			rs = (iword >> 21) & 31;
			l_bit = (iword >> 16) & 1;
			/*  TODO: the l_bit  */
			reg_access_msr(cpu, &cpu->cd.ppc.gpr[rs], 1);
			break;

		case PPC_31_LBZX:
		case PPC_31_LBZUX:
		case PPC_31_LHZX:
		case PPC_31_LHZUX:
		case PPC_31_LWZX:
		case PPC_31_LWZUX:
		case PPC_31_STBX:
		case PPC_31_STBUX:
		case PPC_31_STHX:
		case PPC_31_STHUX:
		case PPC_31_STWX:
		case PPC_31_STWUX:
			rs = (iword >> 21) & 31;
			ra = (iword >> 16) & 31;
			rb = (iword >> 11) & 31;
			update = 0;
			switch (xo) {
			case PPC_31_LBZUX:
			case PPC_31_LHZUX:
			case PPC_31_LWZUX:
			case PPC_31_STBUX:
			case PPC_31_STHUX:
			case PPC_31_STWUX:
				update = 1;
			}
			if (ra == 0)
				addr = 0;
			else
				addr = cpu->cd.ppc.gpr[ra];
			addr += cpu->cd.ppc.gpr[rb];
			load = 0;
			switch (xo) {
			case PPC_31_LBZX:
			case PPC_31_LBZUX:
			case PPC_31_LHZX:
			case PPC_31_LHZUX:
			case PPC_31_LWZX:
			case PPC_31_LWZUX:
				load = 1;
			}

			if (cpu->machine->instruction_trace) {
				if (cpu->cd.ppc.bits == 32)
					debug("\t[0x%08llx", (long long)addr);
				else
					debug("\t[0x%016llx", (long long)addr);
			}

			tmp_data_len = 4;
			switch (xo) {
			case PPC_31_LBZX:
			case PPC_31_LBZUX:
			case PPC_31_STBX:
			case PPC_31_STBUX:
				tmp_data_len = 1;
				break;
			case PPC_31_LHZX:
			case PPC_31_LHZUX:
			case PPC_31_STHX:
			case PPC_31_STHUX:
				tmp_data_len = 2;
				break;
			}

			tmp = 0;

			if (load) {
				r = cpu->memory_rw(cpu, cpu->mem, addr,
				    tmp_data, tmp_data_len, MEM_READ,
				    CACHE_DATA);
				if (r == MEMORY_ACCESS_OK) {
					if (cpu->byte_order ==
					    EMUL_BIG_ENDIAN) {
						for (i=0; i<tmp_data_len; i++) {
							tmp <<= 8;
							tmp += tmp_data[i];
						}
					} else {
						for (i=0; i<tmp_data_len; i++) {
							tmp <<= 8;
							tmp += tmp_data[
							    tmp_data_len - 1
							    - i];
						}
					}
					cpu->cd.ppc.gpr[rs] = tmp;
				}
			} else {
				tmp = cpu->cd.ppc.gpr[rs];
				if (cpu->byte_order == EMUL_BIG_ENDIAN) {
					for (i=0; i<tmp_data_len; i++)
						tmp_data[tmp_data_len-1-i] =
						    tmp >> (8*i);
				} else {
					for (i=0; i<tmp_data_len; i++)
						tmp_data[i] = tmp >> (8*i);
				}

				r = cpu->memory_rw(cpu, cpu->mem, addr,
				    tmp_data, tmp_data_len, MEM_WRITE,
				    CACHE_DATA);
			}

			if (cpu->machine->instruction_trace) {
				if (r == MEMORY_ACCESS_OK) {
					switch (tmp_data_len) {
					case 1:	debug(", data = 0x%02x]\n",
						    (int)tmp);
						break;
					case 2:	debug(", data = 0x%04x]\n",
						    (int)tmp);
						break;
					case 4:	debug(", data = 0x%08x]\n",
						    (int)tmp);
						break;
					default:debug(", data = 0x%016llx]\n",
						    (long long)tmp);
					}
				} else
					debug(", FAILED]\n");
			}

			if (r != MEMORY_ACCESS_OK) {
				/*  TODO: exception?  */
				return 0;
			}

			if (update && ra != 0)
				cpu->cd.ppc.gpr[ra] = addr;
			break;

		case PPC_31_NEG:
		case PPC_31_NEGO:
			rt = (iword >> 21) & 31;
			ra = (iword >> 16) & 31;
			oe_bit = (iword >> 10) & 1;
			rc = iword & 1;
			if (oe_bit) {
				fatal("[ neg: PPC oe not yet implemeted ]\n");
				cpu->running = 0;
				return 0;
			}
			cpu->cd.ppc.gpr[rt] = ~cpu->cd.ppc.gpr[ra] + 1;
			if (rc)
				update_cr0(cpu, cpu->cd.ppc.gpr[rt]);
			break;

		case PPC_31_ADDZE:
		case PPC_31_ADDZEO:
			rt = (iword >> 21) & 31;
			ra = (iword >> 16) & 31;
			oe_bit = (iword >> 10) & 1;
			rc = iword & 1;
			if (oe_bit) {
				fatal("[ addz: PPC oe not yet implemeted ]\n");
				cpu->running = 0;
				return 0;
			}
			old_ca = cpu->cd.ppc.xer & PPC_XER_CA;
			cpu->cd.ppc.xer &= PPC_XER_CA;
			if (cpu->cd.ppc.bits == 32) {
				tmp = (uint32_t)cpu->cd.ppc.gpr[ra];
				tmp2 = tmp;
				/*  printf("addze: tmp2 = %016llx\n",
				    (long long)tmp2);  */
				if (old_ca)
					tmp ++;
				/*  printf("addze: tmp  = %016llx\n\n",
				    (long long)tmp);  */
				/*  TODO: is this CA correct?  */
				if ((tmp >> 32) != (tmp2 >> 32))
					cpu->cd.ppc.xer |= PPC_XER_CA;
				/*  High 32 bits are probably undefined
				    in 32-bit mode (I hope)  */
				cpu->cd.ppc.gpr[rt] = tmp;
			} else {
				fatal("ADDZE 64-bit, TODO\n");
			}
			if (rc)
				update_cr0(cpu, cpu->cd.ppc.gpr[rt]);
			break;

		case PPC_31_MTSR:
			/*  Move to segment register (?)  */
			/*  TODO  */
			break;

		case PPC_31_MTSRIN:
			/*  Move to segment register indirect (?)  */
			rt = (iword >> 21) & 31;
			rb = (iword >> 11) & 31;
			/*  TODO  */
			cpu->cd.ppc.gpr[rt] = 0;
			break;

		case PPC_31_ADDC:
		case PPC_31_ADDCO:
		case PPC_31_ADDE:
		case PPC_31_ADDEO:
		case PPC_31_ADD:
		case PPC_31_ADDO:
		case PPC_31_MULHW:
		case PPC_31_MULHWU:
		case PPC_31_MULLW:
		case PPC_31_MULLWO:
		case PPC_31_SUBFE:
		case PPC_31_SUBFEO:
		case PPC_31_SUBFC:
		case PPC_31_SUBFCO:
		case PPC_31_SUBF:
		case PPC_31_SUBFO:
			rt = (iword >> 21) & 31;
			ra = (iword >> 16) & 31;
			rb = (iword >> 11) & 31;
			oe_bit = (iword >> 10) & 1;
			rc = iword & 1;
			if (oe_bit) {
				fatal("[ add: PPC oe not yet implemeted ]\n");
				cpu->running = 0;
				return 0;
			}
			switch (xo) {
			case PPC_31_ADD:
			case PPC_31_ADDO:
				cpu->cd.ppc.gpr[rt] = cpu->cd.ppc.gpr[ra] +
				    cpu->cd.ppc.gpr[rb];
				break;
			case PPC_31_ADDC:
			case PPC_31_ADDCO:
			case PPC_31_ADDE:
			case PPC_31_ADDEO:
				old_ca = cpu->cd.ppc.xer & PPC_XER_CA;
				cpu->cd.ppc.xer &= PPC_XER_CA;
				if (cpu->cd.ppc.bits == 32) {
					tmp = (uint32_t)cpu->cd.ppc.gpr[ra];
					tmp2 = tmp;
					/*  printf("adde: tmp2 = %016llx\n",
					    (long long)tmp2);  */
					tmp += (uint32_t)cpu->cd.ppc.gpr[rb];
					if ((xo == PPC_31_ADDE ||
					    xo == PPC_31_ADDEO) && old_ca)
						tmp ++;
					/*  printf("adde: tmp  = %016llx\n\n",
					    (long long)tmp);  */
					/*  TODO: is this CA correct?  */
					if ((tmp >> 32) != (tmp2 >> 32))
						cpu->cd.ppc.xer |= PPC_XER_CA;
					/*  High 32 bits are probably undefined
					    in 32-bit mode (I hope)  */
					cpu->cd.ppc.gpr[rt] = tmp;
				} else {
					fatal("ADDE 64-bit, TODO\n");
				}
				break;
			case PPC_31_MULHW:
				cpu->cd.ppc.gpr[rt] = (int64_t) (
				    (int64_t)(int32_t)cpu->cd.ppc.gpr[ra] *
				    (int64_t)(int32_t)cpu->cd.ppc.gpr[rb]);
				cpu->cd.ppc.gpr[rt] >>= 32;
				break;
			case PPC_31_MULHWU:
				cpu->cd.ppc.gpr[rt] = (uint64_t) (
				    (uint64_t)(uint32_t)cpu->cd.ppc.gpr[ra] *
				    (uint64_t)(uint32_t)cpu->cd.ppc.gpr[rb]);
				cpu->cd.ppc.gpr[rt] >>= 32;
				break;
			case PPC_31_MULLW:
			case PPC_31_MULLWO:
				cpu->cd.ppc.gpr[rt] = (int64_t) (
				    (int32_t)cpu->cd.ppc.gpr[ra] *
				    (int32_t)cpu->cd.ppc.gpr[rb]);
				break;
			case PPC_31_SUBF:
			case PPC_31_SUBFO:
				cpu->cd.ppc.gpr[rt] = ~cpu->cd.ppc.gpr[ra] +
				    cpu->cd.ppc.gpr[rb] + 1;
				break;
			case PPC_31_SUBFC:
			case PPC_31_SUBFCO:
			case PPC_31_SUBFE:
			case PPC_31_SUBFEO:
				old_ca = cpu->cd.ppc.xer & PPC_XER_CA;
				if (xo == PPC_31_SUBFC || xo == PPC_31_SUBFCO)
					old_ca = 1;
				cpu->cd.ppc.xer &= PPC_XER_CA;
				if (cpu->cd.ppc.bits == 32) {
					tmp = (~cpu->cd.ppc.gpr[ra])
					    & 0xffffffff;
					tmp2 = tmp;
					tmp += (cpu->cd.ppc.gpr[rb] &
					    0xffffffff);
					if (old_ca)
						tmp ++;
					/*  printf("subfe: tmp2 = %016llx\n",
					    (long long)tmp2);
					    printf("subfe: tmp  = %016llx\n\n",
					    (long long)tmp);  */
					/*  TODO: is this CA correct?  */
					if ((tmp >> 32) != (tmp2 >> 32))
						cpu->cd.ppc.xer |= PPC_XER_CA;
					/*  High 32 bits are probably undefined
					    in 32-bit mode (I hope)  */
					cpu->cd.ppc.gpr[rt] = tmp;
				} else {
					fatal("SUBFE 64-bit, TODO\n");
				}
				break;
			}
			if (rc)
				update_cr0(cpu, cpu->cd.ppc.gpr[rt]);
			break;

		case PPC_31_MFSPR:
		case PPC_31_MFTB:
			rt = (iword >> 21) & 31;
			spr = ((iword >> 6) & 0x3e0) + ((iword >> 16) & 31);
			switch (spr) {
			case 1:	cpu->cd.ppc.gpr[rt] = cpu->cd.ppc.xer;
				break;
			case 8:	cpu->cd.ppc.gpr[rt] = cpu->cd.ppc.lr;
				break;
			case 9:	cpu->cd.ppc.gpr[rt] = cpu->cd.ppc.ctr;
				break;
			case 22:/*  TODO: check pr  */
				cpu->cd.ppc.gpr[rt] = cpu->cd.ppc.dec;
				break;
			case 259:	/*  NOTE: no pr check  */
				cpu->cd.ppc.gpr[rt] = cpu->cd.ppc.sprg3;
				break;
			case 268:	/*  MFTB, NOTE: no pr check  */
				cpu->cd.ppc.gpr[rt] = cpu->cd.ppc.tbl;
				break;
			case 269:	/*  MFTBU, NOTE: no pr check  */
				cpu->cd.ppc.gpr[rt] = cpu->cd.ppc.tbu;
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
			case 310:/*  TODO: check pr  */
				cpu->cd.ppc.gpr[rt] = cpu->cd.ppc.hdec;
				break;
			case 1023:
				/*  TODO: check pr  */
				cpu->cd.ppc.gpr[rt] = cpu->cd.ppc.pir;
				break;
			default:
				fatal("[ unimplemented PPC spr 0x%04x, "
				    "pc = 0x%016llx ]\n",
				    spr, (long long) (cpu->cd.ppc.pc_last));
				/*  cpu->running = 0;
				return 0;  */
				break;
			}
			break;

		case PPC_31_CNTLZW:
			rs = (iword >> 21) & 31;
			ra = (iword >> 16) & 31;
			rc = iword & 1;
			cpu->cd.ppc.gpr[ra] = 0;
			for (i=0; i<32; i++) {
				if (cpu->cd.ppc.gpr[rs] &
				    ((uint64_t)1 << (31-i)))
					break;
				cpu->cd.ppc.gpr[ra] ++;
			}
			if (rc)
				update_cr0(cpu, cpu->cd.ppc.gpr[ra]);
			break;

		case PPC_31_SLW:
		case PPC_31_SRW:
		case PPC_31_AND:
		case PPC_31_ANDC:
		case PPC_31_NOR:
		case PPC_31_OR:
		case PPC_31_ORC:
		case PPC_31_XOR:
		case PPC_31_NAND:
			rs = (iword >> 21) & 31;
			ra = (iword >> 16) & 31;
			rb = (iword >> 11) & 31;
			rc = iword & 1;
			switch (xo) {
			case PPC_31_SLW:
				sh = cpu->cd.ppc.gpr[rb] & 0x3f;
				cpu->cd.ppc.gpr[ra] = cpu->cd.ppc.gpr[rs];
				while (sh-- > 0)
					cpu->cd.ppc.gpr[ra] <<= 1;
				cpu->cd.ppc.gpr[ra] &= 0xffffffff;
				break;
			case PPC_31_SRW:
				sh = cpu->cd.ppc.gpr[rb] & 0x3f;
				cpu->cd.ppc.gpr[ra] = cpu->cd.ppc.gpr[rs]
				    & 0xffffffff;
				while (sh-- > 0)
					cpu->cd.ppc.gpr[ra] >>= 1;
				break;
			case PPC_31_AND:
				cpu->cd.ppc.gpr[ra] = cpu->cd.ppc.gpr[rs] &
				    cpu->cd.ppc.gpr[rb];
				break;
			case PPC_31_ANDC:
				cpu->cd.ppc.gpr[ra] = cpu->cd.ppc.gpr[rs] &
				    (~cpu->cd.ppc.gpr[rb]);
				break;
			case PPC_31_NOR:
				cpu->cd.ppc.gpr[ra] = ~(cpu->cd.ppc.gpr[rs] |
				    cpu->cd.ppc.gpr[rb]);
				break;
			case PPC_31_OR:
				cpu->cd.ppc.gpr[ra] = cpu->cd.ppc.gpr[rs] |
				    cpu->cd.ppc.gpr[rb];
				break;
			case PPC_31_ORC:
				cpu->cd.ppc.gpr[ra] = cpu->cd.ppc.gpr[rs] |
				    (~cpu->cd.ppc.gpr[rb]);
				break;
			case PPC_31_XOR:
				cpu->cd.ppc.gpr[ra] = cpu->cd.ppc.gpr[rs] ^
				    cpu->cd.ppc.gpr[rb];
				break;
			case PPC_31_NAND:
				cpu->cd.ppc.gpr[ra] = ~(cpu->cd.ppc.gpr[rs]
				    & cpu->cd.ppc.gpr[rb]);
				break;
			}
			if (rc)
				update_cr0(cpu, cpu->cd.ppc.gpr[ra]);
			break;

		case PPC_31_TLBIE:
			rb = (iword >> 11) & 31;
			/*  TODO  */
			break;

		case PPC_31_TLBSYNC:
			/*  Only on 603 and 604 (?)  */

			/*  TODO  */
			break;

		case PPC_31_DCCCI:
		case PPC_31_ICCCI:
			/*  Supervisor IBM 4xx Data Cache Congruence Class
			    Invalidate, see www.xilinx.com/publications/
			    xcellonline/partners/xc_pdf/xc_ibm_pwrpc42.pdf
			    or similar  */
			/*  ICCCI is probably Instruction... blah blah  */
			/*  TODO  */
			break;

		case PPC_31_DIVWU:
		case PPC_31_DIVWUO:
		case PPC_31_DIVW:
		case PPC_31_DIVWO:
			rt = (iword >> 21) & 31;
			ra = (iword >> 16) & 31;
			rb = (iword >> 11) & 31;
			oe_bit = (iword >> 10) & 1;
			rc = iword & 1;
			switch (xo) {
			case PPC_31_DIVWU:
			case PPC_31_DIVWUO:
				tmp = cpu->cd.ppc.gpr[ra] & 0xffffffff;
				tmp2 = cpu->cd.ppc.gpr[rb] & 0xffffffff;
				if (tmp2 == 0) {
					/*  Undefined:  */
					tmp = 0;
				} else {
					tmp = tmp / tmp2;
				}
				cpu->cd.ppc.gpr[rt] = (int64_t)(int32_t)tmp;
				break;
			case PPC_31_DIVW:
			case PPC_31_DIVWO:
				tmp = (int64_t)(int32_t)cpu->cd.ppc.gpr[ra];
				tmp2 = (int64_t)(int32_t)cpu->cd.ppc.gpr[rb];
				if (tmp2 == 0) {
					/*  Undefined:  */
					tmp = 0;
				} else {
					tmp = (int64_t)tmp / (int64_t)tmp2;
				}
				cpu->cd.ppc.gpr[rt] = (int64_t)(int32_t)tmp;
				break;
			}
			if (rc)
				update_cr0(cpu, cpu->cd.ppc.gpr[rt]);
			if (oe_bit) {
				fatal("[ divwu: PPC oe not yet implemeted ]\n");
				cpu->running = 0;
				return 0;
			}
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
			case 22:	/*  TODO: check pr  */
				cpu->cd.ppc.dec = cpu->cd.ppc.gpr[rs];
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
			case 284:
				/*  TODO: check pr  */
				cpu->cd.ppc.tbl = cpu->cd.ppc.gpr[rs];
				break;
			case 285:
				/*  TODO: check pr  */
				cpu->cd.ppc.tbu = cpu->cd.ppc.gpr[rs];
				break;
			case 287:
				fatal("[ PPC: attempt to write to PVR ]\n");
				break;
			case 310:	/*  TODO: check hypv  */
				cpu->cd.ppc.hdec = cpu->cd.ppc.gpr[rs];
				break;
			case 1023:
				/*  TODO: check pr  */
				cpu->cd.ppc.pir = cpu->cd.ppc.gpr[rs];
				break;
			default:
				fatal("[ unimplemented PPC spr 0x%04x, "
				    "pc = 0x%016llx ]\n",
				    spr, (long long) (cpu->cd.ppc.pc_last));
				/*  cpu->running = 0;
				return 0;  */
				break;
			}
			break;

		case PPC_31_SYNC:
			/*  TODO: actually sync  */
			break;

		case PPC_31_STSWI:
			rs = (iword >> 21) & 31;
			ra = (iword >> 16) & 31;
			nb = (iword >> 11) & 31;
			if (nb == 0)
				nb = 32;
			if (ra == 0)
				addr = 0;
			else
				addr = cpu->cd.ppc.gpr[ra];

			if (cpu->machine->instruction_trace) {
				if (cpu->cd.ppc.bits == 32)
					debug("\t[0x%08llx", (long long)addr);
				else
					debug("\t[0x%016llx", (long long)addr);
			}

			i = 24;
			r = 0;	/*  There can be multiple errors  */
			while (nb > 0) {
				tmp_data[0] = cpu->cd.ppc.gpr[rs] >> i;
				if (cpu->memory_rw(cpu, cpu->mem, addr,
				    tmp_data, 1, MEM_WRITE, CACHE_DATA)
				    != MEMORY_ACCESS_OK)
					r++;
				nb--; addr++; i-=8;
				if (i < 0) {
					i = 24;
					rs = (rs + 1) % 32;
				}
			}

			if (cpu->machine->instruction_trace) {
				if (r == 0)
					debug(", ...]\n");
				else
					debug(", FAILED]\n");
			}

			if (r > 0) {
				/*  TODO: exception  */
				return 0;
			}
			break;

		case PPC_31_SRAWI:
			rs = (iword >> 21) & 31;
			ra = (iword >> 16) & 31;
			sh = (iword >> 11) & 31;
			rc = iword & 1;
			tmp = cpu->cd.ppc.gpr[rs] & 0xffffffff;
			cpu->cd.ppc.xer &= ~PPC_XER_CA;
			i = 0;
			if (tmp & 0x80000000)
				i = 1;
			while (sh-- > 0) {
				if (tmp & 1)
					i++;
				tmp >>= 1;
				if (tmp & 0x40000000)
					tmp |= 0x80000000;
			}
			cpu->cd.ppc.gpr[ra] = (int64_t)(int32_t)tmp;
			/*  Set the CA bit if rs contained a negative
			    number to begin with, and any 1-bits were
			    shifted out:  */
			if (i > 1)
				cpu->cd.ppc.xer |= PPC_XER_CA;
			if (rc)
				update_cr0(cpu, cpu->cd.ppc.gpr[ra]);
			break;

		case PPC_31_EIEIO:
			/*  TODO: actually eieio  */
			break;

		case PPC_31_EXTSB:
		case PPC_31_EXTSH:
		case PPC_31_EXTSW:
			rs = (iword >> 21) & 31;
			ra = (iword >> 16) & 31;
			rc = iword & 1;
			switch (xo) {
			case PPC_31_EXTSB:
				cpu->cd.ppc.gpr[ra] = (int64_t)
				    (int8_t)cpu->cd.ppc.gpr[rs];
				break;
			case PPC_31_EXTSH:
				cpu->cd.ppc.gpr[ra] = (int64_t)
				    (int16_t)cpu->cd.ppc.gpr[rs];
				break;
			case PPC_31_EXTSW:
				cpu->cd.ppc.gpr[ra] = (int64_t)
				    (int32_t)cpu->cd.ppc.gpr[rs];
				break;
			}
			if (rc)
				update_cr0(cpu, cpu->cd.ppc.gpr[ra]);
			break;

		default:
			fatal("[ unimplemented PPC hi6_31, xo = 0x%04x, "
			    "pc = 0x%016llx ]\n",
			    xo, (long long) (cpu->cd.ppc.pc_last));
			cpu->running = 0;
			return 0;
		}
		break;

	case PPC_HI6_LWZ:
	case PPC_HI6_LWZU:
	case PPC_HI6_LHZ:
	case PPC_HI6_LHZU:
	case PPC_HI6_LHA:
	case PPC_HI6_LHAU:
	case PPC_HI6_LBZ:
	case PPC_HI6_LBZU:
	case PPC_HI6_STW:
	case PPC_HI6_STWU:
	case PPC_HI6_STH:
	case PPC_HI6_STHU:
	case PPC_HI6_STB:
	case PPC_HI6_STBU:
	case PPC_HI6_LFD:
	case PPC_HI6_STFD:
		/*  NOTE: Loads use rt, not rs, but are otherwise similar
		    to stores. This code uses rs for both.  */
		rs = (iword >> 21) & 31;
		ra = (iword >> 16) & 31;
		imm = (int16_t)(iword & 0xffff);

		fpreg = 0; load = 1; update = 0; tmp_data_len = 4;
		arithflag = 0;

		switch (hi6) {
		case PPC_HI6_LWZU:
		case PPC_HI6_LHZU:
		case PPC_HI6_LHAU:
		case PPC_HI6_LBZU:
		case PPC_HI6_STBU:
		case PPC_HI6_STHU:
		case PPC_HI6_STWU:
			update = 1;
		}

		switch (hi6) {
		case PPC_HI6_STW:
		case PPC_HI6_STWU:
		case PPC_HI6_STH:
		case PPC_HI6_STHU:
		case PPC_HI6_STB:
		case PPC_HI6_STBU:
		case PPC_HI6_STFD:
			load = 0;
		}

		switch (hi6) {
		case PPC_HI6_LFD:
		case PPC_HI6_STFD:
			tmp_data_len = 8;
			break;
		case PPC_HI6_LBZ:
		case PPC_HI6_LBZU:
		case PPC_HI6_STB:
		case PPC_HI6_STBU:
			tmp_data_len = 1;
			break;
		case PPC_HI6_LHZ:
		case PPC_HI6_LHZU:
		case PPC_HI6_LHA:
		case PPC_HI6_LHAU:
		case PPC_HI6_STH:
		case PPC_HI6_STHU:
			tmp_data_len = 2;
			break;
		}

		switch (hi6) {
		case PPC_HI6_LFD:
		case PPC_HI6_STFD:
			fpreg = 1;
		}

		switch (hi6) {
		case PPC_HI6_LHA:
		case PPC_HI6_LHAU:
			arithflag = 1;
		}

		if (ra == 0) {
			if (update)
				fatal("[ PPC WARNING: invalid Update form ]\n");
			addr = 0;
		} else
			addr = cpu->cd.ppc.gpr[ra];

		if (load && update && ra == rs)
			fatal("[ PPC WARNING: invalid Update load form ]\n");

		addr += imm;

		/*  TODO: alignment check?  */

		if (cpu->machine->instruction_trace) {
			if (cpu->cd.ppc.bits == 32)
				debug("\t[0x%08llx", (long long)addr);
			else
				debug("\t[0x%016llx", (long long)addr);
		}

		if (load) {
			r = cpu->memory_rw(cpu, cpu->mem, addr, tmp_data,
			    tmp_data_len, MEM_READ, CACHE_DATA);

			if (r == MEMORY_ACCESS_OK) {
				tmp = 0;
				if (arithflag) {
					if (cpu->byte_order ==
					    EMUL_BIG_ENDIAN) {
						if (tmp_data[0] & 0x80)
							tmp --;
					} else {
						if (tmp_data[tmp_data_len-1]
						    & 0x80)
							tmp --;
					}
				}
				if (cpu->byte_order == EMUL_BIG_ENDIAN) {
					for (i=0; i<tmp_data_len; i++) {
						tmp <<= 8;
						tmp += tmp_data[i];
					}
				} else {
					for (i=0; i<tmp_data_len; i++) {
						tmp <<= 8;
						tmp += tmp_data[
						    tmp_data_len - 1 -i];
					}
				}

				if (!fpreg)
					cpu->cd.ppc.gpr[rs] = tmp;
				else
					cpu->cd.ppc.fpr[rs] = tmp;
			}
		} else {
			if (!fpreg)
				tmp = cpu->cd.ppc.gpr[rs];
			else
				tmp = cpu->cd.ppc.fpr[rs];

			if (cpu->byte_order == EMUL_BIG_ENDIAN) {
				for (i=0; i<tmp_data_len; i++)
					tmp_data[tmp_data_len-1-i] =
					    tmp >> (8*i);
			} else {
				for (i=0; i<tmp_data_len; i++)
					tmp_data[i] = tmp >> (8*i);
			}

			r = cpu->memory_rw(cpu, cpu->mem, addr, tmp_data,
			    tmp_data_len, MEM_WRITE, CACHE_DATA);
		}

		if (cpu->machine->instruction_trace) {
			if (r == MEMORY_ACCESS_OK) {
				switch (tmp_data_len) {
				case 1:	debug(", data = 0x%02x]\n", (int)tmp);
					break;
				case 2:	debug(", data = 0x%04x]\n", (int)tmp);
					break;
				case 4:	debug(", data = 0x%08x]\n", (int)tmp);
					break;
				default:debug(", data = 0x%016llx]\n",
					    (long long)tmp);
				}
			} else
				debug(", FAILED]\n");
		}

		if (r != MEMORY_ACCESS_OK) {
			/*  TODO: exception?  */
			return 0;
		}

		if (update && ra != 0)
			cpu->cd.ppc.gpr[ra] = addr;
		break;

	case PPC_HI6_LMW:
	case PPC_HI6_STMW:
		/*  NOTE: Loads use rt, not rs, but are otherwise similar
		    to stores. This code uses rs for both.  */
		rs = (iword >> 21) & 31;
		ra = (iword >> 16) & 31;
		imm = (int16_t)(iword & 0xffff);

		load = 1; tmp_data_len = 4;

		switch (hi6) {
		case PPC_HI6_STMW:
			load = 0;
		}

		if (ra == 0) {
			addr = 0;
		} else
			addr = cpu->cd.ppc.gpr[ra];

		if (load && rs == 0)
			fatal("[ PPC WARNING: invalid LMW form ]\n");

		addr += imm;

		/*  TODO: alignment check?  */

		if (cpu->machine->instruction_trace) {
			if (cpu->cd.ppc.bits == 32)
				debug("\t[0x%08llx", (long long)addr);
			else
				debug("\t[0x%016llx", (long long)addr);
		}

		/*  There can be multiple errors!  */
		r = 0;

		while (rs <= 31) {
			if (load) {
				if (cpu->memory_rw(cpu, cpu->mem, addr,
				    tmp_data, tmp_data_len, MEM_READ,
				    CACHE_DATA) != MEMORY_ACCESS_OK)
					r++;

				if (r == 0) {
					tmp = 0;
					if (cpu->byte_order ==
					    EMUL_BIG_ENDIAN) {
						for (i=0; i<tmp_data_len; i++) {
							tmp <<= 8;
							tmp += tmp_data[i];
						}
					} else {
						for (i=0; i<tmp_data_len; i++) {
							tmp <<= 8;
							tmp += tmp_data[
							    tmp_data_len - 1
							    - i];
						}
					}

					cpu->cd.ppc.gpr[rs] = tmp;
				}
			} else {
				tmp = cpu->cd.ppc.gpr[rs];

				if (cpu->byte_order == EMUL_BIG_ENDIAN) {
					for (i=0; i<tmp_data_len; i++)
						tmp_data[tmp_data_len-1-i] =
						    tmp >> (8*i);
				} else {
					for (i=0; i<tmp_data_len; i++)
						tmp_data[i] = tmp >> (8*i);
				}

				if (cpu->memory_rw(cpu, cpu->mem, addr,
				    tmp_data, tmp_data_len, MEM_WRITE,
				    CACHE_DATA) != MEMORY_ACCESS_OK)
					r ++;
			}

			/*  TODO: Exception!  */

			/*  Go to next register, multiword...  */
			rs ++;
			addr += tmp_data_len;
		}

		if (cpu->machine->instruction_trace) {
			if (r == 0) {
				debug(", data = ...]\n");
			} else
				debug(", FAILED]\n");
		}

		if (r > 0)
			return 0;
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
