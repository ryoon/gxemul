#ifndef	EMUL_H
#define	EMUL_H

/*
 *  Copyright (C) 2004  Anders Gavare.  All rights reserved.
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
 *  $Id: emul.h,v 1.17 2004-12-22 17:49:59 debug Exp $
 */

#include "misc.h"

#define	CPU_NAME_MAXLEN		48

#define	MAX_BREAKPOINTS		8
#define	BREAKPOINT_FLAG_R	1

#include "symbol.h"

struct emul {
	char	emul_cpu_name[CPU_NAME_MAXLEN];
	int	emulation_type;
	int	machine;
	char	*machine_name;

	struct symbol_context symbol_context;

	int	random_mem_contents;
	int	physical_ram_in_mb;
	int	booting_from_diskimage;

	int	show_opcode_statistics;
	int	prom_emulation;
	int	register_dump;

	int	n_breakpoints;
	char	*breakpoint_string[MAX_BREAKPOINTS];
	uint64_t breakpoint_addr[MAX_BREAKPOINTS];
	int	breakpoint_flags[MAX_BREAKPOINTS];

	/*  Cache sizes: (1 << x) x=0 for default values  */
	int	cache_picache;
	int	cache_pdcache;
	int	cache_secondary;
	int	cache_picache_linesize;
	int	cache_pdcache_linesize;
	int	cache_secondary_linesize;

	int	dbe_on_nonexistant_memaccess;
	int	bintrans_enable;
	int	bintrans_enabled_from_start;
	int	instruction_trace;
	int	single_step;
	int	trace_on_bad_address;
	int	show_nr_of_instructions;
	int64_t	max_instructions;
	int	emulated_hz;
	int	max_random_cycles_per_chunk;
	int	speed_tricks;
	int	userland_emul;
	int	force_netboot;
	int	slow_serial_interrupts_hack_for_linux;

	char	*boot_kernel_filename;
	char	*boot_string_argument;

	int	bootstrap_cpu;
	int	use_random_bootstrap_cpu;
	int	ncpus;
	struct cpu **cpus;

	int	automatic_clock_adjustment;

	int	exit_without_entering_debugger;

	int	show_trace_tree;
	int	verbose;

	int	n_gfx_cards;

	int	use_x11;
	int	x11_scaledown;
	int	x11_n_display_names;
	char	**x11_display_names;
	int	x11_current_display_name_nr;	/*  updated by x11.c  */
};

/*  emul.c:  */
struct emul *emul_new(void);
void emul_start(struct emul *emul);


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
/*  Machine types:  */
#define	HPCMIPS_CASIO_BE300	1

/*  Playstation 2:  */
#define	PLAYSTATION2_BDA	0xffffffffa0001000ULL
#define	PLAYSTATION2_OPTARGS	0xffffffff81fff100ULL
#define	PLAYSTATION2_SIFBIOS	0xffffffffbfc10000ULL

/*  SGI and ARC:  */
#define	MACHINE_ARC_NEC_RD94		1
#define	MACHINE_ARC_JAZZ_PICA		2
#define	MACHINE_ARC_NEC_R94		3
#define	MACHINE_ARC_DESKTECH_TYNE	4
#define	MACHINE_ARC_JAZZ_MAGNUM		5
#define	MACHINE_ARC_NEC_R98		6
#define	MACHINE_ARC_JAZZ_M700		7
#define	MACHINE_ARC_NEC_R96		8


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


#endif	/*  EMUL_H  */

