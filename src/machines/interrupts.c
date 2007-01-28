/*
 *  Copyright (C) 2003-2007  Anders Gavare.  All rights reserved.
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
 *  $Id: interrupts.c,v 1.24 2007-01-28 11:29:53 debug Exp $
 *
 *  Machine-dependent interrupt glue.
 *
 *
 *  NOTE: This file is legacy code, and should be removed as soon as it
 *        has been rewritten / moved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "devices.h"
#include "machine.h"
#include "machine_interrupts.h"
#include "misc.h"

#include "dec_kmin.h"
#include "dec_kn01.h"
#include "dec_kn03.h"
#include "dec_maxine.h"


/*
 *  DECstation KMIN interrupts:
 *
 *  TC slot 3 = system slot.
 */
void kmin_interrupt(struct machine *m, struct cpu *cpu, int irq_nr, int assrt)
{
	irq_nr -= 8;
	/*  debug("kmin_interrupt(): irq_nr=%i assrt=%i\n", irq_nr, assrt);  */

	if (assrt)
		m->md_int.dec_ioasic_data->reg[(IOASIC_INTR -
		    IOASIC_SLOT_1_START) / 0x10] |= irq_nr;
	else
		m->md_int.dec_ioasic_data->reg[(IOASIC_INTR -
		    IOASIC_SLOT_1_START) / 0x10] &= ~irq_nr;

	if (m->md_int.dec_ioasic_data->reg[(IOASIC_INTR - IOASIC_SLOT_1_START)
	    / 0x10] & m->md_int.dec_ioasic_data->reg[(IOASIC_IMSK -
	    IOASIC_SLOT_1_START) / 0x10])
		cpu_interrupt(cpu, KMIN_INT_TC3);
	else
		cpu_interrupt_ack(cpu, KMIN_INT_TC3);
}


/*
 *  DECstation KN03 interrupts:
 */
void kn03_interrupt(struct machine *m, struct cpu *cpu, int irq_nr, int assrt)
{
	irq_nr -= 8;
	/*  debug("kn03_interrupt(): irq_nr=0x%x assrt=%i\n",
	    irq_nr, assrt);  */

	if (assrt)
		m->md_int.dec_ioasic_data->reg[(IOASIC_INTR -
		    IOASIC_SLOT_1_START) / 0x10] |= irq_nr;
	else
		m->md_int.dec_ioasic_data->reg[(IOASIC_INTR -
		    IOASIC_SLOT_1_START) / 0x10] &= ~irq_nr;

	if (m->md_int.dec_ioasic_data->reg[(IOASIC_INTR - IOASIC_SLOT_1_START)
	    / 0x10] & m->md_int.dec_ioasic_data->reg[(IOASIC_IMSK -
	    IOASIC_SLOT_1_START) / 0x10])
		cpu_interrupt(cpu, KN03_INT_ASIC);
	else
		cpu_interrupt_ack(cpu, KN03_INT_ASIC);
}


/*
 *  DECstation MAXINE interrupts:
 */
void maxine_interrupt(struct machine *m, struct cpu *cpu,
	int irq_nr, int assrt)
{
	irq_nr -= 8;
	debug("maxine_interrupt(): irq_nr=0x%x assrt=%i\n", irq_nr, assrt);

	if (assrt)
		m->md_int.dec_ioasic_data->reg[(IOASIC_INTR -
		    IOASIC_SLOT_1_START) / 0x10] |= irq_nr;
	else
		m->md_int.dec_ioasic_data->reg[(IOASIC_INTR -
		    IOASIC_SLOT_1_START) / 0x10] &= ~irq_nr;

	if (m->md_int.dec_ioasic_data->reg[(IOASIC_INTR - IOASIC_SLOT_1_START)
	    / 0x10] & m->md_int.dec_ioasic_data->reg[(IOASIC_IMSK -
	    IOASIC_SLOT_1_START) / 0x10])
		cpu_interrupt(cpu, XINE_INT_TC3);
	else
		cpu_interrupt_ack(cpu, XINE_INT_TC3);
}


/*
 *  Jazz interrupts (for Acer PICA-61 etc):
 *
 *  0..7			MIPS interrupts
 *  8 + x, where x = 0..15	Jazz interrupts
 *  8 + x, where x = 16..31	ISA interrupt (irq nr + 16)
 */
void jazz_interrupt(struct machine *m, struct cpu *cpu, int irq_nr, int assrt)
{
	uint32_t irq;
	int isa = 0;

	irq_nr -= 8;

	/*  debug("jazz_interrupt() irq_nr = %i, assrt = %i\n",
		irq_nr, assrt);  */

	if (irq_nr >= 16) {
		isa = 1;
		irq_nr -= 16;
	}

	irq = 1 << irq_nr;

	if (isa) {
		if (assrt)
			m->md_int.jazz_data->isa_int_asserted |= irq;
		else
			m->md_int.jazz_data->isa_int_asserted &= ~irq;
	} else {
		if (assrt)
			m->md_int.jazz_data->int_asserted |= irq;
		else
			m->md_int.jazz_data->int_asserted &= ~irq;
	}

	/*  debug("   %08x %08x\n", m->md_int.jazz_data->int_asserted,
		m->md_int.jazz_data->int_enable_mask);  */
	/*  debug("   %08x %08x\n", m->md_int.jazz_data->isa_int_asserted,
		m->md_int.jazz_data->isa_int_enable_mask);  */

	if (m->md_int.jazz_data->int_asserted
	    /* & m->md_int.jazz_data->int_enable_mask  */ & ~0x8000 )
		cpu_interrupt(cpu, 3);
	else
		cpu_interrupt_ack(cpu, 3);

	if (m->md_int.jazz_data->isa_int_asserted &
	    m->md_int.jazz_data->isa_int_enable_mask)
		cpu_interrupt(cpu, 4);
	else
		cpu_interrupt_ack(cpu, 4);

	/*  TODO: this "15" (0x8000) is the timer... fix this?  */
	if (m->md_int.jazz_data->int_asserted & 0x8000)
		cpu_interrupt(cpu, 6);
	else
		cpu_interrupt_ack(cpu, 6);
}


/*
 *  SGI "IP22" interrupt routine:
 */
void sgi_ip22_interrupt(struct machine *m, struct cpu *cpu,
	int irq_nr, int assrt)
{
	/*
	 *  SGI-IP22 specific interrupt stuff:
	 *
	 *  irq_nr should be 8 + x, where x = 0..31 for local0,
	 *  and 32..63 for local1 interrupts.
	 *  Add 64*y for "mappable" interrupts, where 1<<y is
	 *  the mappable interrupt bitmask. TODO: this misses 64*0 !
	 */

	uint32_t newmask;
	uint32_t stat, mask;

	irq_nr -= 8;
	newmask = 1 << (irq_nr & 31);

	if (irq_nr >= 64) {
		int ms = irq_nr / 64;
		uint32_t new = 1 << ms;
		if (assrt)
			m->md_int.sgi_ip22_data->reg[4] |= new;
		else
			m->md_int.sgi_ip22_data->reg[4] &= ~new;
		/*  TODO: is this enough?  */
		irq_nr &= 63;
	}

	if (irq_nr < 32) {
		if (assrt)
			m->md_int.sgi_ip22_data->reg[0] |= newmask;
		else
			m->md_int.sgi_ip22_data->reg[0] &= ~newmask;
	} else {
		if (assrt)
			m->md_int.sgi_ip22_data->reg[2] |= newmask;
		else
			m->md_int.sgi_ip22_data->reg[2] &= ~newmask;
	}

	/*  Read stat and mask for local0:  */
	stat = m->md_int.sgi_ip22_data->reg[0];
	mask = m->md_int.sgi_ip22_data->reg[1];
	if ((stat & mask) == 0)
		cpu_interrupt_ack(cpu, 2);
	else
		cpu_interrupt(cpu, 2);

	/*  Read stat and mask for local1:  */
	stat = m->md_int.sgi_ip22_data->reg[2];
	mask = m->md_int.sgi_ip22_data->reg[3];
	if ((stat & mask) == 0)
		cpu_interrupt_ack(cpu, 3);
	else
		cpu_interrupt(cpu, 3);
}


/*
 *  SGI "IP30" interrupt routine:
 *
 *  irq_nr = 8 + 1 + nr, where nr is:
 *	0..49		HEART irqs	hardware irq 2,3,4
 *	50		HEART timer	hardware irq 5
 *	51..63		HEART errors	hardware irq 6
 *
 *  according to Linux/IP30.
 */
void sgi_ip30_interrupt(struct machine *m, struct cpu *cpu,
	int irq_nr, int assrt)
{
	uint64_t newmask;
	uint64_t stat, mask;

	irq_nr -= 8;
	if (irq_nr == 0)
		goto just_assert_and_such;
	irq_nr --;

	newmask = (int64_t)1 << irq_nr;

	if (assrt)
		m->md_int.sgi_ip30_data->isr |= newmask;
	else
		m->md_int.sgi_ip30_data->isr &= ~newmask;

just_assert_and_such:

	cpu_interrupt_ack(cpu, 2);
	cpu_interrupt_ack(cpu, 3);
	cpu_interrupt_ack(cpu, 4);
	cpu_interrupt_ack(cpu, 5);
	cpu_interrupt_ack(cpu, 6);

	stat = m->md_int.sgi_ip30_data->isr;
	mask = m->md_int.sgi_ip30_data->imask0;

	if ((stat & mask) & 0x000000000000ffffULL)
		cpu_interrupt(cpu, 2);
	if ((stat & mask) & 0x00000000ffff0000ULL)
		cpu_interrupt(cpu, 3);
	if ((stat & mask) & 0x0003ffff00000000ULL)
		cpu_interrupt(cpu, 4);
	if ((stat & mask) & 0x0004000000000000ULL)
		cpu_interrupt(cpu, 5);
	if ((stat & mask) & 0xfff8000000000000ULL)
		cpu_interrupt(cpu, 6);
}


/*
 *  CPC700 interrupt routine:
 *
 *  irq_nr should be 0..31. (32 means reassertion.)
 */
void cpc700_interrupt(struct machine *m, struct cpu *cpu,
	int irq_nr, int assrt)
{
	if (irq_nr < 32) {
		uint32_t mask = 1 << (irq_nr & 31);
		if (assrt)
			m->md_int.cpc700_data->sr |= mask;
		else
			m->md_int.cpc700_data->sr &= ~mask;
	}

	if ((m->md_int.cpc700_data->sr & m->md_int.cpc700_data->er) != 0)
		cpu_interrupt(cpu, 65);
	else
		cpu_interrupt_ack(cpu, 65);
}


/*
 *  i80321 (ARM) Interrupt Controller.
 *
 *  (Used by the IQ80321 machine.)
 */
void i80321_interrupt(struct machine *m, struct cpu *cpu, int irq_nr, int assrt)
{
	uint32_t mask = 1 << (irq_nr & 31);
	if (irq_nr < 32) {
		if (assrt)
			cpu->cd.arm.i80321_isrc |= mask;
		else
			cpu->cd.arm.i80321_isrc &= ~mask;
	}

	/*  fatal("isrc = %08x  inten = %08x\n",
	    cpu->cd.arm.i80321_isrc, cpu->cd.arm.i80321_inten);  */

	if (cpu->cd.arm.i80321_isrc & cpu->cd.arm.i80321_inten)
		cpu_interrupt(m->cpus[0], 65);
	else
		cpu_interrupt_ack(m->cpus[0], 65);
}

