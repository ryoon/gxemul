#ifndef	CPU_IA64_H
#define	CPU_IA64_H

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
 *  $Id: cpu_ia64.h,v 1.9 2006-02-09 22:40:27 debug Exp $
 */

#include "misc.h"


struct cpu_family;

#define	IA64_N_IC_ARGS			3
#define	IA64_INSTR_ALIGNMENT_SHIFT	4
#define	IA64_IC_ENTRIES_SHIFT		8
#define	IA64_IC_ENTRIES_PER_PAGE	(1 << IA64_IC_ENTRIES_SHIFT)
#define	IA64_PC_TO_IC_ENTRY(a)		(((a)>>IA64_INSTR_ALIGNMENT_SHIFT) \
					& (IA64_IC_ENTRIES_PER_PAGE-1))
#define	IA64_ADDR_TO_PAGENR(a)		((a) >> (IA64_IC_ENTRIES_SHIFT \
					+ IA64_INSTR_ALIGNMENT_SHIFT))

DYNTRANS_MISC_DECLARATIONS(ia64,IA64,uint64_t)

#define	IA64_MAX_VPH_TLB_ENTRIES		128

#define	N_IA64_REG	128

struct ia64_cpu {
	/*  TODO  */
	uint64_t	r[N_IA64_REG];


	/*
	 *  Instruction translation cache and Virtual->Physical->Host
	 *  address translation:
	 */
	DYNTRANS_ITC(ia64)
	VPH_TLBS(ia64,IA64)
	VPH64(ia64,IA64,uint8_t)
};


/*  cpu_ia64.c:  */
void ia64_update_translation_table(struct cpu *cpu, uint64_t vaddr_page,
	unsigned char *host_page, int writeflag, uint64_t paddr_page);
void ia64_invalidate_translation_caches(struct cpu *cpu, uint64_t, int);
void ia64_invalidate_code_translation(struct cpu *cpu, uint64_t, int);
int ia64_memory_rw(struct cpu *cpu, struct memory *mem, uint64_t vaddr,
	unsigned char *data, size_t len, int writeflag, int cache_flags);
int ia64_userland_memory_rw(struct cpu *cpu, struct memory *mem,
	uint64_t vaddr, unsigned char *data, size_t len, int writeflag,
	int cache_flags);
int ia64_cpu_family_init(struct cpu_family *);


#endif	/*  CPU_IA64_H  */
