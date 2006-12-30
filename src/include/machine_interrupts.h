#ifndef	MACHINE_INTERRUPTS_H
#define	MACHINE_INTERRUPTS_H

/*
 *  Copyright (C) 2005-2007  Anders Gavare.  All rights reserved.
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
 *  $Id: machine_interrupts.h,v 1.3 2006-12-30 13:31:01 debug Exp $
 */

#include "machine.h"

void kn02_interrupt(struct machine *m, struct cpu *cpu, int irq_nr, int assrt);
void kmin_interrupt(struct machine *m, struct cpu *cpu, int irq_nr, int assrt);
void kn03_interrupt(struct machine *m, struct cpu *cpu, int irq_nr, int assrt);
void maxine_interrupt(struct machine *m, struct cpu *cpu,
	int irq_nr, int assrt);
void kn230_interrupt(struct machine *m, struct cpu *, int irq_nr, int assrt);
void jazz_interrupt(struct machine *m, struct cpu *cpu, int irq_nr, int assrt);
void vr41xx_interrupt(struct machine *m, struct cpu *, int irq_nr, int assrt);
void ps2_interrupt(struct machine *m, struct cpu *cpu, int irq_nr, int assrt);
void sgi_ip22_interrupt(struct machine *m, struct cpu *cpu,
	int irq_nr, int assrt);
void sgi_ip30_interrupt(struct machine *m, struct cpu *cpu,
	int irq_nr, int assrt);
void sgi_ip32_interrupt(struct machine *m, struct cpu *cpu,
	int irq_nr, int assrt);
void au1x00_interrupt(struct machine *m, struct cpu *cpu,
	int irq_nr, int assrt);
void cpc700_interrupt(struct machine *m, struct cpu *cpu,
	int irq_nr, int assrt);
void isa8_interrupt(struct machine *m, struct cpu *cpu, int irq_nr, int assrt);
void x86_pc_interrupt(struct machine *m, struct cpu *, int irq_nr, int assrt);
void isa32_interrupt(struct machine *m, struct cpu *cpu, int irq_nr, int assrt);
void gc_interrupt(struct machine *m, struct cpu *cpu, int irq_nr, int assrt);
void i80321_interrupt(struct machine *m, struct cpu *cpu, int, int assrt);

#endif	/*  MACHINE_INTERRUPTS_H  */
