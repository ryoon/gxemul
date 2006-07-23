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
 *  $Id: cpu_transputer.h,v 1.6 2006-07-23 15:42:20 debug Exp $
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
 *
 *  (TODO: Add the T9000 too?)
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

/*  Indirect ("operate") opcodes:  */
#define	N_TRANSPUTER_OPC_F_NAMES	0x90
#define	TRANSPUTER_OPC_F_NAMES	{	\
	"rev",  "lb",   "bsub", "endp", "diff", "add",  "gcall","in", \
	"prod", "gt",   "wsub", "out",  "sub",  "startp","outbyte","outword",\
	"seterr","0x11","resetch","csub0", "0x14", "stopp","ladd", "stlb", \
	"sthf", "norm", "ldiv", "ldpi", "stlf", "xdble","ldpri","rem", \
	"ret",  "lend", "ldtimer","0x23","0x24","0x25", "0x26", "0x27", \
	"0x28", "testerr","testpranal","tin", "div", "0x2d", "dist", "disc", \
	"diss", "lmul", "not",  "xor",  "bcnt", "lshr", "lshl", "lsum", \
	"lsub", "runp", "xword","sb",   "gajw", "savel","saveh","wcnt", \
	"shr" , "shl",  "mint", "alt",  "altwt","altend","and","enbt", \
	"enbc", "enbs", "move", "or",   "csngl", "ccnt1", "talt", "ldiff", \
	"sthb", "taltwt","sum", "mul","sttimer","stoperr","cword","clrhalterr",\
	"sethalterr", "testhalterr", "dup", "move2dinit",		\
	"move2dall", "move2dnonzero","move2dzero","0x5f",		\
	"0x60", "0x61", "0x62", "unpacksn","0x64","0x65","0x66","0x67", \
	"0x68", "0x69", "0x6a", "0x6b", "postnormsn","roundsn","0x6e","0x6f", \
	"0x70", "ldinf","fmul", "cflerr",				\
	"crcword", "crcbyte", "bitcnt", "bitrevword",			\
	"bitrevnbits","0x79","0x7a","0x7b", "0x7c", "0x7d", "0x7e", "0x7f", \
	"0x80", "wsubdb","0x82", "0x83", "0x84", "0x85", "0x86", "0x87", \
	"0x88", "0x89", "0x8a", "0x8b", "0x8c", "0x8d", "0x8e", "0x8f"	}

#define	T_OPC_F_REV		0x00
#define	T_OPC_F_LB		0x01
#define	T_OPC_F_BSUB		0x02
#define	T_OPC_F_ENDP		0x03
#define	T_OPC_F_DIFF		0x04
#define	T_OPC_F_ADD		0x05
#define	T_OPC_F_GCALL		0x06
#define	T_OPC_F_IN		0x07
#define	T_OPC_F_PROD		0x08
#define	T_OPC_F_GT		0x09
#define	T_OPC_F_WSUB		0x0a
#define	T_OPC_F_OUT		0x0b
#define	T_OPC_F_SUB		0x0c
#define	T_OPC_F_STARTP		0x0d
#define	T_OPC_F_OUTBYTE		0x0e
#define	T_OPC_F_OUTWORD		0x0f
#define	T_OPC_F_SETERR		0x10
#define	T_OPC_F_RESETCH		0x12
#define	T_OPC_F_CSUB0		0x13
#define	T_OPC_F_STOPP		0x15
#define	T_OPC_F_LADD		0x16
#define	T_OPC_F_STLB		0x17
#define	T_OPC_F_STHF		0x18
#define	T_OPC_F_NORM		0x19
#define	T_OPC_F_LDIV		0x1a
#define	T_OPC_F_LDPI		0x1b
#define	T_OPC_F_STLF		0x1c
#define	T_OPC_F_XDBLE		0x1d
#define	T_OPC_F_LDPRI		0x1e
#define	T_OPC_F_REM		0x1f
#define	T_OPC_F_RET		0x20
#define	T_OPC_F_LEND		0x21
#define	T_OPC_F_LDTIMER		0x22
#define	T_OPC_F_TESTERR		0x29
#define	T_OPC_F_TESTPRANAL	0x2a
#define	T_OPC_F_TIN		0x2b
#define	T_OPC_F_DIV		0x2c
#define	T_OPC_F_DIST		0x2e
#define	T_OPC_F_DISC		0x2f
#define	T_OPC_F_DISS		0x30
#define	T_OPC_F_LMUL		0x31
#define	T_OPC_F_NOT		0x32
#define	T_OPC_F_XOR		0x33
#define	T_OPC_F_BCNT		0x34
#define	T_OPC_F_LSHR		0x35
#define	T_OPC_F_LSHL		0x36
#define	T_OPC_F_LSUM		0x37
#define	T_OPC_F_LSUB		0x38
#define	T_OPC_F_RUNP		0x39
#define	T_OPC_F_XWORD		0x3a
#define	T_OPC_F_SB		0x3b
#define	T_OPC_F_GAJW		0x3c
#define	T_OPC_F_SAVEL		0x3d
#define	T_OPC_F_SAVEH		0x3e
#define	T_OPC_F_WCNT		0x3f
#define	T_OPC_F_SHR		0x40
#define	T_OPC_F_SHL		0x41
#define	T_OPC_F_MINT		0x42
#define	T_OPC_F_ALT		0x43
#define	T_OPC_F_ALTWT		0x44
#define	T_OPC_F_ALTEND		0x45
#define	T_OPC_F_AND		0x46
#define	T_OPC_F_ENBT		0x47
#define	T_OPC_F_ENBC		0x48
#define	T_OPC_F_ENBS		0x49
#define	T_OPC_F_MOVE		0x4a
#define	T_OPC_F_OR		0x4b
#define	T_OPC_F_CSNGL		0x4c
#define	T_OPC_F_CCNT1		0x4d
#define	T_OPC_F_TALT		0x4e
#define	T_OPC_F_LDIFF		0x4f
#define	T_OPC_F_STHB		0x50
#define	T_OPC_F_TALTWT		0x51
#define	T_OPC_F_SUM		0x52
#define	T_OPC_F_STTIMER		0x54
#define	T_OPC_F_MUL		0x53
#define	T_OPC_F_STOPERR		0x55
#define	T_OPC_F_CWORD		0x56
#define	T_OPC_F_CLRHALTERR	0x57
#define	T_OPC_F_SETHALTERR	0x58
#define	T_OPC_F_TESTHALTERR	0x59
#define	T_OPC_F_DUP		0x5a
#define	T_OPC_F_MOVE2DINIT	0x5b
#define	T_OPC_F_MOVE2DALL	0x5c
#define	T_OPC_F_MOVE2DNONZERO	0x5d
#define	T_OPC_F_MOVE2DZERO	0x5e
#define	T_OPC_F_UNPACKSN	0x63
#define	T_OPC_F_POSTNORMSN	0x6c
#define	T_OPC_F_ROUNDSN		0x6d
#define	T_OPC_F_LDINF		0x71
#define	T_OPC_F_FMUL		0x72
#define	T_OPC_F_CFLERR		0x73
#define	T_OPC_F_CRCWORD		0x74
#define	T_OPC_F_CRCBYTE		0x75
#define	T_OPC_F_BITCNT		0x76
#define	T_OPC_F_BITREVWORD	0x77
#define	T_OPC_F_BITREVNBITS	0x78
#define	T_OPC_F_WSUBSB		0x81

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
