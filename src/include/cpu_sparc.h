#ifndef	CPU_SPARC_H
#define	CPU_SPARC_H

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
 *  $Id: cpu_sparc.h,v 1.18 2005-12-04 03:12:08 debug Exp $
 */

#include "misc.h"


struct cpu_family;


#define	SPARC_N_IC_ARGS			3
#define	SPARC_INSTR_ALIGNMENT_SHIFT	2
#define	SPARC_IC_ENTRIES_SHIFT		10
#define	SPARC_IC_ENTRIES_PER_PAGE	(1 << SPARC_IC_ENTRIES_SHIFT)
#define	SPARC_PC_TO_IC_ENTRY(a)		(((a)>>SPARC_INSTR_ALIGNMENT_SHIFT) \
					& (SPARC_IC_ENTRIES_PER_PAGE-1))
#define	SPARC_ADDR_TO_PAGENR(a)		((a) >> (SPARC_IC_ENTRIES_SHIFT \
					+ SPARC_INSTR_ALIGNMENT_SHIFT))

struct sparc_instr_call {
	void	(*f)(struct cpu *, struct sparc_instr_call *);
	size_t	arg[SPARC_N_IC_ARGS];
};

/*  Translation cache struct for each physical page:  */
struct sparc_tc_physpage {
	struct sparc_instr_call ics[SPARC_IC_ENTRIES_PER_PAGE + 1];
	uint32_t	next_ofs;	/*  or 0 for end of chain  */
	int		flags;
	uint64_t	physaddr;
};

#define	SPARC_N_VPH_ENTRIES		1048576

#define	SPARC_MAX_VPH_TLB_ENTRIES		256
struct sparc_vpg_tlb_entry {
	uint8_t		valid;
	uint8_t		writeflag;
	unsigned char	*host_page;
	int64_t		timestamp;
	uint64_t	vaddr_page;
	uint64_t	paddr_page;
};

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
	"[24]","ldsba","ldsha","[27]", "[28]","ldstuba","[30]","swapa",	\
	"ldf","ldfsr","[34]","lddf", "stf","stfsr","stdfq","stdf",	\
	"[40]","[41]","[42]","[43]", "[44]","[45]","[46]","[47]",	\
	"ldc","ldcsr","[50]","lddc", "stc","stcsr","scdfq","scdf" }

struct sparc_cpu {

	uint64_t	r[N_SPARC_REG];
	uint64_t	zero;			/*  for dyntrans; ALWAYS zero */


	/*
	 *  Instruction translation cache:
	 */

	/*  cur_ic_page is a pointer to an array of SPARC_IC_ENTRIES_PER_PAGE
	    instruction call entries. next_ic points to the next such
	    call to be executed.  */
	struct sparc_tc_physpage	*cur_physpage;
	struct sparc_instr_call	*cur_ic_page;
	struct sparc_instr_call	*next_ic;

	void			(*combination_check)(struct cpu *,
				    struct sparc_instr_call *, int low_addr);

	/*
	 *  Virtual -> physical -> host address translation:
	 *
	 *  host_load and host_store point to arrays of SPARC_N_VPH_ENTRIES
	 *  pointers (to host pages); phys_addr points to an array of
	 *  SPARC_N_VPH_ENTRIES uint32_t.
	 */

	struct sparc_vpg_tlb_entry  vph_tlb_entry[SPARC_MAX_VPH_TLB_ENTRIES];
	unsigned char		    *host_load[SPARC_N_VPH_ENTRIES]; 
	unsigned char		    *host_store[SPARC_N_VPH_ENTRIES];
	uint32_t		    phys_addr[SPARC_N_VPH_ENTRIES]; 
	struct sparc_tc_physpage    *phys_page[SPARC_N_VPH_ENTRIES];

	uint32_t		    phystranslation[SPARC_N_VPH_ENTRIES/32];
	uint8_t			    vaddr_to_tlbindex[SPARC_N_VPH_ENTRIES];
};


/*  cpu_sparc.c:  */
void sparc_update_translation_table(struct cpu *cpu, uint64_t vaddr_page,
	unsigned char *host_page, int writeflag, uint64_t paddr_page);
void sparc_invalidate_translation_caches(struct cpu *cpu, uint64_t, int);
void sparc_invalidate_code_translation(struct cpu *cpu, uint64_t, int);
int sparc_memory_rw(struct cpu *cpu, struct memory *mem, uint64_t vaddr,
	unsigned char *data, size_t len, int writeflag, int cache_flags);
int sparc_cpu_family_init(struct cpu_family *);


#endif	/*  CPU_SPARC_H  */
