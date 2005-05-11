#ifndef	CPU_X86_H
#define	CPU_X86_H

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
 *  $Id: cpu_x86.h,v 1.12 2005-05-11 00:21:47 debug Exp $
 */

#include "misc.h"


struct cpu_family;

#define	N_X86_REGS		16

#define	x86_reg_names		{			\
	"ax", "cx", "dx", "bx", "sp", "bp", "si", "di",	\
	"08", "09", "10", "11", "12", "13", "14", "15" }
#define	x86_reg_names_bytes	{			\
	"al", "cl", "dl", "bl", "ah", "ch", "dh", "bh" }

#define	X86_R_AX	0
#define	X86_R_CX	1
#define	X86_R_DX	2
#define	X86_R_BX	3
#define	X86_R_SP	4
#define	X86_R_BP	5
#define	X86_R_SI	6
#define	X86_R_DI	7

#define	N_X86_SEGS		8
/*  (All of these 8 are not actually used.)  */

#define	X86_S_ES	0
#define	X86_S_CS	1
#define	X86_S_SS	2
#define	X86_S_DS	3
#define	X86_S_FS	4
#define	X86_S_GS	5

#define	x86_seg_names	{ "es", "cs", "ss", "ds", "fs", "gs", "segr6", "segr7" }

#define	N_X86_CREGS		8

#define	x86_cond_names	{ "o", "b", "z", "be", "s", "p", "l", "le" }
#define	N_X86_CONDS		8

#define	X86_MODEL_8086		1
#define	X86_MODEL_80386		2
#define	X86_MODEL_PENTIUM	3
#define	X86_MODEL_AMD64		4

struct x86_model {
	int		model_number;
	char		*name;
};

#define	x86_models {							\
	{ X86_MODEL_8086, "8086" },					\
	{ X86_MODEL_80386, "80386" },					\
	{ X86_MODEL_PENTIUM, "PENTIUM" },				\
	{ X86_MODEL_AMD64, "AMD64" },					\
	{ 0, NULL }							\
	}


struct x86_cpu {
	struct x86_model model;

	int		bits;		/*  16, 32, or 64  */
	int		mode;		/*  16, 32, or 64  */

	int		delayed_mode_change;

	uint16_t	cursegment;	/*  for 16-bit memory_rw  */
	int		seg_override;

	uint64_t	gdtr;
	uint64_t	gdtr_limit;
	uint64_t	idtr;
	uint64_t	idtr_limit;

	uint64_t	rflags;
	uint64_t	cr[N_X86_CREGS];

	uint16_t	s[N_X86_SEGS];
	uint64_t	r[N_X86_REGS];
};


#define	X86_FLAGS_CF	(1)		/*  Carry Flag  */
#define	X86_FLAGS_PF	(4)		/*  Parity Flag  */
#define	X86_FLAGS_AF	(16)		/*  Adjust/AuxilaryCarry Flag  */
#define	X86_FLAGS_ZF	(64)		/*  Zero Flag  */
#define	X86_FLAGS_SF	(128)		/*  Sign Flag  */
#define	X86_FLAGS_TF	(256)		/*  Trap Flag  */
#define	X86_FLAGS_IF	(512)		/*  Interrupt Enable Flag  */
#define	X86_FLAGS_DF	(1024)		/*  Direction Flag  */
#define	X86_FLAGS_OF	(2048)		/*  Overflow Flag  */
/*  Bits 12 and 13 are I/O Privilege Level  */
#define	X86_FLAGS_NT	(1<<14)		/*  Nested Task Flag  */
#define	X86_FLAGS_RF	(1<<16)		/*  Resume Flag  */
#define	X86_FLAGS_VM	(1<<17)		/*  VM86 Flag  */


/*  cpu_x86.c:  */
int x86_memory_rw(struct cpu *cpu, struct memory *mem, uint64_t vaddr,
	unsigned char *data, size_t len, int writeflag, int cache_flags);
int x86_cpu_family_init(struct cpu_family *);


#endif	/*  CPU_X86_H  */
