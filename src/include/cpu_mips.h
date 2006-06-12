#ifndef	CPU_MIPS_H
#define	CPU_MIPS_H

/*
 *  Copyright (C) 2003-2006  Anders Gavare.  All rights reserved.
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
 *  $Id: cpu_mips.h,v 1.38 2006-06-12 21:35:08 debug Exp $
 */

#include "misc.h"

/*  #define MFHILO_DELAY  */

struct cpu_family;
struct emul;
struct machine;

/*
 *  CPU type definitions:  See mips_cpu_types.h.
 */

struct mips_cpu_type_def {
	char		*name;
	int		rev;
	int		sub;
	char		flags;
	char		exc_model;		/*  EXC3K or EXC4K  */
	char		mmu_model;		/*  MMU3K or MMU4K  */
	char		isa_level;		/*  1, 2, 3, 4, 5, 32, 64  */
	char		isa_revision;		/*  1 or 2 (for MIPS32/64)  */
	int		nr_of_tlb_entries;	/*  32, 48, 64, ...  */
	char		instrs_per_cycle;	/*  simplified, 1, 2, or 4  */
	int		picache;
	int		pilinesize;
	int		piways;
	int		pdcache;
	int		pdlinesize;
	int		pdways;
	int		scache;
	int		slinesize;
	int		sways;
};

#define	INITIAL_PC			0xffffffffbfc00000ULL
#define	INITIAL_STACK_POINTER		(0xffffffffa0008000ULL - 256)


/*
 *  Coproc 0:
 */
#define	N_MIPS_COPROC_REGS	32
struct mips_tlb {
	uint64_t	hi;
	uint64_t	lo0;
	uint64_t	lo1;
	uint64_t	mask;
};


/*
 *  Coproc 1:
 */
/*  FPU control registers:  */
#define	N_MIPS_FCRS			32
#define	MIPS_FPU_FCIR			0
#define	MIPS_FPU_FCCR			25
#define	MIPS_FPU_FCSR			31
#define	   MIPS_FCSR_FCC0_SHIFT		   23
#define	   MIPS_FCSR_FCC1_SHIFT		   25

struct mips_coproc {
	int		coproc_nr;
	uint64_t	reg[N_MIPS_COPROC_REGS];

	/*  Only for COP0:  */
	struct mips_tlb	*tlbs;
	int		nr_of_tlbs;

	/*  Only for COP1:  floating point control registers  */
	/*  (Maybe also for COP0?)  */
	uint64_t	fcr[N_MIPS_FCRS];
};

#define	N_MIPS_COPROCS		4

#define	N_MIPS_GPRS		32	/*  General purpose registers  */
#define	N_MIPS_FPRS		32	/*  Floating point registers  */

/*
 *  These should all be 2 characters wide:
 *
 *  NOTE: These are for 32-bit ABIs. For the 64-bit ABI, registers 8..11
 *  are used to pass arguments and are then called "a4".."a7".
 *
 *  TODO: Should there be two different variants of this? It's not really
 *  possible to figure out in some easy way if the code running was
 *  written for a 32-bit or 64-bit ABI.
 */
#define MIPS_REGISTER_NAMES	{ \
	"zr", "at", "v0", "v1", "a0", "a1", "a2", "a3", \
	"t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7", \
	"s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", \
	"t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"  }

#define	MIPS_GPR_ZERO		0		/*  zero  */
#define	MIPS_GPR_AT		1		/*  at  */
#define	MIPS_GPR_V0		2		/*  v0  */
#define	MIPS_GPR_V1		3		/*  v1  */
#define	MIPS_GPR_A0		4		/*  a0  */
#define	MIPS_GPR_A1		5		/*  a1  */
#define	MIPS_GPR_A2		6		/*  a2  */
#define	MIPS_GPR_A3		7		/*  a3  */
#define	MIPS_GPR_T0		8		/*  t0  */
#define	MIPS_GPR_T1		9		/*  t1  */
#define	MIPS_GPR_T2		10		/*  t2  */
#define	MIPS_GPR_T3		11		/*  t3  */
#define	MIPS_GPR_T4		12		/*  t4  */
#define	MIPS_GPR_T5		13		/*  t5  */
#define	MIPS_GPR_T6		14		/*  t6  */
#define	MIPS_GPR_T7		15		/*  t7  */
#define	MIPS_GPR_S0		16		/*  s0  */
#define	MIPS_GPR_S1		17		/*  s1  */
#define	MIPS_GPR_S2		18		/*  s2  */
#define	MIPS_GPR_S3		19		/*  s3  */
#define	MIPS_GPR_S4		20		/*  s4  */
#define	MIPS_GPR_S5		21		/*  s5  */
#define	MIPS_GPR_S6		22		/*  s6  */
#define	MIPS_GPR_S7		23		/*  s7  */
#define	MIPS_GPR_T8		24		/*  t8  */
#define	MIPS_GPR_T9		25		/*  t9  */
#define	MIPS_GPR_K0		26		/*  k0  */
#define	MIPS_GPR_K1		27		/*  k1  */
#define	MIPS_GPR_GP		28		/*  gp  */
#define	MIPS_GPR_SP		29		/*  sp  */
#define	MIPS_GPR_FP		30		/*  fp  */
#define	MIPS_GPR_RA		31		/*  ra  */

#define	N_HI6			64
#define	N_SPECIAL		64
#define	N_REGIMM		32

/*******************************  OLD:  *****************************/

/*  Number of "tiny" translation cache entries:  */
#define	N_TRANSLATION_CACHE_INSTR	5
#define	N_TRANSLATION_CACHE_DATA	5

struct translation_cache_entry {
	int		wf;
	uint64_t	vaddr_pfn;
	uint64_t	paddr;
};

/*  This should be a value which the program counter
    can "never" have:  */
#define	PC_LAST_PAGE_IMPOSSIBLE_VALUE	3

/*  An "impossible" paddr:  */
#define	IMPOSSIBLE_PADDR		0x1212343456566767ULL

#define	DEFAULT_PCACHE_SIZE		15	/*  32 KB  */
#define	DEFAULT_PCACHE_LINESIZE		5	/*  32 bytes  */

struct r3000_cache_line {
	uint32_t	tag_paddr;
	int		tag_valid;
};
#define	R3000_TAG_VALID		1
#define	R3000_TAG_DIRTY		2

struct r4000_cache_line {
	char		dummy;
};

/********************************************************************/

#ifdef ONEKPAGE
#define	MIPS_IC_ENTRIES_SHIFT		8
#else
#define	MIPS_IC_ENTRIES_SHIFT		10
#endif

#define	MIPS_N_IC_ARGS			3
#define	MIPS_INSTR_ALIGNMENT_SHIFT	2
#define	MIPS_IC_ENTRIES_PER_PAGE	(1 << MIPS_IC_ENTRIES_SHIFT)
#define	MIPS_PC_TO_IC_ENTRY(a)		(((a)>>MIPS_INSTR_ALIGNMENT_SHIFT) \
					& (MIPS_IC_ENTRIES_PER_PAGE-1))
#define	MIPS_ADDR_TO_PAGENR(a)		((a) >> (MIPS_IC_ENTRIES_SHIFT \
					+ MIPS_INSTR_ALIGNMENT_SHIFT))

#define	MIPS_L2N		17
#define	MIPS_L3N		18

#define	MIPS_MAX_VPH_TLB_ENTRIES	128
DYNTRANS_MISC_DECLARATIONS(mips,MIPS,uint64_t)
DYNTRANS_MISC64_DECLARATIONS(mips,MIPS,uint8_t)

#if 0
struct mips_instr_call {
	void	(*f)(struct cpu *, struct mips_instr_call *);
	size_t	arg[MIPS_N_IC_ARGS];
};

/*  Translation cache struct for each physical page:  */
struct mips_tc_physpage {
	struct mips_instr_call ics[MIPS_IC_ENTRIES_PER_PAGE + 3];
	uint32_t	next_ofs;	/*  or 0 for end of chain  */
	int		flags;
	uint64_t	physaddr;
};

struct mips_vpg_tlb_entry {
	uint8_t		valid;
	uint8_t		writeflag;
	unsigned char	*host_page;
	int64_t		timestamp;
	uint64_t	vaddr_page;
	uint64_t	paddr_page;
};
#endif

/*******************************  OLD:  *****************************/

#define	BINTRANS_DONT_RUN_NEXT		0x1000000
#define	BINTRANS_N_MASK			0x0ffffff

#define	N_BINTRANS_VADDR_TO_HOST	20

/*  Virtual to host address translation tables:  */
struct vth32_table {
	void			*haddr_entry[1024 * 2];
	uint32_t		paddr_entry[1024];
	uint32_t		*bintrans_chunks[1024];
	struct vth32_table	*next_free;
	int			refcount;
};

/********************************************************************/

struct mips_cpu {
	struct mips_cpu_type_def cpu_type;

	struct mips_coproc *coproc[N_MIPS_COPROCS];

	int		compare_register_set;

	/*  Special purpose registers:  */
	uint64_t	pc_last;		/*  PC of last instruction   */
	uint64_t	hi;
	uint64_t	lo;

	/*  General purpose registers:  */
	uint64_t	gpr[N_MIPS_GPRS];

	/*
	 *  The translation_cached stuff is used to speed up the
	 *  most recent lookups into the TLB.  Whenever the TLB is
	 *  written to, translation_cached[] must be filled with zeros.
	 */
#ifdef USE_TINY_CACHE
	struct translation_cache_entry
			translation_cache_instr[N_TRANSLATION_CACHE_INSTR];
	struct translation_cache_entry
			translation_cache_data[N_TRANSLATION_CACHE_DATA];
#endif

	/*
	 *  For faster memory lookup when running instructions:
	 *
	 *  Reading memory to load instructions is a very common thing in the
	 *  emulator, and an instruction is very often read from the address
	 *  following the previously executed instruction. That means that we
	 *  don't have to go through the TLB each time.
	 *
	 *  We then get the vaddr -> paddr translation for free. There is an
	 *  even better case when the paddr is a RAM address (as opposed to an
	 *  address in a memory mapped device). Then we can figure out the
	 *  address in the host's memory directly, and skip the paddr -> host
	 *  address calculation as well.
	 *
	 *  A modification to the TLB should set the virtual_page variable to
	 *  an "impossible" value, so that there won't be a hit on the next
	 *  instruction.
	 */
	uint64_t	pc_last_virtual_page;
	uint64_t	pc_last_physical_page;
	unsigned char	*pc_last_host_4k_page;

	/*  MIPS Bintrans:  */
	int		dont_run_next_bintrans;
	int		bintrans_instructions_executed;  /*  set to the
				number of bintranslated instructions executed
				when running a bintrans codechunk  */
	int		pc_bintrans_paddr_valid;
	uint64_t	pc_bintrans_paddr;
	unsigned char	*pc_bintrans_host_4kpage;

	/*  Chunk base address:  */
	unsigned char	*chunk_base_address;

	/*  This should work for 32-bit MIPS emulation:  */
	struct vth32_table *vaddr_to_hostaddr_nulltable;
	struct vth32_table *vaddr_to_hostaddr_r2k3k_icachetable;
	struct vth32_table *vaddr_to_hostaddr_r2k3k_dcachetable;
	struct vth32_table **vaddr_to_hostaddr_table0_kernel;
	struct vth32_table **vaddr_to_hostaddr_table0_cacheisol_i;
	struct vth32_table **vaddr_to_hostaddr_table0_cacheisol_d;
	struct vth32_table **vaddr_to_hostaddr_table0_user;
	struct vth32_table **vaddr_to_hostaddr_table0;  /*  should point to kernel or user  */
	struct vth32_table *next_free_vth_table;

/*  Testing...  */
	unsigned char	**host_OLD_load;
	unsigned char	**host_OLD_store;
	unsigned char	**host_load_orig;
	unsigned char	**host_store_orig;
	unsigned char	**huge_r2k3k_cache_table;

	/*  For 64-bit (generic) emulation:  */
	unsigned char	*(*fast_vaddr_to_hostaddr)(struct cpu *cpu,
			    uint64_t vaddr, int writeflag);
	int		bintrans_next_index;
	int		bintrans_data_writable[N_BINTRANS_VADDR_TO_HOST];
	uint64_t	bintrans_data_vaddr[N_BINTRANS_VADDR_TO_HOST];
	unsigned char	*bintrans_data_hostpage[N_BINTRANS_VADDR_TO_HOST];

	void		(*bintrans_load_32bit)(struct cpu *);	/*  Note: incorrect args  */
	void		(*bintrans_store_32bit)(struct cpu *);	/*  Note: incorrect args  */
	void		(*bintrans_jump_to_32bit_pc)(struct cpu *);
	void		(*bintrans_simple_exception)(struct cpu *, int);
	void		(*bintrans_fast_rfe)(struct cpu *);
	void		(*bintrans_fast_eret)(struct cpu *);
	void		(*bintrans_fast_tlbwri)(struct cpu *, int);
	void		(*bintrans_fast_tlbpr)(struct cpu *, int);

#ifdef ENABLE_INSTRUCTION_DELAYS
	int		instruction_delay;
#endif

	int		nullify_next;		/*  set to 1 if next instruction
							is to be nullified  */

	int		show_trace_delay;	/*  0=normal, > 0 = delay until show_trace  */
	uint64_t	show_trace_addr;

	int		last_was_jumptoself;
	int		jump_to_self_reg;

#ifdef MFHILO_DELAY
	int		mfhi_delay;	/*  instructions since last mfhi  */
	int		mflo_delay;	/*  instructions since last mflo  */
#endif

	int		rmw;		/*  Read-Modify-Write  */
	int		rmw_len;	/*  Length of rmw modification  */
	uint64_t	rmw_addr;	/*  Address of rmw modification  */

	/*
	 *  NOTE:  The R5900 has 128-bit registers. I'm not really sure
	 *  whether they are used a lot or not, at least with code produced
	 *  with gcc they are not. An important case however is lq and sq
	 *  (load and store of 128-bit values). These "upper halves" of R5900
	 *  quadwords can be used in those cases.
	 *
	 *  hi1 and lo1 are the high 64-bit parts of the hi and lo registers.
	 *  sa is a 32-bit "shift amount" register.
	 *
	 *  TODO:  Generalize this.
	 */
	uint64_t	gpr_quadhi[N_MIPS_GPRS];
	uint64_t	hi1;
	uint64_t	lo1;
	uint32_t	r5900_sa;


	/*  Data and Instruction caches:  */
	unsigned char	*cache[2];
	void		*cache_tags[2];
	uint64_t	cache_last_paddr[2];
	int		cache_size[2];
	int		cache_linesize[2];
	int		cache_mask[2];
	int		cache_miss_penalty[2];

	/*  Other stuff:  */
	uint64_t	cop0_config_select1;


	/*  NEW DYNTRANS:  */


	/*
	 *  Instruction translation cache and Virtual->Physical->Host
	 *  address translation:
	 */
	DYNTRANS_ITC(mips)
	VPH_TLBS(mips,MIPS)
	VPH32(mips,MIPS,uint64_t,uint8_t)
	VPH64(mips,MIPS,uint8_t)
};


/*  cpu_mips.c:  */
int mips_cpu_instruction_has_delayslot(struct cpu *cpu, unsigned char *ib);
void mips_cpu_tlbdump(struct machine *m, int x, int rawflag);
void mips_cpu_register_match(struct machine *m, char *name, 
	int writeflag, uint64_t *valuep, int *match_register);
void mips_cpu_register_dump(struct cpu *cpu, int gprs, int coprocs);
int mips_cpu_disassemble_instr(struct cpu *cpu, unsigned char *instr,
        int running, uint64_t addr, int bintrans);
int mips_cpu_interrupt(struct cpu *cpu, uint64_t irq_nr);
int mips_cpu_interrupt_ack(struct cpu *cpu, uint64_t irq_nr);
void mips_cpu_exception(struct cpu *cpu, int exccode, int tlb, uint64_t vaddr,
        /*  uint64_t pagemask,  */  int coproc_nr, uint64_t vaddr_vpn2,
        int vaddr_asid, int x_64);
void mips_cpu_cause_simple_exception(struct cpu *cpu, int exc_code);
int mips_cpu_run(struct emul *emul, struct machine *machine);
void mips_cpu_dumpinfo(struct cpu *cpu);
void mips_cpu_list_available_types(void);
int mips_cpu_family_init(struct cpu_family *);


/*  cpu_mips_coproc.c:  */
struct mips_coproc *mips_coproc_new(struct cpu *cpu, int coproc_nr);
void mips_coproc_tlb_set_entry(struct cpu *cpu, int entrynr, int size,
        uint64_t vaddr, uint64_t paddr0, uint64_t paddr1,
        int valid0, int valid1, int dirty0, int dirty1, int global, int asid,
        int cachealgo0, int cachealgo1);
void mips_OLD_update_translation_table(struct cpu *cpu, uint64_t vaddr_page,
        unsigned char *host_page, int writeflag, uint64_t paddr_page);
void clear_all_chunks_from_all_tables(struct cpu *cpu);
void mips_invalidate_translation_caches_paddr(struct cpu *cpu, uint64_t, int);
void coproc_register_read(struct cpu *cpu,
        struct mips_coproc *cp, int reg_nr, uint64_t *ptr, int select);
void coproc_register_write(struct cpu *cpu,
        struct mips_coproc *cp, int reg_nr, uint64_t *ptr, int flag64,
	int select);
void coproc_tlbpr(struct cpu *cpu, int readflag);
void coproc_tlbwri(struct cpu *cpu, int randomflag);
void coproc_rfe(struct cpu *cpu);
void coproc_eret(struct cpu *cpu);
void coproc_function(struct cpu *cpu, struct mips_coproc *cp, int cpnr,
        uint32_t function, int unassemble_only, int running);


/*  memory_mips.c:  */
int memory_cache_R3000(struct cpu *cpu, int cache, uint64_t paddr,
	int writeflag, size_t len, unsigned char *data);
int mips_memory_rw(struct cpu *cpu, struct memory *mem, uint64_t vaddr,
	unsigned char *data, size_t len, int writeflag, int cache_flags);


/*  Dyntrans unaligned load/store:  */
void mips_unaligned_loadstore(struct cpu *cpu, struct mips_instr_call *ic, 
	int is_left, int wlen, int store);


void mips_update_translation_table(struct cpu *cpu, uint64_t vaddr_page,
	unsigned char *host_page, int writeflag, uint64_t paddr_page);
void mips_invalidate_translation_caches(struct cpu *cpu, uint64_t, int);
void mips_invalidate_code_translation(struct cpu *cpu, uint64_t, int);
void mips32_update_translation_table(struct cpu *cpu, uint64_t vaddr_page,
	unsigned char *host_page, int writeflag, uint64_t paddr_page);
void mips32_invalidate_translation_caches(struct cpu *cpu, uint64_t, int);
void mips32_invalidate_code_translation(struct cpu *cpu, uint64_t, int);
void mips_init_64bit_dummy_tables(struct cpu *cpu);


#endif	/*  CPU_MIPS_H  */
