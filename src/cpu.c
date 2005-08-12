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
 *  $Id: cpu.c,v 1.315 2005-08-12 18:34:00 debug Exp $
 *
 *  Common routines for CPU emulation. (Not specific to any CPU type.)
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>

#include "cpu.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"


extern int quiet_mode;
extern int show_opcode_statistics;


static struct cpu_family *first_cpu_family = NULL;


/*
 *  cpu_new():
 *
 *  Create a new cpu object.  Each family is tried in sequence until a
 *  CPU family recognizes the cpu_type_name.
 */
struct cpu *cpu_new(struct memory *mem, struct machine *machine,
        int cpu_id, char *name)
{
	struct cpu *cpu;
	struct cpu_family *fp;
	char *cpu_type_name;

	if (name == NULL) {
		fprintf(stderr, "cpu_new(): cpu name = NULL?\n");
		exit(1);
	}

	cpu_type_name = strdup(name);
	if (cpu_type_name == NULL) {
		fprintf(stderr, "cpu_new(): out of memory\n");
		exit(1);
	}

	cpu = zeroed_alloc(sizeof(struct cpu));

	cpu->memory_rw          = NULL;
	cpu->name               = cpu_type_name;
	cpu->mem                = mem;
	cpu->machine            = machine;
	cpu->cpu_id             = cpu_id;
	cpu->byte_order         = EMUL_LITTLE_ENDIAN;
	cpu->bootstrap_cpu_flag = 0;
	cpu->running            = 0;

	cpu_create_or_reset_tc(cpu);

	fp = first_cpu_family;

	while (fp != NULL) {
		if (fp->cpu_new != NULL) {
			if (fp->cpu_new(cpu, mem, machine, cpu_id,
			    cpu_type_name)) {
				/*  Sanity check:  */
				if (cpu->memory_rw == NULL) {
					fatal("\ncpu_new(): memory_rw == "
					    "NULL\n");
					exit(1);
				}
				return cpu;
			}
		}

		fp = fp->next;
	}

	fatal("\ncpu_new(): unknown cpu type '%s'\n", cpu_type_name);
	exit(1);
}


/*
 *  cpu_show_full_statistics():
 *
 *  Show detailed statistics on opcode usage on each cpu.
 */
void cpu_show_full_statistics(struct machine *m)
{
	if (m->cpu_family == NULL ||
	    m->cpu_family->show_full_statistics == NULL)
		fatal("cpu_show_full_statistics(): NULL\n");
	else
		m->cpu_family->show_full_statistics(m);
}


/*
 *  cpu_tlbdump():
 *
 *  Called from the debugger to dump the TLB in a readable format.
 *  x is the cpu number to dump, or -1 to dump all CPUs.
 *                                              
 *  If rawflag is nonzero, then the TLB contents isn't formated nicely,
 *  just dumped.
 */
void cpu_tlbdump(struct machine *m, int x, int rawflag)
{
	if (m->cpu_family == NULL || m->cpu_family->tlbdump == NULL)
		fatal("cpu_tlbdump(): NULL\n");
	else
		m->cpu_family->tlbdump(m, x, rawflag);
}


/*
 *  cpu_register_match():
 *
 *  Used by the debugger.
 */
void cpu_register_match(struct machine *m, char *name,
	int writeflag, uint64_t *valuep, int *match_register)
{
	if (m->cpu_family == NULL || m->cpu_family->register_match == NULL)
		fatal("cpu_register_match(): NULL\n");
	else
		m->cpu_family->register_match(m, name, writeflag,
		    valuep, match_register);
}


/*
 *  cpu_disassemble_instr():
 *
 *  Convert an instruction word into human readable format, for instruction
 *  tracing.
 */
int cpu_disassemble_instr(struct machine *m, struct cpu *cpu,
	unsigned char *instr, int running, uint64_t addr, int bintrans)
{
	if (m->cpu_family == NULL || m->cpu_family->disassemble_instr == NULL) {
		fatal("cpu_disassemble_instr(): NULL\n");
		return 0;
	} else
		return m->cpu_family->disassemble_instr(cpu, instr,
		    running, addr, bintrans);
}


/*                       
 *  cpu_register_dump():
 *
 *  Dump cpu registers in a relatively readable format.
 *
 *  gprs: set to non-zero to dump GPRs. (CPU dependant.)
 *  coprocs: set bit 0..x to dump registers in coproc 0..x. (CPU dependant.)
 */
void cpu_register_dump(struct machine *m, struct cpu *cpu,
	int gprs, int coprocs)
{
	if (m->cpu_family == NULL || m->cpu_family->register_dump == NULL)
		fatal("cpu_register_dump(): NULL\n");
	else
		m->cpu_family->register_dump(cpu, gprs, coprocs);
}


/*
 *  cpu_interrupt():
 *
 *  Assert an interrupt.
 *  Return value is 1 if the interrupt was asserted, 0 otherwise.
 */
int cpu_interrupt(struct cpu *cpu, uint64_t irq_nr)
{
	if (cpu->machine->cpu_family == NULL ||
	    cpu->machine->cpu_family->interrupt == NULL) {
		fatal("cpu_interrupt(): NULL\n");
		return 0;
	} else
		return cpu->machine->cpu_family->interrupt(cpu, irq_nr);
}


/*
 *  cpu_interrupt_ack():
 *
 *  Acknowledge an interrupt.
 *  Return value is 1 if the interrupt was deasserted, 0 otherwise.
 */
int cpu_interrupt_ack(struct cpu *cpu, uint64_t irq_nr)
{
	if (cpu->machine->cpu_family == NULL ||
	    cpu->machine->cpu_family->interrupt_ack == NULL) {
		/*  debug("cpu_interrupt_ack(): NULL\n");  */
		return 0;
	} else
		return cpu->machine->cpu_family->interrupt_ack(cpu, irq_nr);
}


/*
 *  cpu_functioncall_trace():
 *
 *  This function should be called if machine->show_trace_tree is enabled, and
 *  a function call is being made. f contains the address of the function.
 */
void cpu_functioncall_trace(struct cpu *cpu, uint64_t f)
{
	int i, n_args = -1;
	char *symbol;
	uint64_t offset;

	if (cpu->machine->ncpus > 1)
		fatal("cpu%i:\t", cpu->cpu_id);

	cpu->trace_tree_depth ++;
	for (i=0; i<cpu->trace_tree_depth; i++)
		fatal("  ");

	fatal("<");
	symbol = get_symbol_name_and_n_args(&cpu->machine->symbol_context,
	    f, &offset, &n_args);
	if (symbol != NULL)
		fatal("%s", symbol);
	else {
		if (cpu->is_32bit)
			fatal("0x%08x", (int)f);
		else
			fatal("0x%llx", (long long)f);
	}
	fatal("(");

	if (cpu->machine->cpu_family->functioncall_trace != NULL)
		cpu->machine->cpu_family->functioncall_trace(cpu, f, n_args);

	fatal(")>\n");
}


/*
 *  cpu_functioncall_trace_return():
 *
 *  This function should be called if machine->show_trace_tree is enabled, and
 *  a function is being returned from.
 *
 *  TODO: Print return value? This could be implemented similar to the
 *  cpu->functioncall_trace function call above.
 */
void cpu_functioncall_trace_return(struct cpu *cpu)
{
	cpu->trace_tree_depth --;
	if (cpu->trace_tree_depth < 0)
		cpu->trace_tree_depth = 0;
}


/*
 *  cpu_create_or_reset_tc():
 *
 *  Create the translation cache in memory (ie allocate memory for it), if
 *  necessary, and then reset it to an initial state.
 */
void cpu_create_or_reset_tc(struct cpu *cpu)
{
	if (cpu->translation_cache == NULL)
		cpu->translation_cache = zeroed_alloc(DYNTRANS_CACHE_SIZE + 
		    DYNTRANS_CACHE_MARGIN);

	/*  Create an empty table at the beginning of the translation cache:  */
	memset(cpu->translation_cache, 0, sizeof(uint32_t)
	    * N_BASE_TABLE_ENTRIES);

	cpu->translation_cache_cur_ofs =
	    N_BASE_TABLE_ENTRIES * sizeof(uint32_t);

	/*
	 *  There might be other translation pointers that still point to
	 *  within the translation_cache region. Let's invalidate those too:
	 */
	if (cpu->invalidate_code_translation_caches != NULL)
		cpu->invalidate_code_translation_caches(cpu);
}


/*
 *  cpu_run():
 *
 *  Run instructions on all CPUs in this machine, for a "medium duration"
 *  (or until all CPUs have halted).
 *
 *  Return value is 1 if anything happened, 0 if all CPUs are stopped.
 */
int cpu_run(struct emul *emul, struct machine *m)
{
	if (m->cpu_family == NULL || m->cpu_family->run == NULL) {
		fatal("cpu_run(): NULL\n");
		return 0;
	} else
		return m->cpu_family->run(emul, m);
}


/*
 *  cpu_dumpinfo():
 *
 *  Dumps info about a CPU using debug(). "cpu0: CPUNAME, running" (or similar)
 *  is outputed, and it is up to CPU dependant code to complete the line.
 */
void cpu_dumpinfo(struct machine *m, struct cpu *cpu)
{
	debug("cpu%i: %s, %s", cpu->cpu_id, cpu->name,
	    cpu->running? "running" : "stopped");

	if (m->cpu_family == NULL || m->cpu_family->dumpinfo == NULL)
		fatal("cpu_dumpinfo(): NULL\n");
	else
		m->cpu_family->dumpinfo(cpu);
}


/*
 *  cpu_list_available_types():
 *
 *  Print a list of available CPU types for each cpu family.
 */
void cpu_list_available_types(void)
{
	struct cpu_family *fp;
	int iadd = 4;

	fp = first_cpu_family;

	if (fp == NULL) {
		debug("No CPUs defined!\n");
		return;
	}

	while (fp != NULL) {
		debug("%s:\n", fp->name);
		debug_indentation(iadd);
		if (fp->list_available_types != NULL)
			fp->list_available_types();
		else
			debug("(internal error: list_available_types"
			    " = NULL)\n");
		debug_indentation(-iadd);

		fp = fp->next;
	}
}


/*
 *  cpu_run_deinit():
 *
 *  Shuts down all CPUs in a machine when ending a simulation. (This function
 *  should only need to be called once for each machine.)
 */
void cpu_run_deinit(struct machine *machine)
{
	int te;

	/*
	 *  Two last ticks of every hardware device.  This will allow
	 *  framebuffers to draw the last updates to the screen before
	 *  halting.
	 */
        for (te=0; te<machine->n_tick_entries; te++) {
		machine->tick_func[te](machine->cpus[0],
		    machine->tick_extra[te]);
		machine->tick_func[te](machine->cpus[0],
		    machine->tick_extra[te]);
	}

	debug("cpu_run_deinit(): All CPUs halted.\n");

	if (machine->show_nr_of_instructions || !quiet_mode)
		cpu_show_cycles(machine, 1);

	if (show_opcode_statistics)
		cpu_show_full_statistics(machine);

	fflush(stdout);
}


/*
 *  cpu_show_cycles():
 *
 *  If automatic adjustment of clock interrupts is turned on, then recalculate
 *  emulated_hz.  Also, if show_nr_of_instructions is on, then print a
 *  line to stdout about how many instructions/cycles have been executed so
 *  far.
 */
void cpu_show_cycles(struct machine *machine, int forced)
{
	uint64_t offset, pc;
	char *symbol;
	int64_t mseconds, ninstrs, is, avg;
	struct timeval tv;
	int h, m, s, ms, d, instrs_per_cycle = 1;

	static int64_t mseconds_last = 0;
	static int64_t ninstrs_last = -1;

	switch (machine->arch) {
	case ARCH_MIPS:
		instrs_per_cycle = machine->cpus[machine->bootstrap_cpu]->
		    cd.mips.cpu_type.instrs_per_cycle;
		break;
	}

	pc = machine->cpus[machine->bootstrap_cpu]->pc;

	gettimeofday(&tv, NULL);
	mseconds = (tv.tv_sec - machine->starttime.tv_sec) * 1000
	         + (tv.tv_usec - machine->starttime.tv_usec) / 1000;

	if (mseconds == 0)
		mseconds = 1;

	if (mseconds - mseconds_last == 0)
		mseconds ++;

	ninstrs = machine->ncycles_since_gettimeofday * instrs_per_cycle;

	if (machine->automatic_clock_adjustment) {
		static int first_adjustment = 1;

		/*  Current nr of cycles per second:  */
		int64_t cur_cycles_per_second = 1000 *
		    (ninstrs-ninstrs_last) / (mseconds-mseconds_last)
		    / instrs_per_cycle;

		if (cur_cycles_per_second < 1000000)
			cur_cycles_per_second = 1000000;

		if (first_adjustment) {
			machine->emulated_hz = cur_cycles_per_second;
			first_adjustment = 0;
		} else {
			machine->emulated_hz = (15 * machine->emulated_hz +
			    cur_cycles_per_second) / 16;
		}

		/*  debug("[ updating emulated_hz to %lli Hz ]\n",
		    (long long)machine->emulated_hz);  */
	}


	/*  RETURN here, unless show_nr_of_instructions (-N) is turned on:  */
	if (!machine->show_nr_of_instructions && !forced)
		goto do_return;

	printf("[ %lli instrs",
	    (long long)(machine->ncycles * instrs_per_cycle));

	if (!machine->automatic_clock_adjustment) {
		d = machine->emulated_hz / 1000;
		if (d < 1)
			d = 1;
		ms = machine->ncycles / d;
		h = ms / 3600000;
		ms -= 3600000 * h;
		m = ms / 60000;
		ms -= 60000 * m;
		s = ms / 1000;
		ms -= 1000 * s;

		printf("emulated time = %02i:%02i:%02i.%03i; ", h, m, s, ms);
	}

	/*  Instructions per second, and average so far:  */
	is = 1000 * (ninstrs-ninstrs_last) / (mseconds-mseconds_last);
	avg = (long long)1000 * ninstrs / mseconds;
	if (is < 0)
		is = 0;
	if (avg < 0)
		avg = 0;
	printf("; i/s=%lli avg=%lli", (long long)is, (long long)avg);

	symbol = get_symbol_name(&machine->symbol_context, pc, &offset);

	if (machine->ncpus == 1) {
		if (machine->cpus[machine->bootstrap_cpu]->is_32bit)
			printf("; pc=0x%08x", (int)pc);
		else
			printf("; pc=0x%016llx", (long long)pc);
	}

	if (symbol != NULL)
		printf(" <%s>", symbol);
	printf(" ]\n");

do_return:
	ninstrs_last = ninstrs;
	mseconds_last = mseconds;
}


/*
 *  cpu_run_init():
 *
 *  Prepare to run instructions on all CPUs in this machine. (This function
 *  should only need to be called once for each machine.)
 */
void cpu_run_init(struct machine *machine)
{
	int ncpus = machine->ncpus;
	int te;

	machine->a_few_cycles = 1048576;
	machine->ncycles_flush = 0;
	machine->ncycles = 0;
	machine->ncycles_show = 0;

	/*
	 *  Instead of doing { one cycle, check hardware ticks }, we
	 *  can do { n cycles, check hardware ticks }, as long as
	 *  n is at most as much as the lowest number of cycles/tick
	 *  for any hardware device.
	 */
	for (te=0; te<machine->n_tick_entries; te++) {
		if (machine->ticks_reset_value[te] < machine->a_few_cycles)
			machine->a_few_cycles = machine->ticks_reset_value[te];
	}

	machine->a_few_cycles >>= 1;
	if (machine->a_few_cycles < 1)
		machine->a_few_cycles = 1;

	if (ncpus > 1 && machine->max_random_cycles_per_chunk == 0)
		machine->a_few_cycles = 1;

	/*  debug("cpu_run_init(): a_few_cycles = %i\n",
	    machine->a_few_cycles);  */

	/*  For performance measurement:  */
	gettimeofday(&machine->starttime, NULL);
	machine->ncycles_since_gettimeofday = 0;
}


/*
 *  add_cpu_family():
 *
 *  Allocates a cpu_family struct and calls an init function for the
 *  family to fill in reasonable data and pointers.
 */
static void add_cpu_family(int (*family_init)(struct cpu_family *), int arch)
{
	struct cpu_family *fp, *tmp;
	int res;

	fp = malloc(sizeof(struct cpu_family));
	if (fp == NULL) {
		fprintf(stderr, "add_cpu_family(): out of memory\n");
		exit(1);
	}
	memset(fp, 0, sizeof(struct cpu_family));

	/*
	 *  family_init() returns 1 if the struct has been filled with
	 *  valid data, 0 if suppor for the cpu family isn't compiled
	 *  into the emulator.
	 */
	res = family_init(fp);
	if (!res) {
		free(fp);
		return;
	}
	fp->arch = arch;
	fp->next = NULL;

	/*  Add last in family chain:  */
	tmp = first_cpu_family;
	if (tmp == NULL) {
		first_cpu_family = fp;
	} else {
		while (tmp->next != NULL)
			tmp = tmp->next;
		tmp->next = fp;
	}
}


/*
 *  cpu_family_ptr_by_number():
 *
 *  Returns a pointer to a CPU family based on the ARCH_* integers.
 */
struct cpu_family *cpu_family_ptr_by_number(int arch)
{
	struct cpu_family *fp;
	fp = first_cpu_family;

	/*  YUCK! This is too hardcoded! TODO  */

	while (fp != NULL) {
		if (arch == fp->arch)
			return fp;
		fp = fp->next;
	}

	return NULL;
}


/*
 *  cpu_init():
 *
 *  Should be called before any other cpu_*() function.
 */
void cpu_init(void)
{
	/*  Note: These are registered in alphabetic order.  */

#ifdef ENABLE_ALPHA
	add_cpu_family(alpha_cpu_family_init, ARCH_ALPHA);
#endif

#ifdef ENABLE_ARM
	add_cpu_family(arm_cpu_family_init, ARCH_ARM);
#endif

#ifdef ENABLE_IA64
	add_cpu_family(ia64_cpu_family_init, ARCH_IA64);
#endif

#ifdef ENABLE_M68K
	add_cpu_family(m68k_cpu_family_init, ARCH_M68K);
#endif

#ifdef ENABLE_MIPS
	add_cpu_family(mips_cpu_family_init, ARCH_MIPS);
#endif

#ifdef ENABLE_PPC
	add_cpu_family(ppc_cpu_family_init, ARCH_PPC);
#endif

#ifdef ENABLE_X86
	add_cpu_family(x86_cpu_family_init, ARCH_X86);
#endif
}

