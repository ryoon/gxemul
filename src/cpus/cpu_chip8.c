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
 *  $Id: cpu_chip8.c,v 1.1 2006-08-27 10:37:30 debug Exp $
 *
 *  CHIP8 CPU emulation.
 *
 *  See http://members.aol.com/autismuk/chip8/chip8def.htm for a good list of
 *  CHIP8 opcodes.
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
#include "timer.h"


#define	DYNTRANS_32
#include "tmp_chip8_head.c"


static void chip8_timer_tick(struct timer *timer, void *extra)
{
	struct cpu *cpu = (struct cpu *) extra;
	int dec = 3;

	if (cpu->cd.chip8.timer_mode_new)
		dec = 1;

	if (cpu->cd.chip8.delay_timer_value > 0)
		cpu->cd.chip8.delay_timer_value -= dec;

	if (cpu->cd.chip8.sound_timer_value > 0)
		cpu->cd.chip8.sound_timer_value -= dec;

	if (cpu->cd.chip8.delay_timer_value < 0)
		cpu->cd.chip8.delay_timer_value = 0;
	if (cpu->cd.chip8.sound_timer_value < 0)
		cpu->cd.chip8.sound_timer_value = 0;
}


/*
 *  chip8_cpu_new():
 *
 *  Create a new CHIP8 cpu object.
 *
 *  Returns 1 on success, 0 if there was no matching CHIP8 processor with
 *  this cpu_type_name.
 */
int chip8_cpu_new(struct cpu *cpu, struct memory *mem, struct machine *machine,
	int cpu_id, char *cpu_type_name)
{
	if (strcasecmp(cpu_type_name, "CHIP8") != 0)
		return 0;

	/*  TODO: SuperCHIP 8  */

	cpu->run_instr = chip8_run_instr;
	cpu->memory_rw = chip8_memory_rw;
	cpu->update_translation_table = chip8_update_translation_table;
	cpu->invalidate_translation_caches =
	    chip8_invalidate_translation_caches;
	cpu->invalidate_code_translation = chip8_invalidate_code_translation;
	cpu->is_32bit = 1;

	cpu->byte_order = EMUL_BIG_ENDIAN;

	cpu->cd.chip8.sp = 0xff0;
	cpu->cd.chip8.xres = 64;
	cpu->cd.chip8.yres = 32;

	cpu->cd.chip8.framebuffer_cache = malloc(cpu->cd.chip8.xres *
	    cpu->cd.chip8.yres);
	if (cpu->cd.chip8.framebuffer_cache == NULL) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}
	memset(cpu->cd.chip8.framebuffer_cache, 0, cpu->cd.chip8.xres *
	    cpu->cd.chip8.yres);

	/*  18.2 Hz for old CHIP8, 60 Hz for new.  */
	cpu->cd.chip8.timer_mode_new = 1;
	cpu->cd.chip8.timer = timer_add(
	    cpu->cd.chip8.timer_mode_new? 60.0 : 18.2,
	    chip8_timer_tick, cpu);

	/*  Only show name and caches etc for CPU nr 0 (in SMP machines):  */
	if (cpu_id == 0) {
		debug("%s", cpu->name);
	}

	return 1;
}


/*
 *  chip8_cpu_list_available_types():
 *
 *  Print a list of available CHIP8 CPU types.
 */
void chip8_cpu_list_available_types(void)
{
	/*  TODO: Super Chip 8?  */
	debug("CHIP8\n");
}


/*
 *  chip8_cpu_dumpinfo():
 */
void chip8_cpu_dumpinfo(struct cpu *cpu)
{
	debug("\n");
}


/*
 *  chip8_cpu_register_dump():
 *
 *  Dump cpu registers in a relatively readable format.
 *
 *  gprs: set to non-zero to dump GPRs and some special-purpose registers.
 *  coprocs: set bit 0..3 to dump registers in coproc 0..3.
 */
void chip8_cpu_register_dump(struct cpu *cpu, int gprs, int coprocs)
{
	char *symbol;
	uint64_t offset;
	int i, x = cpu->cpu_id;

	if (gprs) {
		/*  Special registers (pc, ...) first:  */
		symbol = get_symbol_name(&cpu->machine->symbol_context,
		    cpu->pc, &offset);

		debug("cpu%i: pc=0x%03x", x, (int)(cpu->pc & 0xfff));
		debug("  <%s>\n", symbol != NULL? symbol : " no symbol ");

		for (i=0; i<N_CHIP8_REGS; i++) {
			if ((i % 8) == 0)
			        debug("cpu%i:", x);
		        debug(" v%x=0x%02x", i, cpu->cd.chip8.v[i]);
			if ((i % 8) == 7)
				debug("\n");
		}

		debug("cpu%i: i=0x%03x sp=0x%03x delay=%i sound=%i\n", x,
		    cpu->cd.chip8.index & 0xfff, cpu->cd.chip8.sp,
		    cpu->cd.chip8.delay_timer_value,
		    cpu->cd.chip8.sound_timer_value);
	}
}


/*
 *  chip8_cpu_register_match():
 */
void chip8_cpu_register_match(struct machine *m, char *name,
	int writeflag, uint64_t *valuep, int *match_register)
{
	int cpunr = 0;

	/*  CPU number:  */
	/*  TODO  */

	if (strcasecmp(name, "pc") == 0) {
		if (writeflag) {
			m->cpus[cpunr]->pc = *valuep;
		} else
			*valuep = m->cpus[cpunr]->pc;
		*match_register = 1;
	} else if (name[0] == 'v' && isdigit((int)name[1])) {
		int nr = atoi(name + 1);
		if (nr >= 0 && nr < N_CHIP8_REGS) {
			if (writeflag)
				m->cpus[cpunr]->cd.chip8.v[nr] = *valuep;
			else
				*valuep = m->cpus[cpunr]->cd.chip8.v[nr];
			*match_register = 1;
		}
	} else if (name[0] == 'v' && isalpha((int)name[1])) {
		int nr = name[1];
		if (nr >= 'a' && nr <= 'f')
			nr = nr - 'a' + 10;
		if (nr >= 'A' && nr <= 'F')
			nr = nr - 'A' + 10;
		if (nr >= 0 && nr < N_CHIP8_REGS) {
			if (writeflag)
				m->cpus[cpunr]->cd.chip8.v[nr] = *valuep;
			else
				*valuep = m->cpus[cpunr]->cd.chip8.v[nr];
			*match_register = 1;
		}
	} else if (strcasecmp(name, "i") == 0) {
		if (writeflag) {
			m->cpus[cpunr]->cd.chip8.index = *valuep & 0xfff;
		} else
			*valuep = m->cpus[cpunr]->cd.chip8.index & 0xfff;
		*match_register = 1;
	} else if (strcasecmp(name, "sp") == 0) {
		if (writeflag) {
			m->cpus[cpunr]->cd.chip8.sp = *valuep & 0xfff;
		} else
			*valuep = m->cpus[cpunr]->cd.chip8.sp & 0xfff;
		*match_register = 1;
	}
}


/*
 *  chip8_cpu_tlbdump():
 *
 *  Called from the debugger to dump the TLB in a readable format.
 *  x is the cpu number to dump, or -1 to dump all CPUs.
 *
 *  If rawflag is nonzero, then the TLB contents isn't formated nicely,
 *  just dumped.
 */
void chip8_cpu_tlbdump(struct machine *m, int x, int rawflag)
{
}


/*
 *  chip8_cpu_gdb_stub():
 *
 *  Execute a "remote GDB" command. Returns a newly allocated response string
 *  on success, NULL on failure.
 */
char *chip8_cpu_gdb_stub(struct cpu *cpu, char *cmd)
{
	fatal("chip8_cpu_gdb_stub(): TODO\n");
	return NULL;
}


/*
 *  chip8_cpu_interrupt():
 */
int chip8_cpu_interrupt(struct cpu *cpu, uint64_t irq_nr)
{
	fatal("chip8_cpu_interrupt(): TODO\n");
	return 0;
}


/*
 *  chip8_cpu_interrupt_ack():
 */
int chip8_cpu_interrupt_ack(struct cpu *cpu, uint64_t irq_nr)
{
	/*  fatal("chip8_cpu_interrupt_ack(): TODO\n");  */
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

		default:debug("UNIMPLEMENTED");
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


#include "tmp_chip8_tail.c"

