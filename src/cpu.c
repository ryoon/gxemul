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
 *  $Id: cpu.c,v 1.276 2005-02-01 14:20:38 debug Exp $
 *
 *  Common routines for CPU emulation. (Not specific to any CPU type.)
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>

#include "cpu.h"
#include "cpu_mips.h"
#include "cpu_ppc.h"
#include "machine.h"
#include "misc.h"


extern int quiet_mode;
extern int show_opcode_statistics;


/*
 *  cpu_new():
 *
 *  Create a new cpu object.
 */
struct cpu *cpu_new(struct memory *mem, struct machine *machine,
        int cpu_id, char *cpu_type_name)
{
	struct cpu *c;

	if (cpu_type_name == NULL) {
		fprintf(stderr, "cpu_new(): cpu name = NULL?\n");
		exit(1);
	}

	c = mips_cpu_new(mem, machine, cpu_id, cpu_type_name);
	if (c != NULL)
		return c;

	c = ppc_cpu_new(mem, machine, cpu_id, cpu_type_name);
	if (c != NULL)
		return c;

	fprintf(stderr, "cpu_new(): unknown cpu type '%s'\n", cpu_type_name);
	exit(1);
}


/*
 *  cpu_show_full_statistics():
 *
 *  Show detailed statistics on opcode usage on each cpu.
 */
void cpu_show_full_statistics(struct machine *m)
{
	switch (m->arch) {
	case ARCH_MIPS:
		mips_cpu_show_full_statistics(m);
		break;
	default:
		fatal("cpu_show_full_statistics(): not for PPC yet\n");
	}
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
	switch (m->arch) {
	case ARCH_MIPS:
		mips_cpu_tlbdump(m, x, rawflag);
		break;
	default:
		fatal("cpu_tlbdump(): not for PPC yet\n");
	}
}


/*
 *  cpu_register_match():
 *
 *  Used by the debugger.
 */
void cpu_register_match(struct machine *m, char *name,
	int writeflag, uint64_t *valuep, int *match_register)
{
	switch (m->arch) {
	case ARCH_MIPS:
		mips_cpu_register_match(m, name, writeflag,
		    valuep, match_register);
		break;
	case ARCH_PPC:
		ppc_cpu_register_match(m, name, writeflag,
		    valuep, match_register);
		break;
	default:
		fatal("cpu_register_match(): ?\n");
	}
}


/*
 *  cpu_disassemble_instr():
 *
 *  Convert an instruction word into human readable format, for instruction
 *  tracing.
 */
void cpu_disassemble_instr(struct machine *m, struct cpu *cpu,
	unsigned char *instr, int running, uint64_t addr, int bintrans)
{
	switch (m->arch) {
	case ARCH_MIPS:
		mips_cpu_disassemble_instr(cpu, instr, running, addr, bintrans);
		break;
	default:
		fatal("cpu_disassemble_instr(): not for PPC yet\n");
	}
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
	switch (m->arch) {
	case ARCH_MIPS:
		mips_cpu_register_dump(cpu, gprs, coprocs);
		break;
	case ARCH_PPC:
		ppc_cpu_register_dump(cpu, gprs, coprocs);
		break;
	default:
		fatal("cpu_register_dump(): ?\n");
	}
}


/*
 *  cpu_interrupt():
 *
 *  Assert an interrupt.
 *  Return value is 1 if the interrupt was asserted, 0 otherwise.
 */
int cpu_interrupt(struct cpu *cpu, uint64_t irq_nr)
{
	switch (cpu->machine->arch) {
	case ARCH_MIPS:
		return mips_cpu_interrupt(cpu, irq_nr);
	default:
		fatal("cpu_interrupt(): ?\n");
		return 0;
	}
}


/*
 *  cpu_interrupt_ack():
 *
 *  Acknowledge an interrupt.
 *  Return value is 1 if the interrupt was deasserted, 0 otherwise.
 */
int cpu_interrupt_ack(struct cpu *cpu, uint64_t irq_nr)
{
	switch (cpu->machine->arch) {
	case ARCH_MIPS:
		return mips_cpu_interrupt_ack(cpu, irq_nr);
	default:
		fatal("cpu_interrupt_ack(): ?\n");
		return 0;
	}
}


/*
 *  cpu_run():
 *
 *  Run instructions on all CPUs in this machine, for a "medium duration"
 *  (or until all CPUs have halted).
 *
 *  Return value is 1 if anything happened, 0 if all CPUs are stopped.
 */
int cpu_run(struct emul *emul, struct machine *machine)
{
	switch (machine->arch) {
	case ARCH_MIPS:
		return mips_cpu_run(emul, machine);
		break;
	default:
		fatal("cpu_run(): not for PPC yet\n");
		return 0;
	}
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

	switch (m->arch) {
	case ARCH_MIPS:
		mips_cpu_dumpinfo(cpu);
		break;
	case ARCH_PPC:
		ppc_cpu_dumpinfo(cpu);
		break;
	default:
		fatal("cpu_dumpinfo(): ?\n");
	}
}


/*
 *  cpu_list_available_types():
 *
 *  Print a list of available CPU types for each cpu family.
 */
void cpu_list_available_types(void)
{
	int iadd = 4;

	debug("MIPS:\n");
	debug_indentation(iadd);
	mips_cpu_list_available_types();
	debug_indentation(-iadd);

	debug("PPC:\n");
	debug_indentation(iadd);
	ppc_cpu_list_available_types();
	debug_indentation(-iadd);
}


/*
 *  cpu_run_deinit():
 *
 *  Shuts down all CPUs in a machine when ending a simulation. (This function
 *  should only need to be called once for each machine.)
 */
void cpu_run_deinit(struct emul *emul, struct machine *machine)
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
		cpu_show_cycles(machine, &machine->starttime,
		    machine->ncycles, 1);

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
void cpu_show_cycles(struct machine *machine,
	struct timeval *starttime, int64_t ncycles, int forced)
{
	uint64_t offset;
	char *symbol;
	int64_t mseconds, ninstrs;
	struct timeval tv;
	int h, m, s, ms, d;

	static int64_t mseconds_last = 0;
	static int64_t ninstrs_last = -1;

	if (machine->arch != ARCH_MIPS) {
		fatal("cpu_show_cycles(): not yet for !MIPS\n");
		return;
	}

	gettimeofday(&tv, NULL);
	mseconds = (tv.tv_sec - starttime->tv_sec) * 1000
	         + (tv.tv_usec - starttime->tv_usec) / 1000;

	if (mseconds == 0)
		mseconds = 1;

	if (mseconds - mseconds_last == 0)
		mseconds ++;

	ninstrs = ncycles * machine->cpus[machine->bootstrap_cpu]->cd.mips.cpu_type.instrs_per_cycle;

	if (machine->automatic_clock_adjustment) {
		static int first_adjustment = 1;

		/*  Current nr of cycles per second:  */
		int64_t cur_cycles_per_second = 1000 *
		    (ninstrs-ninstrs_last) / (mseconds-mseconds_last)
		    / machine->cpus[machine->bootstrap_cpu]->cd.mips.cpu_type.instrs_per_cycle;

		if (cur_cycles_per_second < 1500000)
			cur_cycles_per_second = 1500000;

		if (first_adjustment) {
			machine->emulated_hz = cur_cycles_per_second;
			first_adjustment = 0;
		} else {
			machine->emulated_hz = (15 * machine->emulated_hz +
			    cur_cycles_per_second) / 16;
		}

		debug("[ updating emulated_hz to %lli Hz ]\n",
		    (long long)machine->emulated_hz);
	}


	/*  RETURN here, unless show_nr_of_instructions (-N) is turned on:  */
	if (!machine->show_nr_of_instructions && !forced)
		goto do_return;


	printf("[ ");

	if (!machine->automatic_clock_adjustment) {
		d = machine->emulated_hz / 1000;
		if (d < 1)
			d = 1;
		ms = ncycles / d;
		h = ms / 3600000;
		ms -= 3600000 * h;
		m = ms / 60000;
		ms -= 60000 * m;
		s = ms / 1000;
		ms -= 1000 * s;

		printf("emulated time = %02i:%02i:%02i.%03i; ", h, m, s, ms);
	}

	printf("cycles=%lli", (long long) ncycles);

	if (machine->cpus[machine->bootstrap_cpu]->cd.mips.cpu_type.instrs_per_cycle > 1)
		printf(" (%lli instrs)", (long long) ninstrs);

	/*  Instructions per second, and average so far:  */
	printf("; i/s=%lli avg=%lli",
	    (long long) ((long long)1000 * (ninstrs-ninstrs_last)
		/ (mseconds-mseconds_last)),
	    (long long) ((long long)1000 * ninstrs / mseconds));

	symbol = get_symbol_name(&machine->symbol_context,
	    machine->cpus[machine->bootstrap_cpu]->cd.mips.pc, &offset);

	if (machine->cpus[machine->bootstrap_cpu]->cd.mips.cpu_type.isa_level < 3 ||
	    machine->cpus[machine->bootstrap_cpu]->cd.mips.cpu_type.isa_level == 32)
		printf("; pc=%08x",
		    (int)machine->cpus[machine->bootstrap_cpu]->cd.mips.pc);
	else
		printf("; pc=%016llx",
		    (long long)machine->cpus[machine->bootstrap_cpu]->cd.mips.pc);

	printf(" <%s> ]\n", symbol? symbol : "no symbol");

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
void cpu_run_init(struct emul *emul, struct machine *machine)
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
}

