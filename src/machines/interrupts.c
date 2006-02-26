/*
 *  Copyright (C) 2003-2006  Anders Gavare.  All rights reserved.
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
 *  $Id: interrupts.c,v 1.8 2006-02-26 21:39:12 debug Exp $
 *
 *  Machine-dependent interrupt glue.
 */

#include <stdio.h>
#include <string.h>

#include "cpu.h"
#include "devices.h"
#include "machine.h"
#include "machine_interrupts.h"
#include "misc.h"

#include "crimereg.h"

#include "dec_kmin.h"
#include "dec_kn01.h"
#include "dec_kn02.h"
#include "dec_kn03.h"
#include "dec_5100.h"
#include "dec_maxine.h"


/*
 *  DECstation KN02 interrupts:
 */
void kn02_interrupt(struct machine *m, struct cpu *cpu, int irq_nr, int assrt)
{
	int current;

	irq_nr -= 8;
	irq_nr &= 0xff;

	if (assrt) {
		/*  OR in the irq_nr into the CSR:  */
		m->md_int.kn02_csr->csr[0] |= irq_nr;
	} else {
		/*  AND out the irq_nr from the CSR:  */
		m->md_int.kn02_csr->csr[0] &= ~irq_nr;
	}

	current = m->md_int.kn02_csr->csr[0] & m->md_int.kn02_csr->csr[2];
	if (current == 0)
		cpu_interrupt_ack(cpu, 2);
	else
		cpu_interrupt(cpu, 2);
}


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
 *  DECstation KN230 interrupts:
 */
void kn230_interrupt(struct machine *m, struct cpu *cpu, int irq_nr, int assrt)
{
	int r2 = 0;

	m->md_int.kn230_csr->csr |= irq_nr;

	switch (irq_nr) {
	case KN230_CSR_INTR_SII:
	case KN230_CSR_INTR_LANCE:
		r2 = 3;
		break;
	case KN230_CSR_INTR_DZ0:
	case KN230_CSR_INTR_OPT0:
	case KN230_CSR_INTR_OPT1:
		r2 = 2;
		break;
	default:
		fatal("kn230_interrupt(): irq_nr = %i ?\n", irq_nr);
	}

	if (assrt) {
		/*  OR in the irq_nr mask into the CSR:  */
		m->md_int.kn230_csr->csr |= irq_nr;

		/*  Assert MIPS interrupt 2 or 3:  */
		cpu_interrupt(cpu, r2);
	} else {
		/*  AND out the irq_nr mask from the CSR:  */
		m->md_int.kn230_csr->csr &= ~irq_nr;

		/*  If the CSR interrupt bits are all zero,
		    clear the bit in the cause register as well.  */
		if (r2 == 2) {
			/*  irq 2:  */
			if ((m->md_int.kn230_csr->csr & (KN230_CSR_INTR_DZ0
			    | KN230_CSR_INTR_OPT0 | KN230_CSR_INTR_OPT1)) == 0)
				cpu_interrupt_ack(cpu, r2);
		} else {
			/*  irq 3:  */
			if ((m->md_int.kn230_csr->csr & (KN230_CSR_INTR_SII |
			    KN230_CSR_INTR_LANCE)) == 0)
				cpu_interrupt_ack(cpu, r2);
		}
	}
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
 *  VR41xx interrupt routine:
 *
 *  irq_nr = 8 + x
 *	x = 0..15 for level1
 *	x = 16..31 for level2
 *	x = 32+y for GIU interrupt y
 */
void vr41xx_interrupt(struct machine *m, struct cpu *cpu, int irq_nr, int assrt)
{
	int giu_irq = 0;

	irq_nr -= 8;
	if (irq_nr >= 32) {
		giu_irq = irq_nr - 32;

		if (assrt)
			m->md_int.vr41xx_data->giuint |= (1 << giu_irq);
		else
			m->md_int.vr41xx_data->giuint &= ~(1 << giu_irq);
	}

	/*  TODO: This is wrong. What about GIU bit 8?  */

	if (irq_nr != 8) {
		/*  If any GIU bit is asserted, then assert the main
		    GIU interrupt:  */
		if (m->md_int.vr41xx_data->giuint &
		    m->md_int.vr41xx_data->giumask)
			vr41xx_interrupt(m, cpu, 8 + 8, 1);
		else
			vr41xx_interrupt(m, cpu, 8 + 8, 0);
	}

	/*  debug("vr41xx_interrupt(): irq_nr=%i assrt=%i\n",
	    irq_nr, assrt);  */

	if (irq_nr < 16) {
		if (assrt)
			m->md_int.vr41xx_data->sysint1 |= (1 << irq_nr);
		else
			m->md_int.vr41xx_data->sysint1 &= ~(1 << irq_nr);
	} else if (irq_nr < 32) {
		irq_nr -= 16;
		if (assrt)
			m->md_int.vr41xx_data->sysint2 |= (1 << irq_nr);
		else
			m->md_int.vr41xx_data->sysint2 &= ~(1 << irq_nr);
	}

	/*  TODO: Which hardware interrupt pin?  */

	/*  debug("    sysint1=%04x mask=%04x, sysint2=%04x mask=%04x\n",
	    m->md_int.vr41xx_data->sysint1, m->md_int.vr41xx_data->msysint1,
	    m->md_int.vr41xx_data->sysint2, m->md_int.vr41xx_data->msysint2); */

	if ((m->md_int.vr41xx_data->sysint1 & m->md_int.vr41xx_data->msysint1) |
	    (m->md_int.vr41xx_data->sysint2 & m->md_int.vr41xx_data->msysint2))
		cpu_interrupt(cpu, 2);
	else
		cpu_interrupt_ack(cpu, 2);
}


/*
 *  Playstation 2 interrupt routine:
 *
 *  irq_nr =	8 + x		normal irq x
 *		8 + 16 + y	dma irq y
 *		8 + 32 + 0	sbus irq 0 (pcmcia)
 *		8 + 32 + 1	sbus irq 1 (usb)
 */
void ps2_interrupt(struct machine *m, struct cpu *cpu, int irq_nr, int assrt)
{
	irq_nr -= 8;
	debug("ps2_interrupt(): irq_nr=0x%x assrt=%i\n", irq_nr, assrt);

	if (irq_nr >= 32) {
		int msk = 0;
		switch (irq_nr - 32) {
		case 0:	/*  PCMCIA:  */
			msk = 0x100;
			break;
		case 1:	/*  USB:  */
			msk = 0x400;
			break;
		default:
			fatal("ps2_interrupt(): bad irq_nr %i\n", irq_nr);
		}

		if (assrt)
			m->md_int.ps2_data->sbus_smflg |= msk;
		else
			m->md_int.ps2_data->sbus_smflg &= ~msk;

		if (m->md_int.ps2_data->sbus_smflg != 0)
			cpu_interrupt(cpu, 8 + 1);
		else
			cpu_interrupt_ack(cpu, 8 + 1);
		return;
	}

	if (assrt) {
		/*  OR into the INTR:  */
		if (irq_nr < 16)
			m->md_int.ps2_data->intr |= (1 << irq_nr);
		else
			m->md_int.ps2_data->dmac_reg[0x601] |=
			    (1 << (irq_nr-16));
	} else {
		/*  AND out of the INTR:  */
		if (irq_nr < 16)
			m->md_int.ps2_data->intr &= ~(1 << irq_nr);
		else
			m->md_int.ps2_data->dmac_reg[0x601] &=
			    ~(1 << (irq_nr-16));
	}

	/*  TODO: Hm? How about the mask?  */
	if (m->md_int.ps2_data->intr /*  & m->md_int.ps2_data->imask */ )
		cpu_interrupt(cpu, 2);
	else
		cpu_interrupt_ack(cpu, 2);

	/*  TODO: mask?  */
	if (m->md_int.ps2_data->dmac_reg[0x601] & 0xffff)
		cpu_interrupt(cpu, 3);
	else
		cpu_interrupt_ack(cpu, 3);
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
 *  SGI "IP32" interrupt routine:
 */
void sgi_ip32_interrupt(struct machine *m, struct cpu *cpu,
	int irq_nr, int assrt)
{
	/*
	 *  The 64-bit word at crime offset 0x10 is CRIME_INTSTAT,
	 *  which contains the current interrupt bits. CRIME_INTMASK
	 *  contains a mask of which bits are actually in use.
	 *
	 *  crime hardcoded at 0x14000000, for SGI-IP32.
	 *  If any of these bits are asserted, then physical MIPS
	 *  interrupt 2 should be asserted.
	 *
	 *  TODO:  how should all this be done nicely?
	 */

	uint64_t crime_addr = CRIME_INTSTAT;
	uint64_t mace_addr = 0x10;
	uint64_t crime_interrupts, crime_interrupts_mask;
	uint64_t mace_interrupts, mace_interrupt_mask;
	unsigned int i;
	unsigned char x[8];

	/*  Read current MACE interrupt assertions:  */
	memcpy(x, m->md_int.ip32.mace_data->reg + mace_addr,
	    sizeof(uint64_t));
	mace_interrupts = 0;
	for (i=0; i<sizeof(uint64_t); i++) {
		mace_interrupts <<= 8;
		mace_interrupts |= x[i];
	}

	/*  Read current MACE interrupt mask:  */
	memcpy(x, m->md_int.ip32.mace_data->reg + mace_addr + 8,
	    sizeof(uint64_t));
	mace_interrupt_mask = 0;
	for (i=0; i<sizeof(uint64_t); i++) {
		mace_interrupt_mask <<= 8;
		mace_interrupt_mask |= x[i];
	}

	/*
	 *  This mapping of both MACE and CRIME interrupts into the same
	 *  'int' is really ugly.
	 *
	 *  If MACE_PERIPH_MISC or MACE_PERIPH_SERIAL is set, then mask
	 *  that bit out and treat the rest of the word as the mace interrupt
	 *  bitmask.
	 *
	 *  TODO: fix.
	 */
	if (irq_nr & MACE_PERIPH_SERIAL) {
		if (assrt)
			mace_interrupts |= (irq_nr & ~MACE_PERIPH_SERIAL);
		else
			mace_interrupts &= ~(irq_nr & ~MACE_PERIPH_SERIAL);

		irq_nr = MACE_PERIPH_SERIAL;
		if ((mace_interrupts & mace_interrupt_mask) == 0)
			assrt = 0;
		else
			assrt = 1;
	}

	/*  Hopefully _MISC and _SERIAL will not be both on at the same time. */
	if (irq_nr & MACE_PERIPH_MISC) {
		if (assrt)
			mace_interrupts |= (irq_nr & ~MACE_PERIPH_MISC);
		else
			mace_interrupts &= ~(irq_nr & ~MACE_PERIPH_MISC);

		irq_nr = MACE_PERIPH_MISC;
		if ((mace_interrupts & mace_interrupt_mask) == 0)
			assrt = 0;
		else
			assrt = 1;
	}

	/*  Write back MACE interrupt assertions:  */
	for (i=0; i<sizeof(uint64_t); i++)
		x[7-i] = mace_interrupts >> (i*8);
	memcpy(m->md_int.ip32.mace_data->reg + mace_addr, x, sizeof(uint64_t));

	/*  Read CRIME_INTSTAT:  */
	memcpy(x, m->md_int.ip32.crime_data->reg + crime_addr,
	    sizeof(uint64_t));
	crime_interrupts = 0;
	for (i=0; i<sizeof(uint64_t); i++) {
		crime_interrupts <<= 8;
		crime_interrupts |= x[i];
	}

	if (assrt)
		crime_interrupts |= irq_nr;
	else
		crime_interrupts &= ~irq_nr;

	/*  Write back CRIME_INTSTAT:  */
	for (i=0; i<sizeof(uint64_t); i++)
		x[7-i] = crime_interrupts >> (i*8);
	memcpy(m->md_int.ip32.crime_data->reg + crime_addr, x,
	    sizeof(uint64_t));

	/*  Read CRIME_INTMASK:  */
	memcpy(x, m->md_int.ip32.crime_data->reg + CRIME_INTMASK,
	    sizeof(uint64_t));
	crime_interrupts_mask = 0;
	for (i=0; i<sizeof(uint64_t); i++) {
		crime_interrupts_mask <<= 8;
		crime_interrupts_mask |= x[i];
	}

	if ((crime_interrupts & crime_interrupts_mask) == 0)
		cpu_interrupt_ack(cpu, 2);
	else
		cpu_interrupt(cpu, 2);

	/*  printf("sgi_crime_machine_irq(%i,%i): new interrupts = 0x%08x\n",
	    assrt, irq_nr, crime_interrupts);  */
}


/*
 *  Au1x00 interrupt routine:
 *
 *  TODO: This is just bogus so far.  For more info, read this:
 *  http://www.meshcube.org/cgi-bin/viewcvs.cgi/kernel/linux/arch/
 *	mips/au1000/common/
 *
 *  CPU int 2 = IC 0, request 0
 *  CPU int 3 = IC 0, request 1
 *  CPU int 4 = IC 1, request 0
 *  CPU int 5 = IC 1, request 1
 *
 *  Interrupts 0..31 are on interrupt controller 0, interrupts 32..63 are
 *  on controller 1.
 *
 *  Special case: if irq_nr == 64+8, then this just updates the CPU
 *  interrupt assertions.
 */
void au1x00_interrupt(struct machine *m, struct cpu *cpu,
	int irq_nr, int assrt)
{
	uint32_t ms;

	irq_nr -= 8;
	debug("au1x00_interrupt(): irq_nr=%i assrt=%i\n", irq_nr, assrt);

	if (irq_nr < 64) {
		ms = 1 << (irq_nr & 31);

		if (assrt)
			m->md_int.au1x00_ic_data->request0_int |= ms;
		else
			m->md_int.au1x00_ic_data->request0_int &= ~ms;

		/*  TODO: Controller 1  */
	}

	if ((m->md_int.au1x00_ic_data->request0_int &
	    m->md_int.au1x00_ic_data->mask) != 0)
		cpu_interrupt(cpu, 2);
	else
		cpu_interrupt_ack(cpu, 2);

	/*  TODO: What _is_ request1?  */

	/*  TODO: Controller 1  */
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
 *  Interrupt function for Cobalt, evbmips (Malta), and Algor.
 *
 *  (irq_nr = 8 + 16 can be used to just reassert/deassert interrupts.)
 */
void isa8_interrupt(struct machine *m, struct cpu *cpu, int irq_nr, int assrt)
{
	int mask, x;
	int old_isa_assert, new_isa_assert;

	old_isa_assert = m->isa_pic_data.pic1->irr & ~m->isa_pic_data.pic1->ier;

	irq_nr -= 8;
	mask = 1 << (irq_nr & 7);

	if (irq_nr < 8) {
		if (assrt)
			m->isa_pic_data.pic1->irr |= mask;
		else
			m->isa_pic_data.pic1->irr &= ~mask;
	} else if (irq_nr < 16) {
		if (assrt)
			m->isa_pic_data.pic2->irr |= mask;
		else
			m->isa_pic_data.pic2->irr &= ~mask;
	}

	/*  Any interrupt assertions on PIC2 go to irq 2 on PIC1  */
	/*  (TODO: don't hardcode this here)  */
	if (m->isa_pic_data.pic2->irr & ~m->isa_pic_data.pic2->ier)
		m->isa_pic_data.pic1->irr |= 0x04;
	else
		m->isa_pic_data.pic1->irr &= ~0x04;

	/*  Now, PIC1:  */
	new_isa_assert = m->isa_pic_data.pic1->irr & ~m->isa_pic_data.pic1->ier;
	if (old_isa_assert != new_isa_assert) {
		for (x=0; x<16; x++) {
			if (x == 2)
			        continue;
			if (x < 8 && (m->isa_pic_data.pic1->irr &
			    ~m->isa_pic_data.pic1->ier & (1 << x)))
			        break;
			if (x >= 8 && (m->isa_pic_data.pic2->irr &
			    ~m->isa_pic_data.pic2->ier & (1 << (x&7))))
			        break;
		}
		m->isa_pic_data.last_int = x;
	}

	if (m->isa_pic_data.pic1->irr & ~m->isa_pic_data.pic1->ier)
		cpu_interrupt(cpu, m->isa_pic_data.native_irq);
	else
		cpu_interrupt_ack(cpu, m->isa_pic_data.native_irq);
}


/*
 *  x86 (PC) interrupts:
 *
 *  (irq_nr = 16 can be used to just reassert/deassert interrupts.)
 */
void x86_pc_interrupt(struct machine *m, struct cpu *cpu, int irq_nr, int assrt)
{
	int mask = 1 << (irq_nr & 7);

	if (irq_nr < 8) {
		if (assrt)
			m->isa_pic_data.pic1->irr |= mask;
		else
			m->isa_pic_data.pic1->irr &= ~mask;
	} else if (irq_nr < 16) {
		if (m->isa_pic_data.pic2 == NULL) {
			fatal("x86_pc_interrupt(): pic2 used (irq_nr = %i), "
			    "but we are emulating an XT?\n", irq_nr);
			return;
		}
		if (assrt)
			m->isa_pic_data.pic2->irr |= mask;
		else
			m->isa_pic_data.pic2->irr &= ~mask;
	}

	if (m->isa_pic_data.pic2 != NULL) {
		/*  Any interrupt assertions on PIC2 go to irq 2 on PIC1  */
		/*  (TODO: don't hardcode this here)  */
		if (m->isa_pic_data.pic2->irr & ~m->isa_pic_data.pic2->ier)
			m->isa_pic_data.pic1->irr |= 0x04;
		else
			m->isa_pic_data.pic1->irr &= ~0x04;
	}

	/*  Now, PIC1:  */
	if (m->isa_pic_data.pic1->irr & ~m->isa_pic_data.pic1->ier)
		cpu->cd.x86.interrupt_asserted = 1;
	else
		cpu->cd.x86.interrupt_asserted = 0;
}


/*
 *  "Generic" ISA interrupt management, 32 native interrupts, 16 ISA
 *  interrupts.  So far: Footbridge (CATS, NetWinder), BeBox, and PReP.
 *
 *  0..31  = footbridge interrupt
 *  32..47 = ISA interrupts
 *  48     = ISA reassert
 *  64     = reassert (non-ISA)
 */
void isa32_interrupt(struct machine *m, struct cpu *cpu, int irq_nr, int assrt)
{
	uint32_t mask = 1 << (irq_nr & 31);
	int old_isa_assert, new_isa_assert;

	old_isa_assert = m->isa_pic_data.pic1->irr & ~m->isa_pic_data.pic1->ier;

	if (irq_nr >= 32 && irq_nr < 32 + 8) {
		int mm = 1 << (irq_nr & 7);
		if (assrt)
			m->isa_pic_data.pic1->irr |= mm;
		else
			m->isa_pic_data.pic1->irr &= ~mm;
	} else if (irq_nr >= 32+8 && irq_nr < 32+16) {
		int mm = 1 << (irq_nr & 7);
		if (assrt)
			m->isa_pic_data.pic2->irr |= mm;
		else
			m->isa_pic_data.pic2->irr &= ~mm;
	}

	/*  Any interrupt assertions on PIC2 go to irq 2 on PIC1  */
	/*  (TODO: don't hardcode this here)  */
	if (m->isa_pic_data.pic2->irr & ~m->isa_pic_data.pic2->ier)
		m->isa_pic_data.pic1->irr |= 0x04;
	else
		m->isa_pic_data.pic1->irr &= ~0x04;

	/*  Now, PIC1:  */
	new_isa_assert = m->isa_pic_data.pic1->irr & ~m->isa_pic_data.pic1->ier;
	if (old_isa_assert != new_isa_assert || irq_nr == 48) {
		if (new_isa_assert) {
			int x;
			for (x=0; x<16; x++) {
				if (x == 2)
				        continue;
				if (x < 8 && (m->isa_pic_data.pic1->irr &
				    ~m->isa_pic_data.pic1->ier & (1 << x)))
				        break;
				if (x >= 8 && (m->isa_pic_data.pic2->irr &
				    ~m->isa_pic_data.pic2->ier & (1 << (x&7))))
				        break;
			}
			m->isa_pic_data.last_int = x;
			cpu_interrupt(cpu, m->isa_pic_data.native_irq);
		} else
			cpu_interrupt_ack(cpu, m->isa_pic_data.native_irq);
		return;
	}

	switch (m->machine_type) {
	case MACHINE_CATS:
	case MACHINE_NETWINDER:
		if (irq_nr < 32) {
			if (assrt)
				m->md_int.footbridge_data->irq_status |= mask;
			else
				m->md_int.footbridge_data->irq_status &= ~mask;
		}
		if (m->md_int.footbridge_data->irq_status &
		    m->md_int.footbridge_data->irq_enable)
			cpu_interrupt(cpu, 65);
		else
			cpu_interrupt_ack(cpu, 65);
		break;
	case MACHINE_BEBOX:
		if (irq_nr < 32) {
			if (assrt)
				m->md_int.bebox_data->int_status |= mask;
			else
				m->md_int.bebox_data->int_status &= ~mask;
		}
		if (m->md_int.bebox_data->int_status &
		    m->md_int.bebox_data->cpu0_int_mask)
			cpu_interrupt(m->cpus[0], 65);
		else
			cpu_interrupt_ack(m->cpus[0], 65);
		if (m->ncpus > 1 &&
		    m->md_int.bebox_data->int_status &
		    m->md_int.bebox_data->cpu1_int_mask)
			cpu_interrupt(m->cpus[1], 65);
		else
			cpu_interrupt_ack(m->cpus[1], 65);
		break;
	case MACHINE_PREP:
	case MACHINE_MVMEPPC:
		if (irq_nr < 32) {
			if (assrt)
				m->md_int.prep_data->int_status |= mask;
			else
				m->md_int.prep_data->int_status &= ~mask;
		}
		if (m->md_int.prep_data->int_status & 2)
			cpu_interrupt(cpu, 65);
		else
			cpu_interrupt_ack(cpu, 65);
		break;
	}
}


/*
 *  Grand Central interrupt handler.
 *
 *  (Used by MacPPC.)
 */
void gc_interrupt(struct machine *m, struct cpu *cpu, int irq_nr, int assrt)
{
	uint32_t mask = 1 << (irq_nr & 31);
	if (irq_nr < 32) {
		if (assrt)
			m->md_int.gc_data->status_lo |= mask;
		else
			m->md_int.gc_data->status_lo &= ~mask;
	}
	if (irq_nr >= 32 && irq_nr < 64) {
		if (assrt)
			m->md_int.gc_data->status_hi |= mask;
		else
			m->md_int.gc_data->status_hi &= ~mask;
	}

#if 0
	printf("status = %08x %08x  enable = %08x %08x\n",
	    m->md_int.gc_data->status_hi, m->md_int.gc_data->status_lo,
	    m->md_int.gc_data->enable_hi, m->md_int.gc_data->enable_lo);
#endif

	if (m->md_int.gc_data->status_lo & m->md_int.gc_data->enable_lo ||
	    m->md_int.gc_data->status_hi & m->md_int.gc_data->enable_hi)
		cpu_interrupt(m->cpus[0], 65);
	else
		cpu_interrupt_ack(m->cpus[0], 65);
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

