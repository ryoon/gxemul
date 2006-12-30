#ifndef	CPU_RCA180X_H
#define	CPU_RCA180X_H

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
 *  $Id: cpu_rca180x.h,v 1.3 2006-12-30 13:31:00 debug Exp $
 */

#include "misc.h"


struct cpu_family;

#define	N_RCA180X_REGS		16

#define	RCA180X_N_IC_ARGS		3
#define	RCA180X_INSTR_ALIGNMENT_SHIFT	0
#define	RCA180X_IC_ENTRIES_SHIFT	7
#define	RCA180X_IC_ENTRIES_PER_PAGE	(1 << RCA180X_IC_ENTRIES_SHIFT)
#define	RCA180X_PC_TO_IC_ENTRY(a)	(((a)>>RCA180X_INSTR_ALIGNMENT_SHIFT) \
					& (RCA180X_IC_ENTRIES_PER_PAGE-1))
#define	RCA180X_ADDR_TO_PAGENR(a)	((a) >> (RCA180X_IC_ENTRIES_SHIFT \
					+ RCA180X_INSTR_ALIGNMENT_SHIFT))

DYNTRANS_MISC_DECLARATIONS(rca180x,RCA180X,uint64_t)

#define	RCA180X_MAX_VPH_TLB_ENTRIES		64


/*  CHIP8 stuff:  */
#define	N_CHIP8_REGS		16
#define	CHIP8_FB_ADDR		0x10000000

/*  Default font address:  */
#define	CHIP8_FONT_ADDR		8110


struct rca180x_cpu {
	uint16_t	r[N_RCA180X_REGS];	/*  GPRs  */
	uint8_t		d;		/*  Data register  */

	int8_t		df;		/*  Data flag (1 bit)  */
	int8_t		ie;		/*  Interrupt enable (1 bit)  */
	int8_t		q;		/*  Output bit (1 bit)  */
	int8_t		p;		/*  PC select (4 bits)  */
	int8_t		x;		/*  Data pointer select (4 bits)  */
	int8_t		t_x;		/*  X during interrupt  */
	int8_t		t_p;		/*  P during interrupt  */

	/***********************  CHIP8 EMULATION  **************************/

	int8_t		chip8_mode;

	/*
	 *  General Purpose Registers, and the Index register:
	 */
	uint8_t		v[N_CHIP8_REGS];
	uint16_t	index;			/*  only 12 bits used  */

	/*  Stack pointer (not user accessible):  */
	uint16_t	sp;

	/*  64x32 framebuffer (or 128x64 for SCHIP48)  */
	int		xres, yres;
	uint8_t		*framebuffer_cache;

	/*  54.6 Hz (or new mode 60 Hz) timer  */
	struct timer	*timer;
	int		timer_mode_new;
	int		delay_timer_value;
	int		sound_timer_value;


	/*
	 *  Instruction translation cache:
	 */
	DYNTRANS_ITC(rca180x)

	/*
	 *  32-bit virtual -> physical -> host address translation:
	 *
	 *  (All 32 bits are not really needed on RCA180Xs.)
	 */
	VPH_TLBS(rca180x,RCA180X)
	VPH32(rca180x,RCA180X,uint32_t,uint8_t)
};


/*  cpu_rca180x.c:  */
int rca180x_run_instr(struct cpu *cpu);
void rca180x_update_translation_table(struct cpu *cpu, uint64_t vaddr_page,
	unsigned char *host_page, int writeflag, uint64_t paddr_page);
void rca180x_invalidate_translation_caches(struct cpu *cpu, uint64_t, int);
void rca180x_invalidate_code_translation(struct cpu *cpu, uint64_t, int);
int rca180x_memory_rw(struct cpu *cpu, struct memory *mem, uint64_t vaddr,
	unsigned char *data, size_t len, int writeflag, int cache_flags);
int rca180x_cpu_family_init(struct cpu_family *);


#endif	/*  CPU_RCA180X_H  */
