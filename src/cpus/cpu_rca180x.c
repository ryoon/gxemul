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
 *  $Id: cpu_rca180x.c,v 1.3 2006-12-28 12:09:33 debug Exp $
 *
 *  RCA180X CPU emulation.
 *
 *  See http://www.elf-emulation.com/1802.html for a good list of 1802/1805
 *  opcodes.
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
#include "timer.h"


#define	DYNTRANS_32
#include "tmp_rca180x_head.c"


static void rca180x_timer_tick(struct timer *timer, void *extra)
{
	struct cpu *cpu = (struct cpu *) extra;
	int dec = 3;

	if (cpu->cd.rca180x.timer_mode_new)
		dec = 1;

	if (cpu->cd.rca180x.delay_timer_value > 0)
		cpu->cd.rca180x.delay_timer_value -= dec;

	if (cpu->cd.rca180x.sound_timer_value > 0)
		cpu->cd.rca180x.sound_timer_value -= dec;

	if (cpu->cd.rca180x.delay_timer_value < 0)
		cpu->cd.rca180x.delay_timer_value = 0;
	if (cpu->cd.rca180x.sound_timer_value < 0)
		cpu->cd.rca180x.sound_timer_value = 0;
}


/*
 *  rca180x_cpu_new():
 *
 *  Create a new RCA180X cpu object.
 *
 *  Returns 1 on success, 0 if there was no matching RCA180X processor with
 *  this cpu_type_name.
 */
int rca180x_cpu_new(struct cpu *cpu, struct memory *mem,
	struct machine *machine, int cpu_id, char *cpu_type_name)
{
	int i;

	if (strcasecmp(cpu_type_name, "RCA1802") != 0)
		return 0;

	/*  TODO: RCA1805 etc  */

	cpu->run_instr = rca180x_run_instr;
	cpu->memory_rw = rca180x_memory_rw;
	cpu->update_translation_table = rca180x_update_translation_table;
	cpu->invalidate_translation_caches =
	    rca180x_invalidate_translation_caches;
	cpu->invalidate_code_translation = rca180x_invalidate_code_translation;
	cpu->is_32bit = 1;

	cpu->byte_order = EMUL_BIG_ENDIAN;

	/*
	 *  CHIP8 emulation:
	 */
	cpu->cd.rca180x.sp = 0xff0;
	cpu->cd.rca180x.xres = 64;
	cpu->cd.rca180x.yres = 32;

	cpu->cd.rca180x.framebuffer_cache = malloc(cpu->cd.rca180x.xres *
	    cpu->cd.rca180x.yres);
	if (cpu->cd.rca180x.framebuffer_cache == NULL) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}
	memset(cpu->cd.rca180x.framebuffer_cache, 0, cpu->cd.rca180x.xres *
	    cpu->cd.rca180x.yres);

	/*  18.2 Hz for original CHIP8, 60 Hz for new.  */
	cpu->cd.rca180x.timer_mode_new = 1;
	cpu->cd.rca180x.timer = timer_add(
	    cpu->cd.rca180x.timer_mode_new? 60.0 : 18.2,
	    rca180x_timer_tick, cpu);


	/*  Only show name and caches etc for CPU nr 0 (in SMP machines):  */
	if (cpu_id == 0) {
		debug("%s", cpu->name);
	}

	/*  Add all register names to the settings:  */
	CPU_SETTINGS_ADD_REGISTER64("pc", cpu->pc);
	CPU_SETTINGS_ADD_REGISTER16("index", cpu->cd.rca180x.index);
	CPU_SETTINGS_ADD_REGISTER16("sp", cpu->cd.rca180x.sp);
	CPU_SETTINGS_ADD_REGISTER8("d", cpu->cd.rca180x.d);
	CPU_SETTINGS_ADD_REGISTER8("df", cpu->cd.rca180x.df);
	CPU_SETTINGS_ADD_REGISTER8("ie", cpu->cd.rca180x.ie);
	CPU_SETTINGS_ADD_REGISTER8("p", cpu->cd.rca180x.p);
	CPU_SETTINGS_ADD_REGISTER8("q", cpu->cd.rca180x.q);
	CPU_SETTINGS_ADD_REGISTER8("x", cpu->cd.rca180x.x);
	CPU_SETTINGS_ADD_REGISTER8("t_p", cpu->cd.rca180x.t_p);
	CPU_SETTINGS_ADD_REGISTER8("t_x", cpu->cd.rca180x.t_x);
	CPU_SETTINGS_ADD_REGISTER8("chip8_mode", cpu->cd.rca180x.chip8_mode);
	for (i=0; i<N_RCA180X_REGS; i++) {
		char tmpstr[5];
		snprintf(tmpstr, sizeof(tmpstr), "r%x", i);
		CPU_SETTINGS_ADD_REGISTER16(tmpstr, cpu->cd.rca180x.r[i]);
	}
	for (i=0; i<N_CHIP8_REGS; i++) {
		char tmpstr[5];
		snprintf(tmpstr, sizeof(tmpstr), "v%x", i);
		CPU_SETTINGS_ADD_REGISTER8(tmpstr, cpu->cd.rca180x.v[i]);
	}

	return 1;
}


/*
 *  rca180x_cpu_list_available_types():
 *
 *  Print a list of available RCA180X CPU types.
 */
void rca180x_cpu_list_available_types(void)
{
	/*  TODO: RCA1805...  */
	debug("RCA1802\n");
}


/*
 *  rca180x_cpu_dumpinfo():
 */
void rca180x_cpu_dumpinfo(struct cpu *cpu)
{
	debug("\n");
}


/*
 *  rca180x_cpu_register_dump():
 *
 *  Dump cpu registers in a relatively readable format.
 *
 *  gprs: set to non-zero to dump GPRs and some special-purpose registers.
 *  coprocs: set bit 0..3 to dump registers in coproc 0..3.
 */
void rca180x_cpu_register_dump(struct cpu *cpu, int gprs, int coprocs)
{
	char *symbol;
	uint64_t offset;
	int i, x = cpu->cpu_id;

	if (gprs) {
		/*  Special registers (pc, ...) first:  */
		symbol = get_symbol_name(&cpu->machine->symbol_context,
		    cpu->pc, &offset);

		debug("cpu%i: pc=0x%x", x, (int)cpu->pc);
		debug("  <%s>\n", symbol != NULL? symbol : " no symbol ");

		for (i=0; i<N_RCA180X_REGS; i++) {
			if ((i % 4) == 0)
			        debug("cpu%i:", x);
		        debug(" r%x = 0x%04x", i, cpu->cd.rca180x.r[i]);
			if ((i % 4) == 3)
				debug("\n");
		}

		debug("cpu%i: d=0x%02x df=%i ie=%i q=%i p=0x%x x=0x%x t_p=0x%x "
		    "t_x=0x%x chip8_mode=%i\n", x,
		    cpu->cd.rca180x.d, cpu->cd.rca180x.df,
		    cpu->cd.rca180x.ie, cpu->cd.rca180x.q, cpu->cd.rca180x.p,
		    cpu->cd.rca180x.x, cpu->cd.rca180x.t_p, cpu->cd.rca180x.t_x,
		    cpu->cd.rca180x.chip8_mode);

		if (cpu->cd.rca180x.chip8_mode) {
			for (i=0; i<N_CHIP8_REGS; i++) {
				if ((i % 8) == 0)
				        debug("cpu%i:", x);
			        debug(" v%x=0x%02x", i, cpu->cd.rca180x.v[i]);
				if ((i % 8) == 7)
					debug("\n");
			}

			debug("cpu%i: i=0x%04x sp=0x%03x delay=%i sound=%i\n",
			    x, cpu->cd.rca180x.index, cpu->cd.rca180x.sp,
			    cpu->cd.rca180x.delay_timer_value,
			    cpu->cd.rca180x.sound_timer_value);
		}
	}
}


/*
 *  rca180x_cpu_tlbdump():
 *
 *  Called from the debugger to dump the TLB in a readable format.
 *  x is the cpu number to dump, or -1 to dump all CPUs.
 *
 *  If rawflag is nonzero, then the TLB contents isn't formated nicely,
 *  just dumped.
 */
void rca180x_cpu_tlbdump(struct machine *m, int x, int rawflag)
{
}


/*
 *  rca180x_cpu_gdb_stub():
 *
 *  Execute a "remote GDB" command. Returns a newly allocated response string
 *  on success, NULL on failure.
 */
char *rca180x_cpu_gdb_stub(struct cpu *cpu, char *cmd)
{
	fatal("rca180x_cpu_gdb_stub(): TODO\n");
	return NULL;
}


/*
 *  rca180x_cpu_interrupt():
 */
int rca180x_cpu_interrupt(struct cpu *cpu, uint64_t irq_nr)
{
	fatal("rca180x_cpu_interrupt(): TODO\n");
	return 0;
}


/*
 *  rca180x_cpu_interrupt_ack():
 */
int rca180x_cpu_interrupt_ack(struct cpu *cpu, uint64_t irq_nr)
{
	/*  fatal("rca180x_cpu_interrupt_ack(): TODO\n");  */
	return 0;
}


/*
 *  chip8_cpu_disassemble_instr():
 *
 *  Convert an instruction word into human readable format, for instruction
 *  tracing and disassembly.
 *
 *  If running is 1, cpu->pc should be the address of the instruction.
 *
 *  If running is 0, things that depend on the runtime environment (eg.
 *  register contents) will not be shown, and addr will be used instead of
 *  cpu->pc for relative addresses.
 */
int chip8_cpu_disassemble_instr(struct cpu *cpu, unsigned char *ib,
	int running, uint64_t dumpaddr)
{
	uint64_t offset;
	char *symbol, *mnem;
	int no_y;

	if (running)
		dumpaddr = cpu->pc;

	symbol = get_symbol_name(&cpu->machine->symbol_context,
	    dumpaddr, &offset);
	if (symbol != NULL && offset==0)
		debug("<%s>\n", symbol);

	if (cpu->machine->ncpus > 1 && running)
		debug("cpu%i: ", cpu->cpu_id);

	debug("0x%04x:  %02x%02x\t", (int)dumpaddr, ib[0], ib[1]);

	switch (ib[0] >> 4) {

	case 0x0:
		switch(ib[0] & 0xf) {
		case 0x0:
			switch(ib[1] >> 4) {
			case 0xc:
				debug("scdown\t%i\n", ib[1] & 0xf);
				break;
			case 0xe:
				switch(ib[1] & 0xf) {
				case 0x0:
					debug("cls");
					break;
				case 0xe:
					debug("rts");
					break;
				default:debug("UNIMPLEMENTED");
				}
				break;
			case 0xf:
				switch(ib[1] & 0xf) {
				case 0xb:
					debug("scright");
					break;
				case 0xc:
					debug("scleft");
					break;
				case 0xe:
					debug("low");
					break;
				case 0xf:
					debug("high");
					break;
				default:debug("UNIMPLEMENTED");
				}
				break;

			default:debug("UNIMPLEMENTED");
			}
			break;

		default:debug("call\t0x%04x", (ib[0] << 8) + ib[1]);
		}
		break;

	case 0x1:
	case 0x2:
		debug("%s\t0x%03x",
		    (ib[0] >> 4) == 0x1? "jmp" : "jsr",
		    ((ib[0] & 0xf) << 8) + ib[1]);
		break;

	case 0x3:
	case 0x4:
		debug("%s\tv%x, 0x%02x",
		    (ib[0] >> 4) == 0x3? "skeq" : "skne",
		    ib[0] & 0xf, ib[1]);
		break;

	case 0x5:
		if ((ib[1] & 0xf) == 0)
			debug("skeq\tv%x, v%x", ib[0] & 0xf, ib[1] >> 4);
		else
			debug("UNIMPLEMENTED (skeq, but low nibble non-zero)");
		break;

	case 0x6:
	case 0x7:
		debug("%s\tv%x, 0x%02x",
		    (ib[0] >> 4) == 0x6? "mov" : "add",
		    ib[0] & 0xf, ib[1]);
		break;

	case 0x8:
		mnem = "UNIMPLEMENTED";
		no_y = 0;

		switch (ib[1] & 0xf) {
		case  0: mnem = "mov"; break;
		case  1: mnem = "or"; break;
		case  2: mnem = "and"; break;
		case  3: mnem = "xor"; break;
		case  4: mnem = "add"; break;
		case  5: mnem = "sub"; break;
		case  6: mnem = "shr"; no_y = 1; break;
		case  7: mnem = "rsb"; break;
		case 14: mnem = "shl"; no_y = 1; break;
		}

		debug("%s\tv%x", mnem, ib[0] & 0xf);
		if (!no_y)
			debug(", v%x", ib[1] >> 4);
		break;

	case 0x9:
		if ((ib[1] & 0xf) == 0)
			debug("skne\tv%x, v%x", ib[0] & 0xf, ib[1] >> 4);
		else
			debug("UNIMPLEMENTED (skne, but low nibble non-zero)");
		break;

	case 0xa:
	case 0xb:
		debug("%s\t0x%03x",
		    (ib[0] >> 4) == 0xa? "mvi" : "jmi",
		    ((ib[0] & 0xf) << 8) + ib[1]);
		break;

	case 0xc:
		debug("rand\tv%x, 0x%02x", ib[0] & 0xf, ib[1]);
		break;

	case 0xd:
		if ((ib[1] & 0xf) == 0)
			debug("xsprite\tv%x, v%x",
			    ib[0] & 0xf, ib[1] >> 4);
		else
			debug("sprite\tv%x, v%x, %i",
			    ib[0] & 0xf, ib[1] >> 4, ib[1] & 0xf);
		break;

	case 0xe:
		switch (ib[1]) {
		case 0x9e:
		case 0xa1:
			debug("%s\t%x",
			    ib[1] == 0x9e? "skpr" : "skup", ib[0] & 0xf);
			break;
		default:debug("UNIMPLEMENTED");
		}
		break;

	case 0xf:
		switch (ib[1]) {
		case 0x07:
		case 0x0a:
		case 0x15:
		case 0x18:
		case 0x1e:
		case 0x29:
		case 0x30:
		case 0x33:
			mnem = NULL;
			switch (ib[1]) {
			case 0x07: mnem = "gdelay"; break;
			case 0x0a: mnem = "key"; break;
			case 0x15: mnem = "sdelay"; break;
			case 0x18: mnem = "ssound"; break;
			case 0x1e: mnem = "adi"; break;
			case 0x29: mnem = "font"; break;
			case 0x30: mnem = "xfont"; break;
			case 0x33: mnem = "bcd"; break;
			}
			debug("%s\tv%x", mnem, ib[0] & 0xf);
			break;
		case 0x55:
		case 0x65:
			mnem = NULL;
			switch (ib[1]) {
			case 0x55: mnem = "str"; break;
			case 0x65: mnem = "ldr"; break;
			}
			debug("%s\tv0-v%x", mnem, ib[0] & 0xf);
			break;
		default:debug("UNIMPLEMENTED");
		}
		break;

	default:debug("UNIMPLEMENTED");
	}

	debug("\n");

	return sizeof(uint16_t);
}


/*
 *  rca180x_cpu_disassemble_instr():
 *
 *  Convert an instruction word into human readable format, for instruction
 *  tracing and disassembly.
 *
 *  If running is 1, cpu->pc should be the address of the instruction.
 *
 *  If running is 0, things that depend on the runtime environment (eg.
 *  register contents) will not be shown, and addr will be used instead of
 *  cpu->pc for relative addresses.
 */
int rca180x_cpu_disassemble_instr(struct cpu *cpu, unsigned char *ib,
	int running, uint64_t dumpaddr)
{
	uint64_t offset;
	char *symbol, *mnem = NULL;
	int len, no_reg=0;

	if (cpu->cd.rca180x.chip8_mode)
		return chip8_cpu_disassemble_instr(cpu, ib, running, dumpaddr);

	if (running)
		dumpaddr = cpu->pc;

	symbol = get_symbol_name(&cpu->machine->symbol_context,
	    dumpaddr, &offset);
	if (symbol != NULL && offset==0)
		debug("<%s>\n", symbol);

	if (cpu->machine->ncpus > 1 && running)
		debug("cpu%i: ", cpu->cpu_id);

	debug("0x%04x:\t%02x", (int)dumpaddr, ib[0]);
	len = 1;

	switch (ib[0] >> 4) {

	case 0x0:
	case 0x1:
	case 0x2:
	case 0x4:
	case 0x5:
	case 0x8:
	case 0x9:
	case 0xa:
	case 0xb:
	case 0xd:
	case 0xe:
		switch (ib[0] >> 4) {
		case 0x0: mnem = "ldn";
			  if (ib[0] == 0x00) {
				no_reg = 1;
				mnem = "idl";
			  }
			  break;
		case 0x1: mnem = "inc"; break;
		case 0x2: mnem = "dec"; break;
		case 0x4: mnem = "lda"; break;
		case 0x5: mnem = "str"; break;
		case 0x8: mnem = "glo"; break;
		case 0x9: mnem = "ghi"; break;
		case 0xa: mnem = "plo"; break;
		case 0xb: mnem = "phi"; break;
		case 0xd: mnem = "sep"; break;
		case 0xe: mnem = "sex"; break;
		}
		debug("\t%s", mnem);
		if (!no_reg)
			debug("\tr%x", ib[0] & 0xf);
		break;

	case 0x3:
		len ++;
		debug("%02x\t", ib[1]);

		switch (ib[0] & 0xf) {
		case 0x0: debug("br"); break;
		case 0x1: debug("bq"); break;
		case 0x2: debug("bz"); break;
		case 0x3: debug("bdf"); break;
		case 0x4: debug("b1"); break;
		case 0x5: debug("b2"); break;
		case 0x6: debug("b3"); break;
		case 0x7: debug("b4"); break;
		case 0x8: debug("nbr"); break;
		case 0x9: debug("bnq"); break;
		case 0xa: debug("bnz"); break;
		case 0xb: debug("bnf"); break;
		case 0xc: debug("bn1"); break;
		case 0xd: debug("bn2"); break;
		case 0xe: debug("bn3"); break;
		case 0xf: debug("bn4"); break;
		}

		debug("\t0x%04x", ((dumpaddr + 1) & 0xff00) + ib[1]);
		break;

	case 0x6:
		switch (ib[0] & 0xf) {
		case 0x0:
			debug("\tirx");
			break;
		case 0x8:
			debug("\tTODO: 1805 instruction!");
			break;
		default:
			debug("\t%s%i", ib[0] & 8? "inp" : "out", ib[0] & 7);
		}
		break;

	case 0x7:
		switch (ib[0] & 0xf) {

		case 0x0: debug("\tret"); break;
		case 0x1: debug("\tdis"); break;
		case 0x2: debug("\tldxa"); break;
		case 0x3: debug("\tstxd"); break;
		case 0x4: debug("\tadc"); break;
		case 0x5: debug("\tsdb"); break;
		case 0x6: debug("\tshrc"); break;
		case 0x7: debug("\tsmb"); break;
		case 0x8: debug("\tsav"); break;
		case 0x9: debug("\tmark"); break;
		case 0xa: debug("\treq"); break;
		case 0xb: debug("\tseq"); break;
		case 0xe: debug("\tshlc"); break;

		default:
			switch (ib[0] & 0xf) {
			case 0xc: mnem = "adci"; break;
			case 0xd: mnem = "sdbi"; break;
			case 0xf: mnem = "smbi"; break;
			}
			len ++;
			debug("%02x\t%s\t0x%02x", ib[1], mnem, ib[1]);
			break;
		}
		break;

	case 0xc:
		len += 2;
		debug("%02x%02x\t", ib[1], ib[2]);

		switch (ib[0] & 0xf) {
		case 0x0: debug("lbr"); break;
		case 0x1: debug("lbq"); break;
		case 0x2: debug("lbz"); break;
		case 0x3: debug("lbdf"); break;
		case 0x4: debug("nop"); break;
		case 0x5: debug("lsnq"); break;
		case 0x6: debug("lsnz"); break;
		case 0x7: debug("lsnf"); break;
		case 0x8: debug("nlbr"); break;
		case 0x9: debug("lbnq"); break;
		case 0xa: debug("lbnz"); break;
		case 0xb: debug("lbnf"); break;
		case 0xc: debug("lsie"); break;
		case 0xd: debug("lsq"); break;
		case 0xe: debug("lsz"); break;
		case 0xf: debug("lsdf"); break;
		}

		debug("\t0x%02x%02x", ib[1], ib[2]);
		break;

	case 0xf:
		switch (ib[0] & 0xf) {

		case 0x0: debug("\tldx"); break;
		case 0x1: debug("\tor"); break;
		case 0x2: debug("\tand"); break;
		case 0x3: debug("\txor"); break;
		case 0x4: debug("\tadd"); break;
		case 0x5: debug("\tsb"); break;
		case 0x6: debug("\tshr"); break;
		case 0x7: debug("\tsm"); break;
		case 0xe: debug("\tshl"); break;

		default:
			switch (ib[0] & 0xf) {
			case 0x8: mnem = "ldi"; break;
			case 0x9: mnem = "ori"; break;
			case 0xa: mnem = "ani"; break;
			case 0xb: mnem = "xri"; break;
			case 0xc: mnem = "adi"; break;
			case 0xd: mnem = "sdi"; break;
			case 0xf: mnem = "smi"; break;
			}
			len ++;
			debug("%02x\t%s\t0x%02x", ib[1], mnem, ib[1]);
			break;
		}
		break;

	default:debug("\tUNIMPLEMENTED");
	}

	debug("\n");

	return len;
}


#include "tmp_rca180x_tail.c"

