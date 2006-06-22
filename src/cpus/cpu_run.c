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
 *  $Id: cpu_run.c,v 1.9 2006-06-22 13:22:41 debug Exp $
 *
 *  Included from cpu_mips.c, cpu_ppc.c etc.  (The reason for this is that
 *  the call to a specific cpu's routine that runs one instruction will
 *  be inlined from here.)
 *
 *  TODO: Rewrite/cleanup. This is too ugly and inefficient! Also, the
 *        dyntrans stuff doesn't require this kind of complexity, it can be a
 *        lot simpler.
 */

#include "console.h"
#include "debugger.h"


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
	int64_t ncycles_chunk_end;
	int running, rounds;

	/*  The main loop:  */
	running = 1;
	rounds = 0;
	while (running || single_step) {
		ncycles_chunk_end = machine->ncycles + (1 << 17);

		machine->a_few_instrs = machine->a_few_cycles;

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
					/*
					 *  TODO: (Important!)
					 *
					 *  If these are enabled, and focus is
					 *  shifted to another machine in the
					 *  debugger, then the wrong machine
					 *  gets its variables restored!
					 */
					old_instruction_trace =
					    machine->instruction_trace;
					old_quiet_mode = quiet_mode;
					old_show_trace_tree =
					    machine->show_trace_tree;
					machine->instruction_trace = 1;
					machine->show_trace_tree = 1;
					quiet_mode = 0;
					single_step = 2;
				}

				if (single_step)
					debugger();
				for (i=0; i<ncpus; i++)
					if (cpus[i]->running) {
						int instrs_run =
						    CPU_RINSTR(emul,
						    cpus[i]);
						if (i == 0)
							cpu0instrs +=
							    instrs_run;
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
					a_few_instrs2 = machine->a_few_cycles;
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
			 *  use some kind of "mainbus" instead.)
			 */

			for (te=0; te<machine->n_tick_entries; te++) {
				machine->ticks_till_next[te] -= cpu0instrs;
				if (machine->ticks_till_next[te] <= 0) {
					while (machine->ticks_till_next[te]
					    <= 0)
						machine->ticks_till_next[te] +=
						    machine->
						    ticks_reset_value[te];
					machine->tick_func[te](cpus[0],
					    machine->tick_extra[te]);
				}
			}

			/*  Any CPU dead?  */
			for (i=0; i<ncpus; i++) {
				if (cpus[i]->dead && machine->
				    exit_without_entering_debugger == 0)
					single_step = 1;
			}

			machine->ncycles += cpu0instrs;
		} while (running && (machine->ncycles < ncycles_chunk_end));

		/*  If we've done buffered console output,
		    the flush stdout every now and then:  */
		if (machine->ncycles > machine->ncycles_flush + (1<<17)) {
			console_flush();
			machine->ncycles_flush = machine->ncycles;
		}

		if (machine->ncycles > machine->ncycles_show + (1<<25)) {
			machine->ncycles_since_gettimeofday +=
			    (machine->ncycles - machine->ncycles_show);
			cpu_show_cycles(machine, 0);
			machine->ncycles_show = machine->ncycles;
		}

		/*  Let's allow other machines to run.  */
		rounds ++;
		if (rounds > 2)
			break;
	}

	return running;
}

