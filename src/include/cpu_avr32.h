#ifndef	CPU_AVR32_H
#define	CPU_AVR32_H

/*
 *  Copyright (C) 2006-2007  Anders Gavare.  All rights reserved.
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
 *  $Id: cpu_avr32.h,v 1.3 2006-12-30 13:31:00 debug Exp $
 */

#include "misc.h"


/*  AVR32 CPU types:  */
struct avr32_cpu_type_def {
	char		*name;
	int		flags;
};

#define	AVR32_FLAG_A		1
#define	AVR32_FLAG_B		2

#define AVR32_CPU_TYPE_DEFS	{					\
	{ "AVR32A",		AVR32_FLAG_A },				\
	{ "AVR32B",		AVR32_FLAG_B },				\
        { NULL,                 0 }					}

struct cpu_family;


#define	AVR32_N_IC_ARGS			2
#define	AVR32_INSTR_ALIGNMENT_SHIFT	1
#define	AVR32_IC_ENTRIES_SHIFT		11
#define	AVR32_IC_ENTRIES_PER_PAGE	(1 << AVR32_IC_ENTRIES_SHIFT)
#define	AVR32_PC_TO_IC_ENTRY(a)		(((a)>>AVR32_INSTR_ALIGNMENT_SHIFT) \
					& (AVR32_IC_ENTRIES_PER_PAGE-1))
#define	AVR32_ADDR_TO_PAGENR(a)		((a) >> (AVR32_IC_ENTRIES_SHIFT \
					+ AVR32_INSTR_ALIGNMENT_SHIFT))

DYNTRANS_MISC_DECLARATIONS(avr32,AVR32,uint32_t)

#define	AVR32_MAX_VPH_TLB_ENTRIES		128


#define	N_AVR32_GPRS		16

#define	AVR32_GPR_NAMES	{	"r0","r1","r2","r3","r4","r5","r6","r7", \
				"r8","r9","r10","r11","r12","sp","lr","pc" }

#define	AVR32_SP	13
#define	AVR32_LR	14
#define	AVR32_PC	15

struct avr32_cpu {
	struct avr32_cpu_type_def	cpu_type;

	uint32_t		r[N_AVR32_GPRS];/*  GPRs  */
	uint32_t		sr;		/*  Status register  */


	/*
	 *  Instruction translation cache and 32-bit virtual -> physical ->
	 *  host address translation:
	 */
	DYNTRANS_ITC(avr32)
	VPH_TLBS(avr32,AVR32)
	VPH32(avr32,AVR32,uint32_t,uint8_t)
};


#define	AVR32_SR_H	0x20000000	/*  Java Handle  */
#define	AVR32_SR_J	0x10000000	/*  Java State  */
#define	AVR32_SR_DM	0x08000000	/*  Debug State Mask  */
#define	AVR32_SR_D	0x04000000	/*  Debug State  */
#define	AVR32_SR_M2	0x01000000	/*  Mode Bit 2  */
#define	AVR32_SR_M1	0x00800000	/*  Mode Bit 1  */
#define	AVR32_SR_M0	0x00400000	/*  Mode Bit 0  */
#define	AVR32_SR_EM	0x00200000	/*  Exception Mask  */
#define	AVR32_SR_IM	0x001e0000	/*  Interrupt Mask  */
#define	AVR32_SR_IM_SHIFT  17
#define	AVR32_SR_GM	0x00010000	/*  Global Interrupt Mask  */
#define	AVR32_SR_R	0x00008000	/*  Register Remap Enable  */
#define	AVR32_SR_T	0x00004000	/*  Scratch  */
#define	AVR32_SR_L	0x00000020	/*  Lock  */
#define	AVR32_SR_Q	0x00000010	/*  Saturation  */
#define	AVR32_SR_V	0x00000008	/*  Overflow  */
#define	AVR32_SR_N	0x00000004	/*  Sign  */
#define	AVR32_SR_Z	0x00000002	/*  Zero  */
#define	AVR32_SR_C	0x00000001	/*  Carry  */


/*  cpu_avr32.c:  */
int avr32_run_instr(struct cpu *cpu);
void avr32_update_translation_table(struct cpu *cpu, uint64_t vaddr_page,
	unsigned char *host_page, int writeflag, uint64_t paddr_page);
void avr32_invalidate_translation_caches(struct cpu *cpu, uint64_t, int);
void avr32_invalidate_code_translation(struct cpu *cpu, uint64_t, int);
int avr32_memory_rw(struct cpu *cpu, struct memory *mem, uint64_t vaddr,
	unsigned char *data, size_t len, int writeflag, int cache_flags);
int avr32_cpu_family_init(struct cpu_family *);


#endif	/*  CPU_AVR32_H  */
