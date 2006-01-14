#ifndef	CPU_AVR_H
#define	CPU_AVR_H

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
 *  $Id: cpu_avr.h,v 1.13 2006-01-14 12:52:02 debug Exp $
 */

#include "misc.h"


struct cpu_family;

#define	N_AVR_REGS		32

#define	AVR_N_IC_ARGS			3
#define	AVR_INSTR_ALIGNMENT_SHIFT	1
#define	AVR_IC_ENTRIES_SHIFT		11
#define	AVR_IC_ENTRIES_PER_PAGE		(1 << AVR_IC_ENTRIES_SHIFT)
#define	AVR_PC_TO_IC_ENTRY(a)		(((a)>>AVR_INSTR_ALIGNMENT_SHIFT) \
					& (AVR_IC_ENTRIES_PER_PAGE-1))
#define	AVR_ADDR_TO_PAGENR(a)		((a) >> (AVR_IC_ENTRIES_SHIFT \
					+ AVR_INSTR_ALIGNMENT_SHIFT))

DYNTRANS_MISC_DECLARATIONS(avr,AVR,uint64_t)

#define	AVR_MAX_VPH_TLB_ENTRIES		128


#define SREG_NAMES	"cznvshti"

#define	AVR_SREG_C		0x01	/*  Carry flag  */
#define	AVR_SREG_Z		0x02	/*  Zero flag  */
#define	AVR_SREG_N		0x04	/*  Negative flag  */
#define	AVR_SREG_V		0x08	/*  Overflow flag  */
#define	AVR_SREG_S		0x10	/*  Signed test  */
#define	AVR_SREG_H		0x20	/*  Half carry flag  */
#define	AVR_SREG_T		0x40	/*  Transfer bit  */
#define	AVR_SREG_I		0x80	/*  Interrupt enable/disable  */


struct avr_cpu {
	uint32_t	pc_mask;

	/*
	 *  General Purpose Registers:
	 */
	uint8_t		r[N_AVR_REGS];

	/*  Status register:  */
	uint8_t		sreg;

	/*
	 *  In order to keep an accurate cycle count, this variable should be
	 *  increased for those instructions that take longer than 1 cycle to
	 *  execute. The total number of executed cycles is extra_cycles PLUS
	 *  the number of executed instructions.
	 */
	int64_t		extra_cycles;


	/*
	 *  Instruction translation cache:
	 */
	DYNTRANS_ITC(avr)

	/*
	 *  32-bit virtual -> physical -> host address translation:
	 *
	 *  (All of this isn't really needed on AVRs.)
	 */
	VPH32(avr,AVR,uint32_t,uint8_t)
};


/*  cpu_avr.c:  */
void avr_update_translation_table(struct cpu *cpu, uint64_t vaddr_page,
	unsigned char *host_page, int writeflag, uint64_t paddr_page);
void avr_invalidate_translation_caches(struct cpu *cpu, uint64_t, int);
void avr_invalidate_code_translation(struct cpu *cpu, uint64_t, int);
int avr_memory_rw(struct cpu *cpu, struct memory *mem, uint64_t vaddr,
	unsigned char *data, size_t len, int writeflag, int cache_flags);
int avr_cpu_family_init(struct cpu_family *);


#endif	/*  CPU_AVR_H  */
