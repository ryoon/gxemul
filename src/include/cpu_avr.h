#ifndef	CPU_AVR_H
#define	CPU_AVR_H

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
 *  $Id: cpu_avr.h,v 1.1 2005-09-17 17:14:44 debug Exp $
 */

#include "misc.h"


struct cpu_family;

#define	N_AVR_REGS		32

#define	AVR_N_IC_ARGS			3
#define	AVR_INSTR_ALIGNMENT_SHIFT	1
#define	AVR_IC_ENTRIES_SHIFT		11
#define	AVR_IC_ENTRIES_PER_PAGE	(1 << AVR_IC_ENTRIES_SHIFT)
#define	AVR_PC_TO_IC_ENTRY(a)		(((a)>>AVR_INSTR_ALIGNMENT_SHIFT) \
					& (AVR_IC_ENTRIES_PER_PAGE-1))
#define	AVR_ADDR_TO_PAGENR(a)		((a) >> (AVR_IC_ENTRIES_SHIFT \
					+ AVR_INSTR_ALIGNMENT_SHIFT))

struct avr_instr_call {
	void	(*f)(struct cpu *, struct avr_instr_call *);
	size_t	arg[AVR_N_IC_ARGS];
};

/*  Translation cache struct for each physical page:  */
struct avr_tc_physpage {
	uint32_t	next_ofs;	/*  or 0 for end of chain  */
	uint32_t	physaddr;
	int		flags;
	struct avr_instr_call ics[AVR_IC_ENTRIES_PER_PAGE + 1];
};


#define	AVR_N_VPH_ENTRIES	1048576

#define	AVR_MAX_VPH_TLB_ENTRIES		256
struct avr_vpg_tlb_entry {
	int		valid;
	int		writeflag;
	int64_t		timestamp;
	unsigned char	*host_page;
	uint32_t	vaddr_page;
	uint32_t	paddr_page;
};


struct avr_cpu {
	/*
	 *  General Purpose Registers:
	 */
	uint8_t		r[N_AVR_REGS];


	/*
	 *  Instruction translation cache:
	 */

	/*  cur_ic_page is a pointer to an array of AVR_IC_ENTRIES_PER_PAGE
	    instruction call entries. next_ic points to the next such
	    call to be executed.  */
	struct avr_tc_physpage	*cur_physpage;
	struct avr_instr_call	*cur_ic_page;
	struct avr_instr_call	*next_ic;


	/*
	 *  Virtual -> physical -> host address translation:
	 *
	 *  host_load and host_store point to arrays of AVR_N_VPH_ENTRIES
	 *  pointers (to host pages); phys_addr points to an array of
	 *  AVR_N_VPH_ENTRIES uint32_t.
	 */

	struct avr_vpg_tlb_entry	vph_tlb_entry[AVR_MAX_VPH_TLB_ENTRIES];
	unsigned char			*host_load[AVR_N_VPH_ENTRIES];
	unsigned char			*host_store[AVR_N_VPH_ENTRIES];
	uint32_t			phys_addr[AVR_N_VPH_ENTRIES];
	struct avr_tc_physpage		*phys_page[AVR_N_VPH_ENTRIES];
};


/*  cpu_avr.c:  */
void avr_update_translation_table(struct cpu *cpu, uint64_t vaddr_page,
	unsigned char *host_page, int writeflag, uint64_t paddr_page);
void avr_invalidate_translation_caches_paddr(struct cpu *cpu, uint64_t, int);
void avr_invalidate_code_translation(struct cpu *cpu, uint64_t, int);
int avr_memory_rw(struct cpu *cpu, struct memory *mem, uint64_t vaddr,
	unsigned char *data, size_t len, int writeflag, int cache_flags);
int avr_cpu_family_init(struct cpu_family *);


#endif	/*  CPU_AVR_H  */
