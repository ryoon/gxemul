#ifndef	MISC_H
#define	MISC_H

/*
 *  Copyright (C) 2003-2004  Anders Gavare.  All rights reserved.
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
 *  $Id: misc.h,v 1.182 2004-12-14 02:21:20 debug Exp $
 *
 *  Misc. definitions for mips64emul.
 */


#include <sys/types.h>
#include <inttypes.h>

/*
 *  ../config.h contains #defines set by the configure script. Some of these
 *  might reduce speed of the emulator, so don't enable them unless you
 *  need them.
 */

#include "../config.h"

/*  
 *  ENABLE_MIPS16 should be defined on the cc commandline using -D, if you 
 *  want it. (This is done by ./configure --mips16)
 *
 *  ENABLE_INSTRUCTION_DELAYS should be defined on the cc commandline using
 *  -D if you want it. (This is done by ./configure --delays)
 */
#define USE_TINY_CACHE
/*  #define ALWAYS_SIGNEXTEND_32  */
/*  #define HALT_IF_PC_ZERO  */
/*  #define MFHILO_DELAY  */

#ifdef WITH_X11
#include <X11/Xlib.h>
#endif

#ifdef NO_MAP_ANON
#ifdef mmap
#undef mmap
#endif
#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>
static void *no_map_anon_mmap(void *addr, size_t len, int prot, int flags,
	int nonsense_fd, off_t offset)
{
	void *p;
	int fd = open("/dev/zero", O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "Could not open /dev/zero\n");
		exit(1);
	}

	printf("addr=%p len=%lli prot=0x%x flags=0x%x nonsense_fd=%i "
	    "offset=%16lli\n", addr, (long long) len, prot, flags,
	    nonsense_fd, (long long) offset);

	p = mmap(addr, len, prot, flags, fd, offset);

	printf("p = %p\n", p);

	/*  TODO: Close the descriptor?  */
	return p;
}
#define mmap no_map_anon_mmap
#endif


#include "emul.h"

/*  Machine emulation types:  */
#define	EMULTYPE_NONE		0
#define	EMULTYPE_TEST		1
#define	EMULTYPE_DEC		2
#define	EMULTYPE_COBALT		3
#define	EMULTYPE_HPCMIPS	4
#define	EMULTYPE_PS2		5
#define	EMULTYPE_SGI		6
#define	EMULTYPE_ARC		7
#define	EMULTYPE_MESHCUBE	8
#define	EMULTYPE_NETGEAR	9
#define	EMULTYPE_WRT54G		10
#define	EMULTYPE_SONYNEWS	11

/*  Specific machines:  */
#define	MACHINE_NONE		0

/*  DEC:  */
#define	MACHINE_PMAX_3100	1
#define	MACHINE_3MAX_5000	2
#define	MACHINE_3MIN_5000	3
#define	MACHINE_3MAXPLUS_5000	4
#define	MACHINE_5800		5
#define	MACHINE_5400		6
#define	MACHINE_MAXINE_5000	7
#define	MACHINE_5500		11
#define	MACHINE_MIPSMATE_5100	12

#define	DEC_PROM_CALLBACK_STRUCT	0xffffffffbfc04000ULL
#define	DEC_PROM_EMULATION		0xffffffffbfc08000ULL
#define	DEC_PROM_INITIAL_ARGV		(INITIAL_STACK_POINTER + 0x80)
#define	DEC_PROM_STRINGS		0xffffffffbfc20000ULL
#define	DEC_PROM_TCINFO			0xffffffffbfc2c000ULL
#define	DEC_MEMMAP_ADDR			0xffffffffbfc30000ULL


/*  HPCmips:  */
#include "hpc_bootinfo.h"
#define	HPCMIPS_FB_ADDR		0x1f000000
#define	HPCMIPS_FB_XSIZE	640
#define	HPCMIPS_FB_YSIZE	480

/*  Playstation 2:  */
#define	PLAYSTATION2_BDA	0xffffffffa0001000ULL
#define	PLAYSTATION2_OPTARGS	0xffffffff81fff100ULL
#define	PLAYSTATION2_SIFBIOS	0xffffffffbfc10000ULL

/*  SGI and ARC:  */
#include "sgi_arcbios.h"
#define	MACHINE_ARC_NEC_RD94		1
#define	MACHINE_ARC_JAZZ_PICA		2
#define	MACHINE_ARC_NEC_R94		3
#define	MACHINE_ARC_DESKTECH_TYNE	4
#define	MACHINE_ARC_JAZZ_MAGNUM		5
#define	MACHINE_ARC_NEC_R98		6
#define	MACHINE_ARC_JAZZ_M700		7


/*
 *  Problem: kernels seem to be loaded at low addresses in RAM, so
 *  storing environment strings and memory descriptors there is a bad
 *  idea. They are stored at 0xbfc..... instead.  The ARC SPB must
 *  be at physical address 0x1000 though.
 */
#define	SGI_SPB_ADDR		0xffffffff80001000ULL
/*  0xbfc10000 is firmware callback vector stuff  */
#define	ARC_FIRMWARE_VECTORS	0xffffffffbfc80000ULL
#define	ARC_FIRMWARE_ENTRIES	0xffffffffbfc88000ULL
#define	ARC_ARGV_START		0xffffffffbfc90000ULL
#define	ARC_ENV_STRINGS		0xffffffffbfc98000ULL
#define	ARC_ENV_POINTERS	0xffffffffbfc9d000ULL
#define	SGI_SYSID_ADDR		0xffffffffbfca1800ULL
#define	ARC_DSPSTAT_ADDR	0xffffffffbfca1c00ULL
#define	ARC_MEMDESC_ADDR	0xffffffffbfca1c80ULL
#define	ARC_CONFIG_DATA_ADDR	0xffffffffbfca2000ULL
#define	FIRST_ARC_COMPONENT	0xffffffffbfca8000ULL
#define	ARC_PRIVATE_VECTORS	0xffffffffbfcb0000ULL
#define	ARC_PRIVATE_ENTRIES	0xffffffffbfcb8000ULL


/*
 *  CPU type definitions:  See cpu_types.h.
 */

struct cpu_type_def {
	char		*name;
	int		rev;
	int		sub;
	char		flags;
	char		exc_model;		/*  EXC3K or EXC4K  */
	char		mmu_model;		/*  MMU3K or MMU4K  */
	char		isa_level;		/*  1, 2, 3, 4, 5, 32, 64  */
	int		nr_of_tlb_entries;	/*  32, 48, 64, ...  */
	char		instrs_per_cycle;	/*  simplified, 1, 2, or 4, for example  */
	int		default_picache;
	int		default_pdcache;
	int		default_pilinesize;
	int		default_pdlinesize;
	int		default_scache;
	int		default_slinesize;
};


/*  Debug stuff:  */
#define	DEBUG_BUFSIZE		1024

#define	DEFAULT_RAM_IN_MB	32
#define	MAX_DEVICES		24


struct cpu;

struct memory {
	uint64_t	physical_max;
	void		*pagetable;

	int		n_mmapped_devices;
	int		last_accessed_device;
	/*  The following two might speed up things a little bit.  */
	/*  (actually maxaddr is the addr after the last address)  */
	uint64_t	mmap_dev_minaddr;
	uint64_t	mmap_dev_maxaddr;

	const char	*dev_name[MAX_DEVICES];
	uint64_t	dev_baseaddr[MAX_DEVICES];
	uint64_t	dev_length[MAX_DEVICES];
	int		dev_flags[MAX_DEVICES];
	int		(*dev_f[MAX_DEVICES])(struct cpu *,struct memory *,
			    uint64_t,unsigned char *,size_t,int,void *);
	void		*dev_extra[MAX_DEVICES];
	unsigned char	*dev_bintrans_data[MAX_DEVICES];
#ifdef BINTRANS
	uint64_t	dev_bintrans_write_low[MAX_DEVICES];
	uint64_t	dev_bintrans_write_high[MAX_DEVICES];
#endif
};

#define	BITS_PER_PAGETABLE	20
#define	BITS_PER_MEMBLOCK	20
#define	MAX_BITS		40

#define	MEM_READ			0
#define	MEM_WRITE			1

#define	INITIAL_PC			0xffffffffbfc00000ULL
#define	INITIAL_STACK_POINTER		(0xffffffffa0008000ULL - 256)


#define	N_COPROC_REGS		32
#define	N_FCRS			32

/*  TODO:  48 or 64 is max for most processors, but 192 for R8000?  */
#define	MAX_NR_OF_TLBS		192

struct tlb {
	uint64_t	hi;
	uint64_t	lo0;
	uint64_t	lo1;
	uint64_t	mask;
};

struct coproc {
	int		coproc_nr;
	uint64_t	reg[N_COPROC_REGS];

	/*  Only for COP0:  */
	struct tlb	tlbs[MAX_NR_OF_TLBS];
	int		nr_of_tlbs;

	/*  Only for COP1:  floating point control registers  */
	uint64_t	fcr[N_FCRS];
};

#define	N_COPROCS		4

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
#define	GPR_T0		8		/*  t0  */
#define	GPR_T1		9		/*  t1  */
#define	GPR_T2		10		/*  t2  */
#define	GPR_T3		11		/*  t3  */
#define	GPR_T4		12		/*  t4  */
#define	GPR_S0		16		/*  s0  */
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

#define	MAX_TICK_FUNCTIONS	12

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
#define	N_SAFE_BINTRANS_LIMIT		((1 << (N_SAFE_BINTRANS_LIMIT_SHIFT - 1)) - 1)

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
	int		byte_order;
	int		running;
	int		bootstrap_cpu_flag;
	int		cpu_id;

	struct emul	*emul;

	struct cpu_type_def cpu_type;

	struct coproc	*coproc[N_COPROCS];

	void		(*md_interrupt)(struct cpu *, int irq_nr, int);

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

	/*  TODO: remove this once cache functions correctly  */
	int		r10k_cache_disable_TODO;

	/*
	 *  Hardware devices, run every x clock cycles.
	 */
	int		n_tick_entries;
	int		ticks_till_next[MAX_TICK_FUNCTIONS];
	int		ticks_reset_value[MAX_TICK_FUNCTIONS];
	void		(*tick_func[MAX_TICK_FUNCTIONS])(struct cpu *, void *);
	void		*tick_extra[MAX_TICK_FUNCTIONS];
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


/*  main.c:  */
void debug(char *fmt, ...);
void fatal(char *fmt, ...);


/*  arcbios.c:  */
void arcbios_console_init(struct cpu *cpu,
	uint64_t vram, uint64_t ctrlregs, int maxx, int maxy);
void arcbios_add_memory_descriptor(struct cpu *cpu,
	uint64_t base, uint64_t len, int arctype);
uint64_t arcbios_addchild_manual(struct cpu *cpu,
	uint64_t class, uint64_t type, uint64_t flags, uint64_t version,
	uint64_t revision, uint64_t key, uint64_t affinitymask,
	char *identifier, uint64_t parent, void *config_data, size_t config_len);
void arcbios_emul(struct cpu *cpu);
void arcbios_set_64bit_mode(int enable);
void arcbios_set_default_exception_handler(struct cpu *cpu);


/*  coproc.c:  */
struct coproc *coproc_new(struct cpu *cpu, int coproc_nr);
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
void coproc_function(struct cpu *cpu, struct coproc *cp, uint32_t function,
	int unassemble_only, int running);


/*  cpu.c:  */
struct cpu *cpu_new(struct memory *mem, struct emul *emul, int cpu_id, char *cpu_type_name);
void cpu_add_tickfunction(struct cpu *cpu, void (*func)(struct cpu *, void *), void *extra, int clockshift);
void cpu_register_dump(struct cpu *cpu);
void cpu_disassemble_instr(struct cpu *cpu, unsigned char *instr,
	int running, uint64_t addr, int bintrans);
int cpu_interrupt(struct cpu *cpu, int irq_nr);
int cpu_interrupt_ack(struct cpu *cpu, int irq_nr);
void cpu_exception(struct cpu *cpu, int exccode, int tlb, uint64_t vaddr,
	/*  uint64_t pagemask,  */  int coproc_nr, uint64_t vaddr_vpn2, int vaddr_asid, int x_64);
void cpu_cause_simple_exception(struct cpu *cpu, int exc_code);
int cpu_run(struct emul *emul, struct cpu **cpus, int ncpus);


/*  debugger:  */
void debugger_activate(int x);
void debugger(void);


/*  dec_prom.c:  */
void decstation_prom_emul(struct cpu *cpu);


/*  file.c:  */
int file_n_executables_loaded(void);
void file_load(struct memory *mem, char *filename, struct cpu *cpu);


/*  machine.c:  */
unsigned char read_char_from_memory(struct cpu *cpu, int regbase, int offset);
void dump_mem_string(struct cpu *cpu, uint64_t addr);
void store_string(struct cpu *cpu, uint64_t addr, char *s);
void store_64bit_word(struct cpu *cpu, uint64_t addr, uint64_t data64);
void store_32bit_word(struct cpu *cpu, uint64_t addr, uint64_t data32);
void store_64bit_word_in_host(struct cpu *cpu, unsigned char *data, uint64_t data32);
void store_32bit_word_in_host(struct cpu *cpu, unsigned char *data, uint64_t data32);
uint32_t load_32bit_word(struct cpu *cpu, uint64_t addr);
void store_buf(struct cpu *cpu, uint64_t addr, char *s, size_t len);
void machine_init(struct emul *emul, struct memory *mem);


/*  mips16.c:  */
int mips16_to_32(struct cpu *cpu, unsigned char *instr16, unsigned char *instr);


/*  ps2_bios.c:  */
void playstation2_sifbios_emul(struct cpu *cpu);


/*  useremul.c:  */
#define	USERLAND_NONE		0
#define	USERLAND_NETBSD_PMAX	1
#define	USERLAND_ULTRIX_PMAX	2
void useremul_init(struct cpu *, int, char **);
void useremul_syscall(struct cpu *cpu, uint32_t code);


/*  x11.c:  */
#define N_GRAYCOLORS            16
#define	CURSOR_COLOR_TRANSPARENT	-1
#define	CURSOR_COLOR_INVERT		-2
#define	CURSOR_MAXY		64
#define	CURSOR_MAXX		64
/*  Framebuffer windows:  */
struct fb_window {
	int		fb_number;

#ifdef WITH_X11
	/*  x11_fb_winxsize > 0 for a valid fb_window  */
	int		x11_fb_winxsize, x11_fb_winysize;
	int		scaledown;
	Display		*x11_display;

	int		x11_screen;
	int		x11_screen_depth;
	unsigned long	fg_color;
	unsigned long	bg_color;
	XColor		x11_graycolor[N_GRAYCOLORS];
	Window		x11_fb_window;
	GC		x11_fb_gc;

	XImage		*fb_ximage;
	unsigned char	*ximage_data;

	/*  -1 means transparent, 0 and up are grayscales  */
	int		cursor_pixels[CURSOR_MAXY][CURSOR_MAXX];
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

	/*  Host's X11 cursor:  */
	Cursor		host_cursor;
	Pixmap		host_cursor_pixmap;
#endif
};
void x11_redraw_cursor(int);
void x11_redraw(int);
void x11_putpixel_fb(int, int x, int y, int color);
#ifdef WITH_X11
void x11_putimage_fb(int);
#endif
void x11_init(struct emul *);
struct fb_window *x11_fb_init(int xsize, int ysize, char *name,
	int scaledown, struct emul *emul);
void x11_check_event(void);


#endif	/*  MISC_H  */

