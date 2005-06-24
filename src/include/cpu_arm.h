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
 *  $Id: cpu_arm.h,v 1.5 2005-06-24 23:25:39 debug Exp $
 */

#include "misc.h"


struct cpu_family;

#define	N_ARM_REGS		16
#define	ARM_PC			15	/*  gpr 15  */

/*
 *  Translated instruction calls:
 *
 *  The translation cache begins with N_BASE_TABLE_ENTRIES uint32_t offsets
 *  to arm_tc_physpage structs.
 */
#define	N_IC_ARGS			3
#define	IC_ENTRIES_SHIFT		9
#define	IC_ENTRIES_PER_PAGE		(1 << IC_ENTRIES_SHIFT)
#define	PC_TO_IC_ENTRY(a)		(((a) >> 2) & (IC_ENTRIES_PER_PAGE-1))
#define	ADDR_TO_PAGENR(a)		((a) >> (IC_ENTRIES_SHIFT+2))
#define	N_BASE_TABLE_ENTRIES		32768
#define	PAGENR_TO_TABLE_INDEX(a)	((a) & (N_BASE_TABLE_ENTRIES-1))
#define	ARM_TRANSLATION_CACHE_SIZE	(1048576 * 12)
#define	ARM_TRANSLATION_CACHE_MARGIN	65536

struct arm_instr_call {
	void	(*f)(struct cpu *, struct arm_instr_call *);
	size_t	arg[N_IC_ARGS];
};

struct arm_tc_physpage {
	uint32_t	next_ofs;	/*  or 0 for end of chain  */
	uint32_t	physaddr;
	struct arm_instr_call ics[IC_ENTRIES_PER_PAGE + 1];
};

struct arm_cpu {
	/*  General Purpose Registers (including the program counter):  */
	uint32_t		r[N_ARM_REGS];

	unsigned char		*translation_cache;
	size_t			translation_cache_cur_ofs;

	/*  cur_ic_page is a pointer to an array of IC_ENTRIES_PER_PAGE
	    instruction call entries. next_ic points to the next such
	    call to be executed.  */
	struct arm_instr_call	*cur_ic_page;
	struct arm_instr_call	*next_ic;

	int			running_translated;
	int32_t			n_translated_instrs;
};


/*  cpu_arm.c:  */
int arm_memory_rw(struct cpu *cpu, struct memory *mem, uint64_t vaddr,
	unsigned char *data, size_t len, int writeflag, int cache_flags);
int arm_cpu_family_init(struct cpu_family *);


#endif	/*  CPU_ARM_H  */
