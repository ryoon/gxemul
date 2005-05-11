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
 *  $Id: pc_bios.c,v 1.35 2005-05-11 00:38:01 debug Exp $
 *
 *  Generic PC BIOS emulation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

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
 *  set_cursor_scanlines():
 */
static void set_cursor_scanlines(struct cpu *cpu, int start, int end)
{
	unsigned char byte;
	uint64_t ctrlregs = 0x1000003c0ULL;

	byte = 0x0a;
	cpu->memory_rw(cpu, cpu->mem, ctrlregs + 0x14,
	    &byte, sizeof(byte), MEM_WRITE, CACHE_NONE | PHYSICAL);
	byte = start;
	cpu->memory_rw(cpu, cpu->mem, ctrlregs + 0x15,
	    &byte, sizeof(byte), MEM_WRITE, CACHE_NONE | PHYSICAL);
	byte = 0x0b;
	cpu->memory_rw(cpu, cpu->mem, ctrlregs + 0x14,
	    &byte, sizeof(byte), MEM_WRITE, CACHE_NONE | PHYSICAL);
	byte = end;
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

	/*  Put the character on the screen, move cursor, and so on:  */

	get_cursor_pos(cpu, &x, &y);
	switch (ch) {
	case '\r':	x = -1; break;
	case '\n':	x = 80; break;
	case '\b':	x -= 2; break;
	default:	output_char(cpu, x, y, ch, 0x07);
	}
	x++;
	if (x < 0)
		x = 0;
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
	uint64_t ctrlregs = 0x1000003c0ULL;
	unsigned char byte;
	int x,y, oldx,oldy;
	int ah = (cpu->cd.x86.r[X86_R_AX] >> 8) & 0xff;
	int al = cpu->cd.x86.r[X86_R_AX] & 0xff;
	int dh = (cpu->cd.x86.r[X86_R_DX] >> 8) & 0xff;
	int dl = cpu->cd.x86.r[X86_R_DX] & 0xff;
	int ch = (cpu->cd.x86.r[X86_R_CX] >> 8) & 0xff;
	int cl = cpu->cd.x86.r[X86_R_CX] & 0xff;
	int cx = cpu->cd.x86.r[X86_R_CX] & 0xffff;
	int bp = cpu->cd.x86.r[X86_R_BP] & 0xffff;

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
		case 0x13:	/*  320x200 x 256 colors graphics  */
			/*  TODO: really change mode  */
			byte = 0xff;
			cpu->memory_rw(cpu, cpu->mem, ctrlregs + 0x14,
			    &byte, sizeof(byte), MEM_WRITE, CACHE_NONE
			    | PHYSICAL);
			byte = 0x13;
			cpu->memory_rw(cpu, cpu->mem, ctrlregs + 0x15,
			    &byte, sizeof(byte), MEM_WRITE, CACHE_NONE |
			    PHYSICAL);
			set_cursor_scanlines(cpu, 0x40, 0);
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
		set_cursor_scanlines(cpu, ch, cl);
		break;
	case 0x02:	/*  set cursor position  */
		set_cursor_pos(cpu, dl, dh);
		break;
	case 0x03:	/*  read cursor position  */
		get_cursor_pos(cpu, &x, &y);
		cpu->cd.x86.r[X86_R_DX] = (y << 8) + x;
		/*  ch/cl = cursor start end... TODO  */
		cpu->cd.x86.r[X86_R_CX] = 0x000f;
		break;
	case 0x09:	/*  write character and attribute(todo)  */
		while (cx-- > 0)
			pc_bios_putchar(cpu, al);
		break;
	case 0x0e:	/*  tty output  */
		pc_bios_putchar(cpu, al);
		break;
	case 0x0f:	/*  get video mode  */
		cpu->cd.x86.r[X86_R_AX] = (80 << 8) + 25;
		cpu->cd.x86.r[X86_R_BX] &= ~0xff00;	/*  BH = pagenr  */
		break;
	case 0x13:	/*  write string  */
		/*  TODO: other flags in al  */
		get_cursor_pos(cpu, &oldx, &oldy);
		set_cursor_pos(cpu, dl, dh);
		while (cx-- > 0) {
			unsigned char byte;
			cpu->cd.x86.cursegment = cpu->cd.x86.s[X86_S_ES];
			cpu->memory_rw(cpu, cpu->mem, bp++, &byte, 1,
			    MEM_READ, CACHE_NONE | PHYSICAL);
			pc_bios_putchar(cpu, byte);
		}
		if (!(al & 1))
			set_cursor_pos(cpu, oldx, oldy);
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
	int res, nread, err;
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
		cpu->cd.x86.rflags &= ~X86_FLAGS_CF;
		cpu->cd.x86.r[X86_R_AX] &= ~0xff00;
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
/*		cl &= 0x7f; ch &= 0x7f; dh &= 1;  */
		offset = (cl-1 + 18 * dh + 36 * ch) * 512;
		nread = 0; err = 0;
		debug("[ pc_bios_int13(): reading from disk 0x%x, "
		    "CHS=%i,%i,%i ]\n", dl, ch, dh, cl);
		while (al > 0) {
			unsigned char buf[512];

			debug("[ pc_bios_int13(): disk offset = 0x%llx, mem = "
			    " 0x%04x:0x%04x ]\n", (long long)offset,
			    cpu->cd.x86.s[X86_S_ES], bx);

			res = diskimage_access(cpu->machine, dl, 0, offset,
			    buf, sizeof(buf));

			if (!res) {
				err = 4;
				fatal("[ PC BIOS: disk access failed: disk %i, "
				    "CHS = %i,%i,%i ]\n", dl, ch, dh, cl);
				break;
			}

			cpu->cd.x86.cursegment = cpu->cd.x86.s[X86_S_ES];
			if (bx > 0xfe00) {
				err = 9;
				break;
			}
			store_buf(cpu, bx, (char *)buf, sizeof(buf));
			offset += sizeof(buf);
			bx += sizeof(buf);
			al --;
			nread ++;
		}
		cpu->cd.x86.r[X86_R_AX] &= ~0xffff;
		cpu->cd.x86.r[X86_R_AX] |= nread;
		/*  error code in ah? TODO  */
		if (err) {
			cpu->cd.x86.rflags |= X86_FLAGS_CF;
			cpu->cd.x86.r[X86_R_AX] |= (err << 8);
		}
		break;
	case 4:	/*  verify disk sectors  */
		cpu->cd.x86.rflags &= ~X86_FLAGS_CF;
		cpu->cd.x86.r[X86_R_AX] &= ~0xff00;
		/*  Do nothing. :-)  */
		break;
	case 8:	/*  get drive status: TODO  */
		cpu->cd.x86.r[X86_R_AX] &= ~0xffff;
		cpu->cd.x86.r[X86_R_BX] &= ~0xffff;
		cpu->cd.x86.r[X86_R_BX] |= 4;
		cpu->cd.x86.r[X86_R_CX] &= ~0xffff;
		cpu->cd.x86.r[X86_R_CX] |= (79 << 8) | 18;
		cpu->cd.x86.r[X86_R_DX] &= ~0xffff;
		cpu->cd.x86.r[X86_R_DX] |= 0x0101;  /* dl = nr of drives  */
		/*  TODO: dl, es:di and all other regs  */
		cpu->cd.x86.rflags &= ~X86_FLAGS_CF;
		break;
	case 0x15:	/*  Read DASD Type  */
		/*  TODO: generalize  */
		cpu->cd.x86.r[X86_R_AX] &= ~0xff00;
		cpu->cd.x86.r[X86_R_AX] |= 0x0100;
		cpu->cd.x86.rflags &= ~X86_FLAGS_CF;
		break;
	default:
		fatal("FATAL: Unimplemented PC BIOS interrupt 0x13 function"
		    " 0x%02x.\n", ah);
		cpu->running = 0;
		cpu->dead = 1;
	}
}


/*
 *  pc_bios_int14():
 *
 *  Serial port stuff.
 */
static void pc_bios_int14(struct cpu *cpu)
{
	int ah = (cpu->cd.x86.r[X86_R_AX] >> 8) & 0xff;

	switch (ah) {
	case 0:	debug("[ pc_bios_14(): TODO ]\n");
		break;
	default:
		fatal("FATAL: Unimplemented PC BIOS interrupt 0x14 function"
		    " 0x%02x.\n", ah);
		cpu->running = 0;
		cpu->dead = 1;
	}
}


/*
 *  pc_bios_int15():
 */
static void pc_bios_int15(struct cpu *cpu)
{
	int ah = (cpu->cd.x86.r[X86_R_AX] >> 8) & 0xff;

	switch (ah) {
	case 0x41:	/*  TODO  */
		fatal("[ PC BIOS int 0x15,0x41: TODO ]\n");
		cpu->cd.x86.rflags |= X86_FLAGS_CF;
		break;
	case 0x88:	/*  Extended Memory Size Determination  */
		cpu->cd.x86.rflags |= X86_FLAGS_CF;
		cpu->cd.x86.r[X86_R_AX] = (cpu->machine->physical_ram_in_mb
		    - 1) * 1024;
		break;
	case 0xc0:	/*  TODO  */
		fatal("[ PC BIOS int 0x15,0xc0: TODO ]\n");
		cpu->cd.x86.rflags |= X86_FLAGS_CF;
		break;
	case 0xe8:	/*  TODO  */
		fatal("[ PC BIOS int 0x15,0xe8: TODO ]\n");
		cpu->cd.x86.rflags |= X86_FLAGS_CF;
		break;
	default:
		fatal("FATAL: Unimplemented PC BIOS interrupt 0x15 function"
		    " 0x%02x.\n", ah);
		cpu->running = 0;
		cpu->dead = 1;
	}
}


/*
 *  pc_bios_int16():
 *
 *  Keyboard-related functions.
 */
static int pc_bios_int16(struct cpu *cpu)
{
	int res;
	int ah = (cpu->cd.x86.r[X86_R_AX] >> 8) & 0xff;
	int al = cpu->cd.x86.r[X86_R_AX] & 0xff;
	int scancode, asciicode;
	unsigned char tmpchar;

	switch (ah) {
	case 0x00:	/*  getchar  */
	case 0x01:	/*  isavail + getchar  */
		cpu->cd.x86.rflags |= X86_FLAGS_ZF;
		scancode = asciicode = 0;
		if (console_charavail(cpu->machine->main_console_handle)) {
			asciicode = console_readchar(cpu->machine->
			    main_console_handle);
			/*  scancode = TODO  */
			cpu->cd.x86.rflags &= ~X86_FLAGS_ZF;
			cpu->cd.x86.r[X86_R_AX] = (cpu->cd.x86.r[X86_R_AX] &
			    ~0xffff) | scancode << 8 | asciicode;
		}
		if (asciicode == 0 && ah == 0)
			return 0;
		break;
	case 0x02:	/*  read keyboard flags  */
		/*  TODO: keep this byte updated  */
		cpu->cd.x86.cursegment = 0;
		cpu->memory_rw(cpu, cpu->mem, 0x417, &tmpchar, 1,
		    MEM_READ, CACHE_DATA);
		cpu->cd.x86.r[X86_R_AX] = (cpu->cd.x86.r[X86_R_AX] & ~0xff)
		    | tmpchar;
		break;
	case 0x03:	/*  Set Keyboard Typematic Rate  */
		/*  TODO  */
		break;
	default:
		fatal("FATAL: Unimplemented PC BIOS interrupt 0x16 function"
		    " 0x%02x.\n", ah);
		cpu->running = 0;
		cpu->dead = 1;
	}

	return 1;
}


/*
 *  pc_bios_int17():
 *
 *  Printer port stuff.
 */
static void pc_bios_int17(struct cpu *cpu)
{
	int ah = (cpu->cd.x86.r[X86_R_AX] >> 8) & 0xff;

	switch (ah) {
	case 0x01:
		debug("[ PC BIOS int 0x17,0x01: TODO ]\n");
		cpu->cd.x86.r[X86_R_AX] &= ~0xff00;
		break;
	default:
		fatal("FATAL: Unimplemented PC BIOS interrupt 0x17 function"
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
	struct timeval tv;
	uint64_t x;

	switch (ah) {
	case 0x00:	/*  Read tick count.  */
		gettimeofday(&tv, NULL);
		x = tv.tv_sec * 10 + tv.tv_usec / 100000;
		cpu->cd.x86.r[X86_R_CX] = (x >> 16) & 0xffff;
		cpu->cd.x86.r[X86_R_DX] = x & 0xffff;
		break;
	case 0x01:	/*  Set tick count.  */
		fatal("[ PC BIOS int 0x1a function 0x01: Set tick count:"
		    " TODO ]\n");
		break;
	case 0x02:	/*  Read real time clock time (AT,PS/2)  */
		fatal("[ PC BIOS int 0x1a function 0x02: TODO ]\n");
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

	int_nr = addr & 0xff;

	switch (int_nr) {
	case 0x10:  pc_bios_int10(cpu); break;
	case 0x11:	/*  return bios equipment data in ax  */
		/*  TODO: see http://www.uv.tietgen.dk/staff/mlha/
		    PC/Prog/ASM/INT/index.htm#INT 11  */
		cpu->cd.x86.r[X86_R_AX] = 0x042f;
		break;
	case 0x12:	/*  return memory size in KBs  */
		cpu->cd.x86.r[X86_R_AX] = 640;
		break;
	case 0x13:  pc_bios_int13(cpu); break;
	case 0x14:  pc_bios_int14(cpu); break;
	case 0x15:  pc_bios_int15(cpu); break;
	case 0x16:
		if (pc_bios_int16(cpu) == 0)
			return 0;
		break;
	case 0x17:  pc_bios_int17(cpu); break;
	case 0x18:
		pc_bios_printstr(cpu, "Disk boot failed. (INT 0x18 called.)\n");
		cpu->running = 0;
		break;
	case 0x19:
		pc_bios_printstr(cpu, "Rebooting. (INT 0x19 called.)\n");
		cpu->running = 0;
		break;
	case 0x1a:  pc_bios_int1a(cpu); break;
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

	/*  Actually, don't pop flags, because they contain result bits
	    from interrupt calls.  */
	/*  cpu->cd.x86.rflags = (cpu->cd.x86.rflags & ~0xffff)
	    | load_16bit_word(cpu, cpu->cd.x86.r[X86_R_SP] + 4);  */

	cpu->cd.x86.r[X86_R_SP] = (cpu->cd.x86.r[X86_R_SP] & ~0xffff)
	    | ((cpu->cd.x86.r[X86_R_SP] + 6) & 0xffff);

	return 1;
}

