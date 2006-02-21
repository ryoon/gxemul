#ifndef	CPU_SPARC_H
#define	CPU_SPARC_H

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
 *  $Id: cpu_sparc.h,v 1.24 2006-02-21 18:10:42 debug Exp $
 */

#include "misc.h"


struct cpu_family;


/*  SPARC CPU types:  */
struct sparc_cpu_type_def { 
	char		*name;
	int		bits;
	int		icache_shift;
	int		ilinesize;
	int		iway;
	int		dcache_shift;
	int		dlinesize;
	int		dway;
	int		l2cache_shift;
	int		l2linesize;
	int		l2way;
};

#define SPARC_CPU_TYPE_DEFS	{					\
	{ "SPARCv7",	32, 14,5,4, 14,5,4, 0,0,0 },			\
	{ "SPARCv9",	64, 14,5,4, 14,5,4, 0,0,0 },			\
	{ NULL,		0,  0,0,0,  0,0,0,  0,0,0 }			\
	}


#define	SPARC_N_IC_ARGS			3
#define	SPARC_INSTR_ALIGNMENT_SHIFT	2
#define	SPARC_IC_ENTRIES_SHIFT		10
#define	SPARC_IC_ENTRIES_PER_PAGE	(1 << SPARC_IC_ENTRIES_SHIFT)
#define	SPARC_PC_TO_IC_ENTRY(a)		(((a)>>SPARC_INSTR_ALIGNMENT_SHIFT) \
					& (SPARC_IC_ENTRIES_PER_PAGE-1))
#define	SPARC_ADDR_TO_PAGENR(a)		((a) >> (SPARC_IC_ENTRIES_SHIFT \
					+ SPARC_INSTR_ALIGNMENT_SHIFT))

#define	SPARC_L2N		17
#define	SPARC_L3N		18	/*  4KB pages on 32-bit sparc,  */
					/*  8KB pages on 64-bit?  TODO  */

DYNTRANS_MISC_DECLARATIONS(sparc,SPARC,uint64_t)
DYNTRANS_MISC64_DECLARATIONS(sparc,SPARC)

#define	SPARC_MAX_VPH_TLB_ENTRIES		128


#define	N_SPARC_REG		32
#define	SPARC_REG_NAMES	{				\
	"g0","g1","g2","g3","g4","g5","g6","g7",	\
	"o0","o1","o2","o3","o4","o5","sp","o7",	\
	"l0","l1","l2","l3","l4","l5","l6","l7",	\
	"i0","i1","i2","i3","i4","i5","fp","i7" }

#define	N_SPARC_BRANCH_TYPES	16
#define	SPARC_BRANCH_NAMES {						\
	"bn", "be",  "ble", "bl",  "bleu", "bcs", "bneg", "bvs",	\
	"b",  "bne", "bg",  "bge", "bgu",  "bcc", "bpos", "bvc"  }

#define	N_SPARC_REGBRANCH_TYPES	8
#define	SPARC_REGBRANCH_NAMES {						\
	"br?","brz","brlez","brlz","br??","brnz", "brgz", "brgez"  }

#define	N_ALU_INSTR_TYPES	64
#define	SPARC_ALU_NAMES {						\
	"add", "and", "or", "xor", "sub", "andn", "orn", "xnor",	\
	"addx", "[9]", "umul", "smul", "subx", "[13]", "udiv", "sdiv",	\
	"addcc","andcc","orcc","xorcc","subcc","andncc","orncc","xnorcc",\
	"addxcc","[25]","umulcc","smulcc","subxcc","[29]","udivcc","sdivcc",\
	"taddcc","tsubcc","taddcctv","tsubcctv","mulscc","sll","srl","sra",\
	"[40]","[41]","[42]","[43]", "[44]","[45]","[46]","movre",	\
	"[48]","[49]","[50]","[51]", "[52]","[53]","[54]","[55]",	\
	"jmpl", "rett", "trap", "flush", "save", "restore", "[62]","[63]" }

#define	N_LOADSTORE_TYPES	64
#define	SPARC_LOADSTORE_NAMES {						\
	"ld","ldub","lduh","ldd", "st","stb","sth","std",		\
	"[8]","ldsb","ldsh","ldx", "[12]","ldstub","stx","swap",	\
	"lda","lduba","lduha","ldda", "sta","stba","stha","stda",	\
	"[24]","ldsba","ldsha","ldxa", "[28]","ldstuba","stxa","swapa",	 \
	"ldf","ldfsr","[34]","lddf", "stf","stfsr","stdfq","stdf",	\
	"[40]","[41]","[42]","[43]", "[44]","[45]","[46]","[47]",	\
	"ldc","ldcsr","[50]","lddc", "stc","stcsr","scdfq","scdf",	\
	"[56]","[57]","[58]","[59]", "[60]","[61]","casxa","[63]" }

struct sparc_cpu {
	struct sparc_cpu_type_def cpu_type;

	uint64_t	r[N_SPARC_REG];
	uint64_t	zero;			/*  for dyntrans; ALWAYS zero */


	/*
	 *  Instruction translation cache and Virtual->Physical->Host
	 *  address translation:
	 */
	DYNTRANS_ITC(sparc)
	VPH_TLBS(sparc,SPARC)
	VPH32(sparc,SPARC,uint64_t,uint8_t)
	VPH64(sparc,SPARC,uint8_t)
};


/*  cpu_sparc.c:  */
void sparc_update_translation_table(struct cpu *cpu, uint64_t vaddr_page,
	unsigned char *host_page, int writeflag, uint64_t paddr_page);
void sparc_invalidate_translation_caches(struct cpu *cpu, uint64_t, int);
void sparc_invalidate_code_translation(struct cpu *cpu, uint64_t, int);
void sparc32_update_translation_table(struct cpu *cpu, uint64_t vaddr_page,
	unsigned char *host_page, int writeflag, uint64_t paddr_page);
void sparc32_invalidate_translation_caches(struct cpu *cpu, uint64_t, int);
void sparc32_invalidate_code_translation(struct cpu *cpu, uint64_t, int);
int sparc_memory_rw(struct cpu *cpu, struct memory *mem, uint64_t vaddr,
	unsigned char *data, size_t len, int writeflag, int cache_flags);
int sparc_cpu_family_init(struct cpu_family *);


#endif	/*  CPU_SPARC_H  */
