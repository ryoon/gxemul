#ifndef	CPU_H
#define	CPU_H

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
 *  $Id: cpu.h,v 1.1 2005-01-30 00:37:07 debug Exp $
 *
 *  See cpu_common.c.
 */


#include <sys/types.h>
#include <inttypes.h>

struct cpu;
struct emul;
struct machine;
struct memory;


#define	NO_BYTE_ORDER_OVERRIDE		-1
#define	EMUL_LITTLE_ENDIAN		0
#define	EMUL_BIG_ENDIAN			1


/*  cpu_common.c:  */
struct cpu *cpu_new(struct memory *mem, struct machine *machine,
        int cpu_id, char *cpu_type_name);
void cpu_show_full_statistics(struct machine *m);
void cpu_tlbdump(struct machine *m, int x, int rawflag);
void cpu_register_dump(struct cpu *cpu, int gprs, int coprocs);
void cpu_disassemble_instr(struct cpu *cpu, unsigned char *instr,
        int running, uint64_t addr, int bintrans);
int cpu_interrupt(struct cpu *cpu, uint64_t irq_nr);
int cpu_interrupt_ack(struct cpu *cpu, uint64_t irq_nr);
void cpu_run_init(struct emul *emul, struct machine *machine);
int cpu_run(struct emul *emul, struct machine *machine);
void cpu_run_deinit(struct emul *emul, struct machine *machine);
void cpu_dumpinfo(struct cpu *cpu);


#endif	/*  CPU_H  */
