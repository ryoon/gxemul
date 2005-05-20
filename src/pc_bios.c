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
 *  $Id: pc_bios.c,v 1.76 2005-05-20 08:59:57 debug Exp $
 *
 *  Generic PC BIOS emulation.
 *
 *  See http://hdebruijn.soo.dto.tudelft.nl/newpage/interupt/INT.HTM for
 *  details on what different BIOS interrupts do.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cpu.h"
#include "misc.h"

#ifndef	ENABLE_X86

/*  Don't include PC bios support if we don't have x86 cpu support.  */
/*  These are just do-nothing functions.  */

void pc_bios_init(struct cpu *cpu) { }
int pc_bios_emul(struct cpu *cpu) { }


#else


#include "console.h"
#include "cpu_x86.h"
#include "devices.h"
#include "diskimage.h"
#include "machine.h"
#include "memory.h"


extern int quiet_mode;

extern unsigned char font8x8[];

#define dec_to_bcd(x) ( (((x) / 10) << 4) + ((x) % 10) )


/*
 *  add_disk():
 */
static struct pc_bios_disk *add_disk(struct machine *machine, int biosnr,
	int id, int type)
{
	uint64_t bytespercyl;
	struct pc_bios_disk *p = malloc(sizeof(struct pc_bios_disk));

	if (p == NULL) {
		fprintf(stderr, "add_disk(): out of memory\n");
		exit(1);
	}

	p->next = machine->md.pc.first_disk;
	machine->md.pc.first_disk = p;

	p->nr = biosnr; p->id = id; p->type = type;

	p->size = diskimage_getsize(machine, id, type);

	switch (type) {
	case DISKIMAGE_FLOPPY:
		if (p->size < 737280) {
			fatal("\nTODO: add_disk() in pc_bios.c: small (non-80-"
			    "cylinder) floppies?\n\n");
			exit(1);
		}
		p->cylinders = 80;
		p->heads = 2;
		p->sectorspertrack = p->size / (p->cylinders * p->heads * 512);
		break;
	default:/*  Non-floppies:  */
		p->heads = 15;
		p->sectorspertrack = 63;
		bytespercyl = p->heads * p->sectorspertrack * 512;
		p->cylinders = p->size / bytespercyl;
		if (p->cylinders * bytespercyl < p->size)
			p->cylinders ++;
	}

	return p;
}


static struct pc_bios_disk *get_disk(struct machine *machine, int biosnr)
{
	struct pc_bios_disk *p = machine->md.pc.first_disk;
	while (p != NULL) {
		if (p->nr == biosnr)
			break;
		p = p->next;
	}
	return p;
}


/*
 *  output_char():
 */
static void output_char(struct cpu *cpu, int x, int y, int ch, int color)
{
	uint64_t addr = (y * cpu->machine->md.pc.columns + x) * 2 + 0xb8000;
	unsigned char w[2];
	int len = 2;

	w[0] = ch; w[1] = color;
	if (color < 0)
		len = 1;

	cpu->memory_rw(cpu, cpu->mem, addr, &w[0], len, MEM_WRITE,
	    CACHE_NONE | PHYSICAL);
}


/*
 *  cmos_write():
 */
static void cmos_write(struct cpu *cpu, int addr, int value)
{
	unsigned char c;
	c = addr;
	cpu->memory_rw(cpu, cpu->mem, X86_IO_BASE + 0x70, &c, 1, MEM_WRITE,
	    CACHE_NONE | PHYSICAL);
	c = value;
	cpu->memory_rw(cpu, cpu->mem, X86_IO_BASE + 0x71, &c, 1, MEM_WRITE,
	    CACHE_NONE | PHYSICAL);
}


/*
 *  set_cursor_pos():
 */
static void set_cursor_pos(struct cpu *cpu, int x, int y)
{
	int addr = y * cpu->machine->md.pc.columns + x;
	unsigned char byte;
	uint64_t ctrlregs = X86_IO_BASE + 0x3c0;

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
	uint64_t ctrlregs = X86_IO_BASE + 0x3c0;

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
	uint64_t ctrlregs = X86_IO_BASE + 0x3c0;

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

	*x = addr % cpu->machine->md.pc.columns;
	*y = addr / cpu->machine->md.pc.columns;
}


/*
 *  scroll_up():
 */
static void scroll_up(struct cpu *cpu, int x1, int y1, int x2, int y2, int attr)
{
	int x, y;

	if (x1 < 0)   x1 = 0;
	if (y1 < 0)   y1 = 0;
	if (x2 >= cpu->machine->md.pc.columns)
		x2 = cpu->machine->md.pc.columns - 1;
	if (y2 >= cpu->machine->md.pc.rows)
		y2 = cpu->machine->md.pc.rows - 1;

	/*  Scroll up by copying lines:  */
	for (y=y1; y<=y2-1; y++) {
		int addr = (cpu->machine->md.pc.columns*y + x1) * 2 + 0xb8000;
		int len = (x2-x1+1) * 2;
		unsigned char w[160];
		addr += (cpu->machine->md.pc.columns * 2);
		cpu->memory_rw(cpu, cpu->mem, addr, &w[0], len,
		    MEM_READ, CACHE_NONE | PHYSICAL);
		addr -= (cpu->machine->md.pc.columns * 2);
		cpu->memory_rw(cpu, cpu->mem, addr, &w[0], len,
		    MEM_WRITE, CACHE_NONE | PHYSICAL);
	}

	/*  Clear lowest line:  */
	for (x=x1; x<=x2; x++)
		output_char(cpu, x, y2, ' ', attr);
}


/*
 *  scroll_down():
 */
static void scroll_down(struct cpu *cpu, int x1, int y1, int x2, int y2,
	int attr)
{
	int x, y;

	if (x1 < 0)   x1 = 0;
	if (y1 < 0)   y1 = 0;
	if (x2 >= cpu->machine->md.pc.columns)
		x2 = cpu->machine->md.pc.columns - 1;
	if (y2 >= cpu->machine->md.pc.rows)
		y2 = cpu->machine->md.pc.rows - 1;

	/*  Scroll down by copying lines:  */
	for (y=y2; y>=y1+1; y--) {
		int addr = (cpu->machine->md.pc.columns*y + x1) * 2 + 0xb8000;
		int len = (x2-x1+1) * 2;
		unsigned char w[160];
		addr -= cpu->machine->md.pc.columns * 2;
		cpu->memory_rw(cpu, cpu->mem, addr, &w[0], len,
		    MEM_READ, CACHE_NONE | PHYSICAL);
		addr += cpu->machine->md.pc.columns * 2;
		cpu->memory_rw(cpu, cpu->mem, addr, &w[0], len,
		    MEM_WRITE, CACHE_NONE | PHYSICAL);
	}

	/*  Clear the uppermost line:  */
	for (x=x1; x<=x2; x++)
		output_char(cpu, x, y1, ' ', attr);
}


/*
 *  pc_bios_putchar():
 */
static void pc_bios_putchar(struct cpu *cpu, char ch, int attr,
	int linewrap_and_scroll)
{
	int x, y;

	get_cursor_pos(cpu, &x, &y);

	if (!linewrap_and_scroll) {
		if (x < cpu->machine->md.pc.columns &&
		    y < cpu->machine->md.pc.rows) {
			output_char(cpu, x, y, ch, attr);
			x++;
			set_cursor_pos(cpu, x, y);
		}
		return;
	}

	/*  Put the character on the screen, move cursor, and so on:  */
	switch (ch) {
	case '\r':	x = -1; break;
	case '\n':	x = cpu->machine->md.pc.columns; break;
	case '\b':	x -= 2; break;
	default:	output_char(cpu, x, y, ch, attr);
	}
	x++;
	if (x < 0)
		x = 0;
	if (x >= cpu->machine->md.pc.columns) {
		x=0; y++;
	}

	if (attr < 0)
		attr = cpu->machine->md.pc.curcolor;

	if (y >= cpu->machine->md.pc.rows) {
		scroll_up(cpu, 0,0, cpu->machine->md.pc.columns-1,
		    cpu->machine->md.pc.rows-1, attr);
		x = 0; y = cpu->machine->md.pc.rows - 1;
	}
	set_cursor_pos(cpu, x, y);
}


/*
 *  pc_bios_printstr():
 */
static void pc_bios_printstr(struct cpu *cpu, char *s, int attr)
{
	while (*s)
		pc_bios_putchar(cpu, *s++, attr, 1);
}


/*
 *  set_video_mode():
 */
static void set_video_mode(struct cpu *cpu, int al)
{
	uint64_t ctrlregs = X86_IO_BASE + 0x3c0;
	int text, x, y;
	unsigned char byte = 0xff;

	cpu->memory_rw(cpu, cpu->mem, ctrlregs + 0x14, &byte, sizeof(byte),
	    MEM_WRITE, CACHE_NONE | PHYSICAL);
	byte = al;
	cpu->memory_rw(cpu, cpu->mem, ctrlregs + 0x15, &byte, sizeof(byte),
	    MEM_WRITE, CACHE_NONE | PHYSICAL);

	text = 0;

	switch (al) {
	case 0x00:	/*  40x25 non-color textmode  */
	case 0x01:	/*  40x25 color textmode  */
		cpu->machine->md.pc.columns = 40;
		cpu->machine->md.pc.rows = 25;
		text = 1;
		break;
	case 0x02:	/*  80x25 non-color textmode  */
	case 0x03:	/*  80x25 color textmode  */
		cpu->machine->md.pc.columns = 80;
		cpu->machine->md.pc.rows = 25;
		text = 1;
		break;
	case 0x19:	/*  ?  */
		break;
	case 0x0d:	/*  320x200 x 16 colors graphics  */
		set_cursor_scanlines(cpu, 0x40, 0);
		break;
	case 0x12:	/*  640x480 x 16 colors graphics  */
		set_cursor_scanlines(cpu, 0x40, 0);
		break;
	case 0x13:	/*  320x200 x 256 colors graphics  */
		set_cursor_scanlines(cpu, 0x40, 0);
		break;
	default:
		fatal("pc_bios_int10(): unimplemented video mode "
		    "0x%02x\n", al);
		cpu->running = 0;
		cpu->dead = 1;
	}

	cpu->machine->md.pc.curcolor = 0x07;
	cpu->machine->md.pc.videomode = al;

	if (text) {
		/*  Simply clear the screen and home the cursor
		    for now. TODO: More advanced stuff.  */
		set_cursor_pos(cpu, 0, 0);
		for (y=0; y<cpu->machine->md.pc.rows; y++)
			for (x=0; x<cpu->machine->md.pc.columns; x++)
				output_char(cpu, x,y, ' ',
				    cpu->machine->md.pc.curcolor);
	}
}


/*
 *  pc_bios_int8():
 *
 *  Interrupt handler for the timer.
 */
static int pc_bios_int8(struct cpu *cpu)
{
	unsigned char ticks[4];
	unsigned char tmpbyte;

	/*  TODO: ack the timer interrupt some other way?  */
	cpu->memory_rw(cpu, cpu->mem, X86_IO_BASE + 0x43,
	    &tmpbyte, 1, MEM_READ, CACHE_NONE | PHYSICAL);

	/*  EOI the interrupt.  */
	cpu->machine->md.pc.pic1->isr &= ~0x01;

	/*  "Call" INT 0x1C:  */
	/*  TODO: how about non-real-mode?  */
	cpu->memory_rw(cpu, cpu->mem, 0x1C * 4,
	    ticks, 4, MEM_READ, CACHE_NONE | PHYSICAL);
	cpu->pc = ticks[0] + (ticks[1] << 8);
	reload_segment_descriptor(cpu, X86_S_CS, ticks[2] + (ticks[3] << 8));
	return 0;
}


/*
 *  pc_bios_int9():
 *
 *  Interrupt handler for the keyboard.
 */
static void pc_bios_int9(struct cpu *cpu)
{
	uint8_t byte;

	/*  Read a key from the keyboard:  */
	cpu->memory_rw(cpu, cpu->mem, X86_IO_BASE + 0x60,
	    &byte, sizeof(byte), MEM_READ, CACHE_NONE | PHYSICAL);

	/*  (The read should have acknowdledged the interrupt.)  */

	/*  Add the key to the keyboard buffer:  */
	cpu->machine->md.pc.kbd_buf_scancode[
	    cpu->machine->md.pc.kbd_buf_tail] = byte;

	/*  Convert scancode into ASCII:  */
	/*  (TODO: Maybe this should be somewhere else?)  */
	if (byte >= 1 && byte <= 0xf)
		byte = "\0331234567890-=\b\t"[byte-1];
	else if (byte >= 0x10 && byte <= 0x1b)
		byte = "qwertyuiop[]"[byte-0x10];
	else if (byte >= 0x1c && byte <= 0x2b)
		byte = "\rXasdfghjkl;'`X\\"[byte-0x1c];
	else if (byte >= 0x2c && byte <= 0x35)
		byte = "zxcvbnm,./"[byte-0x2c];
	else if (byte >= 0x37 && byte <= 0x39)
		byte = "*X "[byte-0x37];
	else
		byte = 0;

	cpu->machine->md.pc.kbd_buf[cpu->machine->md.pc.kbd_buf_tail] = byte;

	cpu->machine->md.pc.kbd_buf_tail ++;
	cpu->machine->md.pc.kbd_buf_tail %= PC_BIOS_KBD_BUF_SIZE;

	/*  EOI the interrupt.  */
	cpu->machine->md.pc.pic1->isr &= ~0x02;
}


/*
 *  pc_bios_int10():
 *
 *  Video functions.
 */
static void pc_bios_int10(struct cpu *cpu)
{
	uint64_t ctrlregs = X86_IO_BASE + 0x3c0;
	unsigned char byte;
	int x,y, oldx,oldy, text;
	int ah = (cpu->cd.x86.r[X86_R_AX] >> 8) & 0xff;
	int al = cpu->cd.x86.r[X86_R_AX] & 0xff;
	int dh = (cpu->cd.x86.r[X86_R_DX] >> 8) & 0xff;
	int dl = cpu->cd.x86.r[X86_R_DX] & 0xff;
	int ch = (cpu->cd.x86.r[X86_R_CX] >> 8) & 0xff;
	int cl = cpu->cd.x86.r[X86_R_CX] & 0xff;
	int bh = (cpu->cd.x86.r[X86_R_BX] >> 8) & 0xff;
	int bl = cpu->cd.x86.r[X86_R_BX] & 0xff;
	int cx = cpu->cd.x86.r[X86_R_CX] & 0xffff;
	int bp = cpu->cd.x86.r[X86_R_BP] & 0xffff;

	switch (ah) {
	case 0x00:	/*  Switch video mode.  */
		set_video_mode(cpu, al);
		break;
	case 0x01:
		/*  ch = starting line, cl = ending line  */
		/*  TODO: it seems that FreeDOS uses start=6 end=7. hm  */
		if (ch == 6 && cl == 7)
			ch = 12, cl = 14;
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
	case 0x05:	/*  set active display page  */
		if (al != 0)
			fatal("WARNING: int 0x10, func 0x05, al = 0x%02\n", al);
		break;
	case 0x06:
		if (al < 1)
			al = 25;
		while (al-- > 0)
			scroll_up(cpu, cl, ch, dl, dh, bh);
		break;
	case 0x07:
		if (al < 1)
			al = 25;
		while (al-- > 0)
			scroll_down(cpu, cl, ch, dl, dh, bh);
		break;
	case 0x08:	/*  read char and attr at cur position  */
		/*  TODO: return AH=attr, AL=char  */
		break;
	case 0x09:	/*  write character and attribute(todo)  */
	case 0x0a:	/*  write character only  */
		get_cursor_pos(cpu, &oldx, &oldy);
		while (cx-- > 0)
			pc_bios_putchar(cpu, al, ah==9? bl : -1, 0);
		if (ah == 9)
			cpu->machine->md.pc.curcolor = bl;
		set_cursor_pos(cpu, oldx, oldy);
		break;
	case 0x0b:	/*  set color palette  */
		fatal("WARNING: int 0x10, func 0x0b: TODO\n");
		cpu->running = 0;
		break;
	case 0x0e:	/*  tty output  */
		pc_bios_putchar(cpu, al, -1, 1);
		break;
	case 0x0f:	/*  get video mode  */
		cpu->cd.x86.r[X86_R_AX] = cpu->machine->md.pc.columns << 8;

		byte = 0xff;
		cpu->memory_rw(cpu, cpu->mem, ctrlregs + 0x14,
		    &byte, sizeof(byte), MEM_WRITE, CACHE_NONE | PHYSICAL);
		cpu->memory_rw(cpu, cpu->mem, ctrlregs + 0x15,
		    &byte, sizeof(byte), MEM_READ, CACHE_NONE | PHYSICAL);
		cpu->cd.x86.r[X86_R_AX] |= byte;

		cpu->cd.x86.r[X86_R_BX] &= ~0xff00;	/*  BH = pagenr  */
		break;
	case 0x10:	/*  Palette stuff  */
		switch (al) {
		case 0x00:
			/*  Hm. Is this correct? How about the upper 4
			    bits of bh? TODO  */
			byte = bl;
			cpu->memory_rw(cpu, cpu->mem, X86_IO_BASE + 0x3c8,
			    &byte, sizeof(byte), MEM_WRITE, CACHE_NONE |
			    PHYSICAL);
			byte = ((bh >> 2) & 1) * 0xaa + (bh&8? 0x55 : 0);
			cpu->memory_rw(cpu, cpu->mem, X86_IO_BASE + 0x3c9,
			    &byte, sizeof(byte), MEM_WRITE, CACHE_NONE |
			    PHYSICAL);
			byte = ((bh >> 1) & 1) * 0xaa + (bh&8? 0x55 : 0);
			cpu->memory_rw(cpu, cpu->mem, X86_IO_BASE + 0x3c9,
			    &byte, sizeof(byte), MEM_WRITE, CACHE_NONE |
			    PHYSICAL);
			byte = ((bh >> 0) & 1) * 0xaa + (bh&8? 0x55 : 0);
			cpu->memory_rw(cpu, cpu->mem, X86_IO_BASE + 0x3c9,
			    &byte, sizeof(byte), MEM_WRITE, CACHE_NONE |
			    PHYSICAL);
			break;
		case 0x01:
			/*  TODO: Set border color.  */
			debug("TODO int 10,ah=10,al=01\n");
			break;
		case 0x10:
			byte = bl;
			cpu->memory_rw(cpu, cpu->mem, X86_IO_BASE + 0x3c8,
			    &byte, sizeof(byte), MEM_WRITE, CACHE_NONE |
			    PHYSICAL);
			byte = dh;
			cpu->memory_rw(cpu, cpu->mem, X86_IO_BASE + 0x3c9,
			    &byte, sizeof(byte), MEM_WRITE, CACHE_NONE |
			    PHYSICAL);
			byte = cl;
			cpu->memory_rw(cpu, cpu->mem, X86_IO_BASE + 0x3c9,
			    &byte, sizeof(byte), MEM_WRITE, CACHE_NONE |
			    PHYSICAL);
			byte = ch;
			cpu->memory_rw(cpu, cpu->mem, X86_IO_BASE + 0x3c9,
			    &byte, sizeof(byte), MEM_WRITE, CACHE_NONE |
			    PHYSICAL);
			break;
		default:fatal("Unimplemented INT 0x10,AH=0x10,AL=0x%02x\n", al);
			cpu->running = 0;
		}
		break;
	case 0x11:	/*  Character generator  */
		/*  TODO  */
		switch (al) {
		case 0x12:
			break;
		case 0x14:
			break;
		case 0x30:
			switch (bh) {
			case 0x03:	/*  8x8 font  */
				cpu->cd.x86.r[X86_R_BP] &= ~0xffff;
				cpu->cd.x86.r[X86_R_BP] |= 0xfa6e;
				reload_segment_descriptor(cpu, X86_S_ES,0xf000);
				/*  TODO: cx and dl, better values?  */
				cpu->cd.x86.r[X86_R_CX] &= ~0xffff;
				cpu->cd.x86.r[X86_R_CX] |= 16;
				cpu->cd.x86.r[X86_R_DX] &= ~0xff;
				cpu->cd.x86.r[X86_R_DX] |= 24;
				break;
			default:
				fatal("[ pc_bios: Get Font: TODO ]\n");
			}
			break;
		default:fatal("Unimplemented INT 0x10,AH=0x11,AL=0x%02x\n", al);
			cpu->running = 0;
		}
		break;
	case 0x12:	/*  Video Subsystem Configuration  */
		/*  TODO  */
		switch (bl) {
		case 0x10:
			cpu->cd.x86.r[X86_R_BX] &= ~0xffff;
			cpu->cd.x86.r[X86_R_BX] |= 0x0003;
			break;
		case 0x30:	/*  select nr of scanlines (200 + 50*al)  */
			debug("[ pc_bios: %i scanlines ]\n", 200+50*al);
			cpu->cd.x86.r[X86_R_AX] &= ~0xff;
			cpu->cd.x86.r[X86_R_AX] |= 0x12;
			break;
		case 0x34:	/*  TODO  */
			break;
		default:fatal("Unimplemented INT 0x10,AH=0x12,BL=0x%02x\n", bl);
			cpu->running = 0;
		}
		break;
	case 0x13:	/*  write string  */
		/*  TODO: other flags in al  */
		get_cursor_pos(cpu, &oldx, &oldy);
		set_cursor_pos(cpu, dl, dh);
		while (cx-- > 0) {
			int len = 1;
			unsigned char byte[2];
			byte[1] = 0x07;
			if (al & 2)
				len = 2;
			cpu->cd.x86.cursegment = X86_S_ES;
			cpu->memory_rw(cpu, cpu->mem, bp, &byte[0], len,
			    MEM_READ, CACHE_DATA | NO_EXCEPTIONS);
			bp += len;
				pc_bios_putchar(cpu, byte[0], byte[1], 1);
			cpu->machine->md.pc.curcolor = byte[1];
		}
		if (!(al & 1))
			set_cursor_pos(cpu, oldx, oldy);
		break;
	case 0x1a:	/*  get/set video display combination  */
		if (al != 0) {
			fatal("FATAL: Unimplemented BIOS int 0x10 function"
			    " 0x%02x, al=0x%02\n", ah, al);
			cpu->running = 0;
		}
		cpu->cd.x86.r[X86_R_AX] &= ~0xff;
		cpu->cd.x86.r[X86_R_AX] |= 0x1a;
		cpu->cd.x86.r[X86_R_BX] &= ~0xffff;
		cpu->cd.x86.r[X86_R_BX] |= 0x0008;
		break;
	case 0x4f:	/*  VESA  */
		/*  TODO: See http://www.uv.tietgen.dk/staff/mlha/PC/
		    Prog/asm/int/INT10.htm#4F for more info.  */
		switch (al) {
		case 0x00:	/*  Detect VESA  */
			cpu->cd.x86.r[X86_R_AX] &= ~0xffff;
			cpu->cd.x86.r[X86_R_AX] |= 0x004f;
			/*  TODO: the VESA struct at ES:DI  */
			break;
		case 0x01:	/*  Return mode info  */
			fatal("TODO: VESA mode 0x%04x\n", cx);
			break;
		default:
			fatal("TODO: int 0x10, function 0x4f, al=0x%02x\n", al);
		}
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
 *  Disk-related functions. These usually return CF on error.
 */
static void pc_bios_int13(struct cpu *cpu)
{
	struct pc_bios_disk *disk;
	int res, nread, err;
	int ah = (cpu->cd.x86.r[X86_R_AX] >> 8) & 0xff;
	int al = (cpu->cd.x86.r[X86_R_AX] >> 0) & 0xff;
	int dh = (cpu->cd.x86.r[X86_R_DX] >> 8) & 0xff;
	int dl = (cpu->cd.x86.r[X86_R_DX] >> 0) & 0xff;
	int ch = (cpu->cd.x86.r[X86_R_CX] >> 8) & 0xff;
	int cl = (cpu->cd.x86.r[X86_R_CX] >> 0) & 0xff;
	int bx = cpu->cd.x86.r[X86_R_BX] & 0xffff;
	uint64_t offset;

	switch (ah) {
	case 0x00:	/*  Reset disk, dl = drive  */
		cpu->cd.x86.rflags &= ~X86_FLAGS_CF;
		cpu->cd.x86.r[X86_R_AX] &= ~0xff00;
		/*  Do nothing. :-)  */
		break;
	case 0x02:	/*  Read sector  */
	case 0x03:	/*  Write sector  */
		/*
		 *  Read/Write sector(s).  al = nr of sectors
		 *  dh = head, dl = disk id (0-based),
		 *  ch = cyl,  cl = 1-based starting sector nr
		 *  es:bx = destination buffer; return carryflag = error
		 */
		cpu->cd.x86.rflags |= X86_FLAGS_CF;
		disk = get_disk(cpu->machine, cpu->cd.x86.r[X86_R_DX] & 0xff);
		if (disk != NULL) {
			unsigned char *buf;

			cpu->cd.x86.rflags &= ~X86_FLAGS_CF;
			ch = ch + ((cl >> 6) << 8);
			cl = (cl & 0x3f) - 1;
			offset = (cl + disk->sectorspertrack * dh +
			    disk->sectorspertrack * disk->heads * ch) * 512;
			nread = 0; err = 0;
			debug("[ pc_bios_int13(): reading from disk 0x%x, "
			    "CHS=%i,%i,%i ]\n", dl, ch, dh, cl);

			buf = malloc(512 * al);

			if (cl > disk->sectorspertrack || dh >= disk->heads ||
			    ch > disk->cylinders) {
				al = 0; err = 4;  /*  sector not found  */
				fatal("[ pc_bios: attempt to %s outside the d"
				    "isk? bios id=0x%02x, chs=%i,%i,%i, acces"
				    "s at %i,%i,%i ]\n", ah==2? "read" :
				    "write", dl, disk->cylinders, disk->heads,
				    disk->sectorspertrack, ch, dh, cl);
			}

			debug("[ pc_bios_int13(): %s biosdisk 0x%02x (offset="
			    "0x%llx) mem=0x%04x:0x%04x ]\n", ah==2? "read from"
			    : "write to", dl, (long long)offset,
			    cpu->cd.x86.s[X86_S_ES], bx);

			if (ah == 3) {
				fatal("TODO: bios disk write\n");
				/*  TODO  */
			}
			res = diskimage_access(cpu->machine, disk->id,
			    disk->type, 0, offset, buf, al * 512);
			if (!res) {
				err = 4;
				fatal("[ pc_bios_int13(): FAILED to %s"
				    " biosdisk 0x%02x (offset=0x%llx)"
				    " ]\n", ah==2? "read from" :
				    "write to", dl, (long long)offset);
			} else if (ah == 2) {
				cpu->cd.x86.cursegment = X86_S_ES;
				if (bx + 512*al > 0x10000) {
					/*  DMA overrun  */
					fatal("[ pc_bios: DMA overrun ]\n");
					err = 9;
				}
				store_buf(cpu, bx, (char *)buf, 512 * al);
			}

			nread = al;
			free(buf);
			cpu->cd.x86.r[X86_R_AX] &= ~0xffff;
			cpu->cd.x86.r[X86_R_AX] |= nread;
		} else
			err = 0x80;
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
		cpu->cd.x86.rflags |= X86_FLAGS_CF;
		cpu->cd.x86.r[X86_R_AX] &= ~0xffff;
		cpu->cd.x86.r[X86_R_AX] |= 0x8080;
		disk = get_disk(cpu->machine, cpu->cd.x86.r[X86_R_DX] & 0xff);
		if (disk != NULL) {
			int cyl_hi, cyl_lo;

			cyl_lo = disk->cylinders & 255;
			cyl_hi = ((disk->cylinders >> 8) & 3) << 6;
			cyl_hi |= disk->sectorspertrack;

			cpu->cd.x86.r[X86_R_AX] &= ~0xffff;
			cpu->cd.x86.r[X86_R_BX] &= ~0xffff;
			if (disk->type == DISKIMAGE_FLOPPY)
				cpu->cd.x86.r[X86_R_BX] |= 4;
			cpu->cd.x86.r[X86_R_CX] &= ~0xffff;
			cpu->cd.x86.r[X86_R_CX] |= (cyl_lo << 8) | cyl_hi;
			cpu->cd.x86.r[X86_R_DX] &= ~0xffff;
			cpu->cd.x86.r[X86_R_DX] |= 0x01 |
			    ((disk->heads - 1) << 8);
			/*  TODO: dl = nr of drives   */
			/*  TODO: es:di?  */
			cpu->cd.x86.rflags &= ~X86_FLAGS_CF;
		}
		break;
	case 0x15:	/*  Read DASD Type  */
		/*  TODO: generalize  */
		cpu->cd.x86.r[X86_R_AX] &= ~0xff00;
		cpu->cd.x86.r[X86_R_AX] |= 0x0100;
		cpu->cd.x86.rflags &= ~X86_FLAGS_CF;
		break;
	case 0x41:	/*  Check for Extended Functions  */
		/*  There is no such support.  :)  */
		cpu->cd.x86.rflags |= X86_FLAGS_CF;
		break;
	case 0x42:	/*  Extended Read:  */
		/*  TODO  */
		cpu->cd.x86.rflags |= X86_FLAGS_CF;
		cpu->cd.x86.r[X86_R_AX] &= ~0xff00;
		cpu->cd.x86.r[X86_R_AX] |= 0x0100;
		break;
	case 0x48:	/*  ?  */
		/*  TODO  */
		cpu->cd.x86.rflags |= X86_FLAGS_CF;
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
	int al = cpu->cd.x86.r[X86_R_AX] & 0xff;
	int cx = cpu->cd.x86.r[X86_R_CX] & 0xffff;
	int si = cpu->cd.x86.r[X86_R_SI] & 0xffff;
	int m;
	unsigned char src_entry[8];
	unsigned char dst_entry[8];
	uint32_t src_addr, dst_addr;

	switch (ah) {
	case 0x00:	/*  TODO?  */
		fatal("[ PC BIOS int 0x15,0x00: TODO ]\n");
		cpu->cd.x86.rflags |= X86_FLAGS_CF;
		cpu->cd.x86.r[X86_R_AX] &= ~0xff00;
		cpu->cd.x86.r[X86_R_AX] |= 0x8600;	/*  TODO  */
		break;
	case 0x06:	/*  TODO  */
		fatal("[ PC BIOS int 0x15,0x06: TODO ]\n");
		cpu->cd.x86.rflags |= X86_FLAGS_CF;
		cpu->cd.x86.r[X86_R_AX] &= ~0xff00;
		cpu->cd.x86.r[X86_R_AX] |= 0x8600;	/*  TODO  */
		break;
	case 0x24:	/*  TODO  */
		fatal("[ PC BIOS int 0x15,0x24: TODO ]\n");
		cpu->cd.x86.rflags |= X86_FLAGS_CF;
		cpu->cd.x86.r[X86_R_AX] &= ~0xff00;
		cpu->cd.x86.r[X86_R_AX] |= 0x8600;	/*  TODO  */
		break;
	case 0x41:	/*  TODO  */
		fatal("[ PC BIOS int 0x15,0x41: TODO ]\n");
		cpu->cd.x86.rflags |= X86_FLAGS_CF;
		cpu->cd.x86.r[X86_R_AX] &= ~0xff00;
		cpu->cd.x86.r[X86_R_AX] |= 0x8600;	/*  TODO  */
		break;
	case 0x4f:	/*  Keyboard Scancode Intercept (TODO)  */
		cpu->cd.x86.rflags |= X86_FLAGS_CF;
		break;
	case 0x53:	/*  TODO  */
		fatal("[ PC BIOS int 0x15,0x53: TODO ]\n");
		cpu->cd.x86.rflags |= X86_FLAGS_CF;
		break;
	case 0x86:	/*  Wait  */
		/*  No. :-)  */
		cpu->cd.x86.rflags &= ~X86_FLAGS_CF;
		break;
	case 0x87:	/*  Move to/from extended memory, via a GDT  */
		cpu->cd.x86.cursegment = X86_S_ES;
		cpu->memory_rw(cpu, cpu->mem, si + 0x10, src_entry, 8,
		    MEM_READ, CACHE_DATA);
		cpu->memory_rw(cpu, cpu->mem, si + 0x18, dst_entry, 8,
		    MEM_READ, CACHE_DATA);
		src_addr = src_entry[2]+(src_entry[3]<<8)+(src_entry[4]<<16);
		dst_addr = dst_entry[2]+(dst_entry[3]<<8)+(dst_entry[4]<<16);
		if (src_entry[5] != 0x92 && src_entry[5] != 0x93)
			fatal("WARNING: int15,87: bad src access right?"
			    " (0x%02x, should be 0x93)\n", src_entry[5]);
		if (dst_entry[5] != 0x92 && dst_entry[5] != 0x93)
			fatal("WARNING: int15,87: bad dst access right?"
			    " (0x%02x, should be 0x93)\n", dst_entry[5]);
		debug("[ pc_bios: INT15: copying %i bytes from 0x%x to 0x%x"
		    " ]\n", cx*2, src_addr, dst_addr);
		if (cx > 0x8000)
			fatal("WARNING! INT15 func 0x87 cx=0x%04x, max allowed"
			    " is supposed to be 0x8000!\n", cx);
		while (cx*2 > 0) {
			unsigned char buf[2];
			cpu->memory_rw(cpu, cpu->mem, src_addr, buf, 2,
			    MEM_READ, NO_SEGMENTATION | CACHE_DATA);
			cpu->memory_rw(cpu, cpu->mem, dst_addr, buf, 2,
			    MEM_WRITE, NO_SEGMENTATION | CACHE_DATA);
			src_addr += 2; dst_addr += 2; cx --;
		}
		cpu->cd.x86.r[X86_R_AX] &= ~0xff00;
		cpu->cd.x86.rflags &= ~X86_FLAGS_CF;
		cpu->cd.x86.rflags |= X86_FLAGS_ZF;
		break;
	case 0x88:	/*  Extended Memory Size Determination  */
		/*  TODO: Max 16 or 64 MB?  */
		cpu->cd.x86.rflags &= ~X86_FLAGS_CF;
		cpu->cd.x86.r[X86_R_AX] &= ~0xffff;
		if (cpu->machine->physical_ram_in_mb <= 64)
			cpu->cd.x86.r[X86_R_AX] |= (cpu->machine->
			    physical_ram_in_mb - 1) * 1024;
		else
			cpu->cd.x86.r[X86_R_AX] |= 63*1024;
		break;
	case 0x8A:	/*  Get "Big" memory size  */
		cpu->cd.x86.rflags &= ~X86_FLAGS_CF;
		m = (cpu->machine->physical_ram_in_mb - 1) * 1024;
		cpu->cd.x86.r[X86_R_DX] &= ~0xffff;
		cpu->cd.x86.r[X86_R_AX] &= ~0xffff;
		cpu->cd.x86.r[X86_R_DX] |= ((m >> 16) & 0xffff);
		cpu->cd.x86.r[X86_R_AX] |= (m & 0xffff);
		break;
	case 0x91:	/*  Interrupt Complete (bogus)  */
		cpu->cd.x86.rflags &= ~X86_FLAGS_CF;
		break;
	case 0xc0:	/*  System Config: (at 0xfffd:0)  */
		cpu->cd.x86.rflags &= ~X86_FLAGS_CF;
		cpu->cd.x86.r[X86_R_AX] &= ~0xff00;
		cpu->cd.x86.r[X86_R_BX] &= ~0xffff;
		cpu->cd.x86.s[X86_S_ES] = 0xfffd;
		reload_segment_descriptor(cpu, X86_S_ES, 0xfffd);
		break;
	case 0xc1:	/*  Extended Bios Data-seg (TODO)  */
		cpu->cd.x86.rflags |= X86_FLAGS_CF;
		break;
	case 0xe8:	/*  TODO  */
		switch (al) {
		case 0x01:
			cpu->cd.x86.rflags &= ~X86_FLAGS_CF;
			m = cpu->machine->physical_ram_in_mb;
			if (m > 16)
				m = 16;
			m = (m - 1) * 1024;
			/*  between 1MB and 16MB: (1KB blocks)  */
			cpu->cd.x86.r[X86_R_AX] &= ~0xffff;
			cpu->cd.x86.r[X86_R_AX] |= (m & 0xffff);
			/*  mem above 16MB, 64K blocks:  */
			m = cpu->machine->physical_ram_in_mb;
			if (m < 16)
				m = 0;
			else
				m = (m-16) / 16;
			cpu->cd.x86.r[X86_R_BX] &= ~0xffff;
			cpu->cd.x86.r[X86_R_BX] |= (m & 0xffff);
			/*  CX and DX are "configured" memory  */
			cpu->cd.x86.r[X86_R_CX] &= ~0xffff;
			cpu->cd.x86.r[X86_R_DX] &= ~0xffff;
			cpu->cd.x86.r[X86_R_CX] |= (
			    cpu->cd.x86.r[X86_R_AX] & 0xffff);
			cpu->cd.x86.r[X86_R_DX] |= (
			    cpu->cd.x86.r[X86_R_BX] & 0xffff);
			break;
		case 0x20:	/*  Get memory map:  TODO  */
			cpu->cd.x86.rflags |= X86_FLAGS_CF;
			cpu->cd.x86.r[X86_R_AX] &= ~0xff00;
			cpu->cd.x86.r[X86_R_AX] |= 0x8600;
			break;
		default:fatal("[ PC BIOS int 0x15,0xe8: al=0x%02x "
			    " TODO ]\n", al);
			cpu->running = 0;
		}
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
	int ah = (cpu->cd.x86.r[X86_R_AX] >> 8) & 0xff;
	/*  int al = cpu->cd.x86.r[X86_R_AX] & 0xff;  */
	int scancode, asciicode;
	unsigned char tmpchar;

	switch (ah) {
	case 0x00:	/*  getchar  */
		scancode = asciicode = 0;
		if (cpu->machine->md.pc.kbd_buf_head !=
		    cpu->machine->md.pc.kbd_buf_tail) {
			asciicode = cpu->machine->md.pc.kbd_buf[
			    cpu->machine->md.pc.kbd_buf_head];
			scancode = cpu->machine->md.pc.kbd_buf_scancode[
			    cpu->machine->md.pc.kbd_buf_head];
			if (asciicode != 0) {
				cpu->cd.x86.r[X86_R_AX] =
				    (scancode << 8) | asciicode;
			}
			cpu->machine->md.pc.kbd_buf_head ++;
			cpu->machine->md.pc.kbd_buf_head %=
			    PC_BIOS_KBD_BUF_SIZE;
		}
		if (asciicode == 0)
			return 0;
		break;
	case 0x01:	/*  non-destructive "isavail"  */
		cpu->cd.x86.rflags |= X86_FLAGS_ZF;
		cpu->cd.x86.r[X86_R_AX] &= ~0xffff;
		scancode = asciicode = 0;
		if (cpu->machine->md.pc.kbd_buf_head !=
		    cpu->machine->md.pc.kbd_buf_tail) {
			asciicode = cpu->machine->md.pc.kbd_buf[
			    cpu->machine->md.pc.kbd_buf_head];
			scancode = cpu->machine->md.pc.kbd_buf_scancode[
			    cpu->machine->md.pc.kbd_buf_head];
			cpu->cd.x86.rflags &= ~X86_FLAGS_ZF;
			cpu->cd.x86.r[X86_R_AX] |= (scancode << 8) | asciicode;
		}
		break;
	case 0x02:	/*  read keyboard flags  */
		/*  TODO: keep this byte updated  */
		cpu->memory_rw(cpu, cpu->mem, 0x417, &tmpchar, 1,
		    MEM_READ, PHYSICAL);
		cpu->cd.x86.r[X86_R_AX] = (cpu->cd.x86.r[X86_R_AX] & ~0xff)
		    | tmpchar;
		break;
	case 0x03:	/*  Set Keyboard Typematic Rate  */
		/*  TODO  */
		break;
	case 0x92:	/*  Keyboard "Capabilities Check"  */
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
	unsigned char ticks[4];
	int ah = (cpu->cd.x86.r[X86_R_AX] >> 8) & 0xff;
	time_t tim;
	struct tm *tm;

	switch (ah) {
	case 0x00:	/*  Read tick count.  */
		cpu->memory_rw(cpu, cpu->mem, 0x46C,
		    ticks, sizeof(ticks), MEM_READ, CACHE_NONE | PHYSICAL);
		cpu->cd.x86.r[X86_R_CX] = (ticks[3] << 8) | ticks[2];
		cpu->cd.x86.r[X86_R_DX] = (ticks[1] << 8) | ticks[0];
		break;
	case 0x01:	/*  Set tick count.  */
		ticks[0] = cpu->cd.x86.r[X86_R_DX];
		ticks[1] = cpu->cd.x86.r[X86_R_DX] >> 8;
		ticks[2] = cpu->cd.x86.r[X86_R_CX];
		ticks[3] = cpu->cd.x86.r[X86_R_CX] >> 8;
		cpu->memory_rw(cpu, cpu->mem, 0x46C,
		    ticks, sizeof(ticks), MEM_WRITE, CACHE_NONE | PHYSICAL);
		break;
	case 0x02:	/*  Read real time clock time (AT,PS/2)  */
		tim = time(NULL);
		tm = gmtime(&tim);
		cpu->cd.x86.r[X86_R_CX] &= ~0xffff;
		cpu->cd.x86.r[X86_R_DX] &= ~0xffff;
		cpu->cd.x86.r[X86_R_CX] |= (dec_to_bcd(tm->tm_hour) << 8) |
		    dec_to_bcd(tm->tm_min);
		cpu->cd.x86.r[X86_R_DX] |= dec_to_bcd(tm->tm_sec) << 8;
		cpu->cd.x86.rflags &= ~X86_FLAGS_CF;
		break;
	case 0x04:	/*  Read real time clock date (AT,PS/2)  */
		tim = time(NULL);
		tm = gmtime(&tim);
		cpu->cd.x86.r[X86_R_CX] &= ~0xffff;
		cpu->cd.x86.r[X86_R_DX] &= ~0xffff;
		cpu->cd.x86.r[X86_R_CX] |=
		    (dec_to_bcd((tm->tm_year+1900)/100) << 8) |
		    dec_to_bcd(tm->tm_year % 100);
		cpu->cd.x86.r[X86_R_DX] |= (dec_to_bcd(tm->tm_mon+1) << 8) |
		    dec_to_bcd(tm->tm_mday);
		cpu->cd.x86.rflags &= ~X86_FLAGS_CF;
		break;
	case 0xb1:	/*  Intel PCI Bios  */
		/*  ... not installed :)  */
		cpu->cd.x86.rflags |= X86_FLAGS_CF;
		break;
	default:
		fatal("FATAL: Unimplemented PC BIOS interrupt 0x1a function"
		    " 0x%02x.\n", ah);
		cpu->running = 0;
		cpu->dead = 1;
	}
}


/*
 *  pc_bios_int1c():
 *
 *  Increase the timer-tick word at 0x40:0x6C.
 */
static void pc_bios_int1c(struct cpu *cpu)
{
	unsigned char ticks[4];
	int i;

	/*  Increase word at 0x0040:0x006C  */
	cpu->memory_rw(cpu, cpu->mem, 0x46C,
	    ticks, sizeof(ticks), MEM_READ, CACHE_NONE | PHYSICAL);
	for (i=0; i<sizeof(ticks); i++) {
		ticks[i] ++;
		if (ticks[i] != 0)
			break;
	}
	cpu->memory_rw(cpu, cpu->mem, 0x46C,
	    ticks, sizeof(ticks), MEM_WRITE, CACHE_NONE | PHYSICAL);
}


/*
 *  pc_bios_init():
 */
void pc_bios_init(struct cpu *cpu)
{
	char t[81];
	int x, y, nboxlines, i, any_disk = 0, disknr, tmp;
	int boot_id, boot_type, bios_boot_id = 0;

	/*  Go to real mode:  */
	cpu->cd.x86.cr[0] &= ~X86_CR0_PE;

	boot_id = diskimage_bootdev(cpu->machine, &boot_type);

	if (cpu->machine->md.pc.initialized) {
		fatal("ERROR: pc_bios_init(): Already initialized.\n");
		return;
	}

	cpu->machine->md.pc.pic1->irq_base = 0x08;
	cpu->machine->md.pc.pic2->irq_base = 0x70;

	/*  Disk Base Table (11 or 12 bytes?) at F000h:EFC7:  */
	cpu->cd.x86.cursegment = X86_S_FS;
	reload_segment_descriptor(cpu, X86_S_FS, 0xf000);
	store_byte(cpu, 0xefc7 + 0, 0xcf);
	store_byte(cpu, 0xefc7 + 1, 0xb8);
	store_byte(cpu, 0xefc7 + 2, 1);		/*  timer ticks till shutoff  */
	store_byte(cpu, 0xefc7 + 3, 2);		/*  512 bytes per sector  */
	store_byte(cpu, 0xefc7 + 4, 17);
	store_byte(cpu, 0xefc7 + 5, 0xd8);
	store_byte(cpu, 0xefc7 + 6, 0xff);
	store_byte(cpu, 0xefc7 + 7, 0);
	store_byte(cpu, 0xefc7 + 8, 0xf6);
	store_byte(cpu, 0xefc7 + 9, 1);	/*  head bounce delay in msec  */
	store_byte(cpu, 0xefc7 + 10, 1);/*  motor start time in 1/8 secs  */
	store_byte(cpu, 0xefc7 + 11, 1);/*  motor stop time in 1/4 secs  */

	/*  BIOS System Configuration Parameters (8 bytes) at 0xfffd:0:  */
	reload_segment_descriptor(cpu, X86_S_FS, 0xfffd);
	store_byte(cpu, 0, 8); store_byte(cpu, 1, 0);	/*  len  */
	store_byte(cpu, 2, 0xfc);			/*  model  */
	store_byte(cpu, 3, 0);				/*  sub-model  */
	store_byte(cpu, 4, 0);				/*  bios revision  */
	store_byte(cpu, 5, 0x60);			/*  features  */
		/*  see http://members.tripod.com/~oldboard/assembly/
			int_15-c0.html for details  */

	/*  Some info in the last paragraph of the BIOS:  */
	reload_segment_descriptor(cpu, X86_S_FS, 0xffff);
	/*  TODO: current date :-)  */
	store_byte(cpu, 0x05, '0'); store_byte(cpu, 0x06, '1');
	store_byte(cpu, 0x07, '/');
	store_byte(cpu, 0x08, '0'); store_byte(cpu, 0x09, '1');
	store_byte(cpu, 0x0a, '/');
	store_byte(cpu, 0x0b, '0'); store_byte(cpu, 0x0c, '5');
	store_byte(cpu, 0x0e, 0xfc);

	/*  Copy the first 128 chars of the 8x8 VGA font into 0xf000:0xfa6e  */
	reload_segment_descriptor(cpu, X86_S_FS, 0xf000);
	store_buf(cpu, 0xfa6e, (char *)font8x8, 8*128);
	store_buf(cpu, 0xfa6e - 1024, (char *)font8x8 + 1024, 8*128);

	/*
	 *  Initialize all real-mode interrupt vectors to point to somewhere
	 *  within the PC BIOS area (0xf000:0x8yy0), and place an IRET
	 *  instruction (too fool someone who really reads the BIOS memory).
	 */
	for (i=0; i<256; i++) {
		if (i == 0x20)
			i = 0x70;
		if (i == 0x78)
			break;
		reload_segment_descriptor(cpu, X86_S_FS, 0x0000);
		store_16bit_word(cpu, i*4, 0x8000 + i*16);
		store_16bit_word(cpu, i*4 + 2, 0xf000);

		/*  Exceptions: int 0x1e = ptr to disk table, 1f=fonthigh  */
		if (i == 0x1e)
			store_16bit_word(cpu, i*4, 0xefc7);
		if (i == 0x1f)
			store_16bit_word(cpu, i*4, 0xfa6e - 1024);

		reload_segment_descriptor(cpu, X86_S_FS, 0xf000);
		store_byte(cpu, 0x8000 + i*16, 0xCF);	/*  IRET  */
	}

	/*  For SMP emulation, create an "MP" struct in BIOS memory:  */
	if (cpu->machine->ncpus > 1) {
		int chksum;

		reload_segment_descriptor(cpu, X86_S_FS, 0xf000);
		store_buf(cpu, 0x9000, "_MP_", 4);
		store_byte(cpu, 0x9004, 0x10);	/*  ptr to table  */
		store_byte(cpu, 0x9005, 0x90);
		store_byte(cpu, 0x9006, 0x0f);
		store_byte(cpu, 0x9007, 0x00);
		store_byte(cpu, 0x9008, 0x01);	/*  length. should be 1  */
		store_byte(cpu, 0x9009, 0x04);	/*  version. 4 means "1.4"  */
		/*  Byte at 0x0a is checksum. TODO: make this automagic  */
		chksum = '_' + 'M' + 'P' + '_' + 0x10 + 0x90 + 0xf + 1 + 4;
		store_byte(cpu, 0x900a, 0 - chksum);

		/*  TODO: The PCMP struct, at addr 0x9010.  */
	}

	/*  Prepare for text mode: (0x03 = 80x25, 0x01 = 40x25)  */
	set_video_mode(cpu, 0x03);

	/*  See http://members.tripod.com/~oldboard/assembly/bios_data_area.html
	    for more info.  */
	reload_segment_descriptor(cpu, X86_S_FS, 0x0000);
	store_16bit_word(cpu, 0x400, 0x03F8);	/*  COM1  */
	store_16bit_word(cpu, 0x402, 0x0378);	/*  COM2  */
	store_16bit_word(cpu, 0x413, 640);	/*  KB of low RAM  */
	store_byte(cpu, 0x449, cpu->machine->md.pc.videomode);	/* video mode */
	store_16bit_word(cpu, 0x44a, cpu->machine->md.pc.columns);/* columns */
	store_16bit_word(cpu, 0x463, 0x3D4);	/*  CRT base port  */
	store_byte(cpu, 0x484, cpu->machine->md.pc.rows-1);/*  nr of lines-1 */

	cmos_write(cpu, 0x15, 640 & 255);
	cmos_write(cpu, 0x16, 640 >> 8);
	tmp = cpu->machine->physical_ram_in_mb / 1024;
	if (tmp > 63*1024)
		tmp = 63*1024;
	cmos_write(cpu, 0x17, tmp & 255);
	cmos_write(cpu, 0x18, tmp >> 8);

	/*  Clear the screen first:  */
	set_cursor_pos(cpu, 0, 0);
	for (y=0; y<cpu->machine->md.pc.rows; y++)
		for (x=0; x<cpu->machine->md.pc.columns; x++)
			output_char(cpu, x,y, ' ', 0x07);

	nboxlines = cpu->machine->md.pc.columns <= 40? 4 : 3;

	/*  Draw a nice box at the top:  */
	for (y=0; y<nboxlines; y++)
		for (x=0; x<cpu->machine->md.pc.columns; x++) {
			unsigned char ch = ' ';
			if (cpu->machine->use_x11) {
				if (y == 0) {
					ch = 196;
					if (x == 0)
						ch = 218;
					if (x == cpu->machine->md.pc.columns-1)
						ch = 191;
				} else if (y == nboxlines-1) {
					ch = 196;
					if (x == 0)
						ch = 192;
					if (x == cpu->machine->md.pc.columns-1)
						ch = 217;
				} else if (x == 0 || x ==
					    cpu->machine->md.pc.columns-1)
					ch = 179;
			} else {
				if (y == 0 || y == nboxlines-1) {
					ch = '-';
					if (x == 0 || x ==
					    cpu->machine->md.pc.columns-1)
						ch = '+';
				} else {
					if (x == 0 || x ==
					    cpu->machine->md.pc.columns-1)
						ch = '|';
				}
			}
			output_char(cpu, x,y, ch, 0x19);
		}

	sprintf(t, "GXemul");
#ifdef VERSION
	sprintf(t + strlen(t), " "VERSION);
#endif
	set_cursor_pos(cpu, 2, 1);
	pc_bios_printstr(cpu, t, 0x1f);

	sprintf(t, "%i cpu%s (%s), %i MB memory",
	    cpu->machine->ncpus, cpu->machine->ncpus > 1? "s" : "",
	    cpu->cd.x86.model.name, cpu->machine->physical_ram_in_mb);
	if (cpu->machine->md.pc.columns <= 40)
		set_cursor_pos(cpu, 2, 2);
	else
		set_cursor_pos(cpu, 78 - strlen(t), 1);
	pc_bios_printstr(cpu, t, 0x17);
	if (cpu->machine->md.pc.columns <= 40)
		set_cursor_pos(cpu, 0, 5);
	else
		set_cursor_pos(cpu, 0, 4);

	cpu->machine->md.pc.curcolor = 0x07;

	/*  "Detect" Floppies, IDE disks, and SCSI disks:  */
	for (i=0; i<4; i++) {
		if (diskimage_exist(cpu->machine, i, DISKIMAGE_FLOPPY)) {
			struct pc_bios_disk *p;
			p = add_disk(cpu->machine, i, i, DISKIMAGE_FLOPPY);
			sprintf(t, "%c%c", i<2? ('A'+i):' ', i<2? ':':' ');
			pc_bios_printstr(cpu, t, 0xf);
			sprintf(t, " (bios disk %02x)  FLOPPY", i);
			pc_bios_printstr(cpu, t, cpu->machine->md.pc.curcolor);
			sprintf(t, ", %i KB (CHS=%i,%i,%i)", (int)(p->size /
			    1024), p->cylinders, p->heads, p->sectorspertrack);
			pc_bios_printstr(cpu, t, cpu->machine->md.pc.curcolor);
			if (boot_id == i && boot_type == DISKIMAGE_FLOPPY) {
				bios_boot_id = i;
				pc_bios_printstr(cpu, "  [boot device]", 0xf);
			}
			pc_bios_printstr(cpu, "\n",
			    cpu->machine->md.pc.curcolor);
			any_disk = 1;
		}
	}
	disknr = 0x80;
	for (i=0; i<8; i++) {
		if (diskimage_exist(cpu->machine, i, DISKIMAGE_IDE)) {
			struct pc_bios_disk *p;
			p = add_disk(cpu->machine, disknr, i, DISKIMAGE_IDE);
			sprintf(t, "%s", disknr==0x80? "C:" : "  ");
			pc_bios_printstr(cpu, t, 0xf);
			sprintf(t, " (bios disk %02x)  IDE %s, id %i",
			    disknr, diskimage_is_a_cdrom(cpu->machine, i,
				DISKIMAGE_IDE)? "cdrom" : (
			        diskimage_is_a_tape(cpu->machine, i,
				DISKIMAGE_IDE)? "tape" : "disk"),
			    i);
			pc_bios_printstr(cpu, t, cpu->machine->md.pc.curcolor);
			sprintf(t, ", %lli MB", (long long) (p->size >> 20));
			pc_bios_printstr(cpu, t, cpu->machine->md.pc.curcolor);
			if (boot_id == i && boot_type == DISKIMAGE_IDE) {
				bios_boot_id = disknr;
				pc_bios_printstr(cpu, "  [boot device]", 0xf);
			}
			pc_bios_printstr(cpu, "\n",
			    cpu->machine->md.pc.curcolor);
			disknr++;
			any_disk = 1;
		}
	}
	for (i=0; i<8; i++) {
		if (diskimage_exist(cpu->machine, i, DISKIMAGE_SCSI)) {
			struct pc_bios_disk *p;
			p = add_disk(cpu->machine, disknr, i, DISKIMAGE_SCSI);
			sprintf(t, "%s", disknr==0x80? "C:" : "  ");
			pc_bios_printstr(cpu, t, 0xf);
			sprintf(t, " (bios disk %02x)  SCSI disk, id %i",
			    disknr, i);
			pc_bios_printstr(cpu, t, cpu->machine->md.pc.curcolor);
			sprintf(t, ", %lli MB", (long long) (p->size >> 20));
			pc_bios_printstr(cpu, t, cpu->machine->md.pc.curcolor);
			if (boot_id == i && boot_type == DISKIMAGE_SCSI) {
				bios_boot_id = disknr;
				pc_bios_printstr(cpu, "  [boot device]", 0xf);
			}
			pc_bios_printstr(cpu, "\n",
			    cpu->machine->md.pc.curcolor);
			disknr++;
			any_disk = 1;
		}
	}

	if (any_disk)
		pc_bios_printstr(cpu, "\n", cpu->machine->md.pc.curcolor);
	else
		pc_bios_printstr(cpu, "No disks attached!\n\n", 0x0f);

	/*  Registers passed to the bootsector code:  */
	reload_segment_descriptor(cpu, X86_S_CS, 0x0000);
	reload_segment_descriptor(cpu, X86_S_DS, 0x0000);
	reload_segment_descriptor(cpu, X86_S_ES, 0x0000);
	reload_segment_descriptor(cpu, X86_S_SS, 0x0000);

	cpu->cd.x86.r[X86_R_AX] = 0xaa55;
	cpu->cd.x86.r[X86_R_CX] = 0x0001;
	cpu->cd.x86.r[X86_R_DI] = 0xffe4;
	cpu->cd.x86.r[X86_R_SP] = 0xfffe;
	cpu->cd.x86.r[X86_R_DX] = bios_boot_id;

	cpu->cd.x86.rflags |= X86_FLAGS_IF;
	cpu->pc = 0x7c00;

	cpu->machine->md.pc.initialized = 1;
}


/*
 *  pc_bios_emul():
 */
int pc_bios_emul(struct cpu *cpu)
{
	uint32_t addr = (cpu->cd.x86.s[X86_S_CS] << 4) + cpu->pc;
	int int_nr, flags;

	int_nr = (addr >> 4) & 0xff;

	if (cpu->cd.x86.cr[0] & 1) {
		fatal("TODO: BIOS interrupt 0x%02x, but we're not in real-"
		    "mode?\n", int_nr);
		cpu->running = 0;
		return 0;
	}

	switch (int_nr) {
	case 0x02:	/*  NMI?  */
		debug("[ pc_bios: NMI? TODO ]\n");
		break;
	case 0x08:
		if (pc_bios_int8(cpu) == 0)
			return 0;
		break;
	case 0x09:  pc_bios_int9(cpu); break;
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
		pc_bios_printstr(cpu, "Disk boot failed. (INT 0x18 called.)\n",
		    0x07);
		cpu->running = 0;
		break;
	case 0x19:
		pc_bios_printstr(cpu, "Rebooting. (INT 0x19 called.)\n", 0x07);
		cpu->running = 0;
		break;
	case 0x1a:  pc_bios_int1a(cpu); break;
	case 0x1c:  pc_bios_int1c(cpu); break;
	default:
		fatal("FATAL: Unimplemented PC BIOS interrupt 0x%02x.\n",
		    int_nr);
		cpu->running = 0;
		cpu->dead = 1;
		return 0;
	}

	/*
	 *  Return from the interrupt:  Pop ip (pc), cs, and flags.
	 */
	cpu->cd.x86.cursegment = X86_S_SS;
	cpu->pc = load_16bit_word(cpu, cpu->cd.x86.r[X86_R_SP]);
	reload_segment_descriptor(cpu, X86_S_CS,
	    load_16bit_word(cpu, cpu->cd.x86.r[X86_R_SP] + 2));

	/*  Actually, don't pop flags, because they contain result bits
	    from interrupt calls. Only pop the Interrupt Flag.  */
	flags = load_16bit_word(cpu, cpu->cd.x86.r[X86_R_SP] + 4);
	cpu->cd.x86.rflags &= ~X86_FLAGS_IF;
	cpu->cd.x86.rflags |= (flags & X86_FLAGS_IF);

	cpu->cd.x86.r[X86_R_SP] = (cpu->cd.x86.r[X86_R_SP] & ~0xffff)
	    | ((cpu->cd.x86.r[X86_R_SP] + 6) & 0xffff);

	return 1;
}


#endif	/*  ENABLE_X86  */
