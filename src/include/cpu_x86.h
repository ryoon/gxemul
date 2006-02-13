#ifndef	CPU_X86_H
#define	CPU_X86_H

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
 *  $Id: cpu_x86.h,v 1.43 2006-02-13 04:23:25 debug Exp $
 *
 *  x86 (including AMD64) cpu dependent stuff.
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

#define	N_X86_DREGS		8

#define	x86_cond_names	{ "o", "b", "z", "be", "s", "p", "l", "le" }
#define	N_X86_CONDS		8

#define	X86_MODEL_8086		1
#define	X86_MODEL_80286		2
#define	X86_MODEL_80386		3
#define	X86_MODEL_80486		4
#define	X86_MODEL_PENTIUM	5
#define	X86_MODEL_AMD64		6

struct x86_model {
	int		model_number;
	char		*name;
};

#define	x86_models {							\
	{ X86_MODEL_8086, "8086" },					\
	{ X86_MODEL_80286, "80286" },					\
	{ X86_MODEL_80386, "80386" },					\
	{ X86_MODEL_80486, "80486" },					\
	{ X86_MODEL_PENTIUM, "PENTIUM" },				\
	{ X86_MODEL_AMD64, "AMD64" },					\
	{ 0, NULL }							\
	}

#define	X86_N_IC_ARGS			3
#define	X86_INSTR_ALIGNMENT_SHIFT	0
#define	X86_IC_ENTRIES_SHIFT		12
#define	X86_IC_ENTRIES_PER_PAGE		(1 << X86_IC_ENTRIES_SHIFT)
#define	X86_PC_TO_IC_ENTRY(a)		((a) & (X86_IC_ENTRIES_PER_PAGE-1))
#define	X86_ADDR_TO_PAGENR(a)		((a) >> X86_IC_ENTRIES_SHIFT)

DYNTRANS_MISC_DECLARATIONS(x86,X86,uint64_t)

#define	X86_MAX_VPH_TLB_ENTRIES		128

struct descriptor_cache {
	int		valid;
	int		default_op_size;
	int		access_rights;
	int		descr_type;
	int		readable;
	int		writable;
	int		granularity;
	uint64_t	base;
	uint64_t	limit;
};


struct x86_cpu {
	struct x86_model model;

	int		halted;
	int		interrupt_asserted;

	int		cursegment;	/*  NOTE: 0..N_X86_SEGS-1  */
	int		seg_override;	/*  0 or 1  */

	uint64_t	tsc;		/*  time stamp counter  */

	uint64_t	gdtr;		/*  global descriptor table */
	uint32_t	gdtr_limit;
	uint64_t	idtr;		/*  interrupt descriptor table  */
	uint32_t	idtr_limit;

	uint16_t	tr;		/*  task register  */
	uint64_t	tr_base;
	uint32_t	tr_limit;
	uint16_t	ldtr;		/*  local descriptor table register  */
	uint64_t	ldtr_base;
	uint32_t	ldtr_limit;

	uint64_t	rflags;
	uint64_t	cr[N_X86_CREGS];	/*  control registers  */
	uint64_t	dr[N_X86_DREGS];	/*  debug registers  */

	uint16_t	s[N_X86_SEGS];		/*  segment selectors  */
	struct descriptor_cache descr_cache[N_X86_SEGS];

	uint64_t	r[N_X86_REGS];		/*  GPRs  */

	/*  FPU:  */
	uint16_t	fpu_sw;		/*  status word  */
	uint16_t	fpu_cw;		/*  control word  */

	/*  MSRs:  */
	uint64_t	efer;


	/*
	 *  Instruction translation cache and Virtual->Physical->Host
	 *  address translation:
	 */
	DYNTRANS_ITC(x86)
	VPH_TLBS(x86,X86)
	VPH32(x86,X86,uint64_t,uint8_t)
	VPH64(x86,X86,uint8_t)
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
#define	X86_FLAGS_AC	(1<<18)		/*  Alignment Check  */
#define	X86_FLAGS_VIF	(1<<19)		/*  ?  */
#define	X86_FLAGS_VIP	(1<<20)		/*  ?  */
#define	X86_FLAGS_ID	(1<<21)		/*  CPUID present  */

#define	X86_CR0_PE	0x00000001	/*  Protection Enable  */
#define	X86_CR0_MP	0x00000002
#define	X86_CR0_EM	0x00000004
#define	X86_CR0_TS	0x00000008
#define	X86_CR0_ET	0x00000010
#define	X86_CR0_NE	0x00000020
#define	X86_CR0_WP	0x00010000
#define	X86_CR0_AM	0x00040000
#define	X86_CR0_NW	0x20000000
#define	X86_CR0_CD	0x40000000
#define	X86_CR0_PG	0x80000000	/*  Paging Enable  */

#define	X86_CR4_OSXMEX	0x00000400
#define	X86_CR4_OSFXSR	0x00000200
#define	X86_CR4_PCE	0x00000100
#define	X86_CR4_PGE	0x00000080
#define	X86_CR4_MCE	0x00000040
#define	X86_CR4_PAE	0x00000020
#define	X86_CR4_PSE	0x00000010
#define	X86_CR4_DE	0x00000008
#define	X86_CR4_TSD	0x00000004	/*  Time Stamp Disable  */
#define	X86_CR4_PVI	0x00000002
#define	X86_CR4_VME	0x00000001

/*  EFER bits:  */
#define	X86_EFER_FFXSR	0x00004000
#define	X86_EFER_LMSLE	0x00002000
#define	X86_EFER_NXE	0x00000800
#define	X86_EFER_LMA	0x00000400
#define	X86_EFER_LME	0x00000100	/*  Long Mode (64-bit)  */
#define	X86_EFER_SCE	0x00000001

/*  CPUID feature bits:  */
#define	X86_CPUID_ECX_ETPRD	0x00004000
#define	X86_CPUID_ECX_CX16	0x00002000	/*  cmpxchg16b  */
#define	X86_CPUID_ECX_CID	0x00000400
#define	X86_CPUID_ECX_TM2	0x00000100
#define	X86_CPUID_ECX_EST	0x00000080
#define	X86_CPUID_ECX_DSCPL	0x00000010
#define	X86_CPUID_ECX_MON	0x00000004
#define	X86_CPUID_ECX_SSE3	0x00000001
#define	X86_CPUID_EDX_PBE	0x80000000	/*  pending break event  */
#define	X86_CPUID_EDX_IA64	0x40000000
#define	X86_CPUID_EDX_TM1	0x20000000	/*  thermal interrupt  */
#define	X86_CPUID_EDX_HTT	0x10000000	/*  hyper threading  */
#define	X86_CPUID_EDX_SS	0x08000000	/*  self-snoop  */
#define	X86_CPUID_EDX_SSE2	0x04000000
#define	X86_CPUID_EDX_SSE	0x02000000
#define	X86_CPUID_EDX_FXSR	0x01000000
#define	X86_CPUID_EDX_MMX	0x00800000
#define	X86_CPUID_EDX_ACPI	0x00400000
#define	X86_CPUID_EDX_DTES	0x00200000
#define	X86_CPUID_EDX_CLFL	0x00080000
#define	X86_CPUID_EDX_PSN	0x00040000
#define	X86_CPUID_EDX_PSE36	0x00020000
#define	X86_CPUID_EDX_PAT	0x00010000
#define	X86_CPUID_EDX_CMOV	0x00008000
#define	X86_CPUID_EDX_MCA	0x00004000
#define	X86_CPUID_EDX_PGE	0x00002000	/*  global bit in PDE/PTE  */
#define	X86_CPUID_EDX_MTRR	0x00001000
#define	X86_CPUID_EDX_SEP	0x00000800	/*  sysenter/sysexit  */
#define	X86_CPUID_EDX_APIC	0x00000200
#define	X86_CPUID_EDX_CX8	0x00000100	/*  cmpxchg8b  */
#define	X86_CPUID_EDX_MCE	0x00000080
#define	X86_CPUID_EDX_PAE	0x00000040
#define	X86_CPUID_EDX_MSR	0x00000020
#define	X86_CPUID_EDX_TSC	0x00000010
#define	X86_CPUID_EDX_PSE	0x00000008
#define	X86_CPUID_EDX_DE	0x00000004
#define	X86_CPUID_EDX_VME	0x00000002
#define	X86_CPUID_EDX_FPU	0x00000001

/*  Extended CPUID flags:  */
#define	X86_CPUID_EXT_ECX_CR8D	0x00000010
#define	X86_CPUID_EXT_ECX_CMP	0x00000002
#define	X86_CPUID_EXT_ECX_AHF64	0x00000001
#define	X86_CPUID_EXT_EDX_LM	0x20000000	/*  AMD64 Long Mode  */
#define	X86_CPUID_EXT_EDX_FFXSR	0x02000000
/*  TODO: Many bits are duplicated in the Extended CPUID bits!  */

#define	X86_IO_BASE	0x1000000000ULL

/*  Privilege level in the lowest 2 bits of a selector:  */
#define	X86_PL_MASK		0x0003
#define	X86_RING0		0
#define	X86_RING1		1
#define	X86_RING2		2
#define	X86_RING3		3

#define	DESCR_TYPE_CODE		1
#define	DESCR_TYPE_DATA		2


#define	PROTECTED_MODE		(cpu->cd.x86.cr[0] & X86_CR0_PE)
#define	REAL_MODE		(!PROTECTED_MODE)

/*  cpu_x86.c:  */
void reload_segment_descriptor(struct cpu *cpu, int segnr, int selector,
	uint64_t *curpcp);
int x86_interrupt(struct cpu *cpu, int nr, int errcode);
int x86_memory_rw(struct cpu *cpu, struct memory *mem, uint64_t vaddr,
	unsigned char *data, size_t len, int writeflag, int cache_flags);
void x86_update_translation_table(struct cpu *cpu, uint64_t vaddr_page,
        unsigned char *host_page, int writeflag, uint64_t paddr_page);
void x8632_update_translation_table(struct cpu *cpu, uint64_t vaddr_page,
        unsigned char *host_page, int writeflag, uint64_t paddr_page);
void x86_invalidate_translation_caches(struct cpu *cpu, uint64_t, int);
void x8632_invalidate_translation_caches(struct cpu *cpu, uint64_t, int);
void x86_invalidate_code_translation(struct cpu *cpu, uint64_t, int);
void x8632_invalidate_code_translation(struct cpu *cpu, uint64_t, int);
int x86_cpu_family_init(struct cpu_family *);


/*  memory_x86.c:  */
int x86_translate_address(struct cpu *cpu, uint64_t vaddr,
	uint64_t *return_addr, int flags);

#endif	/*  CPU_X86_H  */
