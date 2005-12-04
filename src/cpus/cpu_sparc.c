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
 *  $Id: cpu_sparc.c,v 1.8 2005-12-04 03:12:07 debug Exp $
 *
 *  SPARC CPU emulation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "cpu.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"
#include "symbol.h"


#define	DYNTRANS_DUALMODE_32
/*  #define DYNTRANS_32  */
#include "tmp_sparc_head.c"


static char *sparc_regnames[N_SPARC_REG] = SPARC_REG_NAMES;
static char *sparc_branch_names[N_SPARC_BRANCH_TYPES] = SPARC_BRANCH_NAMES;
static char *sparc_alu_names[N_ALU_INSTR_TYPES] = SPARC_ALU_NAMES;
static char *sparc_loadstore_names[N_LOADSTORE_TYPES] = SPARC_LOADSTORE_NAMES;


/*
 *  sparc_cpu_new():
 *
 *  Create a new SPARC cpu object.
 *
 *  Returns 1 on success, 0 if there was no matching SPARC processor with
 *  this cpu_type_name.
 */
int sparc_cpu_new(struct cpu *cpu, struct memory *mem, struct machine *machine,
	int cpu_id, char *cpu_type_name)
{
	if (strcasecmp(cpu_type_name, "SPARCv9") != 0)
		return 0;

	cpu->memory_rw = sparc_memory_rw;
	cpu->update_translation_table = sparc_update_translation_table;
	cpu->invalidate_translation_caches =
	    sparc_invalidate_translation_caches;
	cpu->invalidate_code_translation =
	    sparc_invalidate_code_translation;

	cpu->byte_order = EMUL_BIG_ENDIAN;
	cpu->is_32bit = 0;

	/*  Only show name and caches etc for CPU nr 0 (in SMP machines):  */
	if (cpu_id == 0) {
		debug("%s", cpu->name);
	}

	return 1;
}


/*
 *  sparc_cpu_list_available_types():
 *
 *  Print a list of available SPARC CPU types.
 */
void sparc_cpu_list_available_types(void)
{
	debug("SPARCv9\n");
	/*  TODO  */
}


/*
 *  sparc_cpu_dumpinfo():
 */
void sparc_cpu_dumpinfo(struct cpu *cpu)
{
	debug("\n");
	/*  TODO  */
}


/*
 *  sparc_cpu_register_dump():
 *
 *  Dump cpu registers in a relatively readable format.
 *
 *  gprs: set to non-zero to dump GPRs and some special-purpose registers.
 *  coprocs: set bit 0..3 to dump registers in coproc 0..3.
 */
void sparc_cpu_register_dump(struct cpu *cpu, int gprs, int coprocs)
{
	char *symbol;
	uint64_t offset;
	int i, x = cpu->cpu_id;
	int bits32 = cpu->is_32bit;

	if (gprs) {
		/*  Special registers (pc, ...) first:  */
		symbol = get_symbol_name(&cpu->machine->symbol_context,
		    cpu->pc, &offset);

		debug("cpu%i: pc = 0x", x);
		if (bits32)
			debug("%08x", (int)cpu->pc);
		else
			debug("%016llx", (long long)cpu->pc);
		debug("  <%s>\n", symbol != NULL? symbol : " no symbol ");

		if (bits32) {
			for (i=0; i<N_SPARC_REG; i++) {
				if ((i & 3) == 0)
					debug("cpu%i: ", x);
				/*  Skip the zero register:  */
				if (i==0) {
					debug("               ");
					continue;
				}
				debug("%s=", sparc_regnames[i]);
				debug("0x%08x", (int) cpu->cd.sparc.r[i]);
				if ((i & 3) < 3)
					debug("  ");
				else
					debug("\n");
			}
		} else {
			for (i=0; i<N_SPARC_REG; i++) {
				if ((i & 1) == 0)
					debug("cpu%i: ", x);
				/*  Skip the zero register:  */
				if (i==0) {
					debug("                         ");
					continue;
				}
				debug("%s = ", sparc_regnames[i]);
				debug("0x%016llx", (long long)
				    cpu->cd.sparc.r[i]);
				if ((i & 1) < 1)
					debug("  ");
				else
					debug("\n");
			}
		}
	}
}


/*
 *  sparc_cpu_register_match():
 */
void sparc_cpu_register_match(struct machine *m, char *name,
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
	}
}


/*
 *  sparc_cpu_interrupt():
 */
int sparc_cpu_interrupt(struct cpu *cpu, uint64_t irq_nr)
{
	fatal("sparc_cpu_interrupt(): TODO\n");
	return 0;
}


/*
 *  sparc_cpu_interrupt_ack():
 */
int sparc_cpu_interrupt_ack(struct cpu *cpu, uint64_t irq_nr)
{
	/*  fatal("sparc_cpu_interrupt_ack(): TODO\n");  */
	return 0;
}


/*
 *  sparc_cpu_disassemble_instr():
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
int sparc_cpu_disassemble_instr(struct cpu *cpu, unsigned char *instr,
	int running, uint64_t dumpaddr, int bintrans)
{
	uint64_t offset, tmp;
	uint32_t iword;
	int hi2, op2, rd, rs1, rs2, siconst, btype, tmps, no_rd = 0;
	int asi, no_rs1 = 0, no_rs2 = 0, jmpl = 0, shift_x = 0;
	char *symbol, *mnem;

	if (running)
		dumpaddr = cpu->pc;

	symbol = get_symbol_name(&cpu->machine->symbol_context,
	    dumpaddr, &offset);
	if (symbol != NULL && offset==0)
		debug("<%s>\n", symbol);

	if (cpu->machine->ncpus > 1 && running)
		debug("cpu%i: ", cpu->cpu_id);

	if (cpu->is_32bit == 32)
		debug("%08x", (int)dumpaddr);
	else
		debug("%016llx", (long long)dumpaddr);

	iword = *(uint32_t *)&instr[0];
	iword = BE32_TO_HOST(iword);

	debug(": %08x\t", iword);

	/*
	 *  Decode the instruction:
	 *
	 *  http://www.cs.unm.edu/~maccabe/classes/341/labman/node9.html is a
	 *  good quick description of SPARC instruction encoding.
	 */

	hi2 = iword >> 30;
	rd = (iword >> 25) & 31;
	btype = rd & (N_SPARC_BRANCH_TYPES - 1);
	rs1 = (iword >> 14) & 31;
	asi = (iword >> 5) & 0xff;
	rs2 = iword & 31;
	siconst = (int16_t)((iword & 0x1fff) << 3) >> 3;
	op2 = (hi2 == 0)? ((iword >> 22) & 7) : ((iword >> 19) & 0x3f);

	switch (hi2) {

	case 0:	switch (op2) {

		case 0:	debug("illtrap\t0x%x", iword & 0x3fffff);
			break;

		case 1:
		case 2:	debug("%s", sparc_branch_names[btype]);
			if (rd & 16)
				debug(",a");
			tmps = iword;
			if (op2 == 2) {
				tmps <<= 10;
				tmps >>= 8;
				debug("\t");
			} else {
				int cc = (iword >> 20) & 3;
				int p = (iword >> 19) & 1;
				tmps <<= 13;
				tmps >>= 11;
				if (!p)
					debug(",pn");
				debug("\t%%%s,", cc==0 ? "icc" :
				    (cc==2 ? "xcc" : "UNKNOWN"));
			}
			tmp = (int64_t)(int32_t)tmps;
			tmp += dumpaddr;
			debug("0x%llx", (long long)tmp);
			symbol = get_symbol_name(&cpu->machine->
			    symbol_context, tmp, &offset);
			if (symbol != NULL)
				debug(" \t<%s>", symbol);
			break;

		case 4:	if (rd == 0) {
				debug("nop");
				break;
			}
			debug("sethi\t%%hi(0x%x),", (iword & 0x3fffff) << 10);
			debug("%%%s", sparc_regnames[rd]);
			break;

		default:debug("UNIMPLEMENTED hi2=%i, op2=0x%x", hi2, op2);
		}
		break;

	case 1:	tmp = (int32_t)iword << 2;
		tmp += dumpaddr;
		debug("call\t0x%llx", (long long)tmp);
		symbol = get_symbol_name(&cpu->machine->symbol_context,
		    tmp, &offset);
		if (symbol != NULL)
			debug(" \t<%s>", symbol);
		break;

	case 2:	mnem = sparc_alu_names[op2];
		switch (op2) {
		case 0:	/*  add  */
			if (rd == rs1 && (iword & 0x3fff) == 0x2001) {
				mnem = "inc";
				no_rs1 = no_rs2 = 1;
			}
			break;
		case 2:	/*  or  */
			if (rs1 == 0) {
				mnem = "mov";
				no_rs1 = 1;
			}
			break;
		case 4:	/*  sub  */
			if (rd == rs1 && (iword & 0x3fff) == 0x2001) {
				mnem = "dec";
				no_rs1 = no_rs2 = 1;
			}
			break;
		case 20:/*  subcc  */
			if (rd == 0) {
				mnem = "cmp";
				no_rd = 1;
			}
			break;
		case 37:/*  sll  */
		case 38:/*  srl  */
		case 39:/*  sra  */
			if (siconst & 0x1000) {
				siconst &= 0x3f;
				shift_x = 1;
			} else
				siconst &= 0x1f;
			break;
		case 56:/*  jmpl  */
			jmpl = 1;
			if (iword == 0x81c7e008) {
				mnem = "ret";
				no_rs1 = no_rs2 = no_rd = 1;
			}
			if (iword == 0x81c3e008) {
				mnem = "retl";
				no_rs1 = no_rs2 = no_rd = 1;
			}
			break;
		case 61:/*  restore  */
			if (iword == 0x81e80000)
				no_rs1 = no_rs2 = no_rd = 1;
			break;
		case 62:if (iword == 0x83f00000) {
				mnem = "retry";
				no_rs1 = no_rs2 = no_rd = 1;
			}
			break;
		}
		debug("%s", mnem);
		if (shift_x)
			debug("x");
		debug("\t");
		if (!no_rs1)
			debug("%%%s", sparc_regnames[rs1]);
		if (!no_rs1 && !no_rs2) {
			if (jmpl)
				debug(",");
			else
				debug(",");
		}
		if (!no_rs2) {
			if ((iword >> 13) & 1)
				debug("%i", siconst);
			else
				debug("%%%s", sparc_regnames[rs2]);
		}
		if (!no_rd)
			debug(",%%%s", sparc_regnames[rd]);
		break;

	case 3:	debug("%s\t", sparc_loadstore_names[op2]);
		if (op2 & 4)
			debug("%%%s,", sparc_regnames[rd]);
		debug("[%%%s", sparc_regnames[rs1]);
		if ((iword >> 13) & 1) {
			if (siconst > 0)
				debug("+");
			if (siconst != 0)
				debug("%i", siconst);
		} else {
			if (rs2 != 0)
				debug("+%%%s", sparc_regnames[rs2]);
		}
		debug("]");
		if (!(op2 & 4))
			debug(",%%%s", sparc_regnames[rd]);
		if (asi != 0)
			debug(", asi=0x%02x", asi);
		break;
	}

	debug("\n");
	return sizeof(iword);
}


#include "tmp_sparc_tail.c"

