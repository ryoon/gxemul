#ifndef	CPU_M68K_H
#define	CPU_M68K_H

/*
 *  Copyright (C) 2005-2007  Anders Gavare.  All rights reserved.
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
 *  $Id: cpu_m68k.h,v 1.15 2006-12-30 13:31:00 debug Exp $
 */

#include "misc.h"


/*  M68K CPU types:  */
struct m68k_cpu_type_def {
	char		*name;
	int		flags;
};

#define M68K_CPU_TYPE_DEFS	{				\
	{ "68000",		0 },				\
	{ "68010",		0 },				\
	{ "68020",		0 },				\
	{ "68030",		0 },				\
	{ "68040",		0 },				\
	{ "68060",		0 },				\
        { NULL,                 0 }				}

struct cpu_family;

#define	N_M68K_DREGS		8
#define	N_M68K_AREGS		8

#define	M68K_N_IC_ARGS			3
#define	M68K_INSTR_ALIGNMENT_SHIFT	1
#define	M68K_IC_ENTRIES_SHIFT		11
#define	M68K_IC_ENTRIES_PER_PAGE	(1 << M68K_IC_ENTRIES_SHIFT)
#define	M68K_PC_TO_IC_ENTRY(a)		(((a)>>M68K_INSTR_ALIGNMENT_SHIFT) \
					& (M68K_IC_ENTRIES_PER_PAGE-1))
#define	M68K_ADDR_TO_PAGENR(a)		((a) >> (M68K_IC_ENTRIES_SHIFT \
					+ M68K_INSTR_ALIGNMENT_SHIFT))

DYNTRANS_MISC_DECLARATIONS(m68k,M68K,uint32_t)

#define	M68K_MAX_VPH_TLB_ENTRIES		128


struct m68k_cpu {
	struct m68k_cpu_type_def	cpu_type;

	/*
	 *  General Purpose Registers:
	 */

	uint32_t		d[N_M68K_DREGS];
	uint32_t		a[N_M68K_AREGS];


	/*
	 *  Instruction translation cache and 32-bit virtual -> physical ->
	 *  host address translation:
	 */
	DYNTRANS_ITC(m68k)
	VPH_TLBS(m68k,M68K)
	VPH32(m68k,M68K,uint32_t,uint8_t)
};


/*  cpu_m68k.c:  */
int m68k_run_instr(struct cpu *cpu);
void m68k_update_translation_table(struct cpu *cpu, uint64_t vaddr_page,
	unsigned char *host_page, int writeflag, uint64_t paddr_page);
void m68k_invalidate_translation_caches(struct cpu *cpu, uint64_t, int);
void m68k_invalidate_code_translation(struct cpu *cpu, uint64_t, int);
int m68k_memory_rw(struct cpu *cpu, struct memory *mem, uint64_t vaddr,
	unsigned char *data, size_t len, int writeflag, int cache_flags);
int m68k_cpu_family_init(struct cpu_family *);


#endif	/*  CPU_M68K_H  */
