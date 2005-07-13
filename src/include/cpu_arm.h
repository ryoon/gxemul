#ifndef	CPU_ARM_H
#define	CPU_ARM_H

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
 *  $Id: cpu_arm.h,v 1.19 2005-07-13 21:22:14 debug Exp $
 */

#include "misc.h"


struct cpu_family;

#define	ARM_SL			10
#define	ARM_FP			11
#define	ARM_IP			12
#define	ARM_SP			13
#define	ARM_LR			14
#define	ARM_PC			15
#define	N_ARM_REGS		16

#define	ARM_REG_NAMES	{					\
	"r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",		\
	"r8", "r9", "sl", "fp", "ip", "sp", "lr", "pc"  }


/*
 *  Translated instruction calls:
 *
 *  The translation cache begins with N_BASE_TABLE_ENTRIES uint32_t offsets
 *  to arm_tc_physpage structs.
 */
#define	ARM_N_IC_ARGS			3
#define	ARM_IC_ENTRIES_SHIFT		10
#define	ARM_IC_ENTRIES_PER_PAGE		(1 << ARM_IC_ENTRIES_SHIFT)
#define	ARM_PC_TO_IC_ENTRY(a)		(((a)>>2) & (ARM_IC_ENTRIES_PER_PAGE-1))
#define	ARM_ADDR_TO_PAGENR(a)		((a) >> (ARM_IC_ENTRIES_SHIFT+2))
#define	ARM_N_BASE_TABLE_ENTRIES	32768
#define	ARM_PAGENR_TO_TABLE_INDEX(a)	((a) & (ARM_N_BASE_TABLE_ENTRIES-1))

struct arm_instr_call {
	void	(*f)(struct cpu *, struct arm_instr_call *);
	size_t	arg[ARM_N_IC_ARGS];
};

/*  512 translated pages should be good enough:  */
#define	ARM_TRANSLATION_CACHE_SIZE	(512 * \
		sizeof(struct arm_instr_call) * ARM_IC_ENTRIES_PER_PAGE)
#define	ARM_TRANSLATION_CACHE_MARGIN	(2 * \
		sizeof(struct arm_instr_call) * ARM_IC_ENTRIES_PER_PAGE)

struct arm_tc_physpage {
	uint32_t	next_ofs;	/*  or 0 for end of chain  */
	uint32_t	physaddr;
	int		flags;
	struct arm_instr_call ics[ARM_IC_ENTRIES_PER_PAGE + 1];
};


#define	ARM_FLAG_N	0x80000000	/*  Negative flag  */
#define	ARM_FLAG_Z	0x40000000	/*  Zero flag  */
#define	ARM_FLAG_C	0x20000000	/*  Carry flag  */
#define	ARM_FLAG_V	0x10000000	/*  Overflow flag  */
#define	ARM_FLAG_I	0x00000080	/*  Interrupt disable  */
#define	ARM_FLAG_F	0x00000040	/*  Fast Interrupt disable  */

#define	ARM_FLAG_MODE	0x0000001f
#define	ARM_MODE_USR26	      0x00
#define	ARM_MODE_FIQ26	      0x01
#define	ARM_MODE_IRQ26	      0x02
#define	ARM_MODE_SVC26	      0x03
#define	ARM_MODE_USR32	      0x10
#define	ARM_MODE_FIQ32	      0x11
#define	ARM_MODE_IRQ32	      0x12
#define	ARM_MODE_SVC32	      0x13
#define	ARM_MODE_ABT32	      0x17
#define	ARM_MODE_UND32	      0x1b


/*  Virtual->physical->host page entry:  */
#define	ARM_N_VPH_ENTRIES		1024
struct arm_vph_page {
	void		*host_load[ARM_N_VPH_ENTRIES];
	void		*host_store[ARM_N_VPH_ENTRIES];
	uint32_t	phys_addr[ARM_N_VPH_ENTRIES];
	int		refcount;
	struct arm_vph_page *next;	/*  Freelist, used if refcount = 0.  */
};

#define	ARM_MAX_VPH_TLB_ENTRIES		64
struct arm_vpg_tlb_entry {
	int		valid;
	int		writeflag;
	int64_t		timestamp;
	unsigned char	*host_page;
	uint32_t	vaddr_page;
	uint32_t	paddr_page;
};


struct arm_cpu {
	/*
	 *  Misc.:
	 */
	uint32_t		flags;


	/*
	 *  General Purpose Registers (including the program counter):
	 *
	 *  r[] always contains the current register set. The others are
	 *  only used to swap to/from when changing modes. (An exception is
	 *  r[0..7], which are never swapped out, they are always present.)
	 */

	uint32_t		r[N_ARM_REGS];
	uint32_t		usr_r8_r14[7];
	uint32_t		fiq_r8_r14[7];
	uint32_t		irq_r13_r14[2];
	uint32_t		svc_r13_r14[2];
	uint32_t		abt_r13_r14[2];
	uint32_t		und_r13_r14[2];


	/*
	 *  Instruction translation cache:
	 */

	unsigned char		*translation_cache;
	size_t			translation_cache_cur_ofs;

	/*  cur_ic_page is a pointer to an array of ARM_IC_ENTRIES_PER_PAGE
	    instruction call entries. next_ic points to the next such
	    call to be executed.  */
	struct arm_tc_physpage	*cur_physpage;
	struct arm_instr_call	*cur_ic_page;
	struct arm_instr_call	*next_ic;

	int			running_translated;
	int32_t			n_translated_instrs;


	/*
	 *  Virtual -> physical -> host address translation:
	 */

	struct arm_vpg_tlb_entry	vph_tlb_entry[ARM_MAX_VPH_TLB_ENTRIES];
	struct arm_vph_page		*vph_default_page;
	struct arm_vph_page		*vph_next_free_page;
	struct arm_vph_page		*vph_table0[ARM_N_VPH_ENTRIES];
};


/*  cpu_arm.c:  */
void arm_update_translation_table(struct cpu *cpu, uint64_t vaddr_page,
	unsigned char *host_page, int writeflag, uint64_t paddr_page);
void arm_invalidate_translation_caches_paddr(struct cpu *cpu, uint64_t paddr);
int arm_memory_rw(struct cpu *cpu, struct memory *mem, uint64_t vaddr,
	unsigned char *data, size_t len, int writeflag, int cache_flags);
int arm_cpu_family_init(struct cpu_family *);


#endif	/*  CPU_ARM_H  */
