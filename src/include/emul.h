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
 *  $Id: emul.h,v 1.15 2004-12-18 03:26:15 debug Exp $
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

#endif	/*  EMUL_H  */
