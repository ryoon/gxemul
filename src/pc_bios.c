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
 *  $Id: pc_bios.c,v 1.49 2005-05-15 21:58:27 debug Exp $
 *
 *  Generic PC BIOS emulation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "console.h"
#include "cpu.h"
#include "cpu_x86.h"
#include "diskimage.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"


extern int quiet_mode;


#define dec_to_bcd(x) ( (((x) / 10) << 4) + ((x) % 10) )


/*
 *  add_disk():
 */
static void add_disk(struct machine *machine, int biosnr, int id, int type)
{
	uint64_t size, bytespercyl;
	struct pc_bios_disk *p = malloc(sizeof(struct pc_bios_disk));

	if (p == NULL) {
		fprintf(stderr, "add_disk(): out of memory\n");
		exit(1);
	}

	p->next = machine->md.pc.first_disk;
	machine->md.pc.first_disk = p;

	p->nr = biosnr; p->id = id; p->type = type;

	size = diskimage_getsize(machine, id, type);

	switch (type) {
	case DISKIMAGE_FLOPPY:
		/*  TODO: other floppy types? 360KB etc?  */
		p->cylinders = 80;
		p->heads = 2;
		p->sectorspertrack = size / (p->cylinders * p->heads * 512);
		break;
	default:/*  Non-floppies:  */
		p->heads = 15;
		p->sectorspertrack = 63;
		bytespercyl = p->heads * p->sectorspertrack * 512;
		p->cylinders = size / bytespercyl;
		if (p->cylinders * bytespercyl < size)
			p->cylinders ++;
	}
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
	uint64_t addr = (y * 80 + x) * 2 + 0xb8000;
	unsigned char w[2];
	int len = 2;

	w[0] = ch; w[1] = color;
	if (color < 0)
		len = 1;

	cpu->memory_rw(cpu, cpu->mem, addr, &w[0], len, MEM_WRITE,
	    CACHE_NONE | PHYSICAL);
}


/*
 *  set_cursor_pos():
 */
static void set_cursor_pos(struct cpu *cpu, int x, int y)
{
	int addr = y * 80 + x;
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

	*x = addr % 80;
	*y = addr / 80;
}


/*
 *  scroll_up():
 */
static void scroll_up(struct cpu *cpu, int x1, int y1, int x2, int y2, int attr)
{
	int x, y;

	if (x1 < 0)   x1 = 0;
	if (y1 < 0)   y1 = 0;
	if (x2 >= 80) x2 = 79;
	if (y2 >= 25) y2 = 24;

	/*  Scroll up by copying lines:  */
	for (y=y1; y<=y2-1; y++) {
		int addr = 160*y + x1*2 + 0xb8000;
		int len = (x2-x1) * 2 + 2;
		unsigned char w[160];
		addr += 160;
		cpu->memory_rw(cpu, cpu->mem, addr, &w[0], len,
		    MEM_READ, CACHE_NONE | PHYSICAL);
		addr -= 160;
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
	if (x2 >= 80) x2 = 79;
	if (y2 >= 25) y2 = 24;

	/*  Scroll down by copying lines:  */
	for (y=y2; y>=y1+1; y--) {
		int addr = 160*y + x1*2 + 0xb8000;
		int len = (x2-x1) * 2 + 2;
		unsigned char w[160];
		addr -= 160;
		cpu->memory_rw(cpu, cpu->mem, addr, &w[0], len,
		    MEM_READ, CACHE_NONE | PHYSICAL);
		addr += 160;
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
static void pc_bios_putchar(struct cpu *cpu, char ch, int attr)
{
	int x, y;

	/*  Put the character on the screen, move cursor, and so on:  */

	get_cursor_pos(cpu, &x, &y);
	switch (ch) {
	case '\r':	x = -1; break;
	case '\n':	x = 80; break;
	case '\b':	x -= 2; break;
	default:	output_char(cpu, x, y, ch, attr);
	}
	x++;
	if (x < 0)
		x = 0;
	if (x >= 80) {
		x=0; y++;
	}

	if (attr < 0)
		attr = cpu->machine->md.pc.curcolor;

	if (y >= 25) {
		scroll_up(cpu, 0,0, 79,24, attr);
		x = 0; y = 24;
	}
	set_cursor_pos(cpu, x, y);
}


/*
 *  pc_bios_printstr():
 */
static void pc_bios_printstr(struct cpu *cpu, char *s, int attr)
{
	while (*s)
		pc_bios_putchar(cpu, *s++, attr);
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
	int x,y, oldx,oldy;
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
		if (al == 0x02)
			al = 0x03;

		/*  TODO: really change mode  */
		byte = 0xff;
		cpu->memory_rw(cpu, cpu->mem, ctrlregs + 0x14,
		    &byte, sizeof(byte), MEM_WRITE, CACHE_NONE
		    | PHYSICAL);
		byte = al;
		cpu->memory_rw(cpu, cpu->mem, ctrlregs + 0x15,
		    &byte, sizeof(byte), MEM_WRITE, CACHE_NONE |
		    PHYSICAL);

		switch (al) {
		case 0x03:	/*  80x25 color textmode  */
		case 0x19:
			/*  Simply clear the screen and home the cursor
			    for now. TODO: More advanced stuff.  */
			set_cursor_pos(cpu, 0, 0);
			for (y=0; y<25; y++)
				for (x=0; x<80; x++)
					output_char(cpu, x,y, ' ', 0x07);
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
		while (cx-- > 0)
			pc_bios_putchar(cpu, al, bl);
		cpu->machine->md.pc.curcolor = bl;
		break;
	case 0x0b:	/*  set color palette  */
		debug("WARNING: int 0x10, func 0x0b: TODO\n");
		break;
	case 0x0e:	/*  tty output  */
		pc_bios_putchar(cpu, al, -1);
		break;
	case 0x0f:	/*  get video mode  */
		cpu->cd.x86.r[X86_R_AX] = 80 << 8;

		byte = 0xff;
		cpu->memory_rw(cpu, cpu->mem, ctrlregs + 0x14,
		    &byte, sizeof(byte), MEM_WRITE, CACHE_NONE | PHYSICAL);
		cpu->memory_rw(cpu, cpu->mem, ctrlregs + 0x15,
		    &byte, sizeof(byte), MEM_READ, CACHE_NONE | PHYSICAL);
		cpu->cd.x86.r[X86_R_AX] |= byte;

		cpu->cd.x86.r[X86_R_BX] &= ~0xff00;	/*  BH = pagenr  */
		break;
	case 0x11:	/*  Character generator  */
		/*  TODO  */
		switch (al) {
		case 0x12:
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
			cpu->cd.x86.cursegment = cpu->cd.x86.s[X86_S_ES];
			cpu->memory_rw(cpu, cpu->mem, bp, &byte[0], len,
			    MEM_READ, CACHE_DATA | NO_EXCEPTIONS);
			bp += len;
				pc_bios_putchar(cpu, byte[0], byte[1]);
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
	case 0x4f:
		fatal("TODO: int 0x10, function 0x4f\n");
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
		cpu->cd.x86.rflags |= X86_FLAGS_CF;
		disk = get_disk(cpu->machine, cpu->cd.x86.r[X86_R_DX] & 0xff);
		if (disk != NULL) {
			cpu->cd.x86.rflags &= ~X86_FLAGS_CF;
			ch = ch + ((cl >> 6) << 8);
			cl = (cl & 0x3f) - 1;
			offset = (cl + disk->sectorspertrack * dh +
			    disk->sectorspertrack * disk->heads * ch) * 512;
			nread = 0; err = 0;
			debug("[ pc_bios_int13(): reading from disk 0x%x, "
			    "CHS=%i,%i,%i ]\n", dl, ch, dh, cl);
			if (cl > 18 || dh > 1 || ch > 79) {
				al = 0; err = 4;  /*  sector not found  */
			}
			while (al > 0) {
				unsigned char buf[512];

				debug("[ pc_bios_int13(): disk offset=0x%llx,"
				    " mem=0x%04x:0x%04x ]\n", (long long)offset,
				    cpu->cd.x86.s[X86_S_ES], bx);

				res = diskimage_access(cpu->machine, disk->id,
				    disk->type, 0, offset, buf, sizeof(buf));

				if (!res) {
					err = 4;
					fatal("[ PC BIOS: disk access failed: "
					    "disk %i, CHS = %i,%i,%i ]\n", dl,
					    ch, dh, cl);
					break;
				}

				cpu->cd.x86.cursegment =
				    cpu->cd.x86.s[X86_S_ES];
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
	int cx = cpu->cd.x86.r[X86_R_CX] & 0xffff;
	int si = cpu->cd.x86.r[X86_R_SI] & 0xffff;
	unsigned char src_entry[8];
	unsigned char dst_entry[8];
	uint32_t src_addr, dst_addr;

	switch (ah) {
	case 0x41:	/*  TODO  */
		fatal("[ PC BIOS int 0x15,0x41: TODO ]\n");
		cpu->cd.x86.rflags |= X86_FLAGS_CF;
		break;
	case 0x53:	/*  TODO  */
		fatal("[ PC BIOS int 0x15,0x53: TODO ]\n");
		cpu->cd.x86.rflags |= X86_FLAGS_CF;
		break;
	case 0x87:	/*  Move to/from extended memory, via a GDT  */
		cpu->cd.x86.cursegment = cpu->cd.x86.s[X86_S_ES];
		cpu->memory_rw(cpu, cpu->mem, si + 0x10, src_entry, 8,
		    MEM_READ, CACHE_DATA);
		cpu->memory_rw(cpu, cpu->mem, si + 0x18, dst_entry, 8,
		    MEM_READ, CACHE_DATA);
		src_addr = src_entry[2]+(src_entry[3]<<8)+(src_entry[4]<<16);
		dst_addr = dst_entry[2]+(dst_entry[3]<<8)+(dst_entry[4]<<16);
		if (src_entry[5] != 0x93)
			fatal("WARNING: int15,87: bad src access right?\n");
		if (dst_entry[5] != 0x93)
			fatal("WARNING: int15,87: bad dst access right?\n");
		debug("[ pc_bios: INT15: copying %i bytes from 0x%x to 0x%x"
		    " ]\n", cx*2, src_addr, dst_addr);
		while (cx*2 > 0) {
			unsigned char buf[2];
			cpu->memory_rw(cpu, cpu->mem, src_addr, buf, 2,
			    MEM_READ, CACHE_DATA);
			cpu->memory_rw(cpu, cpu->mem, dst_addr, buf, 2,
			    MEM_READ, CACHE_DATA);
			src_addr += 2; dst_addr += 2; cx --;
		}
		cpu->cd.x86.r[X86_R_AX] &= ~0xff00;
		cpu->cd.x86.rflags &= ~X86_FLAGS_CF;
		cpu->cd.x86.rflags |= X86_FLAGS_ZF;
		break;
	case 0x88:	/*  Extended Memory Size Determination  */
		cpu->cd.x86.rflags &= ~X86_FLAGS_CF;
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
	int ah = (cpu->cd.x86.r[X86_R_AX] >> 8) & 0xff;
	/*  int al = cpu->cd.x86.r[X86_R_AX] & 0xff;  */
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
	time_t tim;
	struct tm *tm;

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
		cpu->cd.x86.r[X86_R_DX] |= (dec_to_bcd(tm->tm_mon) << 8) |
		    dec_to_bcd(tm->tm_mday + 1);
		cpu->cd.x86.rflags &= ~X86_FLAGS_CF;
		break;
	default:
		fatal("FATAL: Unimplemented PC BIOS interrupt 0x1a function"
		    " 0x%02x.\n", ah);
		cpu->running = 0;
		cpu->dead = 1;
	}
}


/*
 *  pc_bios_init():
 */
void pc_bios_init(struct cpu *cpu)
{
	char t[80];
	int i, any_disk = 0, disknr;
	int boot_id, boot_type, bios_boot_id = 0;

	boot_id = diskimage_bootdev(cpu->machine, &boot_type);

	if (cpu->machine->md.pc.initialized) {
		fatal("ERROR: pc_bios_init(): Already initialized.\n");
		return;
	}

	pc_bios_printstr(cpu, "GXemul", 0x0f);
#ifdef VERSION
	pc_bios_printstr(cpu, " "VERSION, 0x0f);
#endif
	pc_bios_printstr(cpu, "   PC BIOS software emulation\n", 0x0f);

	sprintf(t, "%i cpu%s (%s), %i MB memory\n\n",
	    cpu->machine->ncpus, cpu->machine->ncpus > 1? "s" : "",
	    cpu->cd.x86.model.name, cpu->machine->physical_ram_in_mb);
	pc_bios_printstr(cpu, t, 0x07);

	cpu->machine->md.pc.curcolor = 0x07;

	/*  "Detect" Floppies, IDE disks, and SCSI disks:  */
	for (i=0; i<4; i++) {
		if (diskimage_exist(cpu->machine, i, DISKIMAGE_FLOPPY)) {
			add_disk(cpu->machine, i, i, DISKIMAGE_FLOPPY);
			sprintf(t, "%c%c (bios disk %02x)  FLOPPY",
			    i<2? ('A'+i) : ' ', i<2? ':' : ' ', i);
			pc_bios_printstr(cpu, t, cpu->machine->md.pc.curcolor);
			if (boot_id == i && boot_type == DISKIMAGE_FLOPPY) {
				bios_boot_id = i;
				pc_bios_printstr(cpu, " (boot device)",
				    cpu->machine->md.pc.curcolor);
			}
			pc_bios_printstr(cpu, "\n",
			    cpu->machine->md.pc.curcolor);
			any_disk = 1;
		}
	}
	disknr = 0x80;
	for (i=0; i<8; i++) {
		if (diskimage_exist(cpu->machine, i, DISKIMAGE_IDE)) {
			add_disk(cpu->machine, disknr, i, DISKIMAGE_IDE);
			sprintf(t, "%s (bios disk %02x)  IDE %s, id %i",
			    disknr==0x80? "C:" : "  ", disknr,
			    diskimage_is_a_cdrom(cpu->machine, i,
				DISKIMAGE_IDE)? "cdrom" : (
			        diskimage_is_a_tape(cpu->machine, i,
				DISKIMAGE_IDE)? "tape" : "disk"),
			    i);
			pc_bios_printstr(cpu, t, cpu->machine->md.pc.curcolor);
			if (boot_id == i && boot_type == DISKIMAGE_IDE) {
				bios_boot_id = disknr;
				pc_bios_printstr(cpu, " (boot device)",
				    cpu->machine->md.pc.curcolor);
			}
			pc_bios_printstr(cpu, "\n",
			    cpu->machine->md.pc.curcolor);
			disknr++;
			any_disk = 1;
		}
	}
	for (i=0; i<8; i++) {
		if (diskimage_exist(cpu->machine, i, DISKIMAGE_SCSI)) {
			add_disk(cpu->machine, disknr, i, DISKIMAGE_SCSI);
			sprintf(t, "%s (bios disk %02x)  SCSI disk, id %i",
			    disknr==0x80? "C:" : "  ", disknr, i);
			pc_bios_printstr(cpu, t, cpu->machine->md.pc.curcolor);
			if (boot_id == i && boot_type == DISKIMAGE_SCSI) {
				bios_boot_id = disknr;
				pc_bios_printstr(cpu, " (boot device)",
				    cpu->machine->md.pc.curcolor);
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
		pc_bios_printstr(cpu, "No disks attached.\n\n",
		    cpu->machine->md.pc.curcolor);

	/*  Registers passed to the bootsector code:  */
	cpu->cd.x86.r[X86_R_AX] = 0xaa55;
	cpu->cd.x86.r[X86_R_CX] = 0x0001;
	cpu->cd.x86.r[X86_R_DI] = 0xffe4;
	cpu->cd.x86.r[X86_R_SP] = 0xfffe;
	cpu->cd.x86.r[X86_R_DX] = bios_boot_id;

	cpu->machine->md.pc.initialized = 1;
}


/*
 *  pc_bios_emul():
 */
int pc_bios_emul(struct cpu *cpu)
{
	uint32_t addr = (cpu->cd.x86.s[X86_S_CS] << 4) + cpu->pc;
	int int_nr;

	int_nr = addr & 0xff;

	if (!cpu->machine->md.pc.initialized)
		pc_bios_init(cpu);

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
		pc_bios_printstr(cpu, "Disk boot failed. (INT 0x18 called.)\n",
		    0x07);
		cpu->running = 0;
		break;
	case 0x19:
		pc_bios_printstr(cpu, "Rebooting. (INT 0x19 called.)\n", 0x07);
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

