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
 *  $Id: cpu_common.c,v 1.3 2005-01-30 00:37:09 debug Exp $
 *
 *  Common routines for CPU emulation. (Not specific to any CPU type.)
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include "mips_cpu.h"
#include "misc.h"


/*
 *  cpu_new():
 *
 *  Create a new cpu object.
 */
struct cpu *cpu_new(struct memory *mem, struct machine *machine,
        int cpu_id, char *cpu_type_name)
{
	return mips_cpu_new(mem, machine, cpu_id, cpu_type_name);
}


/*
 *  cpu_show_full_statistics():
 *
 *  Show detailed statistics on opcode usage on each cpu.
 */
void cpu_show_full_statistics(struct machine *m)
{
	cpu_show_full_statistics(m);
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
	mips_cpu_tlbdump(m, x, rawflag);
}


/*
 *  cpu_disassemble_instr():
 *
 *  Convert an instruction word into human readable format, for instruction
 *  tracing.
 */
void cpu_disassemble_instr(struct cpu *cpu, unsigned char *instr,
        int running, uint64_t addr, int bintrans)
{
	mips_cpu_disassemble_instr(cpu, instr, running, addr, bintrans);
}


/*                       
 *  cpu_register_dump():
 *
 *  Dump cpu registers in a relatively readable format.
 *
 *  gprs: set to non-zero to dump GPRs. (CPU dependant.)
 *  coprocs: set bit 0..x to dump registers in coproc 0..x. (CPU dependant.)
 */
void cpu_register_dump(struct cpu *cpu, int gprs, int coprocs)
{
	mips_cpu_register_dump(cpu, gprs, coprocs);
}


/*
 *  cpu_interrupt():
 *
 *  Assert an interrupt.
 *  Return value is 1 if the interrupt was asserted, 0 otherwise.
 */
int cpu_interrupt(struct cpu *cpu, uint64_t irq_nr)
{
	return mips_cpu_interrupt(cpu, irq_nr);
}


/*
 *  cpu_interrupt_ack():
 *
 *  Acknowledge an interrupt.
 *  Return value is 1 if the interrupt was deasserted, 0 otherwise.
 */
int cpu_interrupt_ack(struct cpu *cpu, uint64_t irq_nr)
{
	return mips_cpu_interrupt_ack(cpu, irq_nr);
}


/*
 *  cpu_run_init():
 *
 *  Prepare to run instructions on all CPUs in this machine. (This function
 *  should only need to be called once for each machine.)
 */
void cpu_run_init(struct emul *emul, struct machine *machine)
{
	mips_cpu_run_init(emul, machine);
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
	return mips_cpu_run(emul, machine);
}


/*
 *  cpu_run_deinit():
 *                               
 *  Shuts down all CPUs in a machine when ending a simulation. (This function
 *  should only need to be called once for each machine.)
 */                             
void cpu_run_deinit(struct emul *emul, struct machine *machine)
{
	mips_cpu_run_deinit(emul, machine);
}


/*
 *  cpu_dumpinfo():
 *
 *  Dumps info about a CPU using debug().
 */
void cpu_dumpinfo(struct cpu *cpu)
{
	mips_cpu_dumpinfo(cpu);
}

