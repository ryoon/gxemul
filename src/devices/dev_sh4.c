/*
 *  Copyright (C) 2006  Anders Gavare.  All rights reserved.
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
 *  $Id: dev_sh4.c,v 1.14 2006-10-27 13:12:21 debug Exp $
 *  
 *  SH4 processor specific memory mapped registers (0xf0000000 - 0xffffffff).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "console.h"
#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"
#include "timer.h"

#include "sh4_bscreg.h"
#include "sh4_cache.h"
#include "sh4_exception.h"
#include "sh4_intcreg.h"
#include "sh4_mmu.h"
#include "sh4_scifreg.h"
#include "sh4_tmureg.h"


#define	SH4_REG_BASE		0xff000000
#define	SH4_TICK_SHIFT		14
#define	N_SH4_TIMERS		3

/*  #define debug fatal  */

struct sh4_data {
	int		scif_console_handle;

	/*  Timer Management Unit:  */
	struct timer	*sh4_timer;
	uint32_t	tocr;
	uint32_t	tstr;
	uint32_t	tcnt[N_SH4_TIMERS];
	uint32_t	tcor[N_SH4_TIMERS];
	uint32_t	tcr[N_SH4_TIMERS];
	int		timer_interrupts_pending[N_SH4_TIMERS];
	double		timer_hz[N_SH4_TIMERS];
};


#define	SH4_PSEUDO_TIMER_HZ	125.0


/*
 *  sh4_timer_tick():
 *
 *  This function is called SH4_PSEUDO_TIMER_HZ times per real-world second.
 *  Its job is to update the SH4 timer counters, and if necessary, increase
 *  the number of pending interrupts.
 */
static void sh4_timer_tick(struct timer *t, void *extra)
{
	struct sh4_data *d = (struct sh4_data *) extra;
	int i;

	for (i=0; i<N_SH4_TIMERS; i++) {
		int32_t old = d->tcnt[i];

		/*  printf("tcnt[%i] = %08x   tcor[%i] = %08x\n",
		    i, d->tcnt[i], i, d->tcor[i]);  */

		/*  Only update timers that are currently started:  */
		if (!(d->tstr & (TSTR_STR0 << i)))
			continue;

		/*  Update the current count:  */
		d->tcnt[i] -= d->timer_hz[i] / SH4_PSEUDO_TIMER_HZ;

		/*  Has the timer underflowed?  */
		if ((int32_t)d->tcnt[i] < 0 && old >= 0) {
			d->tcr[i] |= TCR_UNF;

			if (d->tcr[i] & TCR_UNIE)
				d->timer_interrupts_pending[i] ++;

			/*
			 *  Set tcnt[i] to tcor[i]. Note: Since this function
			 *  is only called now and then, adding tcor[i] to
			 *  tcnt[i] produces more correct values for long
			 *  running timers.
			 */
			d->tcnt[i] += d->tcor[i];

			/*  At least make sure that tcnt is non-negative...  */
			if ((int32_t)d->tcnt[i] < 0)
				d->tcnt[i] = 0;
		}
	}
}


DEVICE_TICK(sh4)
{
	struct sh4_data *d = (struct sh4_data *) extra;
	int i;

	for (i=0; i<N_SH4_TIMERS; i++)
		if (d->timer_interrupts_pending[i] > 0)
			cpu_interrupt(cpu, SH_INTEVT_TMU0_TUNI0 + 0x20 * i);
}


DEVICE_ACCESS(sh4_itlb_aa)
{
	uint64_t idata = 0, odata = 0;
	int e = (relative_addr & SH4_ITLB_E_MASK) >> SH4_ITLB_E_SHIFT;

	if (writeflag == MEM_WRITE) {
		idata = memory_readmax64(cpu, data, len);
		cpu->cd.sh.itlb_hi[e] &=
		    ~(SH4_PTEH_VPN_MASK | SH4_PTEH_ASID_MASK);
		cpu->cd.sh.itlb_hi[e] |= (idata &
		    (SH4_ITLB_AA_VPN_MASK | SH4_ITLB_AA_ASID_MASK));
		cpu->cd.sh.itlb_lo[e] &= ~SH4_PTEL_V;
		if (idata & SH4_ITLB_AA_V)
			cpu->cd.sh.itlb_lo[e] |= SH4_PTEL_V;
	} else {
		odata = cpu->cd.sh.itlb_hi[e] &
		    (SH4_ITLB_AA_VPN_MASK | SH4_ITLB_AA_ASID_MASK);
		if (cpu->cd.sh.itlb_lo[e] & SH4_PTEL_V)
			odata |= SH4_ITLB_AA_V;
		memory_writemax64(cpu, data, len, odata);
	}

	/*  TODO: Don't invalidate everything.  */
	cpu->invalidate_translation_caches(cpu, 0, INVALIDATE_ALL);

	return 1;
}


DEVICE_ACCESS(sh4_itlb_da1)
{
	uint32_t mask = SH4_PTEL_SH | SH4_PTEL_C | SH4_PTEL_SZ_MASK |
	    SH4_PTEL_PR_MASK | SH4_PTEL_V | 0x1ffffc00;
	uint64_t idata = 0, odata = 0;
	int e = (relative_addr & SH4_ITLB_E_MASK) >> SH4_ITLB_E_SHIFT;

	if (relative_addr & 0x800000) {
		fatal("sh4_itlb_da1: TODO: da2 area\n");
		exit(1);
	}

	if (writeflag == MEM_WRITE) {
		idata = memory_readmax64(cpu, data, len);
		cpu->cd.sh.itlb_lo[e] &= ~mask;
		cpu->cd.sh.itlb_lo[e] |= (idata & mask);
	} else {
		odata = cpu->cd.sh.itlb_lo[e] & mask;
		memory_writemax64(cpu, data, len, odata);
	}

	/*  TODO: Don't invalidate everything.  */
	cpu->invalidate_translation_caches(cpu, 0, INVALIDATE_ALL);

	return 1;
}


DEVICE_ACCESS(sh4_utlb_aa)
{
	uint64_t idata = 0, odata = 0;
	int i, e = (relative_addr & SH4_UTLB_E_MASK) >> SH4_UTLB_E_SHIFT;
	int a = relative_addr & SH4_UTLB_A;

	if (writeflag == MEM_WRITE) {
		idata = memory_readmax64(cpu, data, len);
		if (a) {
			int n_hits = 0;
			for (i=0; i<SH_N_UTLB_ENTRIES; i++) {
				int sh = cpu->cd.sh.utlb_lo[i] & SH4_PTEL_SH;
				if (!(cpu->cd.sh.utlb_lo[i] & SH4_PTEL_V))
					continue;
				if ((cpu->cd.sh.utlb_hi[i] & SH4_PTEH_VPN_MASK)
				    != (idata & SH4_PTEH_VPN_MASK))
					continue;
				if (!sh && (cpu->cd.sh.utlb_lo[i] &
				    SH4_PTEH_ASID_MASK) != (idata &
				    SH4_PTEH_ASID_MASK))
					continue;
				cpu->cd.sh.utlb_lo[i] &=
				    ~(SH4_PTEL_D | SH4_PTEL_V);
				if (idata & SH4_UTLB_AA_D)
					cpu->cd.sh.utlb_lo[i] |= SH4_PTEL_D;
				if (idata & SH4_UTLB_AA_V)
					cpu->cd.sh.utlb_lo[i] |= SH4_PTEL_V;
				n_hits ++;
			}

			if (n_hits > 1)
				sh_exception(cpu,
				    EXPEVT_RESET_TLB_MULTI_HIT, 0, 0);
		} else {
			cpu->cd.sh.utlb_hi[e] &=
			    ~(SH4_PTEH_VPN_MASK | SH4_PTEH_ASID_MASK);
			cpu->cd.sh.utlb_hi[e] |= (idata &
			    (SH4_UTLB_AA_VPN_MASK | SH4_UTLB_AA_ASID_MASK));

			cpu->cd.sh.utlb_lo[e] &= ~(SH4_PTEL_D | SH4_PTEL_V);
			if (idata & SH4_UTLB_AA_D)
				cpu->cd.sh.utlb_lo[e] |= SH4_PTEL_D;
			if (idata & SH4_UTLB_AA_V)
				cpu->cd.sh.utlb_lo[e] |= SH4_PTEL_V;
		}
	} else {
		odata = cpu->cd.sh.utlb_hi[e] &
		    (SH4_UTLB_AA_VPN_MASK | SH4_UTLB_AA_ASID_MASK);
		if (cpu->cd.sh.utlb_lo[e] & SH4_PTEL_D)
			odata |= SH4_UTLB_AA_D;
		if (cpu->cd.sh.utlb_lo[e] & SH4_PTEL_V)
			odata |= SH4_UTLB_AA_V;
		memory_writemax64(cpu, data, len, odata);
	}

	/*  TODO: Don't invalidate everything.  */
	cpu->invalidate_translation_caches(cpu, 0, INVALIDATE_ALL);

	return 1;
}


DEVICE_ACCESS(sh4_utlb_da1)
{
	uint32_t mask = SH4_PTEL_WT | SH4_PTEL_SH | SH4_PTEL_D | SH4_PTEL_C
	    | SH4_PTEL_SZ_MASK | SH4_PTEL_PR_MASK | SH4_PTEL_V | 0x1ffffc00;
	uint64_t idata = 0, odata = 0;
	int e = (relative_addr & SH4_UTLB_E_MASK) >> SH4_UTLB_E_SHIFT;

	if (relative_addr & 0x800000) {
		fatal("sh4_utlb_da1: TODO: da2 area\n");
		exit(1);
	}

	if (writeflag == MEM_WRITE) {
		idata = memory_readmax64(cpu, data, len);
		cpu->cd.sh.utlb_lo[e] &= ~mask;
		cpu->cd.sh.utlb_lo[e] |= (idata & mask);
	} else {
		odata = cpu->cd.sh.utlb_lo[e] & mask;
		memory_writemax64(cpu, data, len, odata);
	}

	/*  TODO: Don't invalidate everything.  */
	cpu->invalidate_translation_caches(cpu, 0, INVALIDATE_ALL);

	return 1;
}


DEVICE_ACCESS(sh4)
{
	struct sh4_data *d = (struct sh4_data *) extra;
	uint64_t idata = 0, odata = 0;
	int timer_nr = 0;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);

	relative_addr += SH4_REG_BASE;

	switch (relative_addr) {

	/*************************************************/

	case SH4_PVR_ADDR:
		odata = cpu->cd.sh.cpu_type.pvr;
		break;

	case SH4_PRR_ADDR:
		odata = cpu->cd.sh.cpu_type.prr;
		break;

	case SH4_PTEH:
		if (writeflag == MEM_READ)
			odata = cpu->cd.sh.pteh;
		else {
			int old_asid = cpu->cd.sh.pteh & SH4_PTEH_ASID_MASK;
			cpu->cd.sh.pteh = idata;

			if ((idata & SH4_PTEH_ASID_MASK) != old_asid) {
				/*  TODO: Don't invalidate everything?  */
				cpu->invalidate_translation_caches(
				    cpu, 0, INVALIDATE_ALL);
			}
		}
		break;

	case SH4_PTEL:
		if (writeflag == MEM_READ)
			odata = cpu->cd.sh.ptel;
		else
			cpu->cd.sh.ptel = idata;
		break;

	case SH4_TTB:
		if (writeflag == MEM_READ)
			odata = cpu->cd.sh.ttb;
		else
			cpu->cd.sh.ttb = idata;
		break;

	case SH4_TEA:
		if (writeflag == MEM_READ)
			odata = cpu->cd.sh.tea;
		else
			cpu->cd.sh.tea = idata;
		break;

	case SH4_PTEA:
		if (writeflag == MEM_READ)
			odata = cpu->cd.sh.ptea;
		else
			cpu->cd.sh.ptea = idata;
		break;

	case SH4_MMUCR:
		if (writeflag == MEM_READ) {
			odata = cpu->cd.sh.mmucr;
		} else {
			if (idata & SH4_MMUCR_TI) {
				/*  TLB invalidate.  */

				/*  TODO: Only invalidate something specific?
				    And not everything?  */
				cpu->invalidate_translation_caches(cpu,
				    0, INVALIDATE_ALL);

				/*  Should always read back as 0.  */
				idata &= ~SH4_MMUCR_TI;
			}

			cpu->cd.sh.mmucr = idata;
		}
		break;

	case SH4_CCR:
		if (writeflag == MEM_READ) {
			odata = cpu->cd.sh.ccr;
		} else {
			cpu->cd.sh.ccr = idata;
		}
		break;

	case SH4_QACR0:
		if (writeflag == MEM_READ) {
			odata = cpu->cd.sh.qacr0;
		} else {
			cpu->cd.sh.qacr0 = idata;
		}
		break;

	case SH4_QACR1:
		if (writeflag == MEM_READ) {
			odata = cpu->cd.sh.qacr1;
		} else {
			cpu->cd.sh.qacr1 = idata;
		}
		break;

	case SH4_TRA:
		if (writeflag == MEM_READ)
			odata = cpu->cd.sh.tra;
		else
			cpu->cd.sh.tra = idata;
		break;

	case SH4_EXPEVT:
		if (writeflag == MEM_READ)
			odata = cpu->cd.sh.expevt;
		else
			cpu->cd.sh.expevt = idata;
		break;

	case SH4_INTEVT:
		if (writeflag == MEM_READ)
			odata = cpu->cd.sh.intevt;
		else
			cpu->cd.sh.intevt = idata;
		break;


	/********************************/
	/*  TMU: Timer Management Unit  */

	case SH4_TOCR:
		/*  Timer Output Control Register  */
		if (writeflag == MEM_WRITE) {
			d->tocr = idata;
			if (idata & TOCR_TCOE)
				fatal("[ sh4 timer: TCOE not yet "
				    "implemented ]\n");
		} else {
			odata = d->tocr;
		}
		break;

	case SH4_TSTR:
		/*  Timer Start Register  */
		if (writeflag == MEM_READ) {
			odata = d->tstr;
		} else {
			if (idata & 1 && !(d->tstr & 1))
				debug("[ sh4 timer: starting timer 0 ]\n");
			if (idata & 2 && !(d->tstr & 2))
				debug("[ sh4 timer: starting timer 1 ]\n");
			if (idata & 4 && !(d->tstr & 4))
				debug("[ sh4 timer: starting timer 2 ]\n");
			if (!(idata & 1) && d->tstr & 1)
				debug("[ sh4 timer: stopping timer 0 ]\n");
			if (!(idata & 2) && d->tstr & 2)
				debug("[ sh4 timer: stopping timer 1 ]\n");
			if (!(idata & 4) && d->tstr & 4)
				debug("[ sh4 timer: stopping timer 2 ]\n");
			d->tstr = idata;
		}
		break;

	case SH4_TCOR2:
		timer_nr ++;
	case SH4_TCOR1:
		timer_nr ++;
	case SH4_TCOR0:
		/*  Timer Constant Register  */
		if (writeflag == MEM_READ)
			odata = d->tcor[timer_nr];
		else
			d->tcor[timer_nr] = idata;
		break;

	case SH4_TCNT2:
		timer_nr ++;
	case SH4_TCNT1:
		timer_nr ++;
	case SH4_TCNT0:
		/*  Timer Counter Register  */
		if (writeflag == MEM_READ)
			odata = d->tcnt[timer_nr];
		else
			d->tcnt[timer_nr] = idata;
		break;

	case SH4_TCR2:
		timer_nr ++;
	case SH4_TCR1:
		timer_nr ++;
	case SH4_TCR0:
		/*  Timer Control Register  */
		if (writeflag == MEM_READ) {
			odata = d->tcr[timer_nr];
		} else {
			if (cpu->cd.sh.pclock == 0) {
				fatal("INTERNAL ERROR: pclock must be set"
				    " for this machine. Aborting.\n");
				exit(1);
			}

			switch (idata & 3) {
			case TCR_TPSC_P4:
				d->timer_hz[timer_nr] = cpu->cd.sh.pclock/4.0;
				break;
			case TCR_TPSC_P16:
				d->timer_hz[timer_nr] = cpu->cd.sh.pclock/16.0;
				break;
			case TCR_TPSC_P64:
				d->timer_hz[timer_nr] = cpu->cd.sh.pclock/64.0;
				break;
			case TCR_TPSC_P256:
				d->timer_hz[timer_nr] = cpu->cd.sh.pclock/256.0;
				break;
			}

			debug("[ sh4 timer %i clock set to %f Hz ]\n",
			    timer_nr, d->timer_hz[timer_nr]);

			if (idata & (TCR_ICPF | TCR_ICPE1 | TCR_ICPE0 |
			    TCR_CKEG1 | TCR_CKEG0 | TCR_TPSC2)) {
				fatal("Unimplemented SH4 timer control"
				    " bits: 0x%08"PRIx32". Aborting.\n",
				    (int) idata);
				exit(1);
			}

			if (d->tcr[timer_nr] & TCR_UNF && !(idata & TCR_UNF)) {
				cpu_interrupt_ack(cpu, SH_INTEVT_TMU0_TUNI0
				    + 0x20 * timer_nr);
				if (d->timer_interrupts_pending[timer_nr] > 0)
					d->timer_interrupts_pending[timer_nr]--;
			}

			d->tcr[timer_nr] = idata;
		}
		break;


	/*************************************************/
	/*  BSC: Bus State Controller                    */

	case SH4_RFCR:
		/*  TODO  */
		fatal("[ SH4_RFCR: TODO ]\n");
		odata = 0x11;
		break;


	/*********************************/
	/*  INTC:  Interrupt Controller  */

	case SH4_ICR:
		if (writeflag == MEM_WRITE) {
			if (idata & 0x80) {
				fatal("SH4 INTC: IRLM not yet "
				    "supported. TODO\n");
				exit(1);
			}
		}
		break;

	case SH4_IPRA:
		if (writeflag == MEM_READ)
			odata = cpu->cd.sh.intc_ipra;
		else
			cpu->cd.sh.intc_ipra = idata;
		break;

	case SH4_IPRB:
		if (writeflag == MEM_READ)
			odata = cpu->cd.sh.intc_iprb;
		else
			cpu->cd.sh.intc_iprb = idata;
		break;

	case SH4_IPRC:
		if (writeflag == MEM_READ)
			odata = cpu->cd.sh.intc_iprc;
		else
			cpu->cd.sh.intc_iprc = idata;
		break;


	/*************************************************/
	/*  SCIF: Serial Controller Interface with FIFO  */

	case SH4_SCIF_BASE + SCIF_FTDR:
		if (writeflag == MEM_WRITE)
			console_putchar(d->scif_console_handle, idata);
		break;

	case SH4_SCIF_BASE + SCIF_SSR:
		/*  TODO: Implement more of this.  */
		odata = SCSSR2_TDFE | SCSSR2_TEND;
		break;


	/*************************************************/

	default:if (writeflag == MEM_READ) {
			fatal("[ sh4: read from addr 0x%x ]\n",
			    (int)relative_addr);
		} else {
			fatal("[ sh4: write to addr 0x%x: 0x%x ]\n",
			    (int)relative_addr, (int)idata);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


DEVINIT(sh4)
{
	struct machine *machine = devinit->machine;
	struct sh4_data *d = malloc(sizeof(struct sh4_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct sh4_data));

	d->scif_console_handle = console_start_slave(devinit->machine,
	    "SH4 SCIF", 1);

	memory_device_register(machine->memory, devinit->name,
	    SH4_REG_BASE, 0x01000000, dev_sh4_access, d, DM_DEFAULT, NULL);

	/*  On-chip RAM/cache:  */
	dev_ram_init(machine, 0x1e000000, 0x8000, DEV_RAM_RAM, 0x0);

	/*  0xe0000000: Store queues:  */
	dev_ram_init(machine, 0xe0000000, 32 * 2, DEV_RAM_RAM, 0x0);

	/*
	 *  0xf0000000	SH4_CCIA	I-Cache address array
	 *  0xf1000000	SH4_CCID	I-Cache data array
	 *  0xf4000000	SH4_CCDA	D-Cache address array
	 *  0xf5000000	SH4_CCDD	D-Cache data array
	 *
	 *  TODO: Implement more correct cache behaviour?
	 */
	dev_ram_init(machine, SH4_CCIA, SH4_ICACHE_SIZE, DEV_RAM_RAM, 0x0);
	dev_ram_init(machine, SH4_CCID, SH4_ICACHE_SIZE, DEV_RAM_RAM, 0x0);
	dev_ram_init(machine, SH4_CCDA, SH4_DCACHE_SIZE, DEV_RAM_RAM, 0x0);
	dev_ram_init(machine, SH4_CCDD, SH4_DCACHE_SIZE, DEV_RAM_RAM, 0x0);

	/*  0xf2000000	SH4_ITLB_AA  */
	memory_device_register(machine->memory, devinit->name, SH4_ITLB_AA,
	    0x01000000, dev_sh4_itlb_aa_access, d, DM_DEFAULT, NULL);

	/*  0xf3000000	SH4_ITLB_DA1  */
	memory_device_register(machine->memory, devinit->name, SH4_ITLB_DA1,
	    0x01000000, dev_sh4_itlb_da1_access, d, DM_DEFAULT, NULL);

	/*  0xf6000000	SH4_UTLB_AA  */
	memory_device_register(machine->memory, devinit->name, SH4_UTLB_AA,
	    0x01000000, dev_sh4_utlb_aa_access, d, DM_DEFAULT, NULL);

	/*  0xf7000000	SH4_UTLB_DA1  */
	memory_device_register(machine->memory, devinit->name, SH4_UTLB_DA1,
	    0x01000000, dev_sh4_utlb_da1_access, d, DM_DEFAULT, NULL);

	d->sh4_timer = timer_add(SH4_PSEUDO_TIMER_HZ, sh4_timer_tick, d);
	machine_add_tickfunction(devinit->machine, dev_sh4_tick, d,
	    SH4_TICK_SHIFT, 0.0);

	/*  Initial Timer values, according to the SH7750 manual:  */
	d->tcor[0] = 0xffffffff; d->tcnt[0] = 0xffffffff;
	d->tcor[1] = 0xffffffff; d->tcnt[1] = 0xffffffff;
	d->tcor[2] = 0xffffffff; d->tcnt[2] = 0xffffffff;

	return 1;
}

