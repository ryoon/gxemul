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
 *  $Id: cpu_common.c,v 1.1 2005-01-23 13:43:07 debug Exp $
 *
 *  Common routines for CPU emulation. (Not specific to any CPU type.)
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include "mips_cpu.h"
#include "misc.h"


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

