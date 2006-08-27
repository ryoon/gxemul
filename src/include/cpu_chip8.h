#ifndef	CPU_CHIP8_H
#define	CPU_CHIP8_H

/*
 *  Copyright (C) 2006  Anders Gavare.  All rights reserved.
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
 *  $Id: cpu_chip8.h,v 1.1 2006-08-27 10:37:30 debug Exp $
 */

#include "misc.h"


struct cpu_family;

#define	N_CHIP8_REGS		16

#define	CHIP8_N_IC_ARGS			3
#define	CHIP8_INSTR_ALIGNMENT_SHIFT	1
#define	CHIP8_IC_ENTRIES_SHIFT		11
#define	CHIP8_IC_ENTRIES_PER_PAGE		(1 << CHIP8_IC_ENTRIES_SHIFT)
#define	CHIP8_PC_TO_IC_ENTRY(a)		(((a)>>CHIP8_INSTR_ALIGNMENT_SHIFT) \
					& (CHIP8_IC_ENTRIES_PER_PAGE-1))
#define	CHIP8_ADDR_TO_PAGENR(a)		((a) >> (CHIP8_IC_ENTRIES_SHIFT \
					+ CHIP8_INSTR_ALIGNMENT_SHIFT))

DYNTRANS_MISC_DECLARATIONS(chip8,CHIP8,uint64_t)

#define	CHIP8_MAX_VPH_TLB_ENTRIES		16


#define	CHIP8_FB_ADDR		0x10000000


struct chip8_cpu {
	/*
	 *  General Purpose Registers, and the Index register:
	 */
	uint8_t		v[N_CHIP8_REGS];
	uint16_t	index;			/*  only 12 bits used  */

	/*  Stack pointer (not user accessible):  */
	uint16_t	sp;

	/*  64x32 framebuffer for CHIP8, 128x64 for SuperCHIP8  */
	int		xres, yres;
	uint8_t		*framebuffer_cache;

	/*  54.6 Hz (chip8 mode) or 60 Hz (new mode)  */
	struct timer	*timer;
	int		timer_mode_new;
	int		delay_timer_value;
	int		sound_timer_value;

	/*
	 *  Instruction translation cache:
	 */
	DYNTRANS_ITC(chip8)

	/*
	 *  32-bit virtual -> physical -> host address translation:
	 *
	 *  (All 32 bits are not really needed on CHIP8s.)
	 */
	VPH_TLBS(chip8,CHIP8)
	VPH32(chip8,CHIP8,uint32_t,uint8_t)
};


/*  cpu_chip8.c:  */
int chip8_run_instr(struct cpu *cpu);
void chip8_update_translation_table(struct cpu *cpu, uint64_t vaddr_page,
	unsigned char *host_page, int writeflag, uint64_t paddr_page);
void chip8_invalidate_translation_caches(struct cpu *cpu, uint64_t, int);
void chip8_invalidate_code_translation(struct cpu *cpu, uint64_t, int);
int chip8_memory_rw(struct cpu *cpu, struct memory *mem, uint64_t vaddr,
	unsigned char *data, size_t len, int writeflag, int cache_flags);
int chip8_cpu_family_init(struct cpu_family *);


#endif	/*  CPU_CHIP8_H  */
