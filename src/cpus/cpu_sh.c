/*
 *  Copyright (C) 2005-2006  Anders Gavare.  All rights reserved.
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
 *  $Id: cpu_sh.c,v 1.25 2006-09-16 06:28:46 debug Exp $
 *
 *  Hitachi SuperH ("SH") CPU emulation.
 *
 *  TODO: It would be nice if this could encompass both 64-bit SH5, and
 *        32-bit SH encodings. Right now, it only really supports 32-bit mode.
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


#define DYNTRANS_32
#define DYNTRANS_DELAYSLOT
#include "tmp_sh_head.c"


/*
 *  sh_cpu_new():
 *
 *  Create a new SH cpu object.
 *
 *  Returns 1 on success, 0 if there was no matching SH processor with
 *  this cpu_type_name.
 */
int sh_cpu_new(struct cpu *cpu, struct memory *mem, struct machine *machine,
	int cpu_id, char *cpu_type_name)
{
	int i = 0;
	struct sh_cpu_type_def cpu_type_defs[] = SH_CPU_TYPE_DEFS;

	/*  Scan the cpu_type_defs list for this cpu type:  */
	while (cpu_type_defs[i].name != NULL) {
		if (strcasecmp(cpu_type_defs[i].name, cpu_type_name) == 0) {
			break;
		}
		i++;
	}
	if (cpu_type_defs[i].name == NULL)
		return 0;

	cpu->memory_rw = sh_memory_rw;

	cpu->cd.sh.cpu_type = cpu_type_defs[i];
	cpu->byte_order = EMUL_LITTLE_ENDIAN;
	cpu->is_32bit = cpu->cd.sh.cpu_type.bits == 32;
	cpu->cd.sh.compact = 1;		/*  Default to 16-bit opcode mode  */

	if (!cpu->is_32bit) {
		fatal("SH64 emulation not implemented. Sorry.\n");
		exit(1);
	}

	cpu->instruction_has_delayslot = sh_cpu_instruction_has_delayslot;

	cpu->translate_v2p = sh_translate_v2p;

	cpu->run_instr = sh_run_instr;
	cpu->update_translation_table = sh_update_translation_table;
	cpu->invalidate_translation_caches =
	    sh_invalidate_translation_caches;
	cpu->invalidate_code_translation =
	    sh_invalidate_code_translation;

	/*  Only show name and caches etc for CPU nr 0 (in SMP machines):  */
	if (cpu_id == 0) {
		debug("%s", cpu->name);
	}

	/*  Initial value of FPSCR (according to the SH4 manual):  */
	cpu->cd.sh.fpscr = 0x00040001;

	/*  Start in Privileged Mode:  */
	cpu->cd.sh.sr = SH_SR_MD | SH_SR_IMASK;

	/*  Stack pointer at end of physical RAM:  */
	cpu->cd.sh.r[15] = cpu->machine->physical_ram_in_mb * 1048576 - 64;

	CPU_SETTINGS_ADD_REGISTER64("pc", cpu->pc);
	CPU_SETTINGS_ADD_REGISTER32("sr", cpu->cd.sh.sr);
	CPU_SETTINGS_ADD_REGISTER32("pr", cpu->cd.sh.pr);
	CPU_SETTINGS_ADD_REGISTER32("vbr", cpu->cd.sh.vbr);
	CPU_SETTINGS_ADD_REGISTER32("gbr", cpu->cd.sh.gbr);
	CPU_SETTINGS_ADD_REGISTER32("macl", cpu->cd.sh.macl);
	CPU_SETTINGS_ADD_REGISTER32("mach", cpu->cd.sh.mach);
	for (i=0; i<SH_N_GPRS; i++) {
		char tmpstr[5];
		snprintf(tmpstr, sizeof(tmpstr), "r%i", i);
		CPU_SETTINGS_ADD_REGISTER32(tmpstr, cpu->cd.sh.r[i]);
	}

	return 1;
}


/*
 *  sh_cpu_list_available_types():
 *
 *  Print a list of available SH CPU types.
 */
void sh_cpu_list_available_types(void)
{
	int i = 0, j;
	struct sh_cpu_type_def tdefs[] = SH_CPU_TYPE_DEFS;

	while (tdefs[i].name != NULL) {
		debug("%s", tdefs[i].name);
		for (j=10 - strlen(tdefs[i].name); j>0; j--)
			debug(" ");
		i ++;
		if ((i % 6) == 0 || tdefs[i].name == NULL)
			debug("\n");
	}
}


/*
 *  sh_cpu_dumpinfo():
 */
void sh_cpu_dumpinfo(struct cpu *cpu)
{
	debug(" (%s-endian)\n",
	    cpu->byte_order == EMUL_BIG_ENDIAN? "Big" : "Little");
}


/*
 *  sh_cpu_instruction_has_delayslot():
 *
 *  Return 1 if an opcode is a branch, 0 otherwise.
 */
int sh_cpu_instruction_has_delayslot(struct cpu *cpu, unsigned char *ib)
{
	uint16_t iword = *((uint16_t *)&ib[0]);
	int hi4, lo4, lo8;

	if (!cpu->is_32bit)
		return 0;

	if (cpu->byte_order == EMUL_BIG_ENDIAN)
		iword = BE16_TO_HOST(iword);
	else
		iword = LE16_TO_HOST(iword);

	hi4 = iword >> 12; lo4 = iword & 15; lo8 = iword & 255;

        switch (hi4) {
	case 0x0:
		if (iword == 0x000b)	/*  rts  */
			return 1;
		if (lo8 == 0x23)	/*  braf  */
			return 1;
		break;
	case 0x4:
		switch (lo8) {
		case 0x0b:	/*  jsr  */
		case 0x2b:	/*  jmp  */
			return 1;
		}
		break;
	case 0x8:
		switch ((iword >> 8) & 0xf) {
		case 0xd:	/*  bt/s  */
		case 0xf:	/*  bf/s  */
			return 1;
		}
		break;
	case 0xa:	/*  bra  */
		return 1;
	}

	return 0;
}


/*
 *  sh_cpu_register_dump():
 *
 *  Dump cpu registers in a relatively readable format.
 *
 *  gprs: set to non-zero to dump GPRs and some special-purpose registers.
 *  coprocs: set bit 0..3 to dump registers in coproc 0..3.
 */
void sh_cpu_register_dump(struct cpu *cpu, int gprs, int coprocs)
{
	char *symbol;
	uint64_t offset;
	int i, x = cpu->cpu_id, nregs = cpu->cd.sh.compact? 16 : 64;
	int bits32 = cpu->cd.sh.cpu_type.bits == 32;

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

		debug("cpu%i: sr  = 0x%08"PRIx32"  (%s, %s, %s, %s, %s, %s,"
		    " imask=0x%x, %s, %s)\n", x, (int32_t)cpu->cd.sh.sr,
		    (cpu->cd.sh.sr & SH_SR_MD)? "MD" : "!md",
		    (cpu->cd.sh.sr & SH_SR_RB)? "RB" : "!rb",
		    (cpu->cd.sh.sr & SH_SR_BL)? "BL" : "!bl",
		    (cpu->cd.sh.sr & SH_SR_FD)? "FD" : "!fd",
		    (cpu->cd.sh.sr & SH_SR_M)? "M" : "!m",
		    (cpu->cd.sh.sr & SH_SR_Q)? "Q" : "!q",
		    (cpu->cd.sh.sr & SH_SR_IMASK) >> SH_SR_IMASK_SHIFT,
		    (cpu->cd.sh.sr & SH_SR_S)? "S" : "!s",
		    (cpu->cd.sh.sr & SH_SR_T)? "T" : "!t");

		symbol = get_symbol_name(&cpu->machine->symbol_context,
		    cpu->cd.sh.pr, &offset);
		debug("cpu%i: pr  = 0x%08"PRIx32, x, (uint32_t)cpu->cd.sh.pr);
		debug("  <%s>\n", symbol != NULL? symbol : " no symbol ");

		debug("cpu%i: mach = 0x%08"PRIx32"  macl = 0x%08"PRIx32"\n", x,
		    (uint32_t)cpu->cd.sh.mach, (uint32_t)cpu->cd.sh.macl);

		if (bits32) {
			/*  32-bit:  */
			for (i=0; i<nregs; i++) {
				if ((i % 4) == 0)
					debug("cpu%i:", x);
				debug(" r%-2i = 0x%08x ", i,
				    (int)cpu->cd.sh.r[i]);
				if ((i % 4) == 3)
					debug("\n");
			}
		} else {
			/*  64-bit:  */
			for (i=0; i<nregs; i++) {
				int r = (i >> 1) + ((i & 1) << 4);
				if ((i % 2) == 0)
					debug("cpu%i:", x);
				debug(" r%-2i = 0x%016llx ", r,
				    (long long)cpu->cd.sh.r[r]);
				if ((i % 2) == 1)
					debug("\n");
			}
		}
	}

	if (coprocs & 1) {
		/*  System registers:  */
		debug("cpu%i: vbr = 0x%08"PRIx32"\n", x,
		    (uint32_t)cpu->cd.sh.vbr);
	}
}


/*
 *  sh_cpu_tlbdump():
 *
 *  Called from the debugger to dump the TLB in a readable format.
 *  x is the cpu number to dump, or -1 to dump all CPUs.
 *
 *  If rawflag is nonzero, then the TLB contents isn't formated nicely,
 *  just dumped.
 */
void sh_cpu_tlbdump(struct machine *m, int x, int rawflag)
{
}


/*
 *  sh_cpu_gdb_stub():
 *
 *  Execute a "remote GDB" command. Returns a newly allocated response string
 *  on success, NULL on failure.
 */
char *sh_cpu_gdb_stub(struct cpu *cpu, char *cmd)
{
	fatal("sh_cpu_gdb_stub(): TODO\n");
	return NULL;
}


/*
 *  sh_cpu_interrupt():
 */
int sh_cpu_interrupt(struct cpu *cpu, uint64_t irq_nr)
{
	fatal("sh_cpu_interrupt(): TODO\n");
	return 0;
}


/*
 *  sh_cpu_interrupt_ack():
 */
int sh_cpu_interrupt_ack(struct cpu *cpu, uint64_t irq_nr)
{
	/*  fatal("sh_cpu_interrupt_ack(): TODO\n");  */
	return 0;
}


/*
 *  sh_update_sr():
 */
void sh_update_sr(struct cpu *cpu, uint32_t new_sr)
{
	uint32_t old_sr = cpu->cd.sh.sr;

	if ((new_sr & SH_SR_RB) != (old_sr & SH_SR_RB)) {
		fatal("sh_update_sr(): Register bank switching is not"
		    " implemented yet! TODO\n");
		exit(1);
	}

	cpu->cd.sh.sr = new_sr;
}


/*
 *  sh_cpu_disassemble_instr_compact():
 *
 *  SHcompact instruction disassembly. The top 4 bits of each 16-bit
 *  instruction word is used as the main opcode. For most instructions, the
 *  lowest 4 or 8 bits then select sub-opcode.
 */
int sh_cpu_disassemble_instr_compact(struct cpu *cpu, unsigned char *instr,
	int running, uint64_t dumpaddr)
{
	uint64_t addr;
	uint16_t iword;
	int hi4, lo4, lo8, r8, r4;

	if (cpu->byte_order == EMUL_BIG_ENDIAN)
		iword = (instr[0] << 8) + instr[1];
	else
		iword = (instr[1] << 8) + instr[0];

	debug(":  %04x %s\t", iword, cpu->delay_slot? "(d)" : "");
	hi4 = iword >> 12; lo4 = iword & 15; lo8 = iword & 255;
	r8 = (iword >> 8) & 15; r4 = (iword >> 4) & 15;

	/*
	 *  Decode the instruction:
	 */

	switch (hi4) {
	case 0x0:
		if (lo8 == 0x02)
			debug("stc\tsr,r%i\n", r8);
		else if (lo8 == 0x03)
			debug("bsrf\tr%i\n", r8);
		else if (lo4 == 0x4)
			debug("mov.b\tr%i,@(r0,r%i)\n", r4, r8);
		else if (lo4 == 0x5)
			debug("mov.w\tr%i,@(r0,r%i)\n", r4, r8);
		else if (lo4 == 0x6)
			debug("mov.l\tr%i,@(r0,r%i)\n", r4, r8);
		else if (lo4 == 0x7)
			debug("mul.l\tr%i,r%i\n", r4, r8);
		else if (iword == 0x0008)
			debug("clrt\n");
		else if (iword == 0x0009)
			debug("nop\n");
		else if (lo8 == 0x0a)
			debug("sts\tmach,r%i\n", r8);
		else if (iword == 0x000b)
			debug("rts\n");
		else if (lo4 == 0xc)
			debug("mov.b\t@(r0,r%i),r%i\n", r4, r8);
		else if (lo4 == 0xd)
			debug("mov.w\t@(r0,r%i),r%i\n", r4, r8);
		else if (lo4 == 0xe)
			debug("mov.l\t@(r0,r%i),r%i\n", r4, r8);
		else if (lo8 == 0x12)
			debug("stc\tgbr,r%i\n", r8);
		else if (iword == 0x0018)
			debug("sett\n");
		else if (iword == 0x0019)
			debug("div0u\n");
		else if (lo8 == 0x1a)
			debug("sts\tmacl,r%i\n", r8);
		else if (iword == 0x001b)
			debug("sleep\n");
		else if (lo8 == 0x23)
			debug("braf\tr%i\n", r8);
		else if (iword == 0x0028)
			debug("clrmac\n");
		else if (lo8 == 0x29)
			debug("movt\tr%i\n", r8);
		else if (lo8 == 0x2a)
			debug("sts\tpr,r%i\n", r8);
		else if (iword == 0x003b)
			debug("brk\n");
		else if (iword == 0x0048)
			debug("clrs\n");
		else if (iword == 0x0058)
			debug("sets\n");
		else if (lo8 == 0x83)
			debug("pref\t@r%i\n", r8);
		else if (lo8 == 0x93)
			debug("ocbi\t@r%i\n", r8);
		else if (lo8 == 0xa3)
			debug("ocbp\t@r%i\n", r8);
		else if (lo8 == 0xb3)
			debug("ocbwb\t@r%i\n", r8);
		else
			debug("UNIMPLEMENTED hi4=0x%x, lo8=0x%02x\n", hi4, lo8);
		break;
	case 0x1:
		debug("mov.l\tr%i,@(%i,r%i)\n", r4, lo4 * 4, r8);
		break;
	case 0x2:
		if (lo4 == 0x0)
			debug("mov.b\tr%i,@r%i\n", r4, r8);
		else if (lo4 == 0x1)
			debug("mov.w\tr%i,@r%i\n", r4, r8);
		else if (lo4 == 0x2)
			debug("mov.l\tr%i,@r%i\n", r4, r8);
		else if (lo4 == 0x4)
			debug("mov.b\tr%i,@-r%i\n", r4, r8);
		else if (lo4 == 0x5)
			debug("mov.w\tr%i,@-r%i\n", r4, r8);
		else if (lo4 == 0x6)
			debug("mov.l\tr%i,@-r%i\n", r4, r8);
		else if (lo4 == 0x7)
			debug("div0s\tr%i,r%i\n", r4, r8);
		else if (lo4 == 0x8)
			debug("tst\tr%i,r%i\n", r4, r8);
		else if (lo4 == 0x9)
			debug("and\tr%i,r%i\n", r4, r8);
		else if (lo4 == 0xa)
			debug("xor\tr%i,r%i\n", r4, r8);
		else if (lo4 == 0xb)
			debug("or\tr%i,r%i\n", r4, r8);
		else if (lo4 == 0xc)
			debug("cmp/str\tr%i,r%i\n", r4, r8);
		else if (lo4 == 0xd)
			debug("xtrct\tr%i,r%i\n", r4, r8);
		else if (lo4 == 0xe)
			debug("mulu.w\tr%i,r%i\n", r4, r8);
		else if (lo4 == 0xf)
			debug("muls.w\tr%i,r%i\n", r4, r8);
		else
			debug("UNIMPLEMENTED hi4=0x%x, lo8=0x%02x\n", hi4, lo8);
		break;
	case 0x3:
		if (lo4 == 0x0)
			debug("cmp/eq\tr%i,r%i\n", r4, r8);
		else if (lo4 == 0x2)
			debug("cmp/hs\tr%i,r%i\n", r4, r8);
		else if (lo4 == 0x3)
			debug("cmp/ge\tr%i,r%i\n", r4, r8);
		else if (lo4 == 0x4)
			debug("div1\tr%i,r%i\n", r4, r8);
		else if (lo4 == 0x5)
			debug("dmulu.l\tr%i,r%i\n", r4, r8);
		else if (lo4 == 0x6)
			debug("cmp/hi\tr%i,r%i\n", r4, r8);
		else if (lo4 == 0x7)
			debug("cmp/gt\tr%i,r%i\n", r4, r8);
		else if (lo4 == 0x8)
			debug("sub\tr%i,r%i\n", r4, r8);
		else if (lo4 == 0xa)
			debug("subc\tr%i,r%i\n", r4, r8);
		else if (lo4 == 0xb)
			debug("subv\tr%i,r%i\n", r4, r8);
		else if (lo4 == 0xc)
			debug("add\tr%i,r%i\n", r4, r8);
		else if (lo4 == 0xd)
			debug("dmuls.l\tr%i,r%i\n", r4, r8);
		else if (lo4 == 0xe)
			debug("addc\tr%i,r%i\n", r4, r8);
		else if (lo4 == 0xf)
			debug("addv\tr%i,r%i\n", r4, r8);
		else
			debug("UNIMPLEMENTED hi4=0x%x, lo8=0x%02x\n", hi4, lo8);
		break;
	case 0x4:
		if (lo8 == 0x00)
			debug("shll\tr%i\n", r8);
		else if (lo8 == 0x01)
			debug("shlr\tr%i\n", r8);
		else if (lo8 == 0x04)
			debug("rotl\tr%i\n", r8);
		else if (lo8 == 0x05)
			debug("rotr\tr%i\n", r8);
		else if (lo8 == 0x06)
			debug("lds.l\t@r%i+,mach\n", r8);
		else if (lo8 == 0x08)
			debug("shll2\tr%i\n", r8);
		else if (lo8 == 0x09)
			debug("shlr2\tr%i\n", r8);
		else if (lo8 == 0x0a)
			debug("lds\tr%i,mach\n", r8);
		else if (lo8 == 0x0b)
			debug("jsr\t@r%i\n", r8);
		else if (lo4 == 0xc)
			debug("shad\tr%i,r%i\n", r4, r8);
		else if (lo4 == 0xd)
			debug("shld\tr%i,r%i\n", r4, r8);
		else if (lo8 == 0x0e)
			debug("ldc\tr%i,sr\n", r8);
		else if (lo8 == 0x10)
			debug("dt\tr%i\n", r8);
		else if (lo8 == 0x11)
			debug("cmp/pz\tr%i\n", r8);
		else if (lo8 == 0x15)
			debug("cmp/pl\tr%i\n", r8);
		else if (lo8 == 0x16)
			debug("lds.l\t@r%i+,macl\n", r8);
		else if (lo8 == 0x18)
			debug("shll8\tr%i\n", r8);
		else if (lo8 == 0x19)
			debug("shlr8\tr%i\n", r8);
		else if (lo8 == 0x1a)
			debug("lds\tr%i,macl\n", r8);
		else if (lo8 == 0x1b)
			debug("tas.b\t@r%i\n", r8);
		else if (lo8 == 0x1e)
			debug("ldc\tr%i,gbr\n", r8);
		else if (lo8 == 0x20)
			debug("shal\tr%i\n", r8);
		else if (lo8 == 0x21)
			debug("shar\tr%i\n", r8);
		else if (lo8 == 0x22)
			debug("sts.l\tpr,@-r%i\n", r8);
		else if (lo8 == 0x24)
			debug("rotcl\tr%i\n", r8);
		else if (lo8 == 0x25)
			debug("rotcr\tr%i\n", r8);
		else if (lo8 == 0x26)
			debug("lds.l\t@r%i+,pr\n", r8);
		else if (lo8 == 0x28)
			debug("shll16\tr%i\n", r8);
		else if (lo8 == 0x29)
			debug("shlr16\tr%i\n", r8);
		else if (lo8 == 0x2a)
			debug("lds\tr%i,pr\n", r8);
		else if (lo8 == 0x2b)
			debug("jmp\t@r%i\n", r8);
		else if (lo8 == 0x2e)
			debug("ldc\tr%i,vbr\n", r8);
		else if (lo8 == 0x56)
			debug("lds.l\t@r%i+,fpul\n", r8);
		else if (lo8 == 0x5a)
			debug("lds\tr%i,fpul\n", r8);
		else if (lo8 == 0x6a)
			debug("lds\tr%i,fpscr\n", r8);
		else if ((lo8 & 0x8f) == 0x8e)
			debug("ldc\tr%i,r%i_bank\n", r8, (lo8 >> 4) & 7);
		else
			debug("UNIMPLEMENTED hi4=0x%x, lo8=0x%02x\n", hi4, lo8);
		break;
	case 0x5:
		debug("mov.l\t@(%i,r%i),r%i\n", lo4 * 4, r4, r8);
		break;
	case 0x6:
		if (lo4 == 0x0)
			debug("mov.b\t@r%i,r%i\n", r4, r8);
		else if (lo4 == 0x1)
			debug("mov.w\t@r%i,r%i\n", r4, r8);
		else if (lo4 == 0x2)
			debug("mov.l\t@r%i,r%i\n", r4, r8);
		else if (lo4 == 0x3)
			debug("mov\tr%i,r%i\n", r4, r8);
		else if (lo4 == 0x4)
			debug("mov.b\t@r%i+,r%i\n", r4, r8);
		else if (lo4 == 0x6)
			debug("mov.l\t@r%i+,r%i\n", r4, r8);
		else if (lo4 == 0x7)
			debug("not\tr%i,r%i\n", r4, r8);
		else if (lo4 == 0x8)
			debug("swap.b\tr%i,r%i\n", r4, r8);
		else if (lo4 == 0x9)
			debug("swap.w\tr%i,r%i\n", r4, r8);
		else if (lo4 == 0xa)
			debug("negc\tr%i,r%i\n", r4, r8);
		else if (lo4 == 0xb)
			debug("neg\tr%i,r%i\n", r4, r8);
		else if (lo4 == 0xc)
			debug("extu.b\tr%i,r%i\n", r4, r8);
		else if (lo4 == 0xd)
			debug("extu.w\tr%i,r%i\n", r4, r8);
		else if (lo4 == 0xe)
			debug("exts.b\tr%i,r%i\n", r4, r8);
		else if (lo4 == 0xf)
			debug("exts.w\tr%i,r%i\n", r4, r8);
		else
			debug("UNIMPLEMENTED hi4=0x%x, lo8=0x%02x\n", hi4, lo8);
		break;
	case 0x7:
		debug("add\t#%i,r%i\n", (int8_t)lo8, r8);
		break;
	case 0x8:
		if (r8 == 0x8)
			debug("cmp/eq\t#%i,r0\n", (int8_t)lo8);
		else if (r8 == 0x9 || r8 == 0xb || r8 == 0xd || r8 == 0xf) {
			addr = (int8_t)lo8;
			addr = dumpaddr + 4 + (addr << 1);
			debug("b%s%s\t0x%x\n",
			    (r8 == 0x9 || r8 == 0xd)? "t" : "f",
			    (r8 == 0x9 || r8 == 0xb)? "" : "/s", (int)addr);
		} else
			debug("UNIMPLEMENTED hi4=0x%x,0x%x\n", hi4, r8);
		break;
	case 0x9:
	case 0xd:
		addr = lo8 * (hi4==9? 2 : 4);
		addr += (dumpaddr & ~(hi4==9? 1 : 3)) + 4;
		debug("mov.%s\t0x%x,r%i\n", hi4==9? "w":"l", (int)addr, r8);
		break;
	case 0xa:
	case 0xb:
		addr = (int32_t)(int16_t)((iword & 0xfff) << 4);
		addr = ((int32_t)addr >> 3);
		addr += dumpaddr + 4;
		debug("%s\t0x%x\n", hi4==0xa? "bra":"bsr", (int)addr);
		break;
	case 0xc:
		if (r8 == 0x3)
			debug("trapa\t#%i\n", (uint8_t)lo8);
		else if (r8 == 0x7) {
			addr = lo8 * 4 + (dumpaddr & ~3) + 4;
			debug("mova\t0x%x,r0\n", (int)addr);
		} else if (r8 == 0x8)
			debug("tst\t#%i,r0\n", (uint8_t)lo8);
		else if (r8 == 0x9)
			debug("and\t#%i,r0\n", (uint8_t)lo8);
		else if (r8 == 0xa)
			debug("xor\t#%i,r0\n", (uint8_t)lo8);
		else if (r8 == 0xb)
			debug("or\t#%i,r0\n", (uint8_t)lo8);
		else if (r8 == 0xc)
			debug("tst.b\t#%i,@(r0,gbr)\n", (uint8_t)lo8);
		else if (r8 == 0xd)
			debug("and.b\t#%i,@(r0,gbr)\n", (uint8_t)lo8);
		else if (r8 == 0xe)
			debug("xor.b\t#%i,@(r0,gbr)\n", (uint8_t)lo8);
		else if (r8 == 0xf)
			debug("or.b\t#%i,@(r0,gbr)\n", (uint8_t)lo8);
		else
			debug("UNIMPLEMENTED hi4=0x%x,0x%x\n", hi4, r8);
		break;
	case 0xe:
		debug("mov\t#%i,r%i\n", (int8_t)lo8, r8);
		break;
	default:debug("UNIMPLEMENTED hi4=0x%x\n", hi4);
	}

	return sizeof(iword);
}


/*
 *  sh_cpu_disassemble_instr():
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
int sh_cpu_disassemble_instr(struct cpu *cpu, unsigned char *instr,
	int running, uint64_t dumpaddr)
{
	uint64_t offset;
	uint32_t iword;
	char *symbol;

	if (running)
		dumpaddr = cpu->pc;

	symbol = get_symbol_name(&cpu->machine->symbol_context,
	    dumpaddr, &offset);
	if (symbol != NULL && offset==0)
		debug("<%s>\n", symbol);

	if (cpu->machine->ncpus > 1 && running)
		debug("cpu%i: ", cpu->cpu_id);

	if (cpu->cd.sh.cpu_type.bits == 32)
		debug("%08x", (int)dumpaddr);
	else
		debug("%016llx", (long long)dumpaddr);

	if (cpu->cd.sh.compact)
		return sh_cpu_disassemble_instr_compact(cpu, instr,
		    running, dumpaddr);

	if (cpu->byte_order == EMUL_BIG_ENDIAN)
		iword = (instr[0] << 24) + (instr[1] << 16) + (instr[2] << 8)
		    + instr[3];
	else
		iword = (instr[3] << 24) + (instr[2] << 16) + (instr[1] << 8)
		    + instr[0];

	debug(": %08x\t", iword);

	/*
	 *  Decode the instruction:
	 */

	debug("TODO\n");

	return sizeof(iword);
}


#include "tmp_sh_tail.c"

