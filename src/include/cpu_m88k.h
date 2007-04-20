#ifndef	CPU_M88K_H
#define	CPU_M88K_H

/*
 *  Copyright (C) 2007  Anders Gavare.  All rights reserved.
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
 *  $Id: cpu_m88k.h,v 1.2 2007-04-20 16:32:05 debug Exp $
 */

#include "misc.h"
#include "interrupt.h"

struct cpu_family;

/*  M88K CPU types:  */
struct m88k_cpu_type_def {
	char		*name;
	int		type;
};

#define	M88K_CPU_TYPE_DEFS	{					\
	{ "88100", 88100 },						\
	{ "88110", 88110 },						\
	{ NULL,    0     }						\
	}


#define	N_M88K_REGS		32

#define	M88K_N_IC_ARGS			3
#define	M88K_INSTR_ALIGNMENT_SHIFT	2
#define	M88K_IC_ENTRIES_SHIFT		10
#define	M88K_IC_ENTRIES_PER_PAGE		(1 << M88K_IC_ENTRIES_SHIFT)
#define	M88K_PC_TO_IC_ENTRY(a)		(((a)>>M88K_INSTR_ALIGNMENT_SHIFT) \
					& (M88K_IC_ENTRIES_PER_PAGE-1))
#define	M88K_ADDR_TO_PAGENR(a)		((a) >> (M88K_IC_ENTRIES_SHIFT \
					+ M88K_INSTR_ALIGNMENT_SHIFT))

DYNTRANS_MISC_DECLARATIONS(m88k,M88K,uint32_t)

#define	M88K_MAX_VPH_TLB_ENTRIES		128


/*  Register r0 is always zero.  */
#define	M88K_ZERO_REG		0

struct m88k_cpu {
	struct m88k_cpu_type_def cpu_type;

	uint32_t		r[N_M88K_REGS];

	int			irq_asserted;

	/*
	 *  Instruction translation cache, and 32-bit virtual -> physical ->
	 *  host address translation:
	 */
	DYNTRANS_ITC(m88k)
	VPH_TLBS(m88k,M88K)
	VPH32(m88k,M88K,uint32_t,uint8_t)
};


/*  cpu_m88k.c:  */
void m88k_setup_initial_translation_table(struct cpu *cpu, uint32_t ttb_addr);
int m88k_run_instr(struct cpu *cpu);
void m88k_update_translation_table(struct cpu *cpu, uint64_t vaddr_page,
	unsigned char *host_page, int writeflag, uint64_t paddr_page);
void m88k_invalidate_translation_caches(struct cpu *cpu, uint64_t, int);
void m88k_invalidate_code_translation(struct cpu *cpu, uint64_t, int);
int m88k_memory_rw(struct cpu *cpu, struct memory *mem, uint64_t vaddr,
	unsigned char *data, size_t len, int writeflag, int cache_flags);
int m88k_cpu_family_init(struct cpu_family *);

#endif	/*  CPU_M88K_H  */
