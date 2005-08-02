#ifndef	CPU_PPC_H
#define	CPU_PPC_H

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
 *  $Id: cpu_ppc.h,v 1.24 2005-08-02 20:05:51 debug Exp $
 */

#include "misc.h"


struct cpu_family;

#define	MODE_PPC		0
#define	MODE_POWER		1

/*  PPC CPU types:  */
struct ppc_cpu_type_def { 
	char		*name;
	int		bits;
	int		flags;
	int		icache_shift;
	int		iway;
	int		dcache_shift;
	int		dway;
	int		l2cache_shift;
	int		l2way;
	int		altivec;

	/*  TODO: POWER vs PowerPC?  */
};

/*  Flags:  */
#define	PPC_NOFP		1
/*  TODO: Most of these just bogus  */

#define PPC_CPU_TYPE_DEFS	{				\
	{ "PPC405GP", 32, PPC_NOFP, 15, 2, 15, 2, 20, 1, 0 },	\
	{ "PPC603e", 32, 0, 14, 4, 14, 4, 0, 0, 0 },		\
	{ "MPC7400", 32, 0, 15, 2, 15, 2, 19, 1, 1 },		\
	{ "PPC750", 32, 0, 15, 2, 15, 2, 20, 1, 0 },		\
	{ "G4e", 32, 0, 15, 8, 15, 8, 18, 8, 1 },		\
	{ "PPC970", 64, 0, 16, 1, 15, 2, 19, 1, 1 },		\
	{ NULL, 0, 0, 0,0, 0,0, 0,0, 0 }			\
	};

#define	PPC_NGPRS		32
#define	PPC_NFPRS		32


#define	PPC_N_IC_ARGS			3
#define	PPC_IC_ENTRIES_SHIFT		10
#define	PPC_IC_ENTRIES_PER_PAGE		(1 << PPC_IC_ENTRIES_SHIFT)
#define	PPC_PC_TO_IC_ENTRY(a)		(((a)>>2) & (PPC_IC_ENTRIES_PER_PAGE-1))
#define	PPC_ADDR_TO_PAGENR(a)		((a) >> (PPC_IC_ENTRIES_SHIFT+2))

struct ppc_instr_call {
	void	(*f)(struct cpu *, struct ppc_instr_call *);
	size_t	arg[PPC_N_IC_ARGS];
};

/*  Translation cache struct for each physical page:  */
struct ppc_tc_physpage {
	uint32_t	next_ofs;	/*  or 0 for end of chain  */
	uint32_t	physaddr;
	int		flags;
	struct ppc_instr_call ics[PPC_IC_ENTRIES_PER_PAGE + 1];
};

#define	PPC_N_VPH_ENTRIES		1048576

#define	PPC_MAX_VPH_TLB_ENTRIES		256
struct ppc_vpg_tlb_entry {
	int		valid;
	int		writeflag;
	int64_t		timestamp;
	unsigned char	*host_page;
	uint64_t	vaddr_page;
	uint64_t	paddr_page;
};

struct ppc_cpu {
	struct ppc_cpu_type_def cpu_type;

	int		trace_tree_depth;

	uint64_t	of_emul_addr;
	uint64_t	pc_last;

	int		mode;		/*  MODE_PPC or MODE_POWER  */
	int		bits;		/*  32 or 64  */

	uint32_t	cr;		/*  Condition Register  */
	uint32_t	fpscr;		/*  FP Status and Control Register  */
	uint64_t	lr;		/*  Link Register  */
	uint64_t	ctr;		/*  Count Register  */
	uint64_t	gpr[PPC_NGPRS];	/*  General Purpose Registers  */
	uint64_t	xer;		/*  FP Exception Register  */
	uint64_t	fpr[PPC_NFPRS];	/*  Floating-Point Registers  */

	uint32_t	tbl;		/*  Time Base Lower  */
	uint32_t	tbu;		/*  Time Base Upper  */
	uint32_t	dec;		/*  Decrementer  */
	uint32_t	hdec;		/*  Hypervisor Decrementer  */
	uint64_t	ssr0;		/*  Machine status save/restore
					    register 0  */
	uint64_t	ssr1;		/*  Machine status save/restore
					    register 1  */
	uint64_t	msr;		/*  Machine state register  */
	uint64_t	sprg0;		/*  Special Purpose Register G0  */
	uint64_t	sprg1;		/*  Special Purpose Register G1  */
	uint64_t	sprg2;		/*  Special Purpose Register G2  */
	uint64_t	sprg3;		/*  Special Purpose Register G3  */
	uint32_t	pvr;		/*  Processor Version Register  */
	uint32_t	pir;		/*  Processor ID  */


	/*
	 *  Instruction translation cache:
	 */

	/*  cur_ic_page is a pointer to an array of PPC_IC_ENTRIES_PER_PAGE
	    instruction call entries. next_ic points to the next such
	    call to be executed.  */
	struct ppc_tc_physpage	*cur_physpage;
	struct ppc_instr_call	*cur_ic_page;
	struct ppc_instr_call	*next_ic;


	/*
	 *  Virtual -> physical -> host address translation:
	 *
	 *  host_load and host_store point to arrays of PPC_N_VPH_ENTRIES
	 *  pointers (to host pages); phys_addr points to an array of
	 *  PPC_N_VPH_ENTRIES uint32_t.
	 */

	struct ppc_vpg_tlb_entry	vph_tlb_entry[PPC_MAX_VPH_TLB_ENTRIES];
	unsigned char			*host_load[PPC_N_VPH_ENTRIES]; 
	unsigned char			*host_store[PPC_N_VPH_ENTRIES];
	uint32_t			phys_addr[PPC_N_VPH_ENTRIES]; 
	struct ppc_tc_physpage		*phys_page[PPC_N_VPH_ENTRIES];
};


/*  Machine status word bits: (according to Book 3)  */
#define	PPC_MSR_SF	(1ULL << 63)	/*  Sixty-Four-Bit Mode  */
/*  bits 62..61 are reserved  */
#define	PPC_MSR_HV	(1ULL << 60)	/*  Hypervisor  */
/*  bits 59..17  are reserved  */
#define	PPC_MSR_ILE	(1 << 16)	/*  Interrupt Little-Endian Mode  */
#define	PPC_MSR_EE	(1 << 15)	/*  External Interrupt Enable  */
#define	PPC_MSR_PR	(1 << 14)	/*  Problem State  */
#define	PPC_MSR_FP	(1 << 13)	/*  Floating-Point Available  */
#define	PPC_MSR_ME	(1 << 12)	/*  Machine Check Interrupt Enable  */
#define	PPC_MSR_FE0	(1 << 11)	/*  Floating-Point Exception Mode 0  */
#define	PPC_MSR_SE	(1 << 10)	/*  Single-Step Trace Enable  */
#define	PPC_MSR_BE	(1 << 9)	/*  Branch Trace Enable  */
#define	PPC_MSR_FE1	(1 << 8)	/*  Floating-Point Exception Mode 1  */
#define	PPC_MSR_IR	(1 << 5)	/*  Instruction Relocate  */
#define	PPC_MSR_DR	(1 << 4)	/*  Data Relocate  */
#define	PPC_MSR_PMM	(1 << 2)	/*  Performance Monitor Mark  */
#define	PPC_MSR_RI	(1 << 1)	/*  Recoverable Interrupt  */
#define	PPC_MSR_LE	(1)		/*  Little-Endian Mode  */

/*  XER bits:  */
#define	PPC_XER_SO	(1 << 31)	/*  Summary Overflow  */
#define	PPC_XER_OV	(1 << 30)	/*  Overflow  */
#define	PPC_XER_CA	(1 << 29)	/*  Carry  */


/*  cpu_ppc.c:  */
void ppc_update_translation_table(struct cpu *cpu, uint64_t vaddr_page,
	unsigned char *host_page, int writeflag, uint64_t paddr_page);
void ppc_invalidate_translation_caches_paddr(struct cpu *cpu, uint64_t paddr);
int ppc_memory_rw(struct cpu *cpu, struct memory *mem, uint64_t vaddr,
	unsigned char *data, size_t len, int writeflag, int cache_flags);
int ppc_cpu_family_init(struct cpu_family *);


#endif	/*  CPU_PPC_H  */
