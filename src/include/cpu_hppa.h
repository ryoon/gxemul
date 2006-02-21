#ifndef	CPU_HPPA_H
#define	CPU_HPPA_H

/*
 *  Copyright (C) 2005-2006  Anders Gavare.  All rights reserved.
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
 *  $Id: cpu_hppa.h,v 1.16 2006-02-21 18:10:42 debug Exp $
 */

#include "misc.h"


struct cpu_family;


#define	HPPA_N_IC_ARGS			3
#define	HPPA_INSTR_ALIGNMENT_SHIFT	2
#define	HPPA_IC_ENTRIES_SHIFT		10
#define	HPPA_IC_ENTRIES_PER_PAGE	(1 << HPPA_IC_ENTRIES_SHIFT)
#define	HPPA_PC_TO_IC_ENTRY(a)		(((a)>>HPPA_INSTR_ALIGNMENT_SHIFT) \
					& (HPPA_IC_ENTRIES_PER_PAGE-1))
#define	HPPA_ADDR_TO_PAGENR(a)		((a) >> (HPPA_IC_ENTRIES_SHIFT \
					+ HPPA_INSTR_ALIGNMENT_SHIFT))

#define	HPPA_L2N		17
#define	HPPA_L3N		18

DYNTRANS_MISC_DECLARATIONS(hppa,HPPA,uint64_t)
DYNTRANS_MISC64_DECLARATIONS(hppa,HPPA)

#define	HPPA_MAX_VPH_TLB_ENTRIES		128


#define	HPPA_NREGS		32

struct hppa_cpu {
	int		bits;		/*  32 or 64  */

	uint64_t	r[HPPA_NREGS];


	/*
	 *  Instruction translation cache and Virtual->Physical->Host
	 *  address translation:
	 */
	DYNTRANS_ITC(hppa)
	VPH_TLBS(hppa,HPPA)
	VPH32(hppa,HPPA,uint64_t,uint8_t)
	VPH64(hppa,HPPA,uint8_t)
};


/*  cpu_hppa.c:  */
void hppa_update_translation_table(struct cpu *cpu, uint64_t vaddr_page,
	unsigned char *host_page, int writeflag, uint64_t paddr_page);
void hppa_invalidate_translation_caches(struct cpu *cpu, uint64_t, int);
void hppa_invalidate_code_translation(struct cpu *cpu, uint64_t, int);
void hppa32_update_translation_table(struct cpu *cpu, uint64_t vaddr_page,
	unsigned char *host_page, int writeflag, uint64_t paddr_page);
void hppa32_invalidate_translation_caches(struct cpu *cpu, uint64_t, int);
void hppa32_invalidate_code_translation(struct cpu *cpu, uint64_t, int);
int hppa_memory_rw(struct cpu *cpu, struct memory *mem, uint64_t vaddr,
	unsigned char *data, size_t len, int writeflag, int cache_flags);
int hppa_cpu_family_init(struct cpu_family *);


#endif	/*  CPU_HPPA_H  */
