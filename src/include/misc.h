#ifndef	MISC_H
#define	MISC_H

/*
 *  Copyright (C) 2003,2004 by Anders Gavare.  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright  
 *     notice, this list of conditions and the following disclaimer in the 
 *     documentation and/or other materials provided with the distribution.
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
 *  $Id: misc.h,v 1.54 2004-06-08 10:49:46 debug Exp $
 *
 *  Misc. definitions for mips64emul.
 *
 *  TODO:  separate into smaller, more orthogonal files.
 *         perhaps cpu.h, opcodes.h (all the opcodes)?
 */

#include <sys/types.h>
#include <inttypes.h>

#ifdef WITH_X11
#include <X11/Xlib.h>
#endif

#ifdef SOLARIS
/*  For Solaris:  */
typedef uint8_t u_int8_t;
typedef uint16_t u_int16_t;
typedef uint32_t u_int32_t;
typedef uint64_t u_int64_t;
#endif

#ifdef HPUX
/*  For HP-UX:  */
typedef uint8_t u_int8_t;
typedef uint16_t u_int16_t;
typedef uint32_t u_int32_t;
typedef uint64_t u_int64_t;
#endif

/*  These can be defined, but they reduce speed of the emulator.
    Don't enable them unless you need them.  */

#define SUPPORT_MIPS16
/*  #define ALWAYS_SIGNEXTEND_32  */
/*  #define HALT_IF_PC_ZERO  */
/*  #define MFHILO_DELAY  */
/*  #define LAST_USED_TLB_EXPERIMENT  */

/*  Machine emulation types:  */
#define	EMULTYPE_NONE		0
#define	EMULTYPE_DEC		1
#define	EMULTYPE_COBALT		2
#define	EMULTYPE_HPCMIPS	3
#define	EMULTYPE_PS2		4
#define	EMULTYPE_SGI		5
#define	EMULTYPE_ARC		6
#define	EMULTYPE_NINTENDO64	7

/*  Specific machines:  */
/*  DEC:  */
#define	MACHINE_NONE		0
#define	MACHINE_PMAX_3100	1
#define	MACHINE_3MAX_5000	2
#define	MACHINE_3MIN_5000	3
#define	MACHINE_3MAXPLUS_5000	4
#define	MACHINE_5800		5
#define	MACHINE_5400		6
#define	MACHINE_MAXINE_5000	7
#define	MACHINE_5500		11
#define	MACHINE_MIPSMATE_5100	12

#include "dec_prom.h"
#include "dec_bootinfo.h"
#define	DEC_PROM_CALLBACK_STRUCT	0xffffffffbfc08000
#define	DEC_PROM_EMULATION		0xffffffffbfc10000
#define	DEC_PROM_INITIAL_ARGV		(INITIAL_STACK_POINTER + 0x80)
#define	DEC_PROM_STRINGS		0xffffffffbfc20000
#define	DEC_PROM_TCINFO			0xffffffffbfc2c000
#define	DEC_MEMMAP_ADDR			0xffffffffbfc30000


/*  HPCmips:  */
#include "hpc_bootinfo.h"
#define	HPCMIPS_FB_ADDR		0x1f000000
#define	HPCMIPS_FB_XSIZE	640
#define	HPCMIPS_FB_YSIZE	480

/*  Playstation 2:  */
#define	PLAYSTATION2_BDA	0xa0001000
#define	PLAYSTATION2_OPTARGS	0x81fff100
#define	PLAYSTATION2_SIFBIOS	0xbfc10000

/*  SGI and ARC:  */
#include "sgi_arcbios.h"
/*
 *  Problem: kernels seem to be loaded at low addresses in RAM, so
 *  storing environment strings and memory descriptors there is a bad
 *  idea. They are stored at 0xbfc..... instead.  The ARC SPB must
 *  be at physical address 0x1000 though.
 */
#define	SGI_SPB_ADDR		0xffffffff80001000
/*  0xbfc10000 is firmware callback vector stuff  */
#define	ARC_FIRMWARE_VECTORS	0xffffffffbfc80000
#define	ARC_FIRMWARE_ENTRIES	0xffffffffbfc88000
#define	ARC_ARGV_START		0xffffffffbfc90000
#define	SGI_ENV_STRINGS		0xffffffffbfc98000
#define	SGI_SYSID_ADDR		0xffffffffbfca1800
#define	ARC_DSPSTAT_ADDR	0xffffffffbfca1c00
#define	ARC_MEMDESC_ADDR	0xffffffffbfca1c80
#define	FIRST_ARC_COMPONENT	0xffffffffbfca8000
#define	ARC_PRIVATE_VECTORS	0xffffffffbfcb0000
#define	ARC_PRIVATE_ENTRIES	0xffffffffbfcb8000

/*  Nintendo 64:  */
/*  nothing yet  */


/*  CPU types:  */
#include "cpuregs.h"			/*  from NetBSD  */
#define	MIPS_5K		129		/*  according to MIPS64 5K User's Manual  */
#define	MIPS_5K_REV	    1		/*  according to MIPS64 5K User's Manual  */

struct cpu_type_def {
	char		*name;
	int		rev;
	int		sub;
	int		flags;
	int		exc_model;		/*  EXC3K or EXC4K  */
	int		mmu_model;		/*  MMU3K or MMU4K  */
	int		isa_level;		/*  1, 2, 3, 4, 5  */
	int		nr_of_tlb_entries;	/*  48, 64, ...  */
};

#define	EXC3K		3
#define	EXC4K		4
#define	MMU3K		3
#define	MMU4K		4
#define	MMU8K		8
#define	MMU10K		10

/*  Bit-field values for the flags field:  */
#define	NOLLSC		1
#define	DCOUNT		2

#define	CPU_DEFAULT	"R4000"

#define	CPU_TYPE_DEFS	{	\
	{ "R2000",	MIPS_R2000, 0x00,	NOLLSC,	EXC3K, MMU3K,	1,	64 }, \
	{ "R2000A",	MIPS_R2000, 0x10,	NOLLSC,	EXC3K, MMU3K,	1,	64 }, \
	{ "R3000",	MIPS_R3000, 0x20,	NOLLSC,	EXC3K, MMU3K,	1,	64 }, \
	{ "R3000A",	MIPS_R3000, 0x30,	NOLLSC,	EXC3K, MMU3K,	1,	64 }, \
	{ "R6000",	MIPS_R6000, 0x00,	0,	EXC3K, MMU3K,	2,	32 }, \
	{ "R4000",	MIPS_R4000, 0x00,	DCOUNT,	EXC4K, MMU4K,	3,	48 }, \
	{ "R10000",	MIPS_R10000,0x26,	0,	EXC4K, MMU10K,	4,	64 }, \
	{ "R4300",	MIPS_R4300, 0x00,	0,	EXC4K, MMU4K,	3,	32 }, /*  No DCOUNT?  */ \
	{ "R4400",	MIPS_R4000, 0x40,	DCOUNT,	EXC4K, MMU4K,	3,	48 }, \
	{ "R4600",	MIPS_R4600, 0x00,	DCOUNT,	EXC4K, MMU4K,	3,	48 }, \
	{ "R4700",	MIPS_R4700, 0x00,	0,	EXC4K, MMU4K,	3,	48 }, /*  No DCOUNT?  */ \
	{ "R8000",	MIPS_R8000, 0,		0,	EXC4K, MMU8K,	4,     192 }, /*  192 tlb entries?  */ \
	{ "R12000",	MIPS_R12000,0,		0,	EXC4K, MMU10K,	4,	64 }, \
	{ "R5000",	MIPS_R5000, 0x21,	DCOUNT,	EXC4K, MMU4K,	4,	48 }, \
	{ "R5900",	MIPS_R5900, 0x20,	0,	EXC4K, MMU4K,	3,	48 }, \
	{ "VR5432",	MIPS_R5400, 13,		0,	EXC4K, MMU4K,	-1,	-1 }, /*  DCOUNT?  */ \
	{ "RM5200",	MIPS_RM5200,0xa0,	0,	EXC4K, MMU4K,	4,	48 }, /*  DCOUNT?  */ \
	{ "RM7000",	MIPS_RM7000,0x0 /* ? */,DCOUNT,	EXC4K, MMU4K,	4,	48 }, \
	{ "5K",		MIPS_5K,    1,		0,	EXC4K, MMU4K,	5,	48 }, /*  DCOUNT?  */ \
	{ NULL } }

/*  Debug stuff:  */
#define	EMUL_DEBUG		"\e[0;1m"	/*  bold  */
#define	EMUL_DEBUG_END		"\e[0m"		/*  restore to normal  */
#define	DEBUG_BUFSIZE		1024

#define	DEFAULT_RAM_IN_MB	32
#define	MAX_PC_DUMPPOINTS	16
#define	MAX_DEVICES		22

#define	BINTRANS_CACHEENTRIES	64

/*  lowest 5 bits are register number, bit 6 and up can be used as flags:  */
#define	MEMREGISTERHINT_WRITE	32

struct cpu;

struct memory {
	size_t		physical_max;		/*  must be less than or equal to 1 << max_bits  */
	int		max_bits;

	int		bits_per_pagetable;
	int		entries_per_pagetable;		/*  same as 1 << bits_per_pagetable  */

	int		bits_per_memblock;
	int		memblock_size;			/*  same as 1 << bits_per_memblock  */

	void		*first_pagetable;

	int		n_mmapped_devices;
	uint64_t	mmap_dev_minaddr;		/*  might speed up things a little bit  */
	uint64_t	mmap_dev_maxaddr;		/*  (actually maxaddr is the addr after the last address)  */

	const char	*dev_name[MAX_DEVICES];
	uint64_t	dev_baseaddr[MAX_DEVICES];
	uint64_t	dev_length[MAX_DEVICES];
	int		(*dev_f[MAX_DEVICES])(struct cpu *,struct memory *,uint64_t,unsigned char *,size_t,int,void *);
	void		*dev_extra[MAX_DEVICES];

	/*  Stuff for binary translation:  */
	uint64_t	bintrans_last_paddr;
	void		*bintrans_last_host4kpage;
	int		bintrans_last_chunk_nr;

	long		bintrans_tickcount;
	uint64_t	bintrans_paddr_start[BINTRANS_CACHEENTRIES];
	uint64_t	bintrans_paddr_end[BINTRANS_CACHEENTRIES];
	void		*bintrans_codechunk[BINTRANS_CACHEENTRIES];
	size_t		bintrans_codechunk_len[BINTRANS_CACHEENTRIES];
	size_t		bintrans_codechunk_time[BINTRANS_CACHEENTRIES];	/*  time for most recent use  */
	int		bintrans_codechunk_ninstr[BINTRANS_CACHEENTRIES];
	int		bintrans_codechunk_memregisterhint[BINTRANS_CACHEENTRIES];
};

/* #define	DEFAULT_BITS_PER_PAGETABLE	12 */	/*  10  or 12  or 16  */
/* #define	DEFAULT_BITS_PER_MEMBLOCK	16 */	/*  14  or 16  */

#define	DEFAULT_BITS_PER_PAGETABLE	20	/*  10  or 12  or 16  or 20  */
#define	DEFAULT_BITS_PER_MEMBLOCK	20	/*  14  or 16  or 20  */

/*  Physical address space:  (choose something like 40, 52 or 64)  */
#define	DEFAULT_MAX_BITS		40

#define	MEM_READ			0
#define	MEM_WRITE			1

#define	INITIAL_PC			0xffffffffbfc00000
#define	INITIAL_STACK_POINTER		(0xffffffffa0008000 - 256)


#define	N_COPROC_REGS		32
#define	N_FCRS			32

/*  TODO:  48 or 64 is max for most processors, but 192 for R8000?  */
#define	MAX_NR_OF_TLBS		192

struct tlb {
	uint64_t	mask;
	uint64_t	hi;
	uint64_t	lo1;
	uint64_t	lo0;
#ifdef LAST_USED_TLB_EXPERIMENT
	uint64_t	last_used;		/*  set to coproc0's count value at access  */
#endif
};

struct coproc {
	int		coproc_nr;
	uint64_t	reg[N_COPROC_REGS];

	/*  Only for COP0:  */
	struct tlb	tlbs[MAX_NR_OF_TLBS];
	int		nr_of_tlbs;

	/*  Only for COP1:  */
	uint64_t	fcr[N_FCRS];		/*  floating point control registers  */
};

#define	N_COPROCS		4

/*  TODO:  Coproc registers are actually CPU dependant, so an R4000
	has other bits/registers than an R3000...
    TODO 2: CPUs like the R10000 are probably even a bit more different.  */

#include "cpuregs.h"

/*  Coprocessor 0's registers:  */
#define	COP0_NAMES	{ "INDEX", "RANDOM", "ENTRYLO0", "ENTRYLO1", \
			  "CONTEXT", "PAGEMASK", "WIRED", "RESERVED_7", \
			  "BADVADDR", "COUNT", "ENTRYHI", "COMPARE", \
			  "STATUS", "CAUSE", "EPC", "PRID", \
			  "CONFIG", "LLADDR", "WATCHLO", "WATCHHI", \
			  "XCONTEXT", "RESERVED_21", "RESERVED_22", "DEBUG", \
			  "DEPC", "PERFCNT", "ERRCTL", "CACHEERR", \
			  "TAGDATA_LO", "TAGDATA_HI", "ERROREPC", "DESAVE" \
			}
#define	COP0_INDEX		0
#define	   INDEX_P		    0x80000000		/*  Probe failure bit. Set by tlbp  */
#define	   INDEX_MASK		    0x3f
#define	   R2K3K_INDEX_P	    0x80000000
#define	   R2K3K_INDEX_MASK	    0x3f00
#define	   R2K3K_INDEX_SHIFT	    8
#define	COP0_RANDOM		1
#define	   RANDOM_MASK		    0x3f
#define	   R2K3K_RANDOM_MASK	    0x3f00
#define	   R2K3K_RANDOM_SHIFT	    8
#define	COP0_ENTRYLO0		2
#define	COP0_ENTRYLO1		3
/*  R4000 ENTRYLO:  */
#define	   ENTRYLO_PFN_MASK	    0x3fffffc0
#define	   ENTRYLO_PFN_SHIFT	    6
#define	   ENTRYLO_C_MASK	    0x00000038		/*  Coherency attribute  */
#define	   ENTRYLO_C_SHIFT	    3
#define	   ENTRYLO_D		    0x04		/*  Dirty bit  */
#define	   ENTRYLO_V		    0x02		/*  Valid bit  */
#define	   ENTRYLO_G		    0x01		/*  Global bit  */
/*  R2000/R3000 ENTRYLO:  */
#define	   R2K3K_ENTRYLO_PFN_MASK   0xfffff000
#define	   R2K3K_ENTRYLO_PFN_SHIFT  12
#define	   R2K3K_ENTRYLO_N	    0x800
#define	   R2K3K_ENTRYLO_D	    0x400
#define	   R2K3K_ENTRYLO_V	    0x200
#define	   R2K3K_ENTRYLO_G	    0x100
#define	COP0_CONTEXT		4
#define	   CONTEXT_BADVPN2_MASK	    0x007ffff0
#define	   CONTEXT_BADVPN2_SHIFT    4
#define	   R2K3K_CONTEXT_BADVPN_MASK	 0x001ffffc
#define	   R2K3K_CONTEXT_BADVPN_SHIFT    2
#define	COP0_PAGEMASK		5
#define	   PAGEMASK_MASK	    0x01ffe000
#define	   PAGEMASK_SHIFT	    13
#define	COP0_WIRED		6
#define	COP0_RESERVED_7		7
#define	COP0_BADVADDR		8
#define	COP0_COUNT		9
#define	COP0_ENTRYHI		10
/*  R4000 ENTRYHI:  */
#define	   ENTRYHI_R_MASK	    0xc000000000000000
#define	   ENTRYHI_R_SHIFT	    62
#define	   ENTRYHI_VPN2_MASK_R10K   0x00000fffffffe000
#define	   ENTRYHI_VPN2_MASK	    0x000000ffffffe000
#define	   ENTRYHI_VPN2_SHIFT	    13
#define	   ENTRYHI_ASID		    0xff
#define	   TLB_G		    (1 << 12)
/*  R2000/R3000 ENTRYHI:  */
#define	   R2K3K_ENTRYHI_VPN_MASK   0xfffff000
#define	   R2K3K_ENTRYHI_VPN_SHIFT  12
#define	   R2K3K_ENTRYHI_ASID_MASK  0xfc0
#define	   R2K3K_ENTRYHI_ASID_SHIFT 6
#define	COP0_COMPARE		11
#define	COP0_STATUS		12
#define	   STATUS_CU_MASK	    0xf0000000		/*  coprocessor usable bits  */
#define	   STATUS_CU_SHIFT	    28
#define	   STATUS_RP		    0x08000000		/*  reduced power  */
#define	   STATUS_FR		    0x04000000		/*  1=32 float regs, 0=16  */
#define	   STATUS_RE		    0x02000000		/*  reverse endian bit  */
#define	   STATUS_BEV		    0x00400000		/*  boot exception vectors (?)  */
/*  STATUS_DS: TODO  */
#define	   STATUS_IM_MASK	    0xff00
#define	   STATUS_IM_SHIFT	    8
#define	   STATUS_KX		    0x80
#define	   STATUS_SX		    0x40
#define	   STATUS_UX		    0x20
#define	   STATUS_KSU_MASK	    0x18
#define	   STATUS_KSU_SHIFT	    3
#define	   STATUS_ERL		    0x04
#define	   STATUS_EXL		    0x02
#define	   STATUS_IE		    0x01
#define	   R5900_STATUS_EIE	    0x10000
#define	COP0_CAUSE		13
#define	   CAUSE_BD		    0x80000000		/*  branch delay flag  */
#define	   CAUSE_CE_MASK	    0x30000000		/*  which coprocessor  */
#define	   CAUSE_CE_SHIFT	    28
#define	   CAUSE_IP_MASK	    0xff00		/*  interrupt pending  */
#define	   CAUSE_IP_SHIFT	    8
#define    CAUSE_EXCCODE_MASK	    0x7c		/*  exception code  */
#define    R2K3K_CAUSE_EXCCODE_MASK 0x3c
#define	   CAUSE_EXCCODE_SHIFT	    2
#define	COP0_EPC		14
#define	COP0_PRID		15
#define	COP0_CONFIG		16
#define	COP0_LLADDR		17
#define	COP0_WATCHLO		18
#define	COP0_WATCHHI		19
#define	COP0_XCONTEXT		20
#define	   XCONTEXT_R_MASK          0x180000000
#define	   XCONTEXT_R_SHIFT         31
#define	   XCONTEXT_BADVPN2_MASK    0x7ffffff0
#define	   XCONTEXT_BADVPN2_SHIFT   4
#define	COP0_FRAMEMASK		21		/*  R10000  */
#define	COP0_RESERVED_22	22
#define	COP0_DEBUG		23
#define	COP0_DEPC		24
#define	COP0_PERFCNT		25
#define	COP0_ERRCTL		26
#define	COP0_CACHEERR		27
#define	COP0_TAGDATA_LO		28
#define	COP0_TAGDATA_HI		29
#define	COP0_ERROREPC		30
#define	COP0_DESAVE		31

/*  Coprocessor 1's registers:  */
#define	COP1_REVISION		0
#define	  COP1_REVISION_MIPS3D	    0x80000		/*  MIPS3D support  */
#define	  COP1_REVISION_PS	    0x40000		/*  Paired-single support  */
#define	  COP1_REVISION_DOUBLE	    0x20000		/*  double precision support  */
#define	  COP1_REVISION_SINGLE	    0x10000		/*  single precision support  */
#define	COP1_CONTROLSTATUS	31

/*  CP0's STATUS KSU values:  */
#define	KSU_KERNEL		0
#define	KSU_SUPERVISOR		1
#define	KSU_USER		2

#define	EXCEPTION_NAMES		{ \
	"INT", "MOD", "TLBL", "TLBS", "ADEL", "ADES", "IBE", "DBE",	\
	"SYS", "BP", "RI", "CPU", "OV", "TR", "VCEI", "FPE",		\
	"16?", "17?", "18?", "19?", "20?", "21?", "22?", "WATCH",	\
	"24?", "25?", "26?", "27?", "28?", "29?", "30?", "VCED" }

/*  CP0's CAUSE exception codes:  */
#define	EXCEPTION_INT		0	/*  Interrupt  */
#define	EXCEPTION_MOD		1	/*  TLB modification exception  */
#define	EXCEPTION_TLBL		2	/*  TLB exception (load or instruction fetch)  */
#define	EXCEPTION_TLBS		3	/*  TLB exception (store)  */
#define	EXCEPTION_ADEL		4	/*  Address Error Exception (load/instr. fetch)  */
#define	EXCEPTION_ADES		5	/*  Address Error Exception (store)  */
#define	EXCEPTION_IBE		6	/*  Bus Error Exception (instruction fetch)  */
#define	EXCEPTION_DBE		7	/*  Bus Error Exception (data: load or store)  */
#define	EXCEPTION_SYS		8	/*  Syscall  */
#define	EXCEPTION_BP		9	/*  Breakpoint  */
#define	EXCEPTION_RI		10	/*  Reserved instruction  */
#define	EXCEPTION_CPU		11	/*  CoProcessor Unusable  */
#define	EXCEPTION_OV		12	/*  Arithmetic Overflow  */
#define	EXCEPTION_TR		13	/*  Trap exception  */
#define	EXCEPTION_VCEI		14	/*  Virtual Coherency Exception, Instruction  */
#define	EXCEPTION_FPE		15	/*  Floating point exception  */
/*  16..22: Unused  */
#define	EXCEPTION_WATCH		23	/*  Reference to WatchHi/WatchLo address  */
/*  24..30: Unused  */
#define	EXCEPTION_VCED		31	/*  Virtual Coherency Exception, Data  */


#define	NGPRS		32			/*  General purpose registers  */
#define	NFPUREGS	32			/*  Floating point registers  */

#define	GPR_ZERO	0		/*  zero  */
#define	GPR_AT		1		/*  at  */
#define	GPR_V0		2		/*  v0  */
#define	GPR_V1		3		/*  v1  */
#define	GPR_A0		4		/*  a0  */
#define	GPR_A1		5		/*  a1  */
#define	GPR_A2		6		/*  a2  */
#define	GPR_A3		7		/*  a3  */
#define	GPR_K0		26		/*  k0  */
#define	GPR_K1		27		/*  k1  */
#define	GPR_GP		28		/*  gp  */
#define	GPR_SP		29		/*  sp  */
#define	GPR_FP		30		/*  fp  */
#define	GPR_RA		31		/*  ra  */

/*  Meaning of delay_slot:  */
#define	NOT_DELAYED		0
#define	DELAYED			1
#define	TO_BE_DELAYED		2

#define	N_HI6			64
#define	N_SPECIAL		64
#define	N_REGIMM		32

#define	MAX_TICK_FUNCTIONS	8

#define	N_TRANSLATION_CACHE	3

struct cpu {
	int		cpu_id;
	int		byte_order;
	int		bootstrap_cpu_flag;
	int		running;

	struct cpu_type_def cpu_type;

	struct coproc	*coproc[N_COPROCS];

	void		(*md_interrupt)(struct cpu *, int irq_nr, int);

	/*  Special purpose registers:  */
	uint64_t	pc;
	uint64_t	pc_last;		/*  PC of last instruction   */
	uint64_t	hi;
	uint64_t	lo;

	int		mips16;			/*  non-zero if MIPS16 code is allowed  */
	uint16_t	mips16_extend;		/*  set on 'extend' instructions to the entire 16-bit extend instruction  */

	int		trace_tree_depth;
	int		instruction_delay;

	uint64_t	delay_jmpaddr;		/*  only used if delay_slot > 0  */
	int		delay_slot;
	int		nullify_next;		/*  set to 1 if next instruction
							is to be nullified  */

	int		show_trace_delay;	/*  0=normal, > 0 = delay until show_trace  */
	uint64_t	show_trace_addr;

	long		time_since_intr_enabling;
	uint64_t	old_status;

	int		r10k_cache_disable_TODO;	/*  TODO: remove this once cache functions correctly  */

	int		bintrans_last_was_jump;
	int		last_was_jumptoself;
	int		jump_to_self_reg;

#ifdef MFHILO_DELAY
	int		mfhi_delay;	/*  instructions left since last mfhi  */
	int		mflo_delay;	/*  instructions left since last mflo  */
#endif

	int		rmw;		/*  Read-Modify-Write  */
	uint64_t	rmw_addr;		/*  Address of rmw modification  */
	int		rmw_len;		/*  Length of rmw modification  */

	/*  General purpose registers:  */
	uint64_t	gpr[NGPRS];

	/*
	 *  TODO:  The R5900 has 128-bit registers. I'm not really
	 *  sure whether they are used a lot or not, at least with
	 *  code produced with gcc they are not.  An important case
	 *  however is lq and sq (load and store of 128-bit values).
	 *  These "upper halves" of R5900 quadwords can be used in
	 *  those cases.
	 *
	 *  TODO:  Generalize this.
	 */
	uint64_t	gpr_quadhi[NGPRS];

	/*
	 *  The translation_cached stuff is used to speed up the
	 *  most recent lookups into the TLB.  Whenever the TLB is
	 *  written to, translation_cached[] must be filled with zeros.
	 */
	int		translation_cached_i;
	int		translation_cached[N_TRANSLATION_CACHE];
	uint64_t	translation_cached_vaddr_pfn[N_TRANSLATION_CACHE];
	uint64_t	translation_cached_paddr[N_TRANSLATION_CACHE];

	struct memory	*mem;

	/*  Data and Instruction caches:  */
	unsigned char	*cache[2];
	int		cache_size[2];
	int		cache_valid[2];
	uint64_t	last_cached_address[2];

	/*
	 *  Hardware devices, run every x clock ticks/instructions.
	 */
	int		n_tick_entries;
	int		ticks_till_next[MAX_TICK_FUNCTIONS];
	int		ticks_reset_value[MAX_TICK_FUNCTIONS];
	void		(*tick_func[MAX_TICK_FUNCTIONS])(struct cpu *, void *);
	void		*tick_extra[MAX_TICK_FUNCTIONS];

	/*
	 *  Statistics:
	 */
	long		stats_opcode[N_HI6];
	long		stats__special[N_SPECIAL];
	long		stats__regimm[N_REGIMM];
	long		stats__special2[N_SPECIAL];
};

#define	CACHE_DATA			0
#define	CACHE_INSTRUCTION		1
#define	CACHE_NONE			2

#define	CACHE_FLAGS_MASK		0x3

#define	NO_EXCEPTIONS			8
#define	PHYSICAL			16

#define	EMUL_LITTLE_ENDIAN		0
#define	EMUL_BIG_ENDIAN			1

#define	DEFAULT_NCPUS			1


/*  Opcodes:  (see page 191 in MIPS_IV_Instruction_Set_v3.2.pdf)  */

#define	HI6_NAMES	{	\
	"special", "regimm", "j", "jal", "beq", "bne", "blez", "bgtz", 			/*  0x00 - 0x07  */	\
	"addi", "addiu", "slti", "sltiu", "andi", "ori", "xori", "lui",			/*  0x08 - 0x0f  */	\
	"cop0", "cop1", "cop2", "cop3", "beql", "bnel", "blezl", "bgtzl",		/*  0x10 - 0x17  */	\
	"daddi", "daddiu", "ldl", "ldr", "special2", "opcode_1d", "lq_mdmx", "sq",	/*  0x18 - 0x1f  */	\
	"lb", "lh", "lwl", "lw", "lbu", "lhu", "lwr", "lwu",				/*  0x20 - 0x27  */	\
	"sb", "sh", "swl", "sw", "sdl", "sdr", "swr", "cache",				/*  0x28 - 0x2f  */	\
	"ll", "lwc1", "lwc2", "lwc3", "lld", "ldc1", "ldc2", "ld",			/*  0x30 - 0x37  */	\
	"sc", "swc1", "swc2", "swc3", "scd", "sdc1", "opcode_3e", "sd"			/*  0x38 - 0x3f  */	}

#define	REGIMM_NAMES	{	\
	"bltz", "bgez", "bltzl", "bgezl", "regimm_04", "regimm_05", "regimm_06", "regimm_07",			/*  0x00 - 0x07  */	\
	"regimm_08", "regimm_09", "regimm_0a", "regimm_0b", "regimm_0c", "regimm_0d", "regimm_0e", "regimm_0f",	/*  0x08 - 0x0f  */	\
	"regimm_10", "regimm_11", "regimm_12", "regimm_13", "regimm_14", "regimm_15", "regimm_16", "regimm_17",	/*  0x10 - 0x17  */	\
	"regimm_18", "regimm_19", "regimm_1a", "regimm_1b", "regimm_1c", "regimm_1d", "regimm_1e", "regimm_1f" 	/*  0x18 - 0x1f  */ }

#define	SPECIAL_NAMES	{	\
	"sll", "special_01", "srl", "sra", "sllv", "special_05", "srlv", "srav",	/*  0x00 - 0x07  */	\
	"jr", "jalr", "movz", "movn", "syscall", "break", "special_0e", "sync",		/*  0x08 - 0x0f  */	\
	"mfhi", "mthi", "mflo", "mtlo", "dsllv", "special_15", "dsrlv", "dsrav",	/*  0x10 - 0x17  */	\
	"mult", "multu", "div", "divu", "dmult", "dmultu", "ddiv", "ddivu",		/*  0x18 - 0x1f  */	\
	"add", "addu", "sub", "subu", "and", "or", "xor", "nor",			/*  0x20 - 0x27  */	\
	"mfsa", "mtsa", "slt", "sltu", "special_2c", "daddu", "special_2e", "dsubu",  /*  0x28 - 0x2f  */	\
	"special_30", "special_31", "special_32", "special_33", "teq", "special_35", "special_36", "special_37", /*  0x30 - 0x37  */	\
	"dsll", "special_39", "dsrl", "dsra", "dsll32", "special_3d", "dsrl32", "dsra32"/*  0x38 - 0x3f  */	}

#define	SPECIAL2_NAMES	{	\
	"madd",        "maddu",       "mul",         "special2_03", "msub",        "msubu",       "special2_06", "special2_07", /*  0x00 - 0x07  */	\
	"mov_xxx",     "pmfhi_lo",    "special2_0a", "special2_0b", "special2_0c", "special2_0d", "special2_0e", "special2_0f",	/*  0x08 - 0x0f  */	\
	"special2_10", "special2_11", "special2_12", "special2_13", "special2_14", "special2_15", "special2_16", "special2_17", /*  0x10 - 0x17  */	\
	"special2_18", "special2_19", "special2_1a", "special2_1b", "special2_1c", "special2_1d", "special2_1e", "special2_1f",	/*  0x18 - 0x1f  */	\
	"clz",         "clo",         "special2_22", "special2_23", "dclz",        "dclo",        "special2_26", "special2_27", /*  0x20 - 0x27  */	\
	"special2_28", "por", 	      "special2_2a", "special2_2b", "special2_2c", "special2_2d", "special2_2e", "special2_2f",	/*  0x28 - 0x2f  */	\
	"special2_30", "special2_31", "special2_32", "special2_33", "special2_34", "special2_35", "special2_36", "special2_37", /*  0x30 - 0x37  */	\
	"special2_38", "special2_39", "special2_3a", "special2_3b", "special2_3c", "special2_3d", "special2_3e", "sdbbp"	/*  0x38 - 0x3f  */  }

#define	HI6_SPECIAL			0x00	/*  000000  */
#define	    SPECIAL_SLL			    0x00    /*  000000  */	/*  MIPS I  */
/*					    0x01	000001  */
#define	    SPECIAL_SRL			    0x02    /*	000010  */	/*  MIPS I  */
#define	    SPECIAL_SRA			    0x03    /*  000011  */	/*  MIPS I  */
#define	    SPECIAL_SLLV		    0x04    /*  000100  */	/*  MIPS I  */
/*					    0x05	000101  */
#define	    SPECIAL_SRLV		    0x06    /*  000110  */
#define	    SPECIAL_SRAV		    0x07    /*  000111  */	/*  MIPS I  */
#define	    SPECIAL_JR			    0x08    /*  001000  */	/*  MIPS I  */
#define	    SPECIAL_JALR		    0x09    /*  001001  */	/*  MIPS I  */
#define	    SPECIAL_MOVZ		    0x0a    /*	001010  */	/*  MIPS IV  */
#define	    SPECIAL_MOVN		    0x0b    /*	001011  */	/*  MIPS IV  */
#define	    SPECIAL_SYSCALL		    0x0c    /*	001100  */	/*  MIPS I  */
#define	    SPECIAL_BREAK		    0x0d    /*	001101  */	/*  MIPS I  */
/*					    0x0e	001110  */
#define	    SPECIAL_SYNC		    0x0f    /*	001111  */	/*  MIPS II  */
#define	    SPECIAL_MFHI		    0x10    /*  010000  */	/*  MIPS I  */
#define	    SPECIAL_MTHI		    0x11    /*	010001  */	/*  MIPS I  */
#define	    SPECIAL_MFLO		    0x12    /*  010010  */	/*  MIPS I  */
#define	    SPECIAL_MTLO		    0x13    /*	010011  */	/*  MIPS I  */
#define	    SPECIAL_DSLLV		    0x14    /*	010100  */
/*					    0x15	010101  */
#define	    SPECIAL_DSRLV		    0x16    /*  010110  */	/*  MIPS III  */
#define	    SPECIAL_DSRAV		    0x17    /*  010111  */	/*  MIPS III  */
#define	    SPECIAL_MULT		    0x18    /*  011000  */	/*  MIPS I  */
#define	    SPECIAL_MULTU		    0x19    /*	011001  */	/*  MIPS I  */
#define	    SPECIAL_DIV			    0x1a    /*  011010  */	/*  MIPS I  */
#define	    SPECIAL_DIVU		    0x1b    /*	011011  */	/*  MIPS I  */
#define	    SPECIAL_DMULT		    0x1c    /*  011100  */	/*  MIPS III  */
#define	    SPECIAL_DMULTU		    0x1d    /*  011101  */	/*  MIPS III  */
#define	    SPECIAL_DDIV		    0x1e    /*  011110  */	/*  MIPS III  */
#define	    SPECIAL_DDIVU		    0x1f    /*  011111  */	/*  MIPS III  */
#define	    SPECIAL_ADD			    0x20    /*	100000  */	/*  MIPS I  */
#define	    SPECIAL_ADDU		    0x21    /*  100001  */	/*  MIPS I  */
#define	    SPECIAL_SUB			    0x22    /*  100010  */	/*  MIPS I  */
#define	    SPECIAL_SUBU		    0x23    /*  100011  */	/*  MIPS I  */
#define	    SPECIAL_AND			    0x24    /*  100100  */	/*  MIPS I  */
#define	    SPECIAL_OR			    0x25    /*  100101  */	/*  MIPS I  */
#define	    SPECIAL_XOR			    0x26    /*  100110  */	/*  MIPS I  */
#define	    SPECIAL_NOR			    0x27    /*  100111  */	/*  MIPS I  */
#define	    SPECIAL_MFSA		    0x28    /*  101000  */  	/*  Undocumented R5900 ?  */
#define	    SPECIAL_MTSA		    0x29    /*  101001  */  	/*  Undocumented R5900 ?  */
#define	    SPECIAL_SLT			    0x2a    /*  101010  */	/*  MIPS I  */
#define	    SPECIAL_SLTU		    0x2b    /*  101011  */	/*  MIPS I  */
#define	    SPECIAL_DADD		    0x2c    /*  101100  */	/*  MIPS III  */
#define	    SPECIAL_DADDU		    0x2d    /*	101101  */	/*  MIPS III  */
#define	    SPECIAL_DSUB		    0x2e    /*	101110  */
#define	    SPECIAL_DSUBU		    0x2f    /*	101111  */	/*  MIPS III  */
#define	    SPECIAL_TGE			    0x30    /*	110000  */
#define	    SPECIAL_TGEU		    0x31    /*	110001  */
#define	    SPECIAL_TLT			    0x32    /*	110010  */
#define	    SPECIAL_TLTU		    0x33    /*	110011  */
#define	    SPECIAL_TEQ			    0x34    /*	110100  */
/*					    0x35	110101  */
#define	    SPECIAL_TNE			    0x36    /*	110110  */
/*					    0x37	110111  */
#define	    SPECIAL_DSLL		    0x38    /*  111000  */	/*  MIPS III  */
/*					    0x39	111001  */
#define	    SPECIAL_DSRL		    0x3a    /*  111010  */	/*  MIPS III  */
#define	    SPECIAL_DSRA		    0x3b    /*  111011  */	/*  MIPS III  */
#define	    SPECIAL_DSLL32		    0x3c    /*  111100  */	/*  MIPS III  */
/*					    0x3d	111101  */
#define	    SPECIAL_DSRL32		    0x3e    /*  111110  */	/*  MIPS III  */
#define	    SPECIAL_DSRA32		    0x3f    /*  111111  */	/*  MIPS III  */

#define	HI6_REGIMM			0x01	/*  000001  */
#define	    REGIMM_BLTZ			    0x00    /*  00000  */	/*  MIPS I  */
#define	    REGIMM_BGEZ			    0x01    /*  00001  */	/*  MIPS I  */
#define	    REGIMM_BLTZL		    0x02    /*  00010  */	/*  MIPS II  */
#define	    REGIMM_BGEZL		    0x03    /*  00011  */	/*  MIPS II  */
#define	    REGIMM_BLTZAL		    0x10    /*  10000  */
#define	    REGIMM_BGEZAL		    0x11    /*  10001  */
#define	    REGIMM_BLTZALL		    0x12    /*  10010  */
#define	    REGIMM_BGEZALL		    0x13    /*  10011  */
/*  regimm ...............  */

#define	HI6_J				0x02	/*  000010  */	/*  MIPS I  */
#define	HI6_JAL				0x03	/*  000011  */	/*  MIPS I  */
#define	HI6_BEQ				0x04	/*  000100  */	/*  MIPS I  */
#define	HI6_BNE				0x05	/*  000101  */
#define	HI6_BLEZ			0x06	/*  000110  */	/*  MIPS I  */
#define	HI6_BGTZ			0x07	/*  000111  */	/*  MIPS I  */
#define	HI6_ADDI			0x08	/*  001000  */	/*  MIPS I  */
#define	HI6_ADDIU			0x09	/*  001001  */	/*  MIPS I  */
#define	HI6_SLTI			0x0a	/*  001010  */	/*  MIPS I  */
#define	HI6_SLTIU			0x0b	/*  001011  */	/*  MIPS I  */
#define	HI6_ANDI			0x0c	/*  001100  */	/*  MIPS I  */
#define	HI6_ORI				0x0d	/*  001101  */	/*  MIPS I  */
#define	HI6_XORI			0x0e    /*  001110  */	/*  MIPS I  */
#define	HI6_LUI				0x0f	/*  001111  */	/*  MIPS I  */
#define	HI6_COP0			0x10	/*  010000  */
#define	    COPz_MFCz			    0x00    /*  00000  */
#define	    COPz_DMFCz			    0x01    /*  00001  */
#define	    COPz_MTCz			    0x04    /*  00100  */
#define	    COPz_DMTCz			    0x05    /*  00101  */
/*  COP1 fmt codes = bits 25..21 (only if COP1):  */
#define	    COPz_CFCz			    0x02    /*  00010  */  /*  MIPS I  */
#define	    COPz_CTCz			    0x06    /*  00110  */  /*  MIPS I  */
/*  COP0 opcodes = bits 4..0 (only if COP0 and CO=1):  */
#define	    COP0_TLBR			    0x01    /*  00001  */
#define	    COP0_TLBWI			    0x02    /*  00010  */
#define	    COP0_TLBWR			    0x06    /*  00110  */
#define	    COP0_TLBP			    0x08    /*  01000  */
#define	    COP0_RFE			    0x10    /*  10000  */
#define	    COP0_ERET			    0x18    /*  11000  */
#define	HI6_COP1			0x11	/*  010001  */
#define	HI6_COP2			0x12	/*  010010  */
#define	HI6_COP3			0x13	/*  010011  */
#define	HI6_BEQL			0x14	/*  010100  */	/*  MIPS II  */
#define	HI6_BNEL			0x15	/*  010101  */
#define	HI6_BLEZL			0x16	/*  010110  */	/*  MIPS II  */
#define	HI6_BGTZL			0x17	/*  010111  */	/*  MIPS II  */
#define	HI6_DADDI			0x18	/*  011000  */	/*  MIPS III  */
#define	HI6_DADDIU			0x19	/*  011001  */	/*  MIPS III  */
#define	HI6_LDL				0x1a	/*  011010  */	/*  MIPS III  */
#define	HI6_LDR				0x1b	/*  011011  */	/*  MIPS III  */
#define	HI6_SPECIAL2			0x1c	/*  011100  */
#define	    SPECIAL2_MADD		    0x00    /*  000000  */  /*  MIPS32 (?) TODO  */
#define	    SPECIAL2_MADDU		    0x01    /*  000001  */  /*  MIPS32 (?) TODO  */
#define	    SPECIAL2_MUL		    0x02    /*  000010  */  /*  MIPS32 (?) TODO  */
#define	    SPECIAL2_MSUB		    0x04    /*  000100  */  /*  MIPS32 (?) TODO  */
#define	    SPECIAL2_MSUBU		    0x05    /*  000001  */  /*  MIPS32 (?) TODO  */
#define	    SPECIAL2_MOV_XXX		    0x08    /*  001000  */  /*  Undocumented R5900 ?  */
#define	    SPECIAL2_PMFHI		    0x09    /*  001001  */  /*  Undocumented R5900 ?  */
#define	    SPECIAL2_CLZ		    0x20    /*  100100  */  /*  MIPS32 (?) TODO  */
#define	    SPECIAL2_CLO		    0x21    /*  100101  */  /*  MIPS32 (?) TODO  */
#define	    SPECIAL2_DCLZ		    0x24    /*  100100  */  /*  MIPS64 (?) TODO  */
#define	    SPECIAL2_DCLO		    0x25    /*  100101  */  /*  MIPS64 (?) TODO  */
#define	    SPECIAL2_POR		    0x29    /*  101001  */  /*  Undocumented R5900 ?  */
#define	    SPECIAL2_SDBBP		    0x3f    /*  111111  */  /*  EJTAG (?)  TODO  */
/*	JALX (TODO)			0x1d	    011101  */
#define	HI6_LQ_MDMX			0x1e	/*  011110  */	/*  lq on R5900, MDMX on others?  */
#define	HI6_SQ				0x1f	/*  011111  */	/*  R5900 ?  */
#define	HI6_LB				0x20	/*  100000  */	/*  MIPS I  */
#define	HI6_LH				0x21	/*  100001  */	/*  MIPS I  */
#define	HI6_LWL				0x22	/*  100010  */	/*  MIPS I  */
#define	HI6_LW				0x23	/*  100011  */	/*  MIPS I  */
#define	HI6_LBU				0x24	/*  100100  */	/*  MIPS I  */
#define	HI6_LHU				0x25	/*  100101  */	/*  MIPS I  */
#define	HI6_LWR				0x26	/*  100110  */	/*  MIPS I  */
#define	HI6_LWU				0x27	/*  100111  */	/*  MIPS III  */
#define	HI6_SB				0x28	/*  101000  */	/*  MIPS I  */
#define	HI6_SH				0x29	/*  101001  */	/*  MIPS I  */
#define	HI6_SWL				0x2a	/*  101010  */	/*  MIPS I  */
#define	HI6_SW				0x2b	/*  101011  */	/*  MIPS I  */
#define	HI6_SDL				0x2c	/*  101100  */	/*  MIPS III  */
#define	HI6_SDR				0x2d	/*  101101  */	/*  MIPS III  */
#define	HI6_SWR				0x2e	/*  101110  */	/*  MIPS I  */
#define	HI6_CACHE			0x2f	/*  101111  */	/*  ??? R4000  */
#define	HI6_LL				0x30	/*  110000  */	/*  MIPS II  */
#define	HI6_LWC1			0x31	/*  110001  */	/*  MIPS I  */
#define	HI6_LWC2			0x32	/*  110010  */	/*  MIPS I  */
#define	HI6_LWC3			0x33	/*  110011  */	/*  MIPS I  */
#define	HI6_LLD				0x34	/*  110100  */	/*  MIPS III  */
#define	HI6_LDC1			0x35	/*  110101  */	/*  MIPS II  */
#define	HI6_LDC2			0x36	/*  110110  */	/*  MIPS II  */
#define	HI6_LD				0x37	/*  110111  */	/*  MIPS III  */
#define	HI6_SC				0x38	/*  111000  */	/*  MIPS II  */
#define	HI6_SWC1			0x39	/*  111001  */	/*  MIPS I  */
#define	HI6_SWC2			0x3a	/*  111010  */	/*  MIPS I  */
#define	HI6_SWC3			0x3b	/*  111011  */	/*  MIPS I  */
#define	HI6_SCD				0x3c	/*  111100  */	/*  MIPS III  */
#define	HI6_SDC1			0x3d	/*  111101  */  /*  ???  */
/*					0x3e	    111110  */
#define	HI6_SD				0x3f	/*  111111  */	/*  MIPS III  */


/*  main.c:  */
void debug(char *fmt, ...);
void fatal(char *fmt, ...);


/*  arcbios.c:  */
uint32_t arcbios_addchild_manual(uint32_t class, uint32_t type, uint32_t flags, uint16_t version,
	uint16_t revision, uint32_t key, uint32_t affinitymask, char *identifier, uint32_t parent);
void arcbios_emul(struct cpu *cpu);


/*  coproc.c:  */
struct coproc *coproc_new(struct cpu *cpu, int coproc_nr);
void coproc_register_read(struct cpu *cpu,
	struct coproc *cp, int reg_nr, uint64_t *ptr);
void coproc_register_write(struct cpu *cpu,
	struct coproc *cp, int reg_nr, uint64_t *ptr);
void coproc_function(struct cpu *cpu, struct coproc *cp, uint32_t function);


/*  cpu.c:  */
struct cpu *cpu_new(struct memory *mem, int cpu_id, char *cpu_type_name);
void cpu_add_tickfunction(struct cpu *cpu, void (*func)(struct cpu *, void *), void *extra, int clockshift);
void cpu_register_dump(struct cpu *cpu);
int cpu_interrupt(struct cpu *cpu, int irq_nr);
int cpu_interrupt_ack(struct cpu *cpu, int irq_nr);
void cpu_exception(struct cpu *cpu, int exccode, int tlb, uint64_t vaddr,
        uint64_t pagemask, int coproc_nr, uint64_t vaddr_vpn2, int vaddr_asid, int x_64);
int cpu_run(struct cpu **cpus, int ncpus);


/*  dec_prom.c:  */
void decstation_prom_emul(struct cpu *cpu);


/*  emul.c:  */
void emul(void);


/*  file.c:  */
void file_load(struct memory *mem, char *filename, struct cpu *cpu);


/*  machine.c:  */
unsigned char read_char_from_memory(struct cpu *cpu, int regbase, int offset);
void dump_mem_string(struct cpu *cpu, uint64_t addr);
void store_string(uint64_t addr, char *s);
void store_32bit_word(uint64_t addr, uint32_t data32);
uint32_t load_32bit_word(uint64_t addr);
void machine_init(struct memory *mem);


/*  memory.c:  */
#if 0
struct memory *memory_new(int bits_per_pagetable, int bits_per_memblock, size_t physical_max, int max_bits);
int memory_points_to_string(struct cpu *cpu, struct memory *mem, uint64_t addr, int min_string_length);
char *memory_conv_to_string(struct cpu *cpu, struct memory *mem, uint64_t addr, char *buf, int bufsize);
int memory_rw(struct cpu *cpu, struct memory *mem, uint64_t vaddr, unsigned char *data, size_t len, int writeflag, int cache);
void memory_device_register(struct memory *mem, const char *, uint64_t baseaddr, uint64_t len, int (*f)(
	struct cpu *,struct memory *,uint64_t,unsigned char *,size_t,int,void *), void *);
#endif


/*  mips16.c:  */
int mips16_to_32(struct cpu *cpu, unsigned char *instr16, unsigned char *instr);


/*  ps2_bios.c:  */
void playstation2_sifbios_emul(struct cpu *cpu);


/*  symbol.c:  */
int get_symbol_addr(char *symbol, uint64_t *addr);
char *get_symbol_name(uint64_t addr, int *offset);
void add_symbol_name(uint64_t addr, uint64_t len, char *name, int type);
void symbol_readfile(char *fname);
void symbol_recalc_sizes(void);
void symbol_init(void);


/*  useremul.c:  */
#define	USERLAND_NONE		0
#define	USERLAND_NETBSD_PMAX	1
#define	USERLAND_ULTRIX_PMAX	2
#define	USERLAND_IRIX		3
void useremul_init(struct cpu *, struct memory *, int, char **);
void useremul_syscall(struct cpu *cpu, uint32_t code);


/*  x11.c:  */
/*  Framebuffer windows:  */
struct fb_window {
#ifdef WITH_X11
	/*  x11_fb_winxsize > 0 for a valid fb_window  */
	int		x11_fb_winxsize, x11_fb_winysize;
	int		scaledown;
	Display		*x11_display;
	Window		x11_fb_window;
	GC		x11_fb_gc;

	XImage		*fb_ximage;
	unsigned char	*ximage_data;

	XImage		*cursor_ximage;
	int		cursor_x;
	int		cursor_y;
	int		cursor_xsize;
	int		cursor_ysize;
	int		cursor_on;
	int		OLD_cursor_x;
	int		OLD_cursor_y;
	int		OLD_cursor_xsize;
	int		OLD_cursor_ysize;
	int		OLD_cursor_on;

#else
	int		dummy;
#endif
};
void x11_redraw(void);
void x11_putpixel_fb(int, int x, int y, int color);
#ifdef WITH_X11
void x11_putimage_fb(int);
#endif
void x11_init(void);
struct fb_window *x11_fb_init(int xsize, int ysize, char *name, int scaledown);
void x11_check_event(void);


#endif	/*  MISC_H  */

