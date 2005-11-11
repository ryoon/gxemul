#ifndef	CPU_I960_H
#define	CPU_I960_H

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
 *  $Id: cpu_i960.h,v 1.6 2005-11-11 07:31:33 debug Exp $
 */

#include "misc.h"


struct cpu_family;

#define	N_I960_NREGS		32
#define	I960_SP		1
#define	I960_FP		31

#define	I960_N_IC_ARGS			3
#define	I960_INSTR_ALIGNMENT_SHIFT	2
#define	I960_IC_ENTRIES_SHIFT		10
#define	I960_IC_ENTRIES_PER_PAGE	(1 << I960_IC_ENTRIES_SHIFT)
#define	I960_PC_TO_IC_ENTRY(a)		(((a)>>I960_INSTR_ALIGNMENT_SHIFT) \
					& (I960_IC_ENTRIES_PER_PAGE-1))
#define	I960_ADDR_TO_PAGENR(a)		((a) >> (I960_IC_ENTRIES_SHIFT \
					+ I960_INSTR_ALIGNMENT_SHIFT))

struct i960_instr_call {
	void	(*f)(struct cpu *, struct i960_instr_call *);
	int	len;
	size_t	arg[I960_N_IC_ARGS];
};

/*  Translation cache struct for each physical page:  */
struct i960_tc_physpage {
	struct i960_instr_call ics[I960_IC_ENTRIES_PER_PAGE + 1];
	uint32_t	next_ofs;	/*  or 0 for end of chain  */
	uint32_t	physaddr;
	int		flags;
};


#define	I960_N_VPH_ENTRIES	1048576

#define	I960_MAX_VPH_TLB_ENTRIES		256
struct i960_vpg_tlb_entry {
	unsigned char	valid;
	unsigned char	writeflag;
	uint32_t	vaddr_page;
	uint32_t	paddr_page;
	unsigned char	*host_page;
};


struct i960_cpu {
	/*
	 *  General Purpose Registers:
	 */

	uint32_t	r[N_I960_NREGS];


	/*
	 *  Instruction translation cache:
	 */

	/*  cur_ic_page is a pointer to an array of I960_IC_ENTRIES_PER_PAGE
	    instruction call entries. next_ic points to the next such
	    call to be executed.  */
	struct i960_tc_physpage	*cur_physpage;
	struct i960_instr_call	*cur_ic_page;
	struct i960_instr_call	*next_ic;


	/*
	 *  Virtual -> physical -> host address translation:
	 *
	 *  host_load and host_store point to arrays of I960_N_VPH_ENTRIES
	 *  pointers (to host pages); phys_addr points to an array of
	 *  I960_N_VPH_ENTRIES uint32_t.
	 */

	struct i960_vpg_tlb_entry	vph_tlb_entry[I960_MAX_VPH_TLB_ENTRIES];
	unsigned char			*host_load[I960_N_VPH_ENTRIES];
	unsigned char			*host_store[I960_N_VPH_ENTRIES];
	uint32_t			phys_addr[I960_N_VPH_ENTRIES];
	struct i960_tc_physpage		*phys_page[I960_N_VPH_ENTRIES];

	uint32_t			phystranslation[I960_N_VPH_ENTRIES/32];
	uint8_t				vaddr_to_tlbindex[I960_N_VPH_ENTRIES];
};


/*  cpu_i960.c:  */
void i960_update_translation_table(struct cpu *cpu, uint64_t vaddr_page,
	unsigned char *host_page, int writeflag, uint64_t paddr_page);
void i960_invalidate_translation_caches(struct cpu *cpu, uint64_t, int);
void i960_invalidate_code_translation(struct cpu *cpu, uint64_t, int);
int i960_memory_rw(struct cpu *cpu, struct memory *mem, uint64_t vaddr,
	unsigned char *data, size_t len, int writeflag, int cache_flags);
int i960_cpu_family_init(struct cpu_family *);


#endif	/*  CPU_I960_H  */
