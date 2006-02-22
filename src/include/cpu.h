#ifndef	CPU_H
#define	CPU_H

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
 *  $Id: cpu.h,v 1.67 2006-02-22 20:09:09 debug Exp $
 *
 *  CPU-related definitions.
 */


#include <sys/types.h>
#include <inttypes.h>
#include <sys/time.h>

/*  This is needed for undefining 'mips', 'ppc' etc. on weird systems:  */
#include "../../config.h"

/*
 *  Dyntrans misc declarations, used throughout the dyntrans code.
 */
#define DYNTRANS_MISC_DECLARATIONS(arch,ARCH,addrtype)  struct \
	arch ## _instr_call {					\
		void	(*f)(struct cpu *, struct arch ## _instr_call *); \
		size_t	arg[ARCH ## _N_IC_ARGS];			\
	};								\
									\
	/*  Translation cache struct for each physical page:  */	\
	struct arch ## _tc_physpage {					\
		struct arch ## _instr_call ics[ARCH ## _IC_ENTRIES_PER_PAGE+1];\
		uint32_t	next_ofs;	/*  (0 for end of chain)  */ \
		int		flags;					\
		addrtype	physaddr;				\
	};								\
									\
	struct arch ## _vpg_tlb_entry {					\
		uint8_t		valid;					\
		uint8_t		writeflag;				\
		addrtype	vaddr_page;				\
		addrtype	paddr_page;				\
		unsigned char	*host_page;				\
		int64_t		timestamp;				\
	};

#define	DYNTRANS_MISC64_DECLARATIONS(arch,ARCH,tlbindextype)		\
	struct arch ## _l3_64_table {					\
		unsigned char	*host_load[1 << ARCH ## _L3N];		\
		unsigned char	*host_store[1 << ARCH ## _L3N];		\
		uint64_t	phys_addr[1 << ARCH ## _L3N];		\
		tlbindextype	vaddr_to_tlbindex[1 << ARCH ## _L3N];	\
		struct arch ## _tc_physpage *phys_page[1 << ARCH ## _L3N]; \
	};								\
	struct arch ## _l2_64_table {					\
		struct arch ## _l3_64_table	*l3[1 << ARCH ## _L2N];	\
	};

/*
 *  Dyntrans "Instruction Translation Cache":
 *
 *  cur_physpage is a pointer to the current physpage. (It _HAPPENS_ to
 *  be the same as cur_ic_page, because all the instrcalls should be placed
 *  first in the physpage struct!)
 *
 *  cur_ic_page is a pointer to an array of xxx_IC_ENTRIES_PER_PAGE
 *  instruction call entries.
 *
 *  next_ic points to the next such instruction call to be executed.
 *
 *  combination_check, when set to non-NULL, is executed automatically after
 *  an instruction has been translated. (It check for combinations of
 *  instructions; low_addr is the offset of the translated instruction in the
 *  current page, NOT shifted right.)
 */
#define DYNTRANS_ITC(arch)	struct arch ## _tc_physpage *cur_physpage;  \
				struct arch ## _instr_call  *cur_ic_page;   \
				struct arch ## _instr_call  *next_ic;       \
				void (*combination_check)(struct cpu *,     \
				    struct arch ## _instr_call *, int low_addr);

/*
 *  Virtual -> physical -> host address translation TLB entries:
 *  ------------------------------------------------------------
 *
 *  Regardless of whether 32-bit or 64-bit address translation is used, the
 *  same TLB entry structure is used.
 */
#define	VPH_TLBS(arch,ARCH)						\
	struct arch ## _vpg_tlb_entry					\
	    vph_tlb_entry[ARCH ## _MAX_VPH_TLB_ENTRIES];

/*
 *  32-bit dyntrans emulated Virtual -> physical -> host address translation:
 *  -------------------------------------------------------------------------
 *
 *  This stuff assumes that 4 KB pages are used. 20 bits to select a page
 *  means just 1 M entries needed. This is small enough that a couple of
 *  full-size tables can fit in virtual memory on modern hosts (both 32-bit
 *  and 64-bit hosts). :-)
 *
 *  Usage: e.g. VPH32(arm,ARM,uint32_t,uint8_t)
 *           or VPH32(sparc,SPARC,uint64_t,uint16_t)
 *
 *  The vph_tlb_entry entries are cpu dependent tlb entries.
 *
 *  The host_load and host_store entries point to host pages; the phys_addr
 *  entries are uint32_t or uint64_t (emulated physical addresses).
 *
 *  phys_page points to translation cache physpages.
 *
 *  phystranslation is a bitmap which tells us whether a physical page has
 *  a code translation.
 *
 *  vaddr_to_tlbindex is a virtual address to tlb index hint table.
 *  The values in this array are the tlb index plus 1, so a value of, say,
 *  3 means tlb index 2. A value of 0 would mean a tlb index of -1, which
 *  is not a valid index. (I.e. no hit.)
 */
#define	N_VPH32_ENTRIES		1048576
#define	VPH32(arch,ARCH,paddrtype,tlbindextype)				\
	unsigned char		*host_load[N_VPH32_ENTRIES];		\
	unsigned char		*host_store[N_VPH32_ENTRIES];		\
	paddrtype		phys_addr[N_VPH32_ENTRIES];		\
	struct arch ## _tc_physpage  *phys_page[N_VPH32_ENTRIES];	\
	uint32_t		phystranslation[N_VPH32_ENTRIES/32];	\
	tlbindextype		vaddr_to_tlbindex[N_VPH32_ENTRIES];

/*
 *  64-bit dyntrans emulated Virtual -> physical -> host address translation:
 *  -------------------------------------------------------------------------
 *
 *  Usage: e.g. VPH64(alpha,ALPHA,uint8_t)
 *           or VPH64(sparc,SPARC,uint16_t)
 *
 *  l1_64 is an array containing poiners to l2 tables.
 *
 *  l2_64_dummy is a pointer to a "dummy l2 table". Instead of having NULL
 *  pointers in l1_64 for unused slots, a pointer to the dummy table can be
 *  used.
 */
#define	DYNTRANS_L1N		17
#define	VPH64(arch,ARCH,tlbindextype)					\
	struct arch ## _l3_64_table	*l3_64_dummy;			\
	struct arch ## _l2_64_table	*l2_64_dummy;			\
	struct arch ## _l2_64_table	*l1_64[1 << DYNTRANS_L1N];


/*  Include all CPUs' header files here:  */
#include "cpu_alpha.h"
#include "cpu_arm.h"
#include "cpu_avr.h"
#include "cpu_hppa.h"
#include "cpu_i960.h"
#include "cpu_ia64.h"
#include "cpu_m68k.h"
#include "cpu_mips.h"
#include "cpu_ppc.h"
#include "cpu_sh.h"
#include "cpu_sparc.h"
#include "cpu_x86.h"

struct cpu;
struct emul;
struct machine;
struct memory;


struct cpu_family {
	struct cpu_family	*next;
	int			arch;

	/*  These are filled in by each CPU family's init function:  */
	char			*name;
	int			(*cpu_new)(struct cpu *cpu, struct memory *mem,
				    struct machine *machine,
				    int cpu_id, char *cpu_type_name);
	void			(*list_available_types)(void);
	void			(*register_match)(struct machine *m,
				    char *name, int writeflag,
				    uint64_t *valuep, int *match_register);
	int			(*disassemble_instr)(struct cpu *cpu,
				    unsigned char *instr, int running,
				    uint64_t dumpaddr, int bintrans);
	void			(*register_dump)(struct cpu *cpu,
				    int gprs, int coprocs);
	int			(*run)(struct emul *emul,
				    struct machine *machine);
	void			(*dumpinfo)(struct cpu *cpu);
	void			(*show_full_statistics)(struct machine *m);
	void			(*tlbdump)(struct machine *m, int x,
				    int rawflag);
	int			(*interrupt)(struct cpu *cpu, uint64_t irq_nr);
	int			(*interrupt_ack)(struct cpu *cpu,
				    uint64_t irq_nr);
	void			(*functioncall_trace)(struct cpu *,
				    uint64_t f, int n_args);
};


/*
 *  More dyntrans stuff:
 *
 *  The translation cache begins with N_BASE_TABLE_ENTRIES uint32_t offsets
 *  into the cache, for possible translation cache structs for physical pages.
 */

/*  Physpage flags:  */
#define	TRANSLATIONS			1
#define	COMBINATIONS			2

#define	N_SAFE_DYNTRANS_LIMIT_SHIFT	14
#define	N_SAFE_DYNTRANS_LIMIT	((1 << (N_SAFE_DYNTRANS_LIMIT_SHIFT - 1)) - 1)

#define	DYNTRANS_CACHE_SIZE		(16*1048576)
#define	DYNTRANS_CACHE_MARGIN		300000

#define	N_BASE_TABLE_ENTRIES		32768
#define	PAGENR_TO_TABLE_INDEX(a)	((a) & (N_BASE_TABLE_ENTRIES-1))


/*
 *  The generic CPU struct:
 */

struct cpu {
	/*  Pointer back to the machine this CPU is in:  */
	struct machine	*machine;

	int		byte_order;
	int		running;
	int		dead;
	int		bootstrap_cpu_flag;
	int		cpu_id;
	int		is_32bit;	/*  0 for 64-bit, 1 for 32-bit  */
	char		*name;

	struct memory	*mem;
	int		(*memory_rw)(struct cpu *cpu,
			    struct memory *mem, uint64_t vaddr,
			    unsigned char *data, size_t len,
			    int writeflag, int cache_flags);
	int		(*translate_address)(struct cpu *, uint64_t vaddr,
			    uint64_t *return_addr, int flags);
	void		(*update_translation_table)(struct cpu *,
			    uint64_t vaddr_page, unsigned char *host_page,
			    int writeflag, uint64_t paddr_page);
	void		(*invalidate_translation_caches)(struct cpu *,
			    uint64_t paddr, int flags);
	void		(*invalidate_code_translation)(struct cpu *,
			    uint64_t paddr, int flags);
	void		(*useremul_syscall)(struct cpu *cpu, uint32_t code);

	uint64_t	pc;

#ifdef TRACE_NULL_CRASHES
	/*  TODO: remove this, it's MIPS only  */
	int		trace_null_index;
	uint64_t	trace_null_addr[TRACE_NULL_N_ENTRIES];
#endif  

	int		trace_tree_depth;

	/*
	 *  Dynamic translation:
	 */
	int		running_translated;
	int		n_translated_instrs;
	unsigned char	*translation_cache;
	size_t		translation_cache_cur_ofs;

	/*
	 *  CPU-family dependent:
	 */
	union {
		struct alpha_cpu   alpha;
		struct arm_cpu     arm;
		struct avr_cpu     avr;
		struct hppa_cpu    hppa;
		struct i960_cpu    i960;
		struct ia64_cpu    ia64;
		struct m68k_cpu    m68k;
		struct mips_cpu    mips;
		struct ppc_cpu     ppc;
		struct sh_cpu      sh;
		struct sparc_cpu   sparc;
		struct x86_cpu     x86;
	} cd;
};


/*  cpu.c:  */
struct cpu *cpu_new(struct memory *mem, struct machine *machine,
        int cpu_id, char *cpu_type_name);
void cpu_show_full_statistics(struct machine *m);
void cpu_tlbdump(struct machine *m, int x, int rawflag);
void cpu_register_match(struct machine *m, char *name, 
	int writeflag, uint64_t *valuep, int *match_register);
void cpu_register_dump(struct machine *m, struct cpu *cpu,
	int gprs, int coprocs);
int cpu_disassemble_instr(struct machine *m, struct cpu *cpu,
	unsigned char *instr, int running, uint64_t addr, int bintrans);
int cpu_interrupt(struct cpu *cpu, uint64_t irq_nr);
int cpu_interrupt_ack(struct cpu *cpu, uint64_t irq_nr);
void cpu_functioncall_trace(struct cpu *cpu, uint64_t f);
void cpu_functioncall_trace_return(struct cpu *cpu);
void cpu_create_or_reset_tc(struct cpu *cpu);
void cpu_run_init(struct machine *machine);
int cpu_run(struct emul *emul, struct machine *machine);
void cpu_run_deinit(struct machine *machine);
void cpu_dumpinfo(struct machine *m, struct cpu *cpu);
void cpu_list_available_types(void);
void cpu_show_cycles(struct machine *machine, int forced);
struct cpu_family *cpu_family_ptr_by_number(int arch);
void cpu_init(void);


#define	JUST_MARK_AS_NON_WRITABLE	1
#define	INVALIDATE_ALL			2
#define	INVALIDATE_PADDR		4
#define	INVALIDATE_VADDR		8
#define	INVALIDATE_VADDR_UPPER4		16	/*  useful for PPC emulation  */

#define	TLB_CODE			0x02


#define CPU_FAMILY_INIT(n,s)	int n ## _cpu_family_init(		\
	struct cpu_family *fp) {					\
	/*  Fill in the cpu_family struct with valid data for this arch.  */ \
	fp->name = s;							\
	fp->cpu_new = n ## _cpu_new;					\
	fp->list_available_types = n ## _cpu_list_available_types;	\
	fp->register_match = n ## _cpu_register_match;			\
	fp->disassemble_instr = n ## _cpu_disassemble_instr;		\
	fp->register_dump = n ## _cpu_register_dump;			\
	fp->run = n ## _cpu_run;					\
	fp->dumpinfo = n ## _cpu_dumpinfo;				\
	fp->interrupt = n ## _cpu_interrupt; 				\
	fp->interrupt_ack = n ## _cpu_interrupt_ack;			\
	fp->functioncall_trace = n ## _cpu_functioncall_trace;		\
	return 1;							\
	}

#define CPU_OLD_FAMILY_INIT(n,s)	int n ## _cpu_family_init(	\
	struct cpu_family *fp) {					\
	/*  Fill in the cpu_family struct with valid data for this arch.  */ \
	fp->name = s;							\
	fp->cpu_new = n ## _cpu_new;					\
	fp->list_available_types = n ## _cpu_list_available_types;	\
	fp->register_match = n ## _cpu_register_match;			\
	fp->disassemble_instr = n ## _cpu_disassemble_instr;		\
	fp->register_dump = n ## _cpu_register_dump;			\
	fp->run = n ## _OLD_cpu_run;					\
	fp->dumpinfo = n ## _cpu_dumpinfo;				\
	fp->show_full_statistics = n ## _cpu_show_full_statistics;	\
	fp->tlbdump = n ## _cpu_tlbdump;				\
	fp->interrupt = n ## _cpu_interrupt; 				\
	fp->interrupt_ack = n ## _cpu_interrupt_ack;			\
	fp->functioncall_trace = n ## _cpu_functioncall_trace;		\
	return 1;							\
	}


#endif	/*  CPU_H  */
