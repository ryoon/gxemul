/*
 *  Copyright (C) 2003-2004  Anders Gavare.  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright  
 *     notice, this list of conditions and the following disclaimer in the 
 *     documentation and/or other materials provided with the distribution.
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
 *  $Id: arcbios.c,v 1.44 2004-12-05 14:17:21 debug Exp $
 *
 *  ARCBIOS emulation.
 *
 *  This whole file is a mess.
 *
 *  TODO:  Fix.
 *
 *  TODO:  FACTOR OUT COMMON PARTS OF THE 64-bit and 32-bit stuff!!
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <sys/resource.h>
#include <unistd.h>

#include "misc.h"

#include "console.h"
#include "diskimage.h"
#include "memory.h"


extern int quiet_mode;


/*  For reading from the boot partition.  */
static uint64_t arcbios_current_seek_offset = 0;


struct emul_arc_child {
	uint32_t			ptr_peer;
	uint32_t			ptr_child;
	uint32_t			ptr_parent;
	struct arcbios_component	component;
};

struct emul_arc_child64 {
	uint64_t			ptr_peer;
	uint64_t			ptr_child;
	uint64_t			ptr_parent;
	struct arcbios_component64	component;
};

static int arc_64bit = 0;		/*  For some SGI modes  */
static int arc_wordlen = sizeof(uint32_t);

extern int arc_n_memdescriptors;
static uint64_t arcbios_memdescriptor_base = ARC_MEMDESC_ADDR;

static uint64_t arcbios_next_component_address = FIRST_ARC_COMPONENT;
static int n_arc_components = 0;

static uint64_t arcbios_console_vram = 0;
static uint64_t arcbios_console_ctrlregs = 0;
#define MAX_ESC		16
static char arcbios_escape_sequence[MAX_ESC+1];
static int arcbios_in_escape_sequence;
static int arcbios_console_maxx, arcbios_console_maxy;
int arcbios_console_curx = 0, arcbios_console_cury = 0;
int arcbios_console_curcolor = 0x1f;


/*
 *  arcbios_putcell():
 */
static void arcbios_putcell(struct cpu *cpu, int ch, int x, int y)
{
	unsigned char buf[2];
	buf[0] = ch;
	buf[1] = arcbios_console_curcolor;
	memory_rw(cpu, cpu->mem, arcbios_console_vram +
	    2*(x + arcbios_console_maxx * y),
	    &buf[0], sizeof(buf), MEM_WRITE,
	    CACHE_NONE | PHYSICAL);
}


/*
 *  arcbios_console_init():
 *
 *  Called from machine.c whenever an ARC-based machine is running with
 *  a graphical VGA-style framebuffer, which can be used as console.
 */
void arcbios_console_init(struct cpu *cpu,
	uint64_t vram, uint64_t ctrlregs, int maxx, int maxy)
{
	int x, y;

	arcbios_console_vram = vram;
	arcbios_console_ctrlregs = ctrlregs;
	arcbios_console_maxx = maxx;
	arcbios_console_maxy = maxy;
	arcbios_in_escape_sequence = 0;
	arcbios_escape_sequence[0] = '\0';

	for (y=0; y<2; y++)
		for (x=0; x<arcbios_console_maxx; x++) {
			char ch = ' ';
			char *s = " mips64emul"
#ifdef VERSION
			    "-" VERSION
#endif
			    " ARC text console ";

			if (y == 0) {
				arcbios_console_curcolor = 0x70;
				if (x < strlen(s))
					ch = s[x];
			} else
				arcbios_console_curcolor = 0x07;

			arcbios_putcell(cpu, ch, x, y);
		}

	arcbios_console_curx = 0;
	arcbios_console_cury = 18;
	arcbios_console_curcolor = 0x07;
}


/*
 *  handle_esc_seq():
 *
 *  Used by arcbios_putchar().
 */
static void handle_esc_seq(struct cpu *cpu)
{
	int i, len = strlen(arcbios_escape_sequence);
	int row, col, color, code, start, stop;
	char *p;

	if (arcbios_escape_sequence[0] != '[')
		return;

	code = arcbios_escape_sequence[len-1];
	arcbios_escape_sequence[len-1] = '\0';

	switch (code) {
	case 'm':
		color = atoi(arcbios_escape_sequence + 1);
		switch (color) {
		case 0:	/*  Default.  */
			arcbios_console_curcolor = 0x07; break;
		case 1:	/*  "Bold".  */
			arcbios_console_curcolor |= 0x80; break;
		case 30: /*  Black foreground.  */
			arcbios_console_curcolor &= 0xf0;
			arcbios_console_curcolor |= 0x00; break;
		case 31: /*  Red foreground.  */
			arcbios_console_curcolor &= 0xf0;
			arcbios_console_curcolor |= 0x04; break;
		case 32: /*  Green foreground.  */
			arcbios_console_curcolor &= 0xf0;
			arcbios_console_curcolor |= 0x02; break;
		case 33: /*  Yellow foreground.  */
			arcbios_console_curcolor &= 0xf0;
			arcbios_console_curcolor |= 0x06; break;
		case 34: /*  Blue foreground.  */
			arcbios_console_curcolor &= 0xf0;
			arcbios_console_curcolor |= 0x01; break;
		case 35: /*  Red-blue foreground.  */
			arcbios_console_curcolor &= 0xf0;
			arcbios_console_curcolor |= 0x05; break;
		case 36: /*  Green-blue foreground.  */
			arcbios_console_curcolor &= 0xf0;
			arcbios_console_curcolor |= 0x03; break;
		case 37: /*  White foreground.  */
			arcbios_console_curcolor &= 0xf0;
			arcbios_console_curcolor |= 0x07; break;
		case 40: /*  Black background.  */
			arcbios_console_curcolor &= 0x0f;
			arcbios_console_curcolor |= 0x00; break;
		case 41: /*  Red background.  */
			arcbios_console_curcolor &= 0x0f;
			arcbios_console_curcolor |= 0x40; break;
		case 42: /*  Green background.  */
			arcbios_console_curcolor &= 0x0f;
			arcbios_console_curcolor |= 0x20; break;
		case 43: /*  Yellow background.  */
			arcbios_console_curcolor &= 0x0f;
			arcbios_console_curcolor |= 0x60; break;
		case 44: /*  Blue background.  */
			arcbios_console_curcolor &= 0x0f;
			arcbios_console_curcolor |= 0x10; break;
		case 45: /*  Red-blue background.  */
			arcbios_console_curcolor &= 0x0f;
			arcbios_console_curcolor |= 0x50; break;
		case 46: /*  Green-blue background.  */
			arcbios_console_curcolor &= 0x0f;
			arcbios_console_curcolor |= 0x30; break;
		case 47: /*  White background.  */
			arcbios_console_curcolor &= 0x0f;
			arcbios_console_curcolor |= 0x70; break;
		default:fatal("{ handle_esc_seq: color %i }\n", color);
		}
		return;
	case 'H':
		p = strchr(arcbios_escape_sequence, ';');
		if (p == NULL)
			return;		/*  TODO  */
		row = atoi(arcbios_escape_sequence + 1);
		col = atoi(p + 1);
		arcbios_console_curx = col - 1;
		arcbios_console_cury = row - 1;
		return;
	case 'K':
		col = atoi(arcbios_escape_sequence + 1);
		/*  2 = clear line to the right. 1 = to the left (?)  */
		start = 0; stop = arcbios_console_curx;
		if (col == 2) {
			start = arcbios_console_curx;
			stop = arcbios_console_maxx-1;
		}
		for (i=start; i<=stop; i++)
			arcbios_putcell(cpu, ' ', i, arcbios_console_cury);

		return;
	}

	fatal("{ handle_esc_seq(): unimplemented escape sequence: ");
	for (i=0; i<len; i++) {
		int x = arcbios_escape_sequence[i];
		if (i == len-1)
			x = code;

		if (x >= ' ' && x < 127)
			fatal("%c", x);
		else
			fatal("[0x%02x]", x);
	}
	fatal(" }\n");
}


/*
 *  scroll_if_neccessary():
 */
static void scroll_if_neccessary(struct cpu *cpu)
{
	/*  Scroll?  */
	if (arcbios_console_cury >= arcbios_console_maxy) {
		unsigned char buf[2];
		int x, y;
		for (y=0; y<arcbios_console_maxy-1; y++)
			for (x=0; x<arcbios_console_maxx; x++) {
				memory_rw(cpu, cpu->mem, arcbios_console_vram +
				    2*(x + arcbios_console_maxx * (y+1)),
				    &buf[0], sizeof(buf), MEM_READ,
				    CACHE_NONE | PHYSICAL);
				memory_rw(cpu, cpu->mem, arcbios_console_vram +
				    2*(x + arcbios_console_maxx * y),
				    &buf[0], sizeof(buf), MEM_WRITE,
				    CACHE_NONE | PHYSICAL);
			}

		arcbios_console_cury = arcbios_console_maxy - 1;

		for (x=0; x<arcbios_console_maxx; x++)
			arcbios_putcell(cpu, ' ', x, arcbios_console_cury);
	}
}


/*
 *  arcbios_putchar():
 *
 *  If we're using X11 with VGA-style console, then output to that console.
 *  Otherwise, use console_putchar().
 */
static void arcbios_putchar(struct cpu *cpu, int ch)
{
	int addr;
	unsigned char byte;

	if (!cpu->emul->use_x11) {
		/*  Text console output:  */

		/*  SUPER-ugly hack for Windows NT, which uses
		    0x9b instead of ESC + [  */
		if (ch == 0x9b) {
			console_putchar(27);
			ch = '[';
		}
		console_putchar(ch);
		return;
	}

	if (arcbios_in_escape_sequence) {
		int len = strlen(arcbios_escape_sequence);
		arcbios_escape_sequence[len] = ch;
		len++;
		if (len >= MAX_ESC)
			len = MAX_ESC;
		arcbios_escape_sequence[len] = '\0';
		if ((ch >= 'a' && ch <= 'z') ||
		    (ch >= 'A' && ch <= 'Z') || len >= MAX_ESC) {
			handle_esc_seq(cpu);
			arcbios_in_escape_sequence = 0;
		}
	} else {
		if (ch == 27) {
			arcbios_in_escape_sequence = 1;
			arcbios_escape_sequence[0] = '\0';
		} else if (ch == 0x9b) {
			arcbios_in_escape_sequence = 1;
			arcbios_escape_sequence[0] = '[';
			arcbios_escape_sequence[1] = '\0';
		} else if (ch == '\b') {
			if (arcbios_console_curx > 0)
				arcbios_console_curx --;
		} else if (ch == '\r') {
			arcbios_console_curx = 0;
		} else if (ch == '\n') {
			arcbios_console_cury ++;
		} else if (ch == '\t') {
			arcbios_console_curx =
			    ((arcbios_console_curx - 1) | 7) + 1;
			/*  TODO: Print spaces?  */
		} else {
			/*  Put char:  */
			if (arcbios_console_curx >= arcbios_console_maxx) {
				arcbios_console_curx = 0;
				arcbios_console_cury ++;
				scroll_if_neccessary(cpu);
			}
			arcbios_putcell(cpu, ch, arcbios_console_curx,
			    arcbios_console_cury);
			arcbios_console_curx ++;
		}
	}

	scroll_if_neccessary(cpu);

	/*  Update cursor position:  */
	addr = (arcbios_console_curx >= arcbios_console_maxx?
	    arcbios_console_maxx-1 : arcbios_console_curx) +
	    arcbios_console_cury * arcbios_console_maxx;
	byte = 0x0e;
	memory_rw(cpu, cpu->mem, arcbios_console_ctrlregs + 0x14,
	    &byte, sizeof(byte), MEM_WRITE, CACHE_NONE | PHYSICAL);
	byte = (addr >> 8) & 255;
	memory_rw(cpu, cpu->mem, arcbios_console_ctrlregs + 0x15,
	    &byte, sizeof(byte), MEM_WRITE, CACHE_NONE | PHYSICAL);
	byte = 0x0f;
	memory_rw(cpu, cpu->mem, arcbios_console_ctrlregs + 0x14,
	    &byte, sizeof(byte), MEM_WRITE, CACHE_NONE | PHYSICAL);
	byte = addr & 255;
	memory_rw(cpu, cpu->mem, arcbios_console_ctrlregs + 0x15,
	    &byte, sizeof(byte), MEM_WRITE, CACHE_NONE | PHYSICAL);
}


/*
 *  arcbios_add_memory_descriptor():
 *
 *  NOTE: arctype is the ARC type, not the SGI type. This function takes
 *  care of converting, when neccessary.
 */
void arcbios_add_memory_descriptor(struct cpu *cpu,
	uint64_t base, uint64_t len, int arctype)
{
	uint64_t memdesc_addr;
	int s;
	struct arcbios_mem arcbios_mem;
	struct arcbios_mem64 arcbios_mem64;

	base /= 4096;
	len /= 4096;

/*  TODO: Huh? Why isn't it neccessary to convert from arc to sgi types?  */
#if 0
	if (cpu->emul->emulation_type == EMULTYPE_SGI) {
		/*  arctype is SGI style  */
printf("%i => ", arctype);
		switch (arctype) {
		case 0:	arctype = 0; break;
		case 1:	arctype = 1; break;
		case 2:	arctype = 3; break;
		case 3:	arctype = 4; break;
		case 4:	arctype = 5; break;
		case 5:	arctype = 6; break;
		case 6:	arctype = 7; break;
		case 7:	arctype = 2; break;
		}
printf("%i\n", arctype);
	}
#endif
	if (arc_64bit)
		s = sizeof(arcbios_mem64);
	else
		s = sizeof(arcbios_mem);

	memdesc_addr = arcbios_memdescriptor_base +
	    arc_n_memdescriptors * s;

	if (arc_64bit) {
		memset(&arcbios_mem64, 0, s);
		store_32bit_word_in_host(cpu,
		    (unsigned char *)&arcbios_mem64.Type, arctype);
		store_64bit_word_in_host(cpu,
		    (unsigned char *)&arcbios_mem64.BasePage, base);
		store_64bit_word_in_host(cpu,
		    (unsigned char *)&arcbios_mem64.PageCount, len);
		store_buf(cpu, memdesc_addr, (char *)&arcbios_mem64, s);
	} else {
		memset(&arcbios_mem, 0, s);
		store_32bit_word_in_host(cpu,
		    (unsigned char *)&arcbios_mem.Type, arctype);
		store_32bit_word_in_host(cpu,
		    (unsigned char *)&arcbios_mem.BasePage, base);
		store_32bit_word_in_host(cpu,
		    (unsigned char *)&arcbios_mem.PageCount, len);
		store_buf(cpu, memdesc_addr, (char *)&arcbios_mem, s);
	}

	arc_n_memdescriptors ++;
}


/*
 *  arcbios_addchild():
 *
 *  host_tmp_component is a temporary component, with data formated for
 *  the host system.  It needs to be translated/copied into emulated RAM.
 *
 *  Return value is the virtual (emulated) address of the added component.
 *
 *  TODO:  This function doesn't care about memory management, but simply
 *         stores the new child after the last stored child.
 *  TODO:  This stuff is really ugly.
 */
uint64_t arcbios_addchild(struct cpu *cpu,
	struct arcbios_component *host_tmp_component,
	char *identifier, uint32_t parent)
{
	uint64_t a = arcbios_next_component_address;
	uint32_t peer=0;
	uint32_t child=0;
	int n_left;
	uint64_t peeraddr = FIRST_ARC_COMPONENT;

	/*
	 *  This component has no children yet, but it may have peers (that is,
	 *  other components that share this component's parent) so we have to
	 *  set the peer value correctly.
	 *
	 *  Also, if this is the first child of some parent, the parent's child
	 *  pointer should be set to point to this component.  (But only if it
	 *  is the first.)
	 *
	 *  This is really ugly:  scan through all components, starting from
	 *  FIRST_ARC_COMPONENT, to find a component with the same parent as
	 *  this component will have.  If such a component is found, and its
	 *  'peer' value is NULL, then set it to this component's address (a).
	 *
	 *  TODO:  make this nicer
	 */

	n_left = n_arc_components;
	while (n_left > 0) {
		/*  Load parent, child, and peer values:  */
		uint32_t eparent, echild, epeer, tmp;
		unsigned char buf[4];

		/*  debug("[ addchild: peeraddr = 0x%08x ]\n", peeraddr);  */

		memory_rw(cpu, cpu->mem,
		    peeraddr + 0 * arc_wordlen, &buf[0], sizeof(eparent),
		    MEM_READ, CACHE_NONE);
		if (cpu->byte_order == EMUL_BIG_ENDIAN) {
			unsigned char tmp;
			tmp = buf[0]; buf[0] = buf[3]; buf[3] = tmp;
			tmp = buf[1]; buf[1] = buf[2]; buf[2] = tmp;
		}
		epeer   = buf[0] + (buf[1]<<8) + (buf[2]<<16) + (buf[3]<<24);

		memory_rw(cpu, cpu->mem, peeraddr + 1 * arc_wordlen, &buf[0], sizeof(eparent), MEM_READ, CACHE_NONE);
		if (cpu->byte_order == EMUL_BIG_ENDIAN) {
			unsigned char tmp; tmp = buf[0]; buf[0] = buf[3]; buf[3] = tmp;
			tmp = buf[1]; buf[1] = buf[2]; buf[2] = tmp;
		}
		echild  = buf[0] + (buf[1]<<8) + (buf[2]<<16) + (buf[3]<<24);

		memory_rw(cpu, cpu->mem, peeraddr + 2 * arc_wordlen, &buf[0], sizeof(eparent), MEM_READ, CACHE_NONE);
		if (cpu->byte_order == EMUL_BIG_ENDIAN) {
			unsigned char tmp; tmp = buf[0]; buf[0] = buf[3]; buf[3] = tmp;
			tmp = buf[1]; buf[1] = buf[2]; buf[2] = tmp;
		}
		eparent = buf[0] + (buf[1]<<8) + (buf[2]<<16) + (buf[3]<<24);

		/*  debug("  epeer=%x echild=%x eparent=%x\n", epeer,echild,eparent);  */

		if ((uint32_t)eparent == (uint32_t)parent && (uint32_t)epeer == 0) {
			epeer = a;
			store_32bit_word(cpu, peeraddr + 0x00, epeer);
			/*  debug("[ addchild: adding 0x%08x as peer to 0x%08x ]\n", a, peeraddr);  */
		}
		if ((uint32_t)peeraddr == (uint32_t)parent && (uint32_t)echild == 0) {
			echild = a;
			store_32bit_word(cpu, peeraddr + 0x04, echild);
			/*  debug("[ addchild: adding 0x%08x as child to 0x%08x ]\n", a, peeraddr);  */
		}

		/*  Go to the next component:  */
		memory_rw(cpu, cpu->mem, peeraddr + 0x28, &buf[0], sizeof(eparent), MEM_READ, CACHE_NONE);
		if (cpu->byte_order == EMUL_BIG_ENDIAN) {
			unsigned char tmp; tmp = buf[0]; buf[0] = buf[3]; buf[3] = tmp;
			tmp = buf[1]; buf[1] = buf[2]; buf[2] = tmp;
		}
		tmp = buf[0] + (buf[1]<<8) + (buf[2]<<16) + (buf[3]<<24);
		peeraddr += 0x30;
		peeraddr += tmp + 1;
		peeraddr = ((peeraddr - 1) | 3) + 1;

		n_left --;
	}

	store_32bit_word(cpu, a + 0x00, peer);
	store_32bit_word(cpu, a + 0x04, child);
	store_32bit_word(cpu, a + 0x08, parent);
	store_32bit_word(cpu, a+  0x0c, host_tmp_component->Class);
	store_32bit_word(cpu, a+  0x10, host_tmp_component->Type);
	store_32bit_word(cpu, a+  0x14, host_tmp_component->Flags + 65536 * host_tmp_component->Version);
	store_32bit_word(cpu, a+  0x18, host_tmp_component->Revision);
	store_32bit_word(cpu, a+  0x1c, host_tmp_component->Key);
	store_32bit_word(cpu, a+  0x20, host_tmp_component->AffinityMask);
	store_32bit_word(cpu, a+  0x24, host_tmp_component->ConfigurationDataSize);
	store_32bit_word(cpu, a+  0x28, host_tmp_component->IdentifierLength);
	store_32bit_word(cpu, a+  0x2c, host_tmp_component->Identifier);

	arcbios_next_component_address += 0x30;

	if (host_tmp_component->IdentifierLength != 0) {
		store_32bit_word(cpu, a + 0x2c, a + 0x30);
		store_string(cpu, a + 0x30, identifier);
		if (identifier != NULL)
			arcbios_next_component_address += strlen(identifier) + 1;
	}

	arcbios_next_component_address ++;

	/*  Round up to next 0x4 bytes:  */
	arcbios_next_component_address = ((arcbios_next_component_address - 1) | 3) + 1;

	n_arc_components ++;

	return a;
}


/*
 *  arcbios_addchild64():
 *
 *  host_tmp_component is a temporary component, with data formated for
 *  the host system.  It needs to be translated/copied into emulated RAM.
 *
 *  Return value is the virtual (emulated) address of the added component.
 *
 *  TODO:  This function doesn't care about memory management, but simply
 *         stores the new child after the last stored child.
 *  TODO:  This stuff is really ugly.
 */
uint64_t arcbios_addchild64(struct cpu *cpu,
	struct arcbios_component64 *host_tmp_component,
	char *identifier, uint64_t parent)
{
	uint64_t a = arcbios_next_component_address;
	uint64_t peer=0;
	uint64_t child=0;
	int n_left;
	uint64_t peeraddr = FIRST_ARC_COMPONENT;

	/*
	 *  This component has no children yet, but it may have peers (that is,
	 *  other components that share this component's parent) so we have to
	 *  set the peer value correctly.
	 *
	 *  Also, if this is the first child of some parent, the parent's child
	 *  pointer should be set to point to this component.  (But only if it
	 *  is the first.)
	 *
	 *  This is really ugly:  scan through all components, starting from
	 *  FIRST_ARC_COMPONENT, to find a component with the same parent as
	 *  this component will have.  If such a component is found, and its
	 *  'peer' value is NULL, then set it to this component's address (a).
	 *
	 *  TODO:  make this nicer
	 */

	n_left = n_arc_components;
	while (n_left > 0) {
		/*  Load parent, child, and peer values:  */
		uint64_t eparent, echild, epeer, tmp;
		unsigned char buf[8];

		/*  debug("[ addchild: peeraddr = 0x%016llx ]\n", (long long)peeraddr);  */

		memory_rw(cpu, cpu->mem,
		    peeraddr + 0 * arc_wordlen, &buf[0], sizeof(eparent),
		    MEM_READ, CACHE_NONE);
		if (cpu->byte_order == EMUL_BIG_ENDIAN) {
			unsigned char tmp;
			tmp = buf[0]; buf[0] = buf[7]; buf[7] = tmp;
			tmp = buf[1]; buf[1] = buf[6]; buf[6] = tmp;
			tmp = buf[2]; buf[2] = buf[5]; buf[5] = tmp;
			tmp = buf[3]; buf[3] = buf[4]; buf[4] = tmp;
		}
		epeer   = buf[0] + (buf[1]<<8) + (buf[2]<<16) + (buf[3]<<24)
		    + ((uint64_t)buf[4] << 32) + ((uint64_t)buf[5] << 40)
		    + ((uint64_t)buf[6] << 48) + ((uint64_t)buf[7] << 56);

		memory_rw(cpu, cpu->mem, peeraddr +
		    1 * arc_wordlen, &buf[0], sizeof(eparent), MEM_READ, CACHE_NONE);
		if (cpu->byte_order == EMUL_BIG_ENDIAN) {
			unsigned char tmp;
			tmp = buf[0]; buf[0] = buf[7]; buf[7] = tmp;
			tmp = buf[1]; buf[1] = buf[6]; buf[6] = tmp;
			tmp = buf[2]; buf[2] = buf[5]; buf[5] = tmp;
			tmp = buf[3]; buf[3] = buf[4]; buf[4] = tmp;
		}
		echild  = buf[0] + (buf[1]<<8) + (buf[2]<<16) + (buf[3]<<24)
		    + ((uint64_t)buf[4] << 32) + ((uint64_t)buf[5] << 40)
		    + ((uint64_t)buf[6] << 48) + ((uint64_t)buf[7] << 56);

		memory_rw(cpu, cpu->mem, peeraddr +
		    2 * arc_wordlen, &buf[0], sizeof(eparent), MEM_READ, CACHE_NONE);
		if (cpu->byte_order == EMUL_BIG_ENDIAN) {
			unsigned char tmp;
			tmp = buf[0]; buf[0] = buf[7]; buf[7] = tmp;
			tmp = buf[1]; buf[1] = buf[6]; buf[6] = tmp;
			tmp = buf[2]; buf[2] = buf[5]; buf[5] = tmp;
			tmp = buf[3]; buf[3] = buf[4]; buf[4] = tmp;
		}
		eparent = buf[0] + (buf[1]<<8) + (buf[2]<<16) + (buf[3]<<24)
		    + ((uint64_t)buf[4] << 32) + ((uint64_t)buf[5] << 40)
		    + ((uint64_t)buf[6] << 48) + ((uint64_t)buf[7] << 56);

		/*  debug("  epeer=%x echild=%x eparent=%x\n", epeer,echild,eparent);  */

		if (eparent == parent && epeer == 0) {
			epeer = a;
			store_64bit_word(cpu, peeraddr + 0 * arc_wordlen, epeer);
			/*  debug("[ addchild: adding 0x%016llx as peer to 0x%016llx ]\n", (long long)a, (long long)peeraddr);  */
		}
		if (peeraddr == parent && echild == 0) {
			echild = a;
			store_64bit_word(cpu, peeraddr + 1 * arc_wordlen, echild);
			/*  debug("[ addchild: adding 0x%016llx as child to 0x%016llx ]\n", (long long)a, (long long)peeraddr);  */
		}

		/*  Go to the next component:  */
		memory_rw(cpu, cpu->mem, peeraddr + 0x40,
				&buf[0], sizeof(eparent), MEM_READ, CACHE_NONE);
		if (cpu->byte_order == EMUL_BIG_ENDIAN) {
			unsigned char tmp;
			tmp = buf[0]; buf[0] = buf[7]; buf[7] = tmp;
			tmp = buf[1]; buf[1] = buf[6]; buf[6] = tmp;
			tmp = buf[2]; buf[2] = buf[5]; buf[5] = tmp;
			tmp = buf[3]; buf[3] = buf[4]; buf[4] = tmp;
		}
		tmp = buf[0] + (buf[1]<<8) + (buf[2]<<16) + (buf[3]<<24)
		    + ((uint64_t)buf[4] << 32) + ((uint64_t)buf[5] << 40)
		    + ((uint64_t)buf[6] << 48) + ((uint64_t)buf[7] << 56);

		tmp &= 0xfffff;

		peeraddr += 0x50;
		peeraddr += tmp + 1;
		peeraddr = ((peeraddr - 1) | 3) + 1;

		n_left --;
	}

	store_64bit_word(cpu, a + 0x00, peer);
	store_64bit_word(cpu, a + 0x08, child);
	store_64bit_word(cpu, a + 0x10, parent);
	store_64bit_word(cpu, a+  0x18, host_tmp_component->Class);
	store_64bit_word(cpu, a+  0x1c, host_tmp_component->Type);
/*  TODO: these are not in the right order... see the 32-bit version above for more info  */
	store_64bit_word(cpu, a+  0x20, host_tmp_component->Flags);
	store_64bit_word(cpu, a+  0x24, host_tmp_component->Version + ((uint64_t)host_tmp_component->Revision << 16));
	store_64bit_word(cpu, a+  0x28, host_tmp_component->Key);
	store_64bit_word(cpu, a+  0x30, host_tmp_component->AffinityMask);
	store_64bit_word(cpu, a+  0x38, host_tmp_component->ConfigurationDataSize);
	store_64bit_word(cpu, a+  0x40, host_tmp_component->IdentifierLength);
	store_64bit_word(cpu, a+  0x48, host_tmp_component->Identifier);

	arcbios_next_component_address += 0x50;

	if (host_tmp_component->IdentifierLength != 0) {
		store_64bit_word(cpu, a + 0x48, a + 0x50);
		store_string(cpu, a + 0x50, identifier);
		if (identifier != NULL)
			arcbios_next_component_address += strlen(identifier) + 1;
	}

	arcbios_next_component_address ++;

	/*  Round up to next 0x8 bytes:  */
	arcbios_next_component_address = ((arcbios_next_component_address - 1) | 7) + 1;

	n_arc_components ++;

	return a;
}


/*
 *  arcbios_addchild_manual():
 *
 *  Used internally to set up component structures.
 *  Parent may only be NULL for the first (system) component.
 *
 *  Return value is the virtual (emulated) address of the added component.
 */
uint64_t arcbios_addchild_manual(struct cpu *cpu,
	uint64_t class, uint64_t type, uint64_t flags,
	uint64_t version, uint64_t revision, uint64_t key,
	uint64_t affinitymask, char *identifier, uint64_t parent)
{
	/*  This component is only for temporary use:  */
	struct arcbios_component component;
	struct arcbios_component64 component64;

	if (!arc_64bit) {
		component.Class                 = class;
		component.Type                  = type;
		component.Flags                 = flags;
		component.Version               = version;
		component.Revision              = revision;
		component.Key                   = key;
		component.AffinityMask          = affinitymask;
		component.ConfigurationDataSize = 0;
		component.IdentifierLength      = 0;
		component.Identifier            = 0;
		if (identifier != NULL) {
			component.IdentifierLength = strlen(identifier) + 1;
		}
		return arcbios_addchild(cpu, &component, identifier, parent);
	} else {
		component64.Class                 = class;
		component64.Type                  = type;
		component64.Flags                 = flags;
		component64.Version               = version;
		component64.Revision              = revision;
		component64.Key                   = key;
		component64.AffinityMask          = affinitymask;
		component64.ConfigurationDataSize = 0;
		component64.IdentifierLength      = 0;
		component64.Identifier            = 0;
		if (identifier != NULL) {
			component64.IdentifierLength = strlen(identifier) + 1;
		}
		return arcbios_addchild64(cpu, &component64, identifier, parent);
	}
}


/*
 *  arcbios_private_emul():
 *
 *  TODO:  This is probably SGI specific. (?)
 *
 *	0x04	get nvram table
 */
void arcbios_private_emul(struct cpu *cpu)
{
	int vector = cpu->pc & 0xfff;

	switch (vector) {
	case 0x04:
		debug("[ ARCBIOS PRIVATE get nvram table(): TODO ]\n");
		cpu->gpr[GPR_V0] = 0;
		break;
	default:
		cpu_register_dump(cpu);
		debug("a0 points to: ");
		dump_mem_string(cpu, cpu->gpr[GPR_A0]);
		debug("\n");
		fatal("ARCBIOS: unimplemented PRIVATE vector 0x%x\n", vector);
		cpu->running = 0;
	}
}


/*
 *  arcbios_emul():  ARCBIOS emulation
 *
 *	0x0c	Halt()
 *	0x10	PowerDown()
 *	0x14	Restart()
 *	0x18	Reboot()
 *	0x1c	EnterInteractiveMode()
 *	0x24	GetPeer(node)
 *	0x28	GetChild(node)
 *	0x2c	GetParent(node)
 *	0x44	GetSystemId()
 *	0x48	GetMemoryDescriptor(void *)
 *	0x54	GetRelativeTime()
 *	0x5c	Open(path, mode, &fileid)
 *	0x60	Close(handle)
 *	0x64	Read(handle, &buf, len, &actuallen)
 *	0x6c	Write(handle, buf, len, &returnlen)
 *	0x70	Seek(handle, &offset, len)
 *	0x78	GetEnvironmentVariable(char *)
 *	0x88	FlushAllCaches()
 *	0x90	GetDisplayStatus(uint32_t handle)
 */
void arcbios_emul(struct cpu *cpu)
{
	int vector = cpu->pc & 0xfff;
	int i, j;
	int mb_left;
	unsigned char ch2;
	unsigned char buf[40];

	if (cpu->pc >= ARC_PRIVATE_ENTRIES &&
	    cpu->pc < ARC_PRIVATE_ENTRIES + 100*sizeof(uint32_t)) {
		arcbios_private_emul(cpu);
		return;
	}

	switch (vector) {
	case 0x0c:		/*  Halt()  */
	case 0x10:		/*  PowerDown()  */
	case 0x14:		/*  Restart()  */
	case 0x18:		/*  Reboot()  */
	case 0x1c:		/*  EnterInteractiveMode()  */
		debug("[ ARCBIOS Halt() or similar ]\n");
		/*  Halt all CPUs.  */
		for (i=0; i<cpu->emul->ncpus; i++)
			cpu->emul->cpus[i]->running = 0;
		break;
	case 0x24:		/*  GetPeer(node)  */
		{
			uint64_t peer;
			memory_rw(cpu, cpu->mem, cpu->gpr[GPR_A0] - 3 * arc_wordlen, &buf[0], arc_wordlen, MEM_READ, CACHE_NONE);
			if (arc_64bit) {
				if (cpu->byte_order == EMUL_BIG_ENDIAN) {
					unsigned char tmp; tmp = buf[0]; buf[0] = buf[7]; buf[7] = tmp;
					tmp = buf[1]; buf[1] = buf[6]; buf[6] = tmp;
					tmp = buf[2]; buf[2] = buf[5]; buf[5] = tmp;
					tmp = buf[3]; buf[3] = buf[4]; buf[4] = tmp;
				}
				peer = (uint64_t)buf[0] + ((uint64_t)buf[1]<<8) + ((uint64_t)buf[2]<<16) + ((uint64_t)buf[3]<<24) +
				    ((uint64_t)buf[4]<<32) + ((uint64_t)buf[5]<<40) + ((uint64_t)buf[6]<<48) + ((uint64_t)buf[7]<<56);
			} else {
				if (cpu->byte_order == EMUL_BIG_ENDIAN) {
					unsigned char tmp; tmp = buf[0]; buf[0] = buf[3]; buf[3] = tmp;
					tmp = buf[1]; buf[1] = buf[2]; buf[2] = tmp;
				}
				peer = buf[0] + (buf[1]<<8) + (buf[2]<<16) + (buf[3]<<24);
			}

			cpu->gpr[GPR_V0] = peer? (peer + 3 * arc_wordlen) : 0;
			if (!arc_64bit)
				cpu->gpr[GPR_V0] = (int64_t)(int32_t) cpu->gpr[GPR_V0];
		}
		debug("[ ARCBIOS GetPeer(node 0x%016llx): 0x%016llx ]\n",
		    (long long)cpu->gpr[GPR_A0], (long long)cpu->gpr[GPR_V0]);
		break;
	case 0x28:		/*  GetChild(node)  */
		/*  0 for the root, non-0 for children:  */
		if (cpu->gpr[GPR_A0] == 0)
			cpu->gpr[GPR_V0] = FIRST_ARC_COMPONENT + arc_wordlen * 3;
		else {
			uint64_t child = 0;
			memory_rw(cpu, cpu->mem, cpu->gpr[GPR_A0] - 2 * arc_wordlen, &buf[0], arc_wordlen, MEM_READ, CACHE_NONE);
			if (arc_64bit) {
				if (cpu->byte_order == EMUL_BIG_ENDIAN) {
					unsigned char tmp; tmp = buf[0]; buf[0] = buf[7]; buf[7] = tmp;
					tmp = buf[1]; buf[1] = buf[6]; buf[6] = tmp;
					tmp = buf[2]; buf[2] = buf[5]; buf[5] = tmp;
					tmp = buf[3]; buf[3] = buf[4]; buf[4] = tmp;
				}
				child = (uint64_t)buf[0] + ((uint64_t)buf[1]<<8) + ((uint64_t)buf[2]<<16) + ((uint64_t)buf[3]<<24) +
				    ((uint64_t)buf[4]<<32) + ((uint64_t)buf[5]<<40) + ((uint64_t)buf[6]<<48) + ((uint64_t)buf[7]<<56);
			} else {
				if (cpu->byte_order == EMUL_BIG_ENDIAN) {
					unsigned char tmp; tmp = buf[0]; buf[0] = buf[3]; buf[3] = tmp;
					tmp = buf[1]; buf[1] = buf[2]; buf[2] = tmp;
				}
				child = buf[0] + (buf[1]<<8) + (buf[2]<<16) + (buf[3]<<24);
			}

			cpu->gpr[GPR_V0] = child? (child + 3 * arc_wordlen) : 0;
			if (!arc_64bit)
				cpu->gpr[GPR_V0] = (int64_t)(int32_t) cpu->gpr[GPR_V0];
		}
		debug("[ ARCBIOS GetChild(node 0x%016llx): 0x%016llx ]\n",
		    (long long)cpu->gpr[GPR_A0], (long long)cpu->gpr[GPR_V0]);
		break;
	case 0x2c:		/*  GetParent(node)  */
		{
			uint64_t parent;

			memory_rw(cpu, cpu->mem, cpu->gpr[GPR_A0] - 1 * arc_wordlen, &buf[0], arc_wordlen, MEM_READ, CACHE_NONE);

			if (arc_64bit) {
				if (cpu->byte_order == EMUL_BIG_ENDIAN) {
					unsigned char tmp; tmp = buf[0]; buf[0] = buf[7]; buf[7] = tmp;
					tmp = buf[1]; buf[1] = buf[6]; buf[6] = tmp;
					tmp = buf[2]; buf[2] = buf[5]; buf[5] = tmp;
					tmp = buf[3]; buf[3] = buf[4]; buf[4] = tmp;
				}
				parent = (uint64_t)buf[0] + ((uint64_t)buf[1]<<8) + ((uint64_t)buf[2]<<16) + ((uint64_t)buf[3]<<24) +
				    ((uint64_t)buf[4]<<32) + ((uint64_t)buf[5]<<40) + ((uint64_t)buf[6]<<48) + ((uint64_t)buf[7]<<56);
			} else {
				if (cpu->byte_order == EMUL_BIG_ENDIAN) {
					unsigned char tmp; tmp = buf[0]; buf[0] = buf[3]; buf[3] = tmp;
					tmp = buf[1]; buf[1] = buf[2]; buf[2] = tmp;
				}
				parent = buf[0] + (buf[1]<<8) + (buf[2]<<16) + (buf[3]<<24);
			}

			cpu->gpr[GPR_V0] = parent? (parent + 3 * arc_wordlen) : 0;
			if (!arc_64bit)
				cpu->gpr[GPR_V0] = (int64_t)(int32_t) cpu->gpr[GPR_V0];
		}
		debug("[ ARCBIOS GetParent(node 0x%016llx): 0x%016llx ]\n",
		    (long long)cpu->gpr[GPR_A0], (long long)cpu->gpr[GPR_V0]);
		break;
	case 0x44:		/*  GetSystemId()  */
		debug("[ ARCBIOS GetSystemId() ]\n");
		cpu->gpr[GPR_V0] = SGI_SYSID_ADDR;
		break;
	case 0x48:		/*  void *GetMemoryDescriptor(void *ptr)  */
		debug("[ ARCBIOS GetMemoryDescriptor(0x%08x) ]\n", cpu->gpr[GPR_A0]);

		/*  If a0=NULL, then return the first descriptor:  */
		if ((uint32_t)cpu->gpr[GPR_A0] == 0)
			cpu->gpr[GPR_V0] = arcbios_memdescriptor_base;
		else {
			int s = arc_64bit? sizeof(struct arcbios_mem64)
			    : sizeof(struct arcbios_mem);
			int nr = cpu->gpr[GPR_A0] - arcbios_memdescriptor_base;
			nr /= s;
			nr ++;
			cpu->gpr[GPR_V0] = arcbios_memdescriptor_base + s * nr;
			if (nr >= arc_n_memdescriptors)
				cpu->gpr[GPR_V0] = 0;
		}
		break;
	case 0x54:		/*  GetRelativeTime()  */
		debug("[ ARCBIOS GetRelativeTime() ]\n");
		cpu->gpr[GPR_V0] = time(NULL);
		break;
	case 0x5c:		/*  Open(char *path, uint32_t mode, uint32_t *fileID)  */
		debug("[ ARCBIOS Open(0x%x,0x%x,0x%x) ]\n", (int)cpu->gpr[GPR_A0],
		    (int)cpu->gpr[GPR_A1], (int)cpu->gpr[GPR_A2]);
		/*  cpu->gpr[GPR_V0] = ARCBIOS_ENOENT;  */

		/*
		 *  TODO: This is hardcoded to successfully open
		 *  anything, and return it as descriptor 3.
		 *  It is used by the Windows NT SETUPLDR program
		 *  to load stuff from the boot partition.
		 */

		cpu->gpr[GPR_V0] = 0;	/*  Success.  */
		store_32bit_word(cpu, cpu->gpr[GPR_A2], 3);
		break;
	case 0x60:		/*  Close(uint32_t handle)  */
		debug("[ ARCBIOS Close(0x%x) ]\n", (int)cpu->gpr[GPR_A0]);
		/*  TODO  */
		cpu->gpr[GPR_V0] = 0;	/*  Success.  */
		break;
	case 0x64:		/*  Read(handle, void *buf, length, uint32_t *count)  */
		if (cpu->gpr[GPR_A0] == ARCBIOS_STDIN) {
			int i, nread = 0;
			/*
			 *  Before going into the loop, make sure stdout
			 *  is flushed.  If we're using an X11 VGA console,
			 *  then it needs to be flushed as well.
			 */
			fflush(stdout);
			/*  NOTE/TODO: This gives a tick to _everything_  */
			for (i=0; i<cpu->n_tick_entries; i++)
				cpu->tick_func[i](cpu, cpu->tick_extra[i]);

			for (i=0; i<cpu->gpr[GPR_A2]; i++) {
				int x;
				unsigned char ch;

				/*  Read from STDIN is blocking (at least that seems to
				    be how NetBSD's arcdiag wants it)  */
				while ((x = console_readchar()) < 0) {
					if (cpu->emul->use_x11)
						x11_check_event();
					usleep(1);
				}

				ch = x;
				nread ++;
				memory_rw(cpu, cpu->mem, cpu->gpr[GPR_A1] + i, &ch, 1, MEM_WRITE, CACHE_NONE);
			}
			store_32bit_word(cpu, cpu->gpr[GPR_A3], nread);
			cpu->gpr[GPR_V0] = nread? ARCBIOS_ESUCCESS: ARCBIOS_EAGAIN;	/*  TODO: not EAGAIN?  */
		} else {
			int disk_id = diskimage_bootdev();
			int res;
			unsigned char *tmp_buf;

			debug("[ ARCBIOS Read(%i,0x%08x,0x%08x,0x%08x) ]\n", (int)cpu->gpr[GPR_A0],
			    (int)cpu->gpr[GPR_A1], (int)cpu->gpr[GPR_A2], (int)cpu->gpr[GPR_A3]);

			tmp_buf = malloc(cpu->gpr[GPR_A2]);
			if (tmp_buf == NULL) {
				fprintf(stderr, "[ ***  Out of memory in arcbios.c, allocating %i bytes ]\n", (int)cpu->gpr[GPR_A2]);
				break;
			}

			res = diskimage_access(disk_id, 0, arcbios_current_seek_offset,
			    tmp_buf, cpu->gpr[GPR_A2]);

			/*  If the transfer was successful, transfer the data to emulated memory:  */
			if (res) {
				uint64_t dst = cpu->gpr[GPR_A1];
				store_buf(cpu, dst, (char *)tmp_buf, cpu->gpr[GPR_A2]);
				store_32bit_word(cpu, cpu->gpr[GPR_A3], cpu->gpr[GPR_A2]);
				arcbios_current_seek_offset += cpu->gpr[GPR_A2];
				cpu->gpr[GPR_V0] = 0;
			} else
				cpu->gpr[GPR_V0] = ARCBIOS_EIO;
			free(tmp_buf);
		}
		break;
	case 0x68:		/*  GetReadStatus(handle)  */
		/*
		 *  According to arcbios_tty_getchar() in NetBSD's
		 *  dev/arcbios/arcbios_tty.c, GetReadStatus should
		 *  return 0 if there is something available.
		 *
		 *  TODO: Error codes are things like ARCBIOS_EAGAIN.
		 */
		if (cpu->gpr[GPR_A0] == ARCBIOS_STDIN) {
			cpu->gpr[GPR_V0] = console_charavail()? 0 : 1;
		} else {
			fatal("[ ARCBIOS GetReadStatus(%i) ]\n", (int)cpu->gpr[GPR_A0]);
			/*  TODO  */
			cpu->gpr[GPR_V0] = 1;
		}
		break;
	case 0x6c:		/*  Write(handle, buf, len, &returnlen)  */
		if (cpu->gpr[GPR_A0] != ARCBIOS_STDOUT) {
			fatal("[ ARCBIOS Write(%i,0x%08llx,%i,0x%08llx) ]\n",
			    (int)cpu->gpr[GPR_A0], (long long)cpu->gpr[GPR_A1],
			    (int)cpu->gpr[GPR_A2], (long long)cpu->gpr[GPR_A3]);
		} else {
			for (i=0; i<cpu->gpr[GPR_A2]; i++) {
				unsigned char ch = '\0';
				memory_rw(cpu, cpu->mem, cpu->gpr[GPR_A1] + i, &ch, sizeof(ch), MEM_READ, CACHE_NONE);

				arcbios_putchar(cpu, ch);
			}
		}
		store_32bit_word(cpu, cpu->gpr[GPR_A3], cpu->gpr[GPR_A2]);
		cpu->gpr[GPR_V0] = 0;	/*  Success.  */
		break;
	case 0x70:		/*  Seek(uint32_t handle, int64_t *offset, uint32_t whence): uint32_t  */
		debug("[ ARCBIOS Seek(%i,0x%08llx,%i): ",
		    (int)cpu->gpr[GPR_A0], (long long)cpu->gpr[GPR_A1],
		    (int)cpu->gpr[GPR_A2]);

		if (cpu->gpr[GPR_A2] != 0) {
			fatal("[ ARCBIOS Seek(%i,0x%08llx,%i): UNIMPLEMENTED whence=%i ]\n",
			    (int)cpu->gpr[GPR_A0], (long long)cpu->gpr[GPR_A1],
			    (int)cpu->gpr[GPR_A2], (int)cpu->gpr[GPR_A2]);
		}

		{
			unsigned char buf[8];
			uint64_t ofs;
			memory_rw(cpu, cpu->mem, cpu->gpr[GPR_A1], &buf[0], sizeof(buf), MEM_READ, CACHE_NONE);
			if (cpu->byte_order == EMUL_BIG_ENDIAN) {
				unsigned char tmp;
				tmp = buf[0]; buf[0] = buf[7]; buf[7] = tmp;
				tmp = buf[1]; buf[1] = buf[6]; buf[6] = tmp;
				tmp = buf[2]; buf[2] = buf[5]; buf[5] = tmp;
				tmp = buf[3]; buf[3] = buf[4]; buf[4] = tmp;
			}
			ofs = buf[0] + (buf[1] << 8) + (buf[2] << 16) + (buf[3] << 24) +
			    ((uint64_t)buf[4] << 32) + ((uint64_t)buf[5] << 40) +
			    ((uint64_t)buf[6] << 48) + ((uint64_t)buf[7] << 56);
			arcbios_current_seek_offset = ofs;
			debug("%016llx ]\n", (long long)ofs);
		}

		cpu->gpr[GPR_V0] = 0;	/*  Success.  */

		break;
	case 0x78:		/*  GetEnvironmentVariable(char *)  */
		/*  Find the environment variable given by a0:  */
		for (i=0; i<sizeof(buf); i++)
			memory_rw(cpu, cpu->mem, cpu->gpr[GPR_A0] + i, &buf[i], sizeof(char), MEM_READ, CACHE_NONE);
		buf[sizeof(buf)-1] = '\0';
		debug("[ ARCBIOS GetEnvironmentVariable(\"%s\") ]\n", buf);
		for (i=0; i<0x1000; i++) {
			/*  Matching string at offset i?  */
			int nmatches = 0;
			for (j=0; j<strlen((char *)buf); j++) {
				memory_rw(cpu, cpu->mem, (uint64_t)(ARC_ENV_STRINGS + i + j), &ch2, sizeof(char), MEM_READ, CACHE_NONE);
				if (ch2 == buf[j])
					nmatches++;
			}
			memory_rw(cpu, cpu->mem, (uint64_t)(ARC_ENV_STRINGS + i + strlen((char *)buf)), &ch2, sizeof(char), MEM_READ, CACHE_NONE);
			if (nmatches == strlen((char *)buf) && ch2 == '=') {
				cpu->gpr[GPR_V0] = ARC_ENV_STRINGS + i + strlen((char *)buf) + 1;
				return;
			}
		}
		/*  Return NULL if string wasn't found.  */
		cpu->gpr[GPR_V0] = 0;
		break;
	case 0x88:		/*  FlushAllCaches()  */
		debug("[ ARCBIOS FlushAllCaches(): TODO ]\n");
		cpu->gpr[GPR_V0] = 0;
		break;
	case 0x90:		/*  void *GetDisplayStatus(handle)  */
		debug("[ ARCBIOS GetDisplayStatus(%i) ]\n", cpu->gpr[GPR_A0]);
		/*  TODO:  handle different values of 'handle'?  */
		cpu->gpr[GPR_V0] = ARC_DSPSTAT_ADDR;
		break;
	default:
		cpu_register_dump(cpu);
		debug("a0 points to: ");
		dump_mem_string(cpu, cpu->gpr[GPR_A0]);
		debug("\n");
		fatal("ARCBIOS: unimplemented vector 0x%x\n", vector);
		cpu->running = 0;
	}
}


/*
 *  arcbios_set_64bit_mode():
 *
 *  Should be used for some SGI modes.
 */
void arcbios_set_64bit_mode(int enable)
{
	arc_64bit = enable;
	arc_wordlen = arc_64bit? sizeof(uint64_t) : sizeof(uint32_t);
}

