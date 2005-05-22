#ifndef	ARCBIOS_H
#define	ARCBIOS_H

/*
 *  Copyright (C) 2004-2005  Anders Gavare.  All rights reserved.
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
 *  $Id: arcbios.h,v 1.8 2005-05-22 19:40:01 debug Exp $
 *
 *  Headerfile for src/arcbios.c.
 *
 *  (Note: There are also files called arcbios_other.h and sgi_arcbios.h,
 *  which are copied from NetBSD.)
 */

#include "misc.h"
#include "sgi_arcbios.h"

struct cpu;

/*  arcbios.c:  */
void arcbios_add_string_to_component(char *string, uint64_t component);
void arcbios_register_scsicontroller(struct machine *machine,
	uint64_t scsicontroller_component);
uint64_t arcbios_get_scsicontroller(struct machine *machine);
void arcbios_add_memory_descriptor(struct cpu *cpu,
	uint64_t base, uint64_t len, int arctype);
uint64_t arcbios_addchild_manual(struct cpu *cpu,
	uint64_t class, uint64_t type, uint64_t flags, uint64_t version,
	uint64_t revision, uint64_t key, uint64_t affinitymask,
	char *identifier, uint64_t parent, void *config_data,
	size_t config_len);
int arcbios_emul(struct cpu *cpu);
void arcbios_set_default_exception_handler(struct cpu *cpu);

void arcbios_console_init(struct machine *machine,
	uint64_t vram, uint64_t ctrlregs);
void arcbios_init(struct machine *machine, int is64bit,
	uint64_t sgi_ram_offset);


#define	ARC_CONSOLE_MAX_X	80
#define	ARC_CONSOLE_MAX_Y	25

#define	ARC_MAX_ESC		16

#define	MAX_OPEN_STRINGLEN	200
#define	ARC_MAX_HANDLES		10

struct machine_arcbios {
	/*  General stuff:  */
	int		arc_64bit;
	int		wordlen;		/*  cached  */

	/*  VGA Console I/O:  */
	int		vgaconsole;		/*  1 or 0  */
	uint64_t	console_vram;
	uint64_t	console_ctrlregs;
	char		escape_sequence[ARC_MAX_ESC+1];
	int		in_escape_sequence;
	int		console_maxx;
	int		console_maxy;
	int		console_curx;
	int		console_cury;
	int		console_reverse;
	int		console_curcolor;

	/*  File handles:  */
	int		file_handle_in_use[ARC_MAX_HANDLES];
	char		*file_handle_string[ARC_MAX_HANDLES];
	uint64_t	current_seek_offset[ARC_MAX_HANDLES];

	/*  SCSI:  */
	uint64_t	scsicontroller;		/*  component addr  */
};


/*  For internal use in arcbios.c:  */

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


#endif	/*  ARCBIOS_H  */
