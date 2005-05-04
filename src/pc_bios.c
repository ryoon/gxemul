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
 *  $Id: pc_bios.c,v 1.9 2005-05-04 20:59:13 debug Exp $
 *
 *  Generic PC BIOS emulation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "console.h"
#include "cpu.h"
#include "cpu_x86.h"
#include "diskimage.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"


extern int quiet_mode;


/*
 *  output_char():
 */
static void output_char(struct cpu *cpu, int x, int y, int ch, int color)
{
	uint64_t addr = (y * 80 + x) * 2;
	unsigned char w[2];

	w[0] = ch; w[1] = color;
	cpu->cd.x86.cursegment = 0xb800;
	cpu->memory_rw(cpu, cpu->mem, addr, &w[0], sizeof(w), MEM_WRITE,
	    CACHE_NONE | PHYSICAL);
}


/*
 *  set_cursor_pos():
 */
static void set_cursor_pos(struct cpu *cpu, int x, int y)
{
	int addr = y * 80 + x;
	unsigned char byte;
	uint64_t ctrlregs = 0x1000003c0ULL;

	byte = 0x0e;
	cpu->memory_rw(cpu, cpu->mem, ctrlregs + 0x14,
	    &byte, sizeof(byte), MEM_WRITE, CACHE_NONE | PHYSICAL);
	byte = (addr >> 8) & 255;
	cpu->memory_rw(cpu, cpu->mem, ctrlregs + 0x15,
	    &byte, sizeof(byte), MEM_WRITE, CACHE_NONE | PHYSICAL);
	byte = 0x0f;
	cpu->memory_rw(cpu, cpu->mem, ctrlregs + 0x14,
	    &byte, sizeof(byte), MEM_WRITE, CACHE_NONE | PHYSICAL);
	byte = addr & 255;
	cpu->memory_rw(cpu, cpu->mem, ctrlregs + 0x15,
	    &byte, sizeof(byte), MEM_WRITE, CACHE_NONE | PHYSICAL);
}


/*
 *  get_cursor_pos():
 */
static void get_cursor_pos(struct cpu *cpu, int *x, int *y)
{
	int addr;
	unsigned char byte;
	uint64_t ctrlregs = 0x1000003c0ULL;

	byte = 0x0e;
	cpu->memory_rw(cpu, cpu->mem, ctrlregs + 0x14,
	    &byte, sizeof(byte), MEM_WRITE, CACHE_NONE | PHYSICAL);
	cpu->memory_rw(cpu, cpu->mem, ctrlregs + 0x15,
	    &byte, sizeof(byte), MEM_READ, CACHE_NONE | PHYSICAL);
	addr = byte;

	byte = 0x0f;
	cpu->memory_rw(cpu, cpu->mem, ctrlregs + 0x14,
	    &byte, sizeof(byte), MEM_WRITE, CACHE_NONE | PHYSICAL);
	cpu->memory_rw(cpu, cpu->mem, ctrlregs + 0x15,
	    &byte, sizeof(byte), MEM_READ, CACHE_NONE | PHYSICAL);
	addr = addr*256 + byte;

	*x = addr % 80;
	*y = addr / 80;
}


/*
 *  pc_bios_putchar():
 */
static void pc_bios_putchar(struct cpu *cpu, char ch)
{
	int x, y;

	if (!cpu->machine->use_x11) {
		console_putchar(cpu->machine->main_console_handle, ch);
		return;
	}

	/*  Put the character on the screen, move cursor, and so on:  */

	get_cursor_pos(cpu, &x, &y);
	switch (ch) {
	case '\r':	x=-1; break;
	case '\n':	x=80; break;
	default:	output_char(cpu, x, y, ch, 0x07);
	}
	x++;
	if (x >= 80) {
		x=0; y++;
	}
	if (y >= 25) {
		/*  Scroll up by copying lines:  */
		for (y=1; y<25; y++) {
			int addr = 160*y;
			unsigned char w[160];
			cpu->cd.x86.cursegment = 0xb800;
			cpu->memory_rw(cpu, cpu->mem, addr, &w[0], sizeof(w),
			    MEM_READ, CACHE_NONE | PHYSICAL);
			addr -= 160;
			cpu->memory_rw(cpu, cpu->mem, addr, &w[0], sizeof(w),
			    MEM_WRITE, CACHE_NONE | PHYSICAL);
		}

		/*  Clear lowest line:  */
		for (x=0; x<80; x++)
			output_char(cpu, x, 24, ' ', 0x07);

		x = 0; y = 24;
	}
	set_cursor_pos(cpu, x, y);
}


/*
 *  pc_bios_printstr():
 */
static void pc_bios_printstr(struct cpu *cpu, char *s)
{
	while (*s)
		pc_bios_putchar(cpu, *s++);
}


/*
 *  pc_bios_int10():
 *
 *  Video functions.
 */
static void pc_bios_int10(struct cpu *cpu)
{
	int x,y;
	int ah = (cpu->cd.x86.r[X86_R_AX] >> 8) & 0xff;
	int al = cpu->cd.x86.r[X86_R_AX] & 0xff;

	switch (ah) {
	case 0x00:	/*  Switch video mode.  */
		switch (al) {
		case 0x03:	/*  80x25 color textmode  */
			/*  Simply clear the screen and home the cursor
			    for now. TODO: More advanced stuff.  */
			set_cursor_pos(cpu, 0, 0);
			for (y=0; y<25; y++)
				for (x=0; x<80; x++)
					output_char(cpu, x,y, ' ', 0x07);
			break;
		default:
			fatal("pc_bios_int10(): unimplemented video mode "
			    "0x%02x\n", al);
			cpu->running = 0;
			cpu->dead = 1;
		}
		break;
	case 0x01:
		/*  ch = starting line, cl = ending line  */
		/*  fatal("pc_bios_int10(): TODO: set cursor\n");  */
		break;
	case 0x0e:
		pc_bios_putchar(cpu, al);
		break;
	default:
		fatal("FATAL: Unimplemented PC BIOS interrupt 0x10 function"
		    " 0x%02x.\n", ah);
		cpu->running = 0;
		cpu->dead = 1;
	}
}


/*
 *  pc_bios_int13():
 *
 *  Disk-related functions.
 */
static void pc_bios_int13(struct cpu *cpu)
{
	int res;
	int ah = (cpu->cd.x86.r[X86_R_AX] >> 8) & 0xff;
	int al = (cpu->cd.x86.r[X86_R_AX] >> 0) & 0xff;
	int dh = (cpu->cd.x86.r[X86_R_DX] >> 8) & 0xff;
	int dl = (cpu->cd.x86.r[X86_R_DX] >> 0) & 0xff;
	int ch = (cpu->cd.x86.r[X86_R_CX] >> 8) & 0xff;
	int cl = (cpu->cd.x86.r[X86_R_CX] >> 0) & 0xff;
	int bx = cpu->cd.x86.r[X86_R_BX];
	uint64_t offset;

	switch (ah) {
	case 0x00:	/*  Reset disk, dl = drive  */
		/*  Do nothing. :-)  */
		break;
	case 0x02:
		/*
		 *  Read sector.  al = nr of sectors
		 *  dh = head, dl = disk id (0-based),
		 *  ch = cyl,  cl = 1-based starting sector nr
		 *  es:bx = destination buffer; return carryflag = error
		 */
		cpu->cd.x86.rflags &= ~X86_FLAGS_CF;
		offset = (cl-1 + 18 * dh + 36 * ch) * 512;
		while (al > 0) {
			unsigned char buf[512];

			res = diskimage_access(cpu->machine, dl, 0, offset,
			    buf, sizeof(buf));

			if (!res) {
				cpu->cd.x86.rflags |= X86_FLAGS_CF;
				break;
			}

			cpu->cd.x86.cursegment = cpu->cd.x86.s[X86_S_ES];
			store_buf(cpu, bx, (char *)buf, sizeof(buf));
			offset += sizeof(buf);
			al --;
		}
		/*  TODO: error code?  */
		break;
	default:
		fatal("FATAL: Unimplemented PC BIOS interrupt 0x13 function"
		    " 0x%02x.\n", ah);
		cpu->running = 0;
		cpu->dead = 1;
	}
}


/*
 *  pc_bios_int1a():
 *
 *  Time of Day stuff.
 */
static void pc_bios_int1a(struct cpu *cpu)
{
	int ah = (cpu->cd.x86.r[X86_R_AX] >> 8) & 0xff;

	switch (ah) {
	case 0x00:
		/*  Return tick count? TODO  */
		cpu->cd.x86.r[X86_R_CX] = 0;
		cpu->cd.x86.r[X86_R_DX] = 0;
		break;
	default:
		fatal("FATAL: Unimplemented PC BIOS interrupt 0x1a function"
		    " 0x%02x.\n", ah);
		cpu->running = 0;
		cpu->dead = 1;
	}
}


/*
 *  pc_bios_emul():
 */
int pc_bios_emul(struct cpu *cpu)
{
	uint32_t addr = (cpu->cd.x86.s[X86_S_CS] << 4) + cpu->pc;
	int int_nr;

	int_nr = addr & 0xfff;

	switch (int_nr) {
	case 0x10:
		pc_bios_int10(cpu);
		break;
	case 0x13:
		pc_bios_int13(cpu);
		break;
	case 0x18:
		pc_bios_printstr(cpu, "Disk boot failed. (INT 0x18 called.)\n");
		cpu->running = 0;
		break;
	case 0x19:
		pc_bios_printstr(cpu, "Rebooting. (INT 0x19 called.)\n");
		cpu->running = 0;
		break;
	case 0x1a:
		pc_bios_int1a(cpu);
		break;
	default:
		fatal("FATAL: Unimplemented PC BIOS interrupt 0x%02x.\n",
		    int_nr);
		cpu->running = 0;
		cpu->dead = 1;
	}

	/*
	 *  Return from the interrupt:  Pop ip (pc), cs, and flags.
	 */
	cpu->cd.x86.cursegment = cpu->cd.x86.s[X86_S_SS];
	cpu->pc = load_16bit_word(cpu, cpu->cd.x86.r[X86_R_SP]);
	cpu->cd.x86.s[X86_S_CS] =
	    load_16bit_word(cpu, cpu->cd.x86.r[X86_R_SP] + 2);
	cpu->cd.x86.rflags = (cpu->cd.x86.rflags & ~0xffff)
	    | load_16bit_word(cpu, cpu->cd.x86.r[X86_R_SP] + 4);

	cpu->cd.x86.r[X86_R_SP] = (cpu->cd.x86.r[X86_R_SP] & ~0xffff)
	    | ((cpu->cd.x86.r[X86_R_SP] + 6) & 0xffff);

	return 1;
}

