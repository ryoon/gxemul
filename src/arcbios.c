/*
 *  Copyright (C) 2003 by Anders Gavare.  All rights reserved.
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
 *  $Id: arcbios.c,v 1.7 2003-12-20 21:19:44 debug Exp $
 *
 *  ARCBIOS emulation.
 *
 *  This whole file is a mess.
 *  TODO:  Fix.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

#include "misc.h"
#include "console.h"


extern int machine;
extern int register_dump;
extern int instruction_trace;
extern int show_nr_of_instructions;
extern int quiet_mode;
extern int use_x11;
extern int bootstrap_cpu;
extern int ncpus;
extern struct cpu **cpus;


struct emul_arc_child {
	uint32_t	ptr_peer;
	uint32_t	ptr_child;
	uint32_t	ptr_parent;
	struct arcbios_component component;
};

#define	FIRST_ARC_COMPONENT	0x80004000		/*  TODO: ugly  */
uint32_t arcbios_next_component_address = FIRST_ARC_COMPONENT;
int n_arc_components = 0;


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
uint32_t arcbios_addchild(struct arcbios_component *host_tmp_component, char *identifier, uint32_t parent)
{
	uint64_t a = arcbios_next_component_address;
	uint32_t peer=0;
	uint32_t child=0;
	int n_left;
	uint64_t peeraddr = FIRST_ARC_COMPONENT;

	/*
	 *  This component has no children yet, but it may have peers (that is, other components
	 *  that share this component's parent) so we have to set the peer value correctly.
	 *
	 *  Also, if this is the first child of some parent, the parent's child pointer should
	 *  be set to point to this component.  (But only if it is the first.)
	 *
	 *  This is really ugly:  scan through all components, starting from FIRST_ARC_COMPONENT,
	 *  to find a component with the same parent as this component will have.  If such a
	 *  component is found, and its 'peer' value is NULL, then set it to this component's
	 *  address (a).
	 *
	 *  TODO:  make nicer
	 */

	n_left = n_arc_components;
	while (n_left > 0) {
		/*  Load parent, child, and peer values:  */
		uint32_t eparent, echild, epeer, tmp;
		unsigned char buf[4];

		memory_rw(cpus[bootstrap_cpu], cpus[bootstrap_cpu]->mem, peeraddr + 0, &buf[0], sizeof(parent), MEM_READ, CACHE_NONE | NO_EXCEPTIONS);
		if (cpus[bootstrap_cpu]->byte_order == EMUL_BIG_ENDIAN) {
			unsigned char tmp; tmp = buf[0]; buf[0] = buf[3]; buf[3] = tmp;
			tmp = buf[1]; buf[1] = buf[2]; buf[2] = tmp;
		}
		eparent = buf[0] + (buf[1]<<8) + (buf[2]<<16) + (buf[3]<<24);
		memory_rw(cpus[bootstrap_cpu], cpus[bootstrap_cpu]->mem, peeraddr + 0, &buf[0], sizeof(parent), MEM_READ, CACHE_NONE | NO_EXCEPTIONS);
		if (cpus[bootstrap_cpu]->byte_order == EMUL_BIG_ENDIAN) {
			unsigned char tmp; tmp = buf[0]; buf[0] = buf[3]; buf[3] = tmp;
			tmp = buf[1]; buf[1] = buf[2]; buf[2] = tmp;
		}
		echild  = buf[0] + (buf[1]<<8) + (buf[2]<<16) + (buf[3]<<24);
		memory_rw(cpus[bootstrap_cpu], cpus[bootstrap_cpu]->mem, peeraddr + 0, &buf[0], sizeof(parent), MEM_READ, CACHE_NONE | NO_EXCEPTIONS);
		if (cpus[bootstrap_cpu]->byte_order == EMUL_BIG_ENDIAN) {
			unsigned char tmp; tmp = buf[0]; buf[0] = buf[3]; buf[3] = tmp;
			tmp = buf[1]; buf[1] = buf[2]; buf[2] = tmp;
		}
		epeer   = buf[0] + (buf[1]<<8) + (buf[2]<<16) + (buf[3]<<24);

		if (eparent == parent && epeer == 0) {
			epeer = a;
			store_32bit_word(peeraddr + 0x00, echild);
		}
		if (peeraddr == parent && echild == 0) {
			echild = a;
			store_32bit_word(peeraddr + 0x04, echild);
		}

		/*  Go to the next component:  */
		memory_rw(cpus[bootstrap_cpu], cpus[bootstrap_cpu]->mem, peeraddr + 0x28, &buf[0], sizeof(parent), MEM_READ, CACHE_NONE | NO_EXCEPTIONS);
		if (cpus[bootstrap_cpu]->byte_order == EMUL_BIG_ENDIAN) {
			unsigned char tmp; tmp = buf[0]; buf[0] = buf[3]; buf[3] = tmp;
			tmp = buf[1]; buf[1] = buf[2]; buf[2] = tmp;
		}
		tmp = buf[0] + (buf[1]<<8) + (buf[2]<<16) + (buf[3]<<24);
		peeraddr += 0x30;
		peeraddr += tmp;
		peeraddr = ((peeraddr - 1) | 3) + 1;

		n_left --;
	}

	store_32bit_word(a + 0x00, peer);
	store_32bit_word(a + 0x04, child);
	store_32bit_word(a + 0x08, parent);
	store_32bit_word(a+  0x0c, host_tmp_component->Class);
	store_32bit_word(a+  0x10, host_tmp_component->Type);
	store_32bit_word(a+  0x14, host_tmp_component->Flags);
	store_32bit_word(a+  0x18, host_tmp_component->Version + 65536*host_tmp_component->Revision);
	store_32bit_word(a+  0x1c, host_tmp_component->Key);
	store_32bit_word(a+  0x20, host_tmp_component->AffinityMask);
	store_32bit_word(a+  0x24, host_tmp_component->ConfigurationDataSize);
	store_32bit_word(a+  0x28, host_tmp_component->IdentifierLength);
	store_32bit_word(a+  0x2c, host_tmp_component->Identifier);

	arcbios_next_component_address += 0x30;

	if (host_tmp_component->IdentifierLength > 0) {
		store_32bit_word(a + 0x2c, a + 0x30);
		store_string(a + 0x30, identifier);
		arcbios_next_component_address += strlen(identifier) + 1;
	}

	/*  Round up to next 0x4 bytes:  */
	arcbios_next_component_address = ((arcbios_next_component_address - 1) | 3) + 1;

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
uint32_t arcbios_addchild_manual(uint32_t class, uint32_t type, uint32_t flags, uint16_t version,
	uint16_t revision, uint32_t key, uint32_t affinitymask, char *identifier, uint32_t parent)
{
	/*  This component is only for temporary use:  */
	struct arcbios_component component;

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
		component.IdentifierLength = strlen(identifier);
	}

	return arcbios_addchild(&component, identifier, parent);
}


/*
 *  arcbios_emul():  ARCBIOS emulation
 *
 *	0x24	GetPeer(node)
 *	0x28	GetChild(node)
 *	0x2c	GetParent(node)
 *	0x44	GetSystemId()
 *	0x48	GetMemoryDescriptor(void *)
 *	0x6c	Write(handle, buf, len, &returnlen)
 *	0x78	GetEnvironmentVariable(char *)
 *	0x88	FlushAllCaches()
 *	0x90	GetDisplayStatus(uint32_t handle)
 */
void arcbios_emul(struct cpu *cpu)
{
	int vector = cpu->pc & 0xffff;
	int i, j;
	unsigned char ch2;
	char buf[40];

	switch (vector) {
	case 0x24:		/*  GetPeer(node)  */
		debug("[ ARCBIOS GetPeer(node 0x%08x) ]\n", cpu->gpr[GPR_A0]);
		{
			uint32_t peer;
			memory_rw(cpu, cpu->mem, cpu->gpr[GPR_A0] - 0xc, &buf[0], sizeof(peer), MEM_READ, CACHE_NONE | NO_EXCEPTIONS);
			if (cpu->byte_order == EMUL_BIG_ENDIAN) {
				unsigned char tmp; tmp = buf[0]; buf[0] = buf[3]; buf[3] = tmp;
				tmp = buf[1]; buf[1] = buf[2]; buf[2] = tmp;
			}
			peer = buf[0] + (buf[1]<<8) + (buf[2]<<16) + (buf[3]<<24);
			/*  TODO:  ugly: this is little endian, ARC only supports little endian...  */
			cpu->gpr[GPR_V0] = peer;
		}
		break;
	case 0x28:		/*  GetChild(node)  */
		debug("[ ARCBIOS GetChild(node 0x%08x) ]\n", cpu->gpr[GPR_A0]);
		if (cpu->gpr[GPR_A0] == 0)
			cpu->gpr[GPR_V0] = FIRST_ARC_COMPONENT + 0xc;
		else {
			uint32_t child;
			memory_rw(cpu, cpu->mem, cpu->gpr[GPR_A0] - 0x8, &buf[0], sizeof(child), MEM_READ, CACHE_NONE | NO_EXCEPTIONS);
			if (cpu->byte_order == EMUL_BIG_ENDIAN) {
				unsigned char tmp; tmp = buf[0]; buf[0] = buf[3]; buf[3] = tmp;
				tmp = buf[1]; buf[1] = buf[2]; buf[2] = tmp;
			}
			child = buf[0] + (buf[1]<<8) + (buf[2]<<16) + (buf[3]<<24);
			/*  TODO:  ugly: this is little endian, ARC only supports little endian...  */
			cpu->gpr[GPR_V0] = child;
		}
		break;
	case 0x2c:		/*  GetParent(node)  */
		debug("[ ARCBIOS GetParent(node 0x%08x) ]\n", cpu->gpr[GPR_A0]);
		{
			uint32_t parent;
			memory_rw(cpu, cpu->mem, cpu->gpr[GPR_A0] - 0x4, &buf[0], sizeof(parent), MEM_READ, CACHE_NONE | NO_EXCEPTIONS);
			if (cpu->byte_order == EMUL_BIG_ENDIAN) {
				unsigned char tmp; tmp = buf[0]; buf[0] = buf[3]; buf[3] = tmp;
				tmp = buf[1]; buf[1] = buf[2]; buf[2] = tmp;
			}
			parent = buf[0] + (buf[1]<<8) + (buf[2]<<16) + (buf[3]<<24);
			/*  TODO:  ugly: this is little endian, ARC only supports little endian...  */
			cpu->gpr[GPR_V0] = parent;
		}
		break;
	case 0x44:		/*  GetSystemId()  */
		debug("[ ARCBIOS GetSystemId() ]\n");
		cpu->gpr[GPR_V0] = SGI_SYSID_ADDR;
		break;
	case 0x48:		/*  void *GetMemoryDescriptor(void *ptr)  */
		debug("[ ARCBIOS GetMemoryDescriptor(0x%08x) ]\n", cpu->gpr[GPR_A0]);
		if ((uint32_t)cpu->gpr[GPR_A0] < (uint32_t)ARC_MEMDESC_ADDR)
			cpu->gpr[GPR_V0] = ARC_MEMDESC_ADDR;
		else
			cpu->gpr[GPR_V0] = 0;
		break;
	case 0x6c:		/*  Write(handle, buf, len, &returnlen)  */
		if (cpu->gpr[GPR_A0] != 1)	/*  1 = stdout?  */
			debug("[ ARCBIOS Write(%i,0x%08llx,%i,0x%08llx) ]\n",
			    (int)cpu->gpr[GPR_A0], (long long)cpu->gpr[GPR_A1],
			    (int)cpu->gpr[GPR_A2], (long long)cpu->gpr[GPR_A3]);
		for (i=0; i<cpu->gpr[GPR_A2]; i++) {
			unsigned char ch = '\0';
			memory_rw(cpu, cpu->mem, cpu->gpr[GPR_A1] + i, &ch, sizeof(ch), MEM_READ, CACHE_NONE | NO_EXCEPTIONS);
			console_putchar(ch);
		}
		/*  TODO: store len in returnlen  */
		break;
	case 0x78:		/*  GetEnvironmentVariable(char *)  */
		/*  Find the environment variable given by a0:  */
		for (i=0; i<sizeof(buf); i++)
			memory_rw(cpu, cpu->mem, cpu->gpr[GPR_A0] + i, &buf[i], sizeof(char), MEM_READ, CACHE_NONE | NO_EXCEPTIONS);
		buf[sizeof(buf)-1] = '\0';
		debug("[ ARCBIOS GetEnvironmentVariable(\"%s\") ]\n", buf);
		for (i=0; i<0x1000; i++) {
			/*  Matching string at offset i?  */
			int nmatches = 0;
			for (j=0; j<strlen(buf); j++) {
				memory_rw(cpu, cpu->mem, (uint64_t)(SGI_ENV_STRINGS + i + j), &ch2, sizeof(char), MEM_READ, CACHE_NONE | NO_EXCEPTIONS);
				if (ch2 == buf[j])
					nmatches++;
			}
			memory_rw(cpu, cpu->mem, (uint64_t)(SGI_ENV_STRINGS + i + strlen(buf)), &ch2, sizeof(char), MEM_READ, CACHE_NONE | NO_EXCEPTIONS);
			if (nmatches == strlen(buf) && ch2 == '=') {
				cpu->gpr[GPR_V0] = SGI_ENV_STRINGS + i + strlen(buf) + 1;
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
		exit(1);
	}
}

