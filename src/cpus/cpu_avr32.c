/*
 *  Copyright (C) 2006  Anders Gavare.  All rights reserved.
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
 *  $Id: cpu_avr32.c,v 1.2 2006-10-25 10:47:27 debug Exp $
 *
 *  AVR32 CPU emulation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "cpu.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"
#include "settings.h"
#include "symbol.h"


#define	DYNTRANS_32
#define	DYNTRANS_VARIABLE_INSTRUCTION_LENGTH
#include "tmp_avr32_head.c"


static char *avr32_gpr_names[N_AVR32_GPRS] = AVR32_GPR_NAMES;
static char *avr32_single_reg_op_names[32] = {
	"acr",        "scr",        "cpc",        "neg",
	"abs",        "castu.b",    "casts.b",    "castu.h",
	"casts.h",    "brev",       "swap.h",     "swap.b",
	"swap.bh",    "com",        "tnbz",       "rol",
	"ror",        "icall",      "mustr",      "musfr",
	"UNKNOWN_14", "UNKNOWN_15", "UNKNOWN_16", "UNKNOWN_17",
	"UNKNOWN_18", "UNKNOWN_19", "UNKNOWN_1a", "UNKNOWN_1b",
	"UNKNOWN_1c", "UNKNOWN_1d", "UNKNOWN_1e", "UNKNOWN_1f"  };
static char *avr32_dual_reg_op0_names[32] = {
	"add",        "sub",        "rsub",       "cp.w",
	"or",         "eor",        "and",        "tst",
	"andn",       "mov",        "st.w",       "st.h",
	"st.b",       "st.w",       "st.h",       "st.b",
	"ld.w",       "ld.sh",      "ld.uh",      "ld.ub" /* ++ */,
	"ld.w",       "ld.sh",      "ld.uh",      "ld.ub" /* -- */,
	"ld.ub",      "ld.ub",      "ld.ub",      "ld.ub",
	"ld.ub",      "ld.ub",      "ld.ub",      "ld.ub"  };

/*
 *  avr32_cpu_new():
 *
 *  Create a new AVR32 cpu object.
 *
 *  Returns 1 on success, 0 if there was no matching AVR32 processor with
 *  this cpu_type_name.
 */
int avr32_cpu_new(struct cpu *cpu, struct memory *mem, struct machine *machine,
	int cpu_id, char *cpu_type_name)
{
	int i = 0;
	struct avr32_cpu_type_def cpu_type_defs[] = AVR32_CPU_TYPE_DEFS;

	/*  Scan the cpu_type_defs list for this cpu type:  */
	while (cpu_type_defs[i].name != NULL) {
		if (strcasecmp(cpu_type_defs[i].name, cpu_type_name) == 0) {
			break;
		}
		i++;
	}
	if (cpu_type_defs[i].name == NULL)
		return 0;

	cpu->run_instr = avr32_run_instr;
	cpu->memory_rw = avr32_memory_rw;
	cpu->update_translation_table = avr32_update_translation_table;
	cpu->invalidate_translation_caches =
	    avr32_invalidate_translation_caches;
	cpu->invalidate_code_translation = avr32_invalidate_code_translation;
	cpu->is_32bit = 1;
	cpu->byte_order = EMUL_BIG_ENDIAN;

	cpu->cd.avr32.cpu_type = cpu_type_defs[i];

	/*  Only show name and caches etc for CPU nr 0 (in SMP machines):  */
	if (cpu_id == 0) {
		debug("%s", cpu->name);
	}

	/*  Add all register names to the settings:  */
	CPU_SETTINGS_ADD_REGISTER64("pc", cpu->pc);
	for (i=0; i<N_AVR32_GPRS; i++) {
		char tmpstr[7];
		snprintf(tmpstr, sizeof(tmpstr), "r%i", i);
		CPU_SETTINGS_ADD_REGISTER32(tmpstr, cpu->cd.avr32.r[i]);

		/*  r13,r14 should also be known as sp,lr:  */
		if (i >= 13 && i <= 14)
			CPU_SETTINGS_ADD_REGISTER32(
			    avr32_gpr_names[i], cpu->cd.avr32.r[i]);
	}

	CPU_SETTINGS_ADD_REGISTER32("sr", cpu->cd.avr32.sr);

	return 1;
}


/*
 *  avr32_cpu_list_available_types():
 *
 *  Print a list of available AVR32 CPU types.
 */
void avr32_cpu_list_available_types(void)
{
	int i = 0, j;
	struct avr32_cpu_type_def tdefs[] = AVR32_CPU_TYPE_DEFS;

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
 *  avr32_cpu_dumpinfo():
 */
void avr32_cpu_dumpinfo(struct cpu *cpu)
{
	/*  TODO  */
	debug("\n");
}


/*
 *  avr32_cpu_register_dump():
 *
 *  Dump cpu registers in a relatively readable format.
 *
 *  gprs: set to non-zero to dump GPRs and some special-purpose registers.
 *  coprocs: set bit 0..3 to dump registers in coproc 0..3.
 */
void avr32_cpu_register_dump(struct cpu *cpu, int gprs, int coprocs)
{
	char *symbol;
	uint64_t offset;
	int x = cpu->cpu_id, i;

	if (gprs) {
		/*  Special registers (pc, ...) first:  */
		symbol = get_symbol_name(&cpu->machine->symbol_context,
		    cpu->pc, &offset);

		debug("cpu%i: pc = 0x%08"PRIx32, x, (uint32_t)cpu->pc);
		debug("  <%s>\n", symbol != NULL? symbol : " no symbol ");

		debug("cpu%i: sr = 0x%08"PRIx32" (%s,%s,%s,%s, %s0,%s1,%s2",
		    x, cpu->cd.avr32.sr, cpu->cd.avr32.sr & AVR32_SR_H? "H":"h",
		    cpu->cd.avr32.sr & AVR32_SR_J? "J":"j",
		    cpu->cd.avr32.sr & AVR32_SR_DM? "DM":"dm",
		    cpu->cd.avr32.sr & AVR32_SR_D? "D":"d",
		    cpu->cd.avr32.sr & AVR32_SR_M0? "M":"m",
		    cpu->cd.avr32.sr & AVR32_SR_M1? "M":"m",
		    cpu->cd.avr32.sr & AVR32_SR_M2? "M":"m");
		debug(", %s,IM=%x,%s,%s,%s, %s,%s,%s,%s,%s,%s)\n",
		    cpu->cd.avr32.sr & AVR32_SR_EM? "EM":"em",
		    (cpu->cd.avr32.sr & AVR32_SR_IM) >> AVR32_SR_IM_SHIFT,
		    cpu->cd.avr32.sr & AVR32_SR_GM? "GM":"gm",
		    cpu->cd.avr32.sr & AVR32_SR_R? "R":"r",
		    cpu->cd.avr32.sr & AVR32_SR_T? "T":"t",
		    cpu->cd.avr32.sr & AVR32_SR_L? "L":"l",
		    cpu->cd.avr32.sr & AVR32_SR_Q? "Q":"q",
		    cpu->cd.avr32.sr & AVR32_SR_V? "V":"v",
		    cpu->cd.avr32.sr & AVR32_SR_N? "N":"n",
		    cpu->cd.avr32.sr & AVR32_SR_Z? "Z":"z",
		    cpu->cd.avr32.sr & AVR32_SR_C? "C":"c");

		for (i=0; i<15; i++) {
			if ((i % 4) == 0)
				debug("cpu%i:", x);
			debug(" %-3s = 0x%08"PRIx32" ",
			    avr32_gpr_names[i], cpu->cd.avr32.r[i]);
			if ((i % 4) == 3)
				debug("\n");
		}
		debug("\n");
	}
}


/*
 *  avr32_cpu_tlbdump():
 *
 *  Called from the debugger to dump the TLB in a readable format.
 *  x is the cpu number to dump, or -1 to dump all CPUs.
 *
 *  If rawflag is nonzero, then the TLB contents isn't formated nicely,
 *  just dumped.
 */
void avr32_cpu_tlbdump(struct machine *m, int x, int rawflag)
{
}


/*
 *  avr32_cpu_gdb_stub():
 *
 *  Execute a "remote GDB" command. Returns a newly allocated response string
 *  on success, NULL on failure.
 */
char *avr32_cpu_gdb_stub(struct cpu *cpu, char *cmd)
{
	fatal("avr32_cpu_gdb_stub(): TODO\n");
	return NULL;
}


/*
 *  avr32_cpu_interrupt():
 */
int avr32_cpu_interrupt(struct cpu *cpu, uint64_t irq_nr)
{
	fatal("avr32_cpu_interrupt(): TODO\n");
	return 0;
}


/*
 *  avr32_cpu_interrupt_ack():
 */
int avr32_cpu_interrupt_ack(struct cpu *cpu, uint64_t irq_nr)
{
	/*  fatal("avr32_cpu_interrupt_ack(): TODO\n");  */
	return 0;
}


#define	IWORD16		debug("%04x     \t", iword);
#define	IWORD32		debug("%04x %04x\t", iword, iword2);


/*
 *  avr32_cpu_disassemble_instr():
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
int avr32_cpu_disassemble_instr(struct cpu *cpu, unsigned char *ib,
	int running, uint64_t dumpaddr)
{
	uint64_t offset;
	int len, iword, iword2, main_opcode, opcode_class, sub_opcode;
	int r0, r9, k;
	char *symbol;

	if (running)
		dumpaddr = cpu->pc;

	symbol = get_symbol_name(&cpu->machine->symbol_context,
	    dumpaddr, &offset);
	if (symbol != NULL && offset==0)
		debug("<%s>\n", symbol);

	if (cpu->machine->ncpus > 1 && running)
		debug("cpu%i: ", cpu->cpu_id);

	debug("%08x:  ", (int)dumpaddr);

	/*  AVR32 is always big-endian.  */
	iword = (ib[0] << 8) + ib[1];
	iword2 = (ib[2] << 8) + ib[3];
	len = 2;

	/*
	 *  The top three bits are the main opcode. When possible, numbers in
	 *  brackets (e.g. [8.2.1]) indicate the chapter in the AVR32
	 *  instruction set manual describing the instruction format.
	 */
	main_opcode = iword >> 13;
	r9 = opcode_class = (iword >> 9) & 0xf;
	sub_opcode = (iword >> 4) & 0x1f;
	r0 = iword & 0xf;

	switch (main_opcode) {

	case 0:
		/*  [8.2.1]  Two Register Instructions:  */
		switch (sub_opcode) {

		case 0x0a:
		case 0x0b:
		case 0x0c:
			IWORD16;
			debug("%s\t%s++,%s\n",
			    avr32_dual_reg_op0_names[sub_opcode],
			    avr32_gpr_names[r0], avr32_gpr_names[r9]);
			break;

		case 0x0d:
		case 0x0e:
		case 0x0f:
			IWORD16;
			debug("%s\t--%s,%s\n",
			    avr32_dual_reg_op0_names[sub_opcode],
			    avr32_gpr_names[r0], avr32_gpr_names[r9]);
			break;

		case 0x10:
		case 0x11:
		case 0x12:
		case 0x13:
			IWORD16;
			debug("%s\t%s,%s++\n",
			    avr32_dual_reg_op0_names[sub_opcode],
			    avr32_gpr_names[r0], avr32_gpr_names[r9]);
			break;

		case 0x14:
		case 0x15:
		case 0x16:
		case 0x17:
			IWORD16;
			debug("%s\t%s,--%s\n",
			    avr32_dual_reg_op0_names[sub_opcode],
			    avr32_gpr_names[r0], avr32_gpr_names[r9]);
			break;

		case 0x18:
		case 0x19:
		case 0x1a:
		case 0x1b:
		case 0x1c:
		case 0x1d:
		case 0x1e:
		case 0x1f:
			IWORD16;
			debug("%s\t%s,%s[%i]\n",
			    avr32_dual_reg_op0_names[sub_opcode],
			    avr32_gpr_names[r0], avr32_gpr_names[r9],
			    sub_opcode & 7);
			break;

		default:IWORD16;
			debug("%s\t%s,%s\n",
			    avr32_dual_reg_op0_names[sub_opcode],
			    avr32_gpr_names[r0], avr32_gpr_names[r9]);
		}
		break;

	case 2:
		switch (opcode_class) {

		case 14:
			/*  [8.2.2]  Single Register Instructions:  */
			IWORD16;
			debug("%s\t%s\n", avr32_single_reg_op_names[sub_opcode],
			    avr32_gpr_names[r0]);
			break;

		default:IWORD16;
			debug("UNIMPLEMENTED %i,%i\n",
			    main_opcode, opcode_class);
		}
		break;

	case 7:
		switch (sub_opcode) {

		case 1:
			if (opcode_class != 0xa) {
				debug("UNIMPLEMENTED %i,1,%i\n",
				    main_opcode, sub_opcode);
				break;
			}

			/*  [8.2.29]  Cache Operation:  */
			IWORD32;
			k = ((int16_t) (iword2 << 5)) >> 5;
			debug("cache\t%s[%i], 0x%x\n",
			    avr32_gpr_names[r0], k, iword2 >> 11);
			break;

		default:IWORD16;
			debug("UNIMPLEMENTED %i,%i\n",
			    main_opcode, sub_opcode);
		}
		break;

	default:
		IWORD16;
		debug("UNIMPLEMENTED main opcode %i\n", main_opcode);
	}

	return len;
}


#include "tmp_avr32_tail.c"

