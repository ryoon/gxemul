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
 *  $Id: cpu_run.c,v 1.3 2005-02-08 17:18:33 debug Exp $
 *
 *  Included from cpu_mips.c, cpu_ppc.c etc.  (The reason for this is that
 *  the call to a specific cpu's routine that runs one instruction will
 *  be inlined from here.)
 */

#include "console.h"
#include "debugger.h"


static int instrs_per_cycle(struct cpu *cpu) {
#ifdef CPU_RUN_MIPS
	return cpu->cd.mips.cpu_type.instrs_per_cycle;
#else	/*  PPC or undefined  */
	return 1;
#endif
}


/*
 *  CPU_RUN():
 *
 *  Run instructions on all CPUs in this machine, for a "medium duration"
 *  (or until all CPUs have halted).
 *
 *  Return value is 1 if anything happened, 0 if all CPUs are stopped.
 */
int CPU_RUN(struct emul *emul, struct machine *machine)
{
	struct cpu **cpus = machine->cpus;
	int ncpus = machine->ncpus;
	int64_t max_instructions_cached = machine->max_instructions;
	int64_t max_random_cycles_per_chunk_cached =
	    machine->max_random_cycles_per_chunk;
	int64_t ncycles_chunk_end;
	int running, rounds;

	/*  The main loop:  */
	running = 1;
	rounds = 0;
	while (running || single_step) {
		ncycles_chunk_end = machine->ncycles + (1 << 16);

		machine->a_few_instrs = machine->a_few_cycles *
		    instrs_per_cycle(cpus[0]);

		/*  Do a chunk of cycles:  */
		do {
			int i, j, te, cpu0instrs, a_few_instrs2;

			running = 0;
			cpu0instrs = 0;

			/*
			 *  Run instructions from each CPU:
			 */

			/*  Is any cpu alive?  */
			for (i=0; i<ncpus; i++)
				if (cpus[i]->running)
					running = 1;

			if (single_step) {
				if (single_step == 1) {
					old_instruction_trace = machine->instruction_trace;
					old_quiet_mode = quiet_mode;
					old_show_trace_tree = machine->show_trace_tree;
					machine->instruction_trace = 1;
					machine->show_trace_tree = 1;
					quiet_mode = 0;
					single_step = 2;
				}

				for (j=0; j<instrs_per_cycle(cpus[0]); j++) {
					if (single_step)
						debugger();
					for (i=0; i<ncpus; i++)
						if (cpus[i]->running) {
							int instrs_run = CPU_RINSTR(emul, cpus[i]);
							if (i == 0)
								cpu0instrs += instrs_run;
						}
				}
			} else if (max_random_cycles_per_chunk_cached > 0) {
				for (i=0; i<ncpus; i++)
					if (cpus[i]->running && !single_step) {
						a_few_instrs2 = machine->a_few_cycles;
						if (a_few_instrs2 >= max_random_cycles_per_chunk_cached)
							a_few_instrs2 = max_random_cycles_per_chunk_cached;
						j = (random() % a_few_instrs2) + 1;
						j *= instrs_per_cycle(cpus[i]);
						while (j-- >= 1 && cpus[i]->running) {
							int instrs_run = CPU_RINSTR(emul, cpus[i]);
							if (i == 0)
								cpu0instrs += instrs_run;
							if (single_step)
								break;
						}
					}
			} else {
				/*  CPU 0 is special, cpu0instr must be updated.  */
				for (j=0; j<machine->a_few_instrs; ) {
					int instrs_run;
					if (!cpus[0]->running || single_step)
						break;
					do {
						instrs_run =
						    CPU_RINSTR(emul, cpus[0]);
						if (instrs_run == 0 ||
						    single_step) {
							j = machine->a_few_instrs;
							break;
						}
					} while (instrs_run == 0);
					j += instrs_run;
					cpu0instrs += instrs_run;
				}

				/*  CPU 1 and up:  */
				for (i=1; i<ncpus; i++) {
					a_few_instrs2 = machine->a_few_cycles *
					    instrs_per_cycle(cpus[i]);
					for (j=0; j<a_few_instrs2; )
						if (cpus[i]->running) {
							int instrs_run = 0;
							while (!instrs_run) {
								instrs_run = CPU_RINSTR(emul, cpus[i]);
								if (instrs_run == 0 ||
								    single_step) {
									j = a_few_instrs2;
									break;
								}
							}
							j += instrs_run;
						} else
							break;
				}
			}

			/*
			 *  Hardware 'ticks':  (clocks, interrupt sources...)
			 *
			 *  Here, cpu0instrs is the number of instructions
			 *  executed on cpu0.  (TODO: don't use cpu 0 for this,
			 *  use some kind of "mainbus" instead.)  Hardware
			 *  ticks are not per instruction, but per cycle,
			 *  so we divide by the number of
			 *  instructions_per_cycle for cpu0.
			 *
			 *  TODO:  This doesn't work in a machine with, say,
			 *  a mixture of R3000, R4000, and R10000 CPUs, if
			 *  there ever was such a thing.
			 *
			 *  TODO 2:  A small bug occurs if cpu0instrs isn't
			 *  evenly divisible by instrs_per_cycle. We then
			 *  cause hardware ticks a fraction of a cycle too
			 *  often.
			 */
			i = instrs_per_cycle(cpus[0]);
			switch (i) {
			case 1:	break;
			case 2:	cpu0instrs >>= 1; break;
			case 4:	cpu0instrs >>= 2; break;
			default:
				cpu0instrs /= i;
			}

			for (te=0; te<machine->n_tick_entries; te++) {
				machine->ticks_till_next[te] -= cpu0instrs;

				if (machine->ticks_till_next[te] <= 0) {
					while (machine->ticks_till_next[te] <= 0)
						machine->ticks_till_next[te] +=
						    machine->ticks_reset_value[te];
					machine->tick_func[te](cpus[0], machine->tick_extra[te]);
				}
			}

			/*  Any CPU dead?  */
			for (i=0; i<ncpus; i++) {
				if (cpus[i]->dead &&
				    machine->exit_without_entering_debugger == 0)
					single_step = 1;
			}

			machine->ncycles += cpu0instrs;
		} while (running && (machine->ncycles < ncycles_chunk_end));

		/*  If we've done buffered console output,
		    the flush stdout every now and then:  */
		if (machine->ncycles > machine->ncycles_flush + (1<<16)) {
			console_flush();
			machine->ncycles_flush = machine->ncycles;
		}

		if (machine->ncycles > machine->ncycles_show + (1<<23)) {
			cpu_show_cycles(machine, &machine->starttime,
			    machine->ncycles, 0);
			machine->ncycles_show = machine->ncycles;
		}

		if (max_instructions_cached != 0 &&
		    machine->ncycles >= max_instructions_cached)
			running = 0;

		/*  Let's allow other machines to run.  */
		rounds ++;
		if (rounds > 16)
			break;
	}

	return running;
}

