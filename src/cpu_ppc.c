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
 *  $Id: cpu_ppc.c,v 1.11 2005-02-02 18:45:25 debug Exp $
 *
 *  PowerPC/POWER CPU emulation.
 *
 *  TODO: This is just a dummy so far.
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
	cpu->cd.ppc.cpu_type    = cpu_type_defs[found];
	cpu->name               = cpu->cd.ppc.cpu_type.name;
	cpu->mem                = mem;
	cpu->machine            = machine;
	cpu->cpu_id             = cpu_id;
	cpu->byte_order         = EMUL_BIG_ENDIAN;
	cpu->bootstrap_cpu_flag = 0;
	cpu->running            = 0;
	cpu->cd.ppc.bits        = 64;	/*  TODO: how about 32-bit CPUs?  */
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
	uint64_t offset;
	int i, x = cpu->cpu_id;
	int bits32 = cpu->cd.ppc.bits == 32;

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
void ppc_cpu_disassemble_instr(struct cpu *cpu, unsigned char *instr,
	int running, uint64_t dumpaddr, int bintrans)
{
	int hi6, xo, lev, rt, rs, ra, imm, sh, me, rc;
	uint64_t addr, offset;
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

	/*
	 *  NOTE/TODO: The code in debugger.c reverses byte order, because
	 *  it was written for little-endian mips first. Hm. This is ugly.
	 */
	iword = (instr[3] << 24) + (instr[2] << 16) + (instr[1] << 8)
	    + instr[0];

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
	default:
		/*  TODO  */
		debug("unimplemented hi6 = 0x%02x", hi6);
	}

disasm_ret:
	debug("\n");
}


/*
 *  ppc_cpu_run_instr():
 *  
 *  Execute one instruction on a cpu.
 *
 *  Return value is the number of instructions executed during this call,
 *  0 if no instruction was executed.
 */
int ppc_cpu_run_instr(struct emul *emul, struct cpu *cpu)
{
	uint32_t iword;
	unsigned char buf[4];
	int r, hi6, rt, rs, ra, imm;
	uint64_t tmp;
	uint64_t cached_pc = cpu->cd.ppc.pc & ~3;

	r = memory_rw(cpu, cpu->mem, cached_pc, &buf[0], sizeof(buf),
	    MEM_READ, CACHE_INSTRUCTION | PHYSICAL);
	if (!r)
		return 0;

	iword = (buf[0] << 24) ^ (buf[1] << 16) + (buf[2] << 8) | buf[3];

	if (cpu->machine->instruction_trace) {
		/*  TODO: Yuck.  */
		int tmp = buf[0]; buf[0] = buf[3]; buf[3] = tmp;
		tmp = buf[1]; buf[1] = buf[2]; buf[2] = tmp;
		ppc_cpu_disassemble_instr(cpu, buf, 1, 0, 0);
	}

	cpu->cd.ppc.pc += sizeof(iword);
	cached_pc += sizeof(iword);

	hi6 = iword >> 26;

	switch (hi6) {

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
		cpu->cd.ppc.gpr[rt] += imm;
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

	default:
		fatal("[ unimplemented PPC hi6 = 0x%02x, pc = 0x%016llx ]\n",
		    hi6, (long long) (cached_pc - sizeof(iword)));
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
	return 1;
}


#endif	/*  ENABLE_PPC  */
