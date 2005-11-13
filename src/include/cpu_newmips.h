#ifndef	CPU_NEWMIPS_H
#define	CPU_NEWMIPS_H

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
 *  $Id: cpu_newmips.h,v 1.1 2005-11-13 22:34:25 debug Exp $
 */

#include "misc.h"


struct cpu_family;


#define	NEWMIPS_N_IC_ARGS			3
#define	NEWMIPS_INSTR_ALIGNMENT_SHIFT	2
#define	NEWMIPS_IC_ENTRIES_SHIFT		10
#define	NEWMIPS_IC_ENTRIES_PER_PAGE		(1 << NEWMIPS_IC_ENTRIES_SHIFT)
#define	NEWMIPS_PC_TO_IC_ENTRY(a)		(((a)>>NEWMIPS_INSTR_ALIGNMENT_SHIFT) \
					& (NEWMIPS_IC_ENTRIES_PER_PAGE-1))
#define	NEWMIPS_ADDR_TO_PAGENR(a)		((a) >> (NEWMIPS_IC_ENTRIES_SHIFT \
					+ NEWMIPS_INSTR_ALIGNMENT_SHIFT))

struct newmips_instr_call {
	void	(*f)(struct cpu *, struct newmips_instr_call *);
	size_t	arg[NEWMIPS_N_IC_ARGS];
};

/*  Translation cache struct for each physical page:  */
struct newmips_tc_physpage {
	struct newmips_instr_call ics[NEWMIPS_IC_ENTRIES_PER_PAGE + 1];
	uint32_t	next_ofs;	/*  or 0 for end of chain  */
	int		flags;
	uint64_t	physaddr;
};

#define	NEWMIPS_N_VPH_ENTRIES		1048576

#define	NEWMIPS_MAX_VPH_TLB_ENTRIES		128
struct newmips_vpg_tlb_entry {
	unsigned char	valid;
	unsigned char	writeflag;
	int64_t		timestamp;
	uint64_t	vaddr_page;
	uint64_t	paddr_page;
	unsigned char	*host_page;
};

struct newmips_cpu {
	int		bits;		/*  32 or 64  */

	uint64_t	r[32];


	/*
	 *  Instruction translation cache:
	 */

	/*  cur_ic_page is a pointer to an array of NEWMIPS_IC_ENTRIES_PER_PAGE
	    instruction call entries. next_ic points to the next such
	    call to be executed.  */
	struct newmips_tc_physpage	*cur_physpage;
	struct newmips_instr_call	*cur_ic_page;
	struct newmips_instr_call	*next_ic;


	/*
	 *  Virtual -> physical -> host address translation:
	 *
	 *  host_load and host_store point to arrays of NEWMIPS_N_VPH_ENTRIES
	 *  pointers (to host pages); phys_addr points to an array of
	 *  NEWMIPS_N_VPH_ENTRIES uint32_t.
	 */

	struct newmips_vpg_tlb_entry  vph_tlb_entry[NEWMIPS_MAX_VPH_TLB_ENTRIES];
	unsigned char		   *host_load[NEWMIPS_N_VPH_ENTRIES]; 
	unsigned char		   *host_store[NEWMIPS_N_VPH_ENTRIES];
	uint32_t		   phys_addr[NEWMIPS_N_VPH_ENTRIES]; 
	struct newmips_tc_physpage    *phys_page[NEWMIPS_N_VPH_ENTRIES];

	uint32_t		   phystranslation[NEWMIPS_N_VPH_ENTRIES/32];
	uint8_t			   vaddr_to_tlbindex[NEWMIPS_N_VPH_ENTRIES];
};


/*  cpu_newmips.c:  */
void newmips_update_translation_table(struct cpu *cpu, uint64_t vaddr_page,
	unsigned char *host_page, int writeflag, uint64_t paddr_page);
void newmips_invalidate_translation_caches(struct cpu *cpu, uint64_t, int);
void newmips_invalidate_code_translation(struct cpu *cpu, uint64_t, int);
void newmips32_update_translation_table(struct cpu *cpu, uint64_t vaddr_page,
	unsigned char *host_page, int writeflag, uint64_t paddr_page);
void newmips32_invalidate_translation_caches(struct cpu *cpu, uint64_t, int);
void newmips32_invalidate_code_translation(struct cpu *cpu, uint64_t, int);
int newmips_memory_rw(struct cpu *cpu, struct memory *mem, uint64_t vaddr,
	unsigned char *data, size_t len, int writeflag, int cache_flags);
int newmips_cpu_family_init(struct cpu_family *);


#endif	/*  CPU_NEWMIPS_H  */
