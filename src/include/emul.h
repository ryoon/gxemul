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
 *  $Id: emul.h,v 1.9 2004-11-01 09:26:09 debug Exp $
 */

#include "misc.h"

#define	CPU_NAME_MAXLEN		48
#define	MAX_PC_DUMPPOINTS	4

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

	/*  PC Dumppoints: if the PC value ever matches one of these, we set
		register_dump = instruction_trace = 1  */
	int	n_dumppoints;
	char	*dumppoint_string[MAX_PC_DUMPPOINTS];
	uint64_t dumppoint_pc[MAX_PC_DUMPPOINTS];
	int	dumppoint_flag_r[MAX_PC_DUMPPOINTS];
	    /*  0 for instruction trace, 1 for instr.trace + register dump  */

	/*  Cache sizes: (1 << x) x=0 for default values  */
	int	cache_picache;
	int	cache_pdcache;
	int	cache_secondary;
	int	cache_picache_linesize;
	int	cache_pdcache_linesize;
	int	cache_secondary_linesize;

	int	dbe_on_nonexistant_memaccess;
	int	bintrans_enable;
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
	char	*boot_kernel_filename;
	char	*boot_string_argument;

	int	bootstrap_cpu;
	int	use_random_bootstrap_cpu;
	int	ncpus;
	struct cpu **cpus;

	int	automatic_clock_adjustment;

	int	show_trace_tree;
	int	tlb_dump;
	int	verbose;

	int	n_gfx_cards;

	int	use_x11;
	int	x11_scaledown;
};

/*  emul.c:  */
void debugger(void);
struct emul *emul_new(void);
void emul_start(struct emul *emul);

#endif	/*  EMUL_H  */
