/*
 *  Copyright (C) 2003-2005  Anders Gavare.  All rights reserved.
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
 *  $Id: arcbios.c,v 1.69 2005-01-17 15:34:11 debug Exp $
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

#include "console.h"
#include "diskimage.h"
#include "emul.h"
#include "memory.h"
#include "misc.h"
#include "sgi_arcbios.h"


extern int quiet_mode;

extern struct diskimage *diskimages[MAX_DISKIMAGES];
extern int n_diskimages;


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

/*  Configuration data:  */
#define	MAX_CONFIG_DATA		50
static int n_configuration_data = 0;
static uint64_t configuration_data_next_addr = ARC_CONFIG_DATA_ADDR;
static uint64_t configuration_data_component[MAX_CONFIG_DATA];
static int configuration_data_len[MAX_CONFIG_DATA];
static uint64_t configuration_data_configdata[MAX_CONFIG_DATA];

static int arc_64bit = 0;		/*  For some SGI modes  */
static int arc_wordlen = sizeof(uint32_t);

static uint64_t scsicontroller = 0;

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
static int arcbios_console_reverse = 0;
int arcbios_console_curcolor = 0x1f;

/*  Open file handles:  */
#define	MAX_OPEN_STRINGLEN	200
#define	MAX_HANDLES	10
static int file_handle_in_use[MAX_HANDLES];
static unsigned char *file_handle_string[MAX_HANDLES];
static uint64_t arcbios_current_seek_offset[MAX_HANDLES];

#define	MAX_STRING_TO_COMPONENT		20
static unsigned char *arcbios_string_to_component[MAX_STRING_TO_COMPONENT];
static uint64_t arcbios_string_to_component_value[MAX_STRING_TO_COMPONENT];
static int arcbios_n_string_to_components = 0;


/*
 *  arcbios_add_string_to_component():
 */
void arcbios_add_string_to_component(char *string, uint64_t component)
{
	if (arcbios_n_string_to_components >= MAX_STRING_TO_COMPONENT) {
		printf("Too many string-to-component mappings.\n");
		exit(1);
	}

	arcbios_string_to_component[arcbios_n_string_to_components] = malloc(strlen(string) + 1);
	if (arcbios_string_to_component[arcbios_n_string_to_components] == NULL) {
		fprintf(stderr, "out of memory in arcbios_add_string_to_component()\n");
		exit(1);
	}
	memcpy(arcbios_string_to_component[arcbios_n_string_to_components], string, strlen(string) + 1);
	debug("adding component mapping: 0x%016llx = %s\n",
	    (long long)component, string);

	arcbios_string_to_component_value[arcbios_n_string_to_components] = component;
	arcbios_n_string_to_components ++;
}


/*
 *  arcbios_putcell():
 */
static void arcbios_putcell(struct cpu *cpu, int ch, int x, int y)
{
	unsigned char buf[2];
	buf[0] = ch;
	buf[1] = arcbios_console_curcolor;
	if (arcbios_console_reverse)
		buf[1] = ((buf[1] & 0x70) >> 4) | ((buf[1] & 7) << 4) | (buf[1] & 0x88);
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
	arcbios_console_curcolor = 0x1f;

	for (y=1; y<arcbios_console_maxy; y++)
		for (x=0; x<arcbios_console_maxx; x++)
			arcbios_putcell(cpu, ' ', x, y);

	arcbios_console_curx = 0;
	arcbios_console_cury = 1;
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
			arcbios_console_curcolor = 0x1f;
			arcbios_console_reverse = 0; break;
		case 1:	/*  "Bold".  */
			arcbios_console_curcolor |= 0x08; break;
		case 7:	/*  "Reverse".  */
			arcbios_console_reverse = 1; break;
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
		if (col < 1)
			col = 1;
		if (row < 1)
			row = 1;
		arcbios_console_curx = col - 1;
		arcbios_console_cury = row - 1;
		return;
	case 'J':
		/*
		 *  J = clear screen below cursor, and the rest of the
		 *      current line,
		 *  2J = clear whole screen.
		 */
		i = atoi(arcbios_escape_sequence + 1);
		if (i != 0 && i != 2)
			fatal("{ handle_esc_seq(): %iJ }\n", i);
		if (i == 0)
			for (col=arcbios_console_curx; col<arcbios_console_maxx; col++)
				arcbios_putcell(cpu, ' ', col, arcbios_console_cury);
		for (col=0; col<arcbios_console_maxx; col++)
			for (row=i?0:arcbios_console_cury+1; row<arcbios_console_maxy; row++)
				arcbios_putcell(cpu, ' ', col, row);
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
 *  scroll_if_necessary():
 */
static void scroll_if_necessary(struct cpu *cpu)
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

		/*  Hack for Windows NT, which uses 0x9b instead of ESC + [  */
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
				scroll_if_necessary(cpu);
			}
			arcbios_putcell(cpu, ch, arcbios_console_curx,
			    arcbios_console_cury);
			arcbios_console_curx ++;
		}
	}

	scroll_if_necessary(cpu);

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
 *  arcbios_register_scsicontroller():
 */
void arcbios_register_scsicontroller(uint64_t scsicontroller_component)
{
	scsicontroller = scsicontroller_component;
}


/*
 *  arcbios_get_scsicontroller():
 */
uint64_t arcbios_get_scsicontroller(void)
{
	return scsicontroller;
}


/*
 *  arcbios_add_memory_descriptor():
 *
 *  NOTE: arctype is the ARC type, not the SGI type. This function takes
 *  care of converting, when necessary.
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

/*  TODO: Huh? Why isn't it necessary to convert from arc to sgi types?  */
/*  TODO 2: It seems that it _is_ neccessary, but NetBSD's arcdiag doesn't care?  */
#if 1
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
static uint64_t arcbios_addchild(struct cpu *cpu,
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
static uint64_t arcbios_addchild64(struct cpu *cpu,
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
		memory_rw(cpu, cpu->mem, peeraddr + 0x34,
				&buf[0], sizeof(uint32_t), MEM_READ, CACHE_NONE);
		if (cpu->byte_order == EMUL_BIG_ENDIAN) {
			unsigned char tmp;
			tmp = buf[0]; buf[0] = buf[3]; buf[3] = tmp;
			tmp = buf[1]; buf[1] = buf[2]; buf[2] = tmp;
		}
		tmp = buf[0] + (buf[1]<<8) + (buf[2]<<16) + (buf[3]<<24);

		tmp &= 0xfffff;

		peeraddr += 0x50;
		peeraddr += tmp + 1;
		peeraddr = ((peeraddr - 1) | 3) + 1;

		n_left --;
	}

	store_64bit_word(cpu, a + 0x00, peer);
	store_64bit_word(cpu, a + 0x08, child);
	store_64bit_word(cpu, a + 0x10, parent);
	store_32bit_word(cpu, a+  0x18, host_tmp_component->Class);
	store_32bit_word(cpu, a+  0x1c, host_tmp_component->Type);
	store_32bit_word(cpu, a+  0x20, host_tmp_component->Flags);
	store_32bit_word(cpu, a+  0x24, host_tmp_component->Version + ((uint64_t)host_tmp_component->Revision << 16));
	store_32bit_word(cpu, a+  0x28, host_tmp_component->Key);
	store_64bit_word(cpu, a+  0x30, host_tmp_component->AffinityMask);
	store_64bit_word(cpu, a+  0x38, host_tmp_component->ConfigurationDataSize);
	store_64bit_word(cpu, a+  0x40, host_tmp_component->IdentifierLength);
	store_64bit_word(cpu, a+  0x48, host_tmp_component->Identifier);

	/*  TODO: Find out how a REAL ARCS64 implementation does it.  */

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
	uint64_t affinitymask, char *identifier, uint64_t parent,
	void *config_data, size_t config_len)
{
	/*  This component is only for temporary use:  */
	struct arcbios_component component;
	struct arcbios_component64 component64;

	if (config_data != NULL) {
		unsigned char *p = config_data;
		int i;

		if (n_configuration_data >= MAX_CONFIG_DATA) {
			printf("fatal error: you need to increase MAX_CONFIG_DATA\n");
			exit(1);
		}

		for (i=0; i<config_len; i++) {
			unsigned char ch = p[i];
			memory_rw(cpu, cpu->mem, configuration_data_next_addr + i, &ch, 1, MEM_WRITE, CACHE_NONE);
		}

		configuration_data_len[n_configuration_data] = config_len;
		configuration_data_configdata[n_configuration_data] = configuration_data_next_addr;
		configuration_data_next_addr += config_len;
		configuration_data_component[n_configuration_data] = arcbios_next_component_address
		    + (arc_64bit? 0x18 : 0x0c);

		/*  printf("& ADDING %i: configdata=0x%016llx component=0x%016llx\n",
		    n_configuration_data, (long long)configuration_data_configdata[n_configuration_data],
		    (long long)configuration_data_component[n_configuration_data]);  */

		n_configuration_data ++;
	}

	if (!arc_64bit) {
		component.Class                 = class;
		component.Type                  = type;
		component.Flags                 = flags;
		component.Version               = version;
		component.Revision              = revision;
		component.Key                   = key;
		component.AffinityMask          = affinitymask;
		component.ConfigurationDataSize = config_len;
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
		component64.ConfigurationDataSize = config_len;
		component64.IdentifierLength      = 0;
		component64.Identifier            = 0;
		if (identifier != NULL) {
			component64.IdentifierLength = strlen(identifier) + 1;
		}
		return arcbios_addchild64(cpu, &component64, identifier, parent);
	}
}


/*
 *  arcbios_get_msdos_partition_size():
 *
 *  This function tries to parse MSDOS-style partition tables on a disk
 *  image, and return the starting offset (counted in bytes), and the
 *  size, of a specific partition.
 *
 *  NOTE: partition_nr is 1-based!
 *
 *  TODO: This is buggy, it doesn't really handle extended partitions.
 *
 *  See http://www.nondot.org/sabre/os/files/Partitions/Partitions.html
 *  for more info.
 */
static void arcbios_get_msdos_partition_size(int scsi_id,
	int partition_nr, uint64_t *start, uint64_t *size)
{
	int res, i, partition_type, cur_partition = 0;
	unsigned char sector[512];
	unsigned char buf[16];
	uint64_t offset = 0, st;

	/*  Partition 0 is the entire disk image:  */
	*start = 0;
	*size = diskimages[scsi_id]->total_size;
	if (partition_nr == 0)
		return;

ugly_goto:
	*start = 0; *size = 0;

	/*  printf("reading MSDOS partition from offset 0x%llx\n",
	    (long long)offset);  */

	res = diskimage_access(scsi_id, 0, offset, sector, sizeof(sector));
	if (!res) {
		fatal("[ arcbios_get_msdos_partition_size(): couldn't "
		    "read the disk image, id %i, offset 0x%llx ]\n",
		    scsi_id, (long long)offset);
		return;
	}

	if (sector[510] != 0x55 || sector[511] != 0xaa) {
		fatal("[ arcbios_get_msdos_partition_size(): not an "
		    "MSDOS partition table ]\n");
	}

#if 0
	/*  Debug dump:  */
	for (i=0; i<4; i++) {
		int j;
		printf("  partition %i: ", i+1);
		for (j=0; j<16; j++)
			printf(" %02x", sector[446 + i*16 + j]);
		printf("\n");
	}
#endif

	for (i=0; i<4; i++) {
		memmove(buf, sector + 446 + 16*i, 16);

		partition_type = buf[4];

		if (partition_type == 0)
			continue;

		st = (buf[8] + (buf[9] << 8) + (buf[10] << 16) +
		    (buf[11] << 24)) * 512;

		if (start != NULL)
			*start = st;
		if (size != NULL)
			*size = (buf[12] + (buf[13] << 8) + (buf[14] << 16) +
			    (buf[15] << 24)) * 512;

		/*  Extended DOS partition:  */
		if (partition_type == 5) {
			offset += st;
			goto ugly_goto;
		}

		/*  Found the right partition? Then return.  */
		cur_partition ++;
		if (cur_partition == partition_nr)
			return;
	}

	fatal("[ partition(%i) NOT found ]\n", partition_nr);
}


/*
 *  arcbios_handle_to_scsi_id():
 */
static int arcbios_handle_to_scsi_id(int handle)
{
	int id, cdrom;
	char *s;

	if (handle < 0 || handle >= MAX_HANDLES)
		return -1;

	s = (char *)file_handle_string[handle];
	if (s == NULL)
		return -1;

	/*
	 *  s is something like "scsi(0)disk(0)rdisk(0)partition(0)".
	 *  TODO: This is really ugly and hardcoded.
	 */

	if (strncmp(s, "scsi(", 5) != 0 || strlen(s) < 13)
		return -1;

	cdrom = (s[7] == 'c');
	id = cdrom? atoi(s + 13) : atoi(s + 12);

	return id;
}


/*
 *  arcbios_handle_to_start_and_size():
 */
static void arcbios_handle_to_start_and_size(int handle, uint64_t *start,
	uint64_t *size)
{
	char *s = (char *)file_handle_string[handle];
	char *s2;
	int scsi_id = arcbios_handle_to_scsi_id(handle);

	if (scsi_id < 0)
		return;

	/*  This works for "partition(0)":  */
	*start = 0;
	*size = diskimages[scsi_id]->total_size;

	s2 = strstr(s, "partition(");
	if (s2 != NULL) {
		int partition_nr = atoi(s2 + 10);
		/*  printf("partition_nr = %i\n", partition_nr);  */
		if (partition_nr != 0)
			arcbios_get_msdos_partition_size(scsi_id,
			    partition_nr, start, size);
	}
}


/*
 *  arcbios_getfileinformation():
 *
 *  Fill in a GetFileInformation struct in emulated memory,
 *  for a specific file handle. (This is used to get the size
 *  and offsets of partitions on disk images.)
 */
static int arcbios_getfileinformation(struct cpu *cpu)
{
	int handle = cpu->gpr[GPR_A0];
	uint64_t addr = cpu->gpr[GPR_A1];
	uint64_t start, size;

	arcbios_handle_to_start_and_size(handle, &start, &size);

	store_64bit_word(cpu, addr + 0, 0);
	store_64bit_word(cpu, addr + 8, size);
	store_64bit_word(cpu, addr + 16, 0);
	store_32bit_word(cpu, addr + 24, 1);
	store_32bit_word(cpu, addr + 28, 0);
	store_32bit_word(cpu, addr + 32, 0);

	/*  printf("\n!!! size=0x%x start=0x%x\n", (int)size, (int)start);  */

	return ARCBIOS_ESUCCESS;
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
		cpu_register_dump(cpu, 1, 0x1);
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
 *	0x20	ReturnFromMain()
 *	0x24	GetPeer(node)
 *	0x28	GetChild(node)
 *	0x2c	GetParent(node)
 *	0x30	GetConfigurationData(config_data, node)
 *	0x3c	GetComponent(name)
 *	0x44	GetSystemId()
 *	0x48	GetMemoryDescriptor(void *)
 *	0x50	GetTime()
 *	0x54	GetRelativeTime()
 *	0x5c	Open(path, mode, &fileid)
 *	0x60	Close(handle)
 *	0x64	Read(handle, &buf, len, &actuallen)
 *	0x6c	Write(handle, buf, len, &returnlen)
 *	0x70	Seek(handle, &offset, len)
 *	0x78	GetEnvironmentVariable(char *)
 *	0x7c	SetEnvironmentVariable(char *, char *)
 *	0x80	GetFileInformation(handle, buf)
 *	0x88	FlushAllCaches()
 *	0x90	GetDisplayStatus(uint32_t handle)
 *	0x100	undocumented IRIX return from main() (?)
 */
void arcbios_emul(struct cpu *cpu)
{
	int vector = cpu->pc & 0xfff;
	int i, j, handle;
	unsigned char ch2;
	unsigned char buf[40];

	if (cpu->pc >= ARC_PRIVATE_ENTRIES &&
	    cpu->pc < ARC_PRIVATE_ENTRIES + 100*sizeof(uint32_t)) {
		arcbios_private_emul(cpu);
		return;
	}

	if (arc_64bit)
		vector /= 2;

	switch (vector) {
	case 0x0c:		/*  Halt()  */
	case 0x10:		/*  PowerDown()  */
	case 0x14:		/*  Restart()  */
	case 0x18:		/*  Reboot()  */
	case 0x1c:		/*  EnterInteractiveMode()  */
	case 0x20:		/*  ReturnFromMain()  */
		debug("[ ARCBIOS Halt() or similar ]\n");
		/*  Halt all CPUs.  */
		for (i=0; i<cpu->emul->ncpus; i++)
			cpu->emul->cpus[i]->running = 0;
		cpu->emul->exit_without_entering_debugger = 1;
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
	case 0x30:		/*  GetConfigurationData(void *configdata, void *node)  */
		/*  fatal("[ ARCBIOS GetConfigurationData(0x%016llx,0x%016llx) ]\n",
		    (long long)cpu->gpr[GPR_A0], (long long)cpu->gpr[GPR_A1]);  */
		cpu->gpr[GPR_V0] = ARCBIOS_EINVAL;
		for (i=0; i<n_configuration_data; i++) {
			/*  fatal("configuration_data_component[%i] = 0x%016llx\n", i, (long long)configuration_data_component[i]);  */
			if (cpu->gpr[GPR_A1] == configuration_data_component[i]) {
				cpu->gpr[GPR_V0] = 0;
				for (j=0; j<configuration_data_len[i]; j++) {
					unsigned char ch;
					memory_rw(cpu, cpu->mem, configuration_data_configdata[i] + j, &ch, 1, MEM_READ, CACHE_NONE);
					memory_rw(cpu, cpu->mem, cpu->gpr[GPR_A0] + j, &ch, 1, MEM_WRITE, CACHE_NONE);
				}
				break;
			}
		}
		break;
	case 0x3c:		/*  GetComponent(char *name)  */
		debug("[ ARCBIOS GetComponent(\"");
		dump_mem_string(cpu, cpu->gpr[GPR_A0]);
		debug("\") ]\n");

		if (cpu->gpr[GPR_A0] == 0) {
			fatal("[ ARCBIOS GetComponent: NULL ptr ]\n");
		} else {
			unsigned char buf[500];
			int match_index = -1;
			int match_len = 0;

			memset(buf, 0, sizeof(buf));
			for (i=0; i<sizeof(buf); i++) {
				memory_rw(cpu, cpu->mem, cpu->gpr[GPR_A0] + i, &buf[i], 1, MEM_READ, CACHE_NONE);
				if (buf[i] == '\0')
					i = sizeof(buf);
			}
			buf[sizeof(buf) - 1] = '\0';

			/*  "scsi(0)disk(0)rdisk(0)partition(0)" and such.  */
			/*  printf("GetComponent(\"%s\")\n", buf);  */

			/*  Default to NULL return value.  */
			cpu->gpr[GPR_V0] = 0;

			/*  Scan the string to component table:  */
			for (i=0; i<arcbios_n_string_to_components; i++) {
				int m = 0;
				while (buf[m] && arcbios_string_to_component[i][m] &&
				    arcbios_string_to_component[i][m] == buf[m])
					m++;
				if (m > match_len) {
					match_len = m;
					match_index = i;
				}
			}

			if (match_index >= 0) {
				/*  printf("Longest match: '%s'\n", arcbios_string_to_component[match_index]);  */
				cpu->gpr[GPR_V0] = arcbios_string_to_component_value[match_index];
			}
		}
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
	case 0x50:		/*  GetTime()  */
		debug("[ ARCBIOS GetTime() ]\n");
		cpu->gpr[GPR_V0] = 0xffffffff80001000ULL;
		/*  TODO!  */
		break;
	case 0x54:		/*  GetRelativeTime()  */
		debug("[ ARCBIOS GetRelativeTime() ]\n");
		cpu->gpr[GPR_V0] = (int64_t)(int32_t)time(NULL);
		break;
	case 0x5c:		/*  Open(char *path, uint32_t mode, uint32_t *fileID)  */
		debug("[ ARCBIOS Open(\"");
		dump_mem_string(cpu, cpu->gpr[GPR_A0]);
		debug("\",0x%x,0x%x)", (int)cpu->gpr[GPR_A0],
		    (int)cpu->gpr[GPR_A1], (int)cpu->gpr[GPR_A2]);

		cpu->gpr[GPR_V0] = ARCBIOS_ENOENT;

		handle = 3;
		/*  TODO: Starting at 0 would require some updates...  */
		while (file_handle_in_use[handle]) {
			handle ++;
			if (handle >= MAX_HANDLES) {
				cpu->gpr[GPR_V0] = ARCBIOS_EMFILE;
				break;
			}
		}

		if (handle >= MAX_HANDLES) {
			fatal("[ ARCBIOS Open: out of file handles ]\n");
		} else if (cpu->gpr[GPR_A0] == 0) {
			fatal("[ ARCBIOS Open: NULL ptr ]\n");
		} else {
			/*
			 *  TODO: This is hardcoded to successfully open anything.
			 *  It is used by the Windows NT SETUPLDR program to load
			 *  stuff from the boot partition.
			 */
			unsigned char *buf = malloc(MAX_OPEN_STRINGLEN);
			if (buf == NULL) {
				fprintf(stderr, "out of memory\n");
				exit(1);
			}
			memset(buf, 0, MAX_OPEN_STRINGLEN);
			for (i=0; i<MAX_OPEN_STRINGLEN; i++) {
				memory_rw(cpu, cpu->mem, cpu->gpr[GPR_A0] + i, &buf[i], 1, MEM_READ, CACHE_NONE);
				if (buf[i] == '\0')
					i = MAX_OPEN_STRINGLEN;
			}
			buf[MAX_OPEN_STRINGLEN - 1] = '\0';
			file_handle_string[handle] = buf;
			arcbios_current_seek_offset[handle] = 0;
			cpu->gpr[GPR_V0] = ARCBIOS_ESUCCESS;
		}

		if (cpu->gpr[GPR_V0] == ARCBIOS_ESUCCESS) {
			debug(" = handle %i ]\n", handle);
			store_32bit_word(cpu, cpu->gpr[GPR_A2], handle);
			file_handle_in_use[handle] = 1;
		} else
			debug(" = ERROR %i ]\n", (int)cpu->gpr[GPR_V0]);
		break;
	case 0x60:		/*  Close(uint32_t handle)  */
		debug("[ ARCBIOS Close(%i) ]\n", (int)cpu->gpr[GPR_A0]);
		if (!file_handle_in_use[cpu->gpr[GPR_A0]]) {
			fatal("ARCBIOS Close(%i): bad handle\n",
			    (int)cpu->gpr[GPR_A0]);
			cpu->gpr[GPR_V0] = ARCBIOS_EBADF;
		} else {
			file_handle_in_use[cpu->gpr[GPR_A0]] = 0;
			if (file_handle_string[cpu->gpr[GPR_A0]] != NULL)
				free(file_handle_string[cpu->gpr[GPR_A0]]);
			file_handle_string[cpu->gpr[GPR_A0]] = NULL;
			cpu->gpr[GPR_V0] = ARCBIOS_ESUCCESS;
		}
		break;
	case 0x64:		/*  Read(handle, void *buf, length, uint32_t *count)  */
		if (cpu->gpr[GPR_A0] == ARCBIOS_STDIN) {
			int i, nread = 0;
			/*
			 *  Before going into the loop, make sure stdout
			 *  is flushed.  If we're using an X11 VGA console,
			 *  then it needs to be flushed as well.
			 */
			fflush(stdin);
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

				/*
				 *  ESC + '[' should be transformed into 0x9b:
				 *
				 *  NOTE/TODO: This makes the behaviour of just pressing
				 *  ESC a bit harder to define.
				 */
				if (x == 27) {
					while ((x = console_readchar()) < 0) {
						if (cpu->emul->use_x11)
							x11_check_event();
						usleep(1);
					}
					if (x == '[' || x == 'O')
						x = 0x9b;
				}

				ch = x;
				nread ++;
				memory_rw(cpu, cpu->mem, cpu->gpr[GPR_A1] + i, &ch, 1, MEM_WRITE, CACHE_NONE);
			}
			store_32bit_word(cpu, cpu->gpr[GPR_A3], nread);
			cpu->gpr[GPR_V0] = nread? ARCBIOS_ESUCCESS: ARCBIOS_EAGAIN;	/*  TODO: not EAGAIN?  */
		} else {
			int handle = cpu->gpr[GPR_A0];
			int disk_id = arcbios_handle_to_scsi_id(handle);
			uint64_t partition_offset = 0;
			int res;
			uint64_t size;		/*  dummy  */
			unsigned char *tmp_buf;

			arcbios_handle_to_start_and_size(handle, &partition_offset, &size);

			debug("[ ARCBIOS Read(%i,0x%08x,0x%08x,0x%08x) ]\n", (int)cpu->gpr[GPR_A0],
			    (int)cpu->gpr[GPR_A1], (int)cpu->gpr[GPR_A2], (int)cpu->gpr[GPR_A3]);

			tmp_buf = malloc(cpu->gpr[GPR_A2]);
			if (tmp_buf == NULL) {
				fprintf(stderr, "[ ***  Out of memory in arcbios.c, allocating %i bytes ]\n", (int)cpu->gpr[GPR_A2]);
				break;
			}

			res = diskimage_access(disk_id, 0,
			    partition_offset + arcbios_current_seek_offset[handle],
			    tmp_buf, cpu->gpr[GPR_A2]);

			/*  If the transfer was successful, transfer the data to emulated memory:  */
			if (res) {
				uint64_t dst = cpu->gpr[GPR_A1];
				store_buf(cpu, dst, (char *)tmp_buf, cpu->gpr[GPR_A2]);
				store_32bit_word(cpu, cpu->gpr[GPR_A3], cpu->gpr[GPR_A2]);
				arcbios_current_seek_offset[handle] += cpu->gpr[GPR_A2];
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
			fatal("[ ARCBIOS GetReadStatus(%i) from something other than STDIN: TODO ]\n",
			    (int)cpu->gpr[GPR_A0]);
			/*  TODO  */
			cpu->gpr[GPR_V0] = 1;
		}
		break;
	case 0x6c:		/*  Write(handle, buf, len, &returnlen)  */
		if (cpu->gpr[GPR_A0] != ARCBIOS_STDOUT) {
			/*
			 *  TODO: this is just a test
			 */
			int handle = cpu->gpr[GPR_A0];
			int disk_id = arcbios_handle_to_scsi_id(handle);
			uint64_t partition_offset = 0;
			int res, i;
			uint64_t size;		/*  dummy  */
			unsigned char *tmp_buf;

			arcbios_handle_to_start_and_size(handle, &partition_offset, &size);

			debug("[ ARCBIOS Write(%i,0x%08llx,%i,0x%08llx) ]\n",
			    (int)cpu->gpr[GPR_A0], (long long)cpu->gpr[GPR_A1],
			    (int)cpu->gpr[GPR_A2], (long long)cpu->gpr[GPR_A3]);

			tmp_buf = malloc(cpu->gpr[GPR_A2]);
			if (tmp_buf == NULL) {
				fprintf(stderr, "[ ***  Out of memory in arcbios.c, allocating %i bytes ]\n", (int)cpu->gpr[GPR_A2]);
				break;
			}

			for (i=0; i<cpu->gpr[GPR_A2]; i++)
				memory_rw(cpu, cpu->mem, cpu->gpr[GPR_A1] + i,
				    &tmp_buf[i], sizeof(char), MEM_READ,
				    CACHE_NONE);

			res = diskimage_access(disk_id, 1,
			    partition_offset + arcbios_current_seek_offset[handle], tmp_buf,
			    cpu->gpr[GPR_A2]);

			if (res) {
				store_32bit_word(cpu, cpu->gpr[GPR_A3], cpu->gpr[GPR_A2]);
				arcbios_current_seek_offset[handle] += cpu->gpr[GPR_A2];
				cpu->gpr[GPR_V0] = 0;
			} else
				cpu->gpr[GPR_V0] = ARCBIOS_EIO;
			free(tmp_buf);
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
			arcbios_current_seek_offset[cpu->gpr[GPR_A0]] = ofs;
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
	case 0x7c:		/*  SetEnvironmentVariable(char *, char *)  */
		debug("[ ARCBIOS SetEnvironmentVariable(\"");
		dump_mem_string(cpu, cpu->gpr[GPR_A0]);
		debug("\",\"");
		dump_mem_string(cpu, cpu->gpr[GPR_A1]);
		debug("\") ]\n");
		/*  TODO: This is a dummy.  */
		cpu->gpr[GPR_V0] = ARCBIOS_ESUCCESS;
		break;
	case 0x80:		/*  GetFileInformation()  */
		debug("[ ARCBIOS GetFileInformation(%i,0x%x): ",
		    cpu->gpr[GPR_A0], (int)cpu->gpr[GPR_A1]);

		if (cpu->gpr[GPR_A0] >= MAX_HANDLES) {
			debug("invalid file handle ]\n");
			cpu->gpr[GPR_V0] = ARCBIOS_EINVAL;
		} else if (!file_handle_in_use[cpu->gpr[GPR_A0]]) {
			debug("file handle not in use! ]\n");
			cpu->gpr[GPR_V0] = ARCBIOS_EBADF;
		} else {
			debug("'%s' ]\n", file_handle_string[cpu->gpr[GPR_A0]]);
			cpu->gpr[GPR_V0] = arcbios_getfileinformation(cpu);
		}
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
	case 0x100:
		/*
		 *  Undocumented, but used by IRIX when shutting down.
		 *  Like a return_from_main(), or similar.
		 */
		debug("[ ARCBIOS: IRIX return_from_main() (?) ]\n");

/*
 *  Update 2005-01-17:
 *
 *  It seems that this is NOT a return from main,
 *  but something else. Hm...
 */

#if 0
		/*  Halt all CPUs.  */
		for (i=0; i<cpu->emul->ncpus; i++)
			cpu->emul->cpus[i]->running = 0;
		cpu->emul->exit_without_entering_debugger = 1;
#endif

		cpu->gpr[GPR_V0] = 0;
		break;
	case 0x888:
		/*
		 *  Magical crash if there is no exception handling code.
		 */
		fatal("EXCEPTION, but no exception handler installed yet.\n");
		quiet_mode = 0;
		cpu_register_dump(cpu, 1, 0x1);
		cpu->running = 0;
		break;
	default:
		quiet_mode = 0;
		cpu_register_dump(cpu, 1, 0x1);
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


/*
 *  arcbios_set_default_exception_handler():
 */
void arcbios_set_default_exception_handler(struct cpu *cpu)
{
	/*
	 *  The default exception handlers simply jump to 0xbfc88888,
	 *  which is then taken care of in arcbios_emul() above.
	 *
	 *  3c1abfc8        lui     k0,0xbfc8
	 *  375a8888        ori     k0,k0,0x8888
	 *  03400008        jr      k0
	 *  00000000        nop
	 */
	store_32bit_word(cpu, 0xffffffff80000000ULL, 0x3c1abfc8);
	store_32bit_word(cpu, 0xffffffff80000004ULL, 0x375a8888);
	store_32bit_word(cpu, 0xffffffff80000008ULL, 0x03400008);
	store_32bit_word(cpu, 0xffffffff8000000cULL, 0x00000000);

	store_32bit_word(cpu, 0xffffffff80000080ULL, 0x3c1abfc8);
	store_32bit_word(cpu, 0xffffffff80000084ULL, 0x375a8888);
	store_32bit_word(cpu, 0xffffffff80000088ULL, 0x03400008);
	store_32bit_word(cpu, 0xffffffff8000008cULL, 0x00000000);

	store_32bit_word(cpu, 0xffffffff80000180ULL, 0x3c1abfc8);
	store_32bit_word(cpu, 0xffffffff80000184ULL, 0x375a8888);
	store_32bit_word(cpu, 0xffffffff80000188ULL, 0x03400008);
	store_32bit_word(cpu, 0xffffffff8000018cULL, 0x00000000);
}


/*
 *  arcbios_init():
 *
 *  Should be called before any other arcbios function is used.
 */
void arcbios_init(void)
{
	int i;

	/*  File handles 0, 1, and 2 are stdin, stdout, and stderr.  */
	for (i=0; i<MAX_HANDLES; i++) {
		file_handle_in_use[i] = i<3? 1 : 0;
		file_handle_string[i] = NULL;
		arcbios_current_seek_offset[i] = 0;
	}
}

