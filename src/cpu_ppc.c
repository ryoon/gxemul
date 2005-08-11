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
 *  $Id: cpu_ppc.c,v 1.80 2005-08-11 20:29:29 debug Exp $
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

#define	DYNTRANS_DUALMODE_32
#define DYNTRANS_32
#include "tmp_ppc_head.c"


/*
 *  ppc_cpu_new():
 *
 *  Create a new PPC cpu object.
 *
 *  Returns 1 on success, 0 if there was no matching PPC processor with
 *  this cpu_type_name.
 */
int ppc_cpu_new(struct cpu *cpu, struct memory *mem, struct machine *machine,
	int cpu_id, char *cpu_type_name)
{
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
		return 0;

	cpu->memory_rw = ppc_memory_rw;
	cpu->update_translation_table = ppc_update_translation_table;
	cpu->invalidate_translation_caches_paddr =
	    ppc_invalidate_translation_caches_paddr;
	cpu->invalidate_code_translation_caches =
	    ppc_invalidate_code_translation_caches;

	cpu->cd.ppc.cpu_type    = cpu_type_defs[found];
	cpu->name               = cpu->cd.ppc.cpu_type.name;
	cpu->byte_order         = EMUL_BIG_ENDIAN;
	cpu->cd.ppc.mode        = MODE_PPC;	/*  TODO  */
	cpu->cd.ppc.of_emul_addr = 0xff000000;	/*  TODO  */

	/*  Current operating mode:  */
	cpu->cd.ppc.bits        = cpu->cd.ppc.cpu_type.bits;

	cpu->is_32bit = (cpu->cd.ppc.bits == 32)? 1 : 0;

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

	return 1;
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
 *  gprs: set to non-zero to dump GPRs and some special-purpose registers.
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
				int r = (i >> 1) + ((i & 1) << 4);
				if ((i % 2) == 0)
					debug("cpu%i:", x);
				debug(" r%02i = 0x%016llx ", r,
				    (long long)cpu->cd.ppc.gpr[r]);
				if ((i % 2) == 1)
					debug("\n");
			}
		}

		/*  Other special registers:  */
		reg_access_msr(cpu, &tmp, 0);
		debug("cpu%i: msr = 0x%016llx  ", x, (long long)tmp);
		debug("tb  = 0x%08x%08x\n",
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
 *  ppc_cpu_show_full_statistics():
 *
 *  Show detailed statistics on opcode usage on each cpu.
 */
void ppc_cpu_show_full_statistics(struct machine *m)
{
	fatal("ppc_cpu_show_full_statistics(): TODO\n");
}


/*
 *  ppc_cpu_tlbdump():
 *
 *  Called from the debugger to dump the TLB in a readable format.
 *  x is the cpu number to dump, or -1 to dump all CPUs.
 *
 *  If rawflag is nonzero, then the TLB contents isn't formated nicely,
 *  just dumped.
 */
void ppc_cpu_tlbdump(struct machine *m, int x, int rawflag)
{
	fatal("ppc_cpu_tlbdump(): TODO\n");
}


/*
 *  ppc_cpu_interrupt():
 */
int ppc_cpu_interrupt(struct cpu *cpu, uint64_t irq_nr)
{
	fatal("ppc_cpu_interrupt(): TODO\n");
	return 0;
}


/*
 *  ppc_cpu_interrupt_ack():
 */
int ppc_cpu_interrupt_ack(struct cpu *cpu, uint64_t irq_nr)
{
	/*  fatal("ppc_cpu_interrupt_ack(): TODO\n");  */
	return 0;
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
		case PPC_31_MFSRIN:
			/*  Move to/from segment register indirect  */
			rt = (iword >> 21) & 31;
			rb = (iword >> 11) & 31;
			switch (xo) {
			case PPC_31_MTSRIN:  mnem = "mtsrin"; break;
			case PPC_31_MFSRIN:  mnem = "mfsrin"; break;
			}
			debug("%s\tr%i,r%i", mnem, rt, rb);
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
		case PPC_31_SUBFZE:
		case PPC_31_SUBFZEO:
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
			case PPC_31_SUBFZE:
				mnem = power? "sfze" : "subfze";
				break;
			case PPC_31_SUBFZEO:
				mnem = power? "sfzeo" : "subfzeo";
				break;
			}
			debug("%s%s\tr%i,r%i,r%i", mnem, rc? "." : "",
			    rt, ra, rb);
			break;
		case PPC_31_MFSPR:
			rt = (iword >> 21) & 31;
			spr = ((iword >> 6) & 0x3e0) + ((iword >> 16) & 31);
			switch (spr) {
			case 8:	debug("mflr\tr%i", rt);
				break;
			default:debug("mfspr\tr%i,spr%i", rt, spr);
			}
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
		case PPC_31_SRAW:
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
				case PPC_31_SRAW:  mnem =
					power? "sra" : "sraw"; break;
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
		case PPC_31_LSWI:
		case PPC_31_STSWI:
			rs = (iword >> 21) & 31;	/*  lwsi uses rt  */
			ra = (iword >> 16) & 31;
			nb = (iword >> 11) & 31;
			switch (xo) {
			case PPC_31_LSWI:
				mnem = power? "lsi" : "lswi"; break;
			case PPC_31_STSWI:
				mnem = power? "stsi" : "stswi"; break;
			}
			debug("%s\tr%i,r%i,%i", mnem, rs, ra, nb);
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
		break;
	default:
		/*  TODO  */
		debug("unimplemented hi6 = 0x%02x", hi6);
	}

	debug("\n");
	return sizeof(iword);
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


#include "tmp_ppc_tail.c"


#endif	/*  ENABLE_PPC  */
