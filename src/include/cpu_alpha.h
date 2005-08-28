#ifndef	CPU_ALPHA_H
#define	CPU_ALPHA_H

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
 *  $Id: cpu_alpha.h,v 1.23 2005-08-28 20:16:24 debug Exp $
 */

#include "misc.h"


struct cpu_family;

#define	ALPHA_V0		0
#define	ALPHA_A0		16
#define	ALPHA_A1		17
#define	ALPHA_A2		18
#define	ALPHA_A3		19
#define	ALPHA_A4		20
#define	ALPHA_A5		21
#define	ALPHA_RA		26
#define	ALPHA_T12		27
#define	ALPHA_SP		30
#define	ALPHA_ZERO		31
#define	N_ALPHA_REGS		32

#define ALPHA_REG_NAMES		{				\
	"v0", "t0", "t1", "t2", "t3", "t4", "t5", "t6",		\
	"t7", "s0", "s1", "s2", "s3", "s4", "s5", "fp",		\
	"a0", "a1", "a2", "a3", "a4", "a5", "t8", "t9",		\
	"t10", "t11", "ra", "t12", "at", "gp", "sp", "zero" 	}


#define	ALPHA_N_IC_ARGS			3
#define	ALPHA_INSTR_ALIGNMENT_SHIFT	2
#define	ALPHA_IC_ENTRIES_SHIFT		11
#define	ALPHA_IC_ENTRIES_PER_PAGE	(1 << ALPHA_IC_ENTRIES_SHIFT)
#define	ALPHA_PC_TO_IC_ENTRY(a)		(((a)>>ALPHA_INSTR_ALIGNMENT_SHIFT) \
					& (ALPHA_IC_ENTRIES_PER_PAGE-1))
#define	ALPHA_ADDR_TO_PAGENR(a)		((a) >> (ALPHA_IC_ENTRIES_SHIFT \
					+ ALPHA_INSTR_ALIGNMENT_SHIFT))

struct alpha_instr_call {
	void	(*f)(struct cpu *, struct alpha_instr_call *);
	size_t	arg[ALPHA_N_IC_ARGS];
};

/*  Translation cache struct for each physical page:  */
struct alpha_tc_physpage {
	uint32_t	next_ofs;	/*  or 0 for end of chain  */
	uint32_t	physaddr;
	int		flags;
	struct alpha_instr_call ics[ALPHA_IC_ENTRIES_PER_PAGE + 1];
};


/*
 *  Virtual->physical->host page entry:
 *
 *	13 + 13 + 13 bits = 39 bits (should be enough for most userspace
 *	applications)
 *
 *  There is also an additional check for kernel space addresses.
 */
#define	ALPHA_TOPSHIFT			39
#define	ALPHA_TOP_KERNEL		0x1fffff8
#define	ALPHA_LEVEL0_SHIFT		26
#define	ALPHA_LEVEL0			8192
#define	ALPHA_LEVEL1_SHIFT		13
#define	ALPHA_LEVEL1			8192
struct alpha_vph_page {
	void		*host_load[ALPHA_LEVEL1];
	void		*host_store[ALPHA_LEVEL1];
	uint64_t	phys_addr[ALPHA_LEVEL1];
	struct alpha_tc_physpage *phys_page[ALPHA_LEVEL1];
	int		refcount;
	struct alpha_vph_page	*next;	/*  Freelist, used if refcount = 0.  */
};

#define	ALPHA_MAX_VPH_TLB_ENTRIES	128
struct alpha_vpg_tlb_entry {
	int		valid;
	int		writeflag;
	int64_t		timestamp;
	unsigned char	*host_page;
	uint64_t	vaddr_page;
	uint64_t	paddr_page;
};

struct alpha_cpu {
	/*
	 *  General Purpose Registers:
	 */

	uint64_t		r[N_ALPHA_REGS];	/*  Integer  */
	uint64_t		f[N_ALPHA_REGS];	/*  Floating Point  */


	/*  Misc.:  */
	uint64_t		pcc;			/*  Cycle Counter  */
	uint64_t		ipl;
	uint64_t		load_linked_addr;
	int			ll_flag;


	/*
	 *  Instruction translation cache:
	 */

	/*  cur_ic_page is a pointer to an array of ALPHA_IC_ENTRIES_PER_PAGE
	    instruction call entries. next_ic points to the next such
	    call to be executed.  */
	struct alpha_tc_physpage *cur_physpage;
	struct alpha_instr_call	*cur_ic_page;
	struct alpha_instr_call	*next_ic;


	/*
	 *  Virtual -> physical -> host address translation:
	 */

	struct alpha_vpg_tlb_entry vph_tlb_entry[ALPHA_MAX_VPH_TLB_ENTRIES];
	struct alpha_vph_page	*vph_default_page;
	struct alpha_vph_page	*vph_next_free_page;
	struct alpha_vph_table	*vph_next_free_table;
	struct alpha_vph_page	*vph_table0[ALPHA_LEVEL0];
	struct alpha_vph_page	*vph_table0_kernel[ALPHA_LEVEL0];
};


/*  cpu_alpha.c:  */
void alpha_update_translation_table(struct cpu *cpu, uint64_t vaddr_page,
	unsigned char *host_page, int writeflag, uint64_t paddr_page);
void alpha_invalidate_translation_caches_paddr(struct cpu *cpu, uint64_t, int);
void alpha_invalidate_code_translation(struct cpu *cpu, uint64_t, int);
int alpha_memory_rw(struct cpu *cpu, struct memory *mem, uint64_t vaddr,
	unsigned char *data, size_t len, int writeflag, int cache_flags);
int alpha_userland_memory_rw(struct cpu *cpu, struct memory *mem,
	uint64_t vaddr, unsigned char *data, size_t len, int writeflag,
	int cache_flags);
int alpha_cpu_family_init(struct cpu_family *);

/*  cpu_alpha_palcode.c:  */
void alpha_palcode_name(uint32_t palcode, char *buf, size_t buflen);
void alpha_palcode(struct cpu *cpu, uint32_t palcode);


#endif	/*  CPU_ALPHA_H  */
