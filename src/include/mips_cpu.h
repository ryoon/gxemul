#ifndef	MIPS_CPU_H
#define	MIPS_CPU_H

/*
 *  Copyright (C) 2003-2005  Anders Gavare.  All rights reserved.
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
 *  $Id: mips_cpu.h,v 1.3 2005-01-24 16:29:43 debug Exp $
 */

#include "misc.h"

/*  
 *  ENABLE_MIPS16 should be defined on the cc commandline using -D, if you 
 *  want it. (This is done by ./configure --mips16)
 */
/*  #define MFHILO_DELAY  */

struct emul;
struct machine;

/*
 *  CPU type definitions:  See cpu_types.h.
 */

struct mips_cpu_type_def {
	char		*name;
	int		rev;
	int		sub;
	char		flags;
	char		exc_model;		/*  EXC3K or EXC4K  */
	char		mmu_model;		/*  MMU3K or MMU4K  */
	char		isa_level;		/*  1, 2, 3, 4, 5, 32, 64  */
	int		nr_of_tlb_entries;	/*  32, 48, 64, ...  */
	char		instrs_per_cycle;	/*  simplified, 1, 2, or 4  */
	int		default_picache;
	int		default_pdcache;
	int		default_pilinesize;
	int		default_pdlinesize;
	int		default_scache;
	int		default_slinesize;
};

#define	INITIAL_PC			0xffffffffbfc00000ULL
#define	INITIAL_STACK_POINTER		(0xffffffffa0008000ULL - 256)


/*
 *  Coproc 0:
 *
 *  TODO:  48 or 64 is max for most processors, but 192 for R8000?
 */
#define	N_COPROC_REGS		32
struct mips_tlb {
	uint64_t	hi;
	uint64_t	lo0;
	uint64_t	lo1;
	uint64_t	mask;
};


/*
 *  Coproc 1:
 */
#define	N_FCRS			32

struct coproc {
	int		coproc_nr;
	uint64_t	reg[N_COPROC_REGS];

	/*  Only for COP0:  */
	struct mips_tlb	*tlbs;
	int		nr_of_tlbs;

	/*  Only for COP1:  floating point control registers  */
	/*  (Maybe also for COP0?)  */
	uint64_t	fcr[N_FCRS];
};

#define	N_COPROCS	4

#define	NGPRS		32		/*  General purpose registers  */
#define	NFPUREGS	32		/*  Floating point registers  */

/*  These should all be 2 characters wide:  */
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
#define	MIPS_GPR_S0		16		/*  s0  */
#define	MIPS_GPR_S1		17		/*  s1  */
#define	MIPS_GPR_T8		24		/*  t8  */
#define	MIPS_GPR_T9		25		/*  t9  */
#define	MIPS_GPR_K0		26		/*  k0  */
#define	MIPS_GPR_K1		27		/*  k1  */
#define	MIPS_GPR_GP		28		/*  gp  */
#define	MIPS_GPR_SP		29		/*  sp  */
#define	MIPS_GPR_FP		30		/*  fp  */
#define	MIPS_GPR_RA		31		/*  ra  */

/*  Meaning of delay_slot:  */
#define	NOT_DELAYED		0
#define	DELAYED			1
#define	TO_BE_DELAYED		2

#define	N_HI6			64
#define	N_SPECIAL		64
#define	N_REGIMM		32

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

#define	BINTRANS_DONT_RUN_NEXT		0x1000000
#define	BINTRANS_N_MASK			0x0ffffff

#define	N_SAFE_BINTRANS_LIMIT_SHIFT	14
#define	N_SAFE_BINTRANS_LIMIT	((1 << (N_SAFE_BINTRANS_LIMIT_SHIFT - 1)) - 1)

#define	N_BINTRANS_VADDR_TO_HOST	20

/*  Virtual to host address translation tables:  */
struct vth32_table {
	void			*haddr_entry[1024];
	uint32_t		paddr_entry[1024];
	uint32_t		*bintrans_chunks[1024];
	struct vth32_table	*next_free;
	int			refcount;
};

struct cpu {
	/*  Pointer back to the machine this CPU is in:  */
	struct machine	*machine;

	int		byte_order;
	int		running;
	int		bootstrap_cpu_flag;
	int		cpu_id;

	struct mips_cpu_type_def cpu_type;

	struct coproc	*coproc[N_COPROCS];

	void		(*md_interrupt)(struct cpu *, int irq_nr, int);

	int		compare_register_set;

	/*  Special purpose registers:  */
	uint64_t	pc;
	uint64_t	pc_last;		/*  PC of last instruction   */
	uint64_t	hi;
	uint64_t	lo;

	/*  General purpose registers:  */
	uint64_t	gpr[NGPRS];

	struct memory	*mem;
	int		(*translate_address)(struct cpu *, uint64_t vaddr,
			    uint64_t *return_addr, int flags);

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

#ifdef BINTRANS
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

	/*  For 64-bit (generic) emulation:  */
	unsigned char	*(*fast_vaddr_to_hostaddr)(struct cpu *cpu,
			    uint64_t vaddr, int writeflag);
	int		bintrans_next_index;
	int		bintrans_data_writable[N_BINTRANS_VADDR_TO_HOST];
	uint64_t	bintrans_data_vaddr[N_BINTRANS_VADDR_TO_HOST];
	unsigned char	*bintrans_data_hostpage[N_BINTRANS_VADDR_TO_HOST];

	void		(*bintrans_loadstore_32bit)(struct cpu *);	/*  Note: incorrect args  */
	void		(*bintrans_jump_to_32bit_pc)(struct cpu *);
	void		(*bintrans_simple_exception)(struct cpu *, int);
	void		(*bintrans_fast_rfe)(struct cpu *);
	void		(*bintrans_fast_eret)(struct cpu *);
	void		(*bintrans_fast_tlbwri)(struct cpu *, int);
	void		(*bintrans_fast_tlbpr)(struct cpu *, int);
#endif

#ifdef ENABLE_MIPS16
	int		mips16;			/*  non-zero if MIPS16 code is allowed  */
	uint16_t	mips16_extend;		/*  set on 'extend' instructions to the entire 16-bit extend instruction  */
#endif

#ifdef ENABLE_INSTRUCTION_DELAYS
	int		instruction_delay;
#endif

	int		trace_tree_depth;

	uint64_t	delay_jmpaddr;		/*  only used if delay_slot > 0  */
	int		delay_slot;
	int		nullify_next;		/*  set to 1 if next instruction
							is to be nullified  */

	/*  This is set to non-zero, if it is possible at all that an
	    interrupt will occur.  */
	int		cached_interrupt_is_possible;

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
	 *  TODO:  The R5900 has 128-bit registers. I'm not really sure
	 *  whether they are used a lot or not, at least with code produced
	 *  with gcc they are not. An important case however is lq and sq
	 *  (load and store of 128-bit values). These "upper halves" of R5900
	 *  quadwords can be used in those cases.
	 *
	 *  TODO:  Generalize this.
	 */
	uint64_t	gpr_quadhi[NGPRS];


	/*
	 *  Statistics:
	 */
	long		stats_opcode[N_HI6];
	long		stats__special[N_SPECIAL];
	long		stats__regimm[N_REGIMM];
	long		stats__special2[N_SPECIAL];

	/*  Data and Instruction caches:  */
	unsigned char	*cache[2];
	void		*cache_tags[2];
	uint64_t	cache_last_paddr[2];
	int		cache_size[2];
	int		cache_linesize[2];
	int		cache_mask[2];
	int		cache_miss_penalty[2];
};


/*  coproc.c:  */
struct coproc *coproc_new(struct cpu *cpu, int coproc_nr);
void coproc_tlb_set_entry(struct cpu *cpu, int entrynr, int size,
        uint64_t vaddr, uint64_t paddr0, uint64_t paddr1,
        int valid0, int valid1, int dirty0, int dirty1, int global, int asid,
        int cachealgo0, int cachealgo1);
void update_translation_table(struct cpu *cpu, uint64_t vaddr_page,
        unsigned char *host_page, int writeflag, uint64_t paddr_page);
void clear_all_chunks_from_all_tables(struct cpu *cpu);
void invalidate_translation_caches_paddr(struct cpu *cpu, uint64_t paddr);
void coproc_register_read(struct cpu *cpu,
        struct coproc *cp, int reg_nr, uint64_t *ptr);
void coproc_register_write(struct cpu *cpu,
        struct coproc *cp, int reg_nr, uint64_t *ptr, int flag64);
void coproc_tlbpr(struct cpu *cpu, int readflag);
void coproc_tlbwri(struct cpu *cpu, int randomflag);
void coproc_rfe(struct cpu *cpu);
void coproc_eret(struct cpu *cpu);
void coproc_function(struct cpu *cpu, struct coproc *cp, int cpnr,
        uint32_t function, int unassemble_only, int running);


/*  cpu.c:  */
struct cpu *cpu_new(struct memory *mem, struct machine *machine,
        int cpu_id, char *cpu_type_name);
void cpu_show_full_statistics(struct machine *m);
void cpu_register_dump(struct cpu *cpu, int gprs, int coprocs);
void cpu_disassemble_instr(struct cpu *cpu, unsigned char *instr,
        int running, uint64_t addr, int bintrans);
int mips_cpu_interrupt(struct cpu *cpu, int irq_nr);
int mips_cpu_interrupt_ack(struct cpu *cpu, int irq_nr);
void cpu_exception(struct cpu *cpu, int exccode, int tlb, uint64_t vaddr,
        /*  uint64_t pagemask,  */  int coproc_nr, uint64_t vaddr_vpn2,
        int vaddr_asid, int x_64);
void cpu_cause_simple_exception(struct cpu *cpu, int exc_code);
void cpu_run_init(struct emul *emul, struct machine *machine);
int cpu_run(struct emul *emul, struct machine *machine);
void cpu_run_deinit(struct emul *emul, struct machine *machine);


#endif	/*  MIPS_CPU_H  */
