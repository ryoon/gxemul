#ifndef	CPU_TRANSPUTER_H
#define	CPU_TRANSPUTER_H

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
 *  $Id: cpu_transputer.h,v 1.3 2006-07-23 13:19:03 debug Exp $
 */

#include "misc.h"


struct cpu_family;


/*  TRANSPUTER CPU types:  */
struct transputer_cpu_type_def { 
	char		*name;
	int		bits;			/*  16 or 32  */
	int		onchip_ram;		/*  2048 or 4096 bytes  */
	int		features;
};

/*
 *  Features of various transputer processors according to
 *  http://www.enlight.ru/docs/cpu/t-puters/talp/app_g.txt:
 */

#define	T_T4_FP		1		/*  T4 floating point  */
#define	T_T8_FP		2		/*  T8 floating point  */
#define	T_2D_BLOCK	4		/*  2D Block Move instruction  */
#define	T_FMUL		8		/*  FMUL instruction  */
#define	T_DUP		16		/*  DUP instruction  */
#define	T_WSUBDB	32		/*  WSUBDB instruction  */
#define	T_CRC		64		/*  CRC instruction  */
#define	T_BITCOUNT	128		/*  BITCOUNT instruction  */
#define	T_FPTESTERR	256		/*  FPTESTERR instruction  */
#define	T_DEBUG		512		/*  Debug capabilities  */

#define TRANSPUTER_CPU_TYPE_DEFS	{				\
	{ "T212", 16, 2048, 0 },					\
	{ "T222", 16, 4096, 0 },					\
	{ "T225", 16, 4096, T_DUP | T_WSUBDB | T_CRC | T_BITCOUNT |	\
			    T_DEBUG },					\
	{ "T414", 32, 2048, T_FMUL | T_T4_FP },				\
	{ "T425", 32, 4096, T_T4_FP | T_2D_BLOCK | T_FMUL | T_WSUBDB |	\
			    T_DUP | T_CRC | T_BITCOUNT | T_FPTESTERR |	\
			    T_DEBUG },					\
	{ "T800", 32, 4096, T_T8_FP | T_2D_BLOCK | T_FMUL | T_WSUBDB |	\
			    T_DUP | T_CRC | T_BITCOUNT | T_FPTESTERR },	\
	{ "T801", 32, 4096, T_T8_FP | T_2D_BLOCK | T_FMUL | T_WSUBDB |	\
			    T_DUP | T_CRC | T_BITCOUNT | T_FPTESTERR },	\
	{ "T805", 32, 4096, T_T8_FP | T_2D_BLOCK | T_FMUL | T_WSUBDB |	\
			    T_DUP | T_CRC | T_BITCOUNT | T_FPTESTERR |	\
			    T_DEBUG },					\
	{ NULL,   0,     0, 0 } }

#define	TRANSPUTER_INSTRUCTIONS		{				\
	/*  0X  */   "j",	/*  jump			*/	\
	/*  1X  */   "ldlp",	/*  load local pointer		*/	\
	/*  2X  */   "pfix",	/*  prefix			*/	\
	/*  3X  */   "ldnl",	/*  load non-local		*/	\
	/*  4X  */   "ldc",	/*  load constant		*/	\
	/*  5X  */   "ldnlp",	/*  load non-local pointer	*/	\
	/*  6X  */   "nfix",	/*  negative prefix		*/	\
	/*  7X  */   "ldl",	/*  load local			*/	\
	/*  8X  */   "adc",	/*  add constant		*/	\
	/*  9X  */   "call",	/*  call subroutine		*/	\
	/*  AX  */   "cj",	/*  conditional jump		*/	\
	/*  BX  */   "ajw",	/*  adjust workspace		*/	\
	/*  CX  */   "eqc",	/*  equals constant		*/	\
	/*  DX  */   "stl",	/*  store local			*/	\
	/*  EX  */   "stnl",	/*  store non-local		*/	\
	/*  FX  */   "opr"	/*  operate			*/	}

#define	T_OPC_J			0
#define	T_OPC_LDLP		1
#define	T_OPC_PFIX		2
#define	T_OPC_LDNL		3
#define	T_OPC_LDC		4
#define	T_OPC_LDNLP		5
#define	T_OPC_NFIX		6
#define	T_OPC_LDL		7
#define	T_OPC_ADC		8
#define	T_OPC_CALL		9
#define	T_OPC_CJ		10
#define	T_OPC_AJW		11
#define	T_OPC_EQC		12
#define	T_OPC_STL		13
#define	T_OPC_STNL		14
#define	T_OPC_OPR		15

#define	T_OPC_F_REV		0x00
#define	T_OPC_F_SUB		0x0c
#define	T_OPC_F_STHF		0x18
#define	T_OPC_F_STLF		0x1c
#define	T_OPC_F_MINT		0x42

#define	TRANSPUTER_N_IC_ARGS			1
#define	TRANSPUTER_INSTR_ALIGNMENT_SHIFT	0
#define	TRANSPUTER_IC_ENTRIES_SHIFT		12
#define	TRANSPUTER_IC_ENTRIES_PER_PAGE	(1 << TRANSPUTER_IC_ENTRIES_SHIFT)
#define	TRANSPUTER_PC_TO_IC_ENTRY(a)		(((a)>>TRANSPUTER_INSTR_ALIGNMENT_SHIFT) \
					& (TRANSPUTER_IC_ENTRIES_PER_PAGE-1))
#define	TRANSPUTER_ADDR_TO_PAGENR(a)		((a) >> (TRANSPUTER_IC_ENTRIES_SHIFT \
					+ TRANSPUTER_INSTR_ALIGNMENT_SHIFT))

DYNTRANS_MISC_DECLARATIONS(transputer,TRANSPUTER,uint32_t)

#define	TRANSPUTER_MAX_VPH_TLB_ENTRIES		128


struct transputer_cpu {
	struct transputer_cpu_type_def cpu_type;

	uint32_t	a, b, c;	/*  GPRs  */
	uint32_t	wptr;		/*  Workspace/stack pointer  */
	uint32_t	oreg;		/*  Operand register  */

	uint64_t	fa, fb, fc;	/*  Floating point registers  */

	int		error;		/*  Error flags...  */
	int		halt_on_error;
	int		fp_error;

	uint32_t	bptrreg0;	/*  High Priority Front Pointer  */
	uint32_t	fptrreg0;	/*  High Priority Back Pointer  */
	uint32_t	fptrreg1;	/*  Low Priority Front Pointer  */
	uint32_t	bptrreg1;	/*  Low Priority Back Pointer  */

	/*
	 *  Instruction translation cache and 32-bit virtual -> physical ->
	 *  host address translation:
	 */
	DYNTRANS_ITC(transputer)
	VPH_TLBS(transputer,TRANSPUTER)
	VPH32(transputer,TRANSPUTER,uint32_t,uint8_t)
};


/*  cpu_transputer.c:  */
int transputer_run_instr(struct cpu *cpu);
void transputer_update_translation_table(struct cpu *cpu, uint64_t vaddr_page,
	unsigned char *host_page, int writeflag, uint64_t paddr_page);
void transputer_invalidate_translation_caches(struct cpu *cpu, uint64_t, int);
void transputer_invalidate_code_translation(struct cpu *cpu, uint64_t, int);
int transputer_memory_rw(struct cpu *cpu, struct memory *mem, uint64_t vaddr,
	unsigned char *data, size_t len, int writeflag, int cache_flags);
int transputer_cpu_family_init(struct cpu_family *);


#endif	/*  CPU_TRANSPUTER_H  */
