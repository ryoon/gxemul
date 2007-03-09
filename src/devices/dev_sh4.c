/*
 *  Copyright (C) 2006-2007  Anders Gavare.  All rights reserved.
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
 *  $Id: dev_sh4.c,v 1.31 2007-03-09 10:54:28 debug Exp $
 *  
 *  SH4 processor specific memory mapped registers (0xf0000000 - 0xffffffff).
 *
 *  TODO: Lots and lots of stuff.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "console.h"
#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "interrupt.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"
#include "timer.h"

#include "sh4_bscreg.h"
#include "sh4_cache.h"
#include "sh4_dmacreg.h"
#include "sh4_exception.h"
#include "sh4_intcreg.h"
#include "sh4_mmu.h"
#include "sh4_rtcreg.h"
#include "sh4_scifreg.h"
#include "sh4_tmureg.h"


#define	SH4_REG_BASE		0xff000000
#define	SH4_TICK_SHIFT		14
#define	N_SH4_TIMERS		3

/*  General-purpose I/O stuff:  */
#define	SH4_PCTRA		0xff80002c
#define	SH4_PDTRA		0xff800030
#define	SH4_PCTRB		0xff800040
#define	SH4_PDTRB		0xff800044
#define	SH4_GPIOIC		0xff800048

#ifdef UNSTABLE_DEVEL
#define SH4_DEGUG
/*  #define debug fatal  */
#endif

struct sh4_data {
	/*  SCIF (Serial controller):  */
	uint16_t	scif_smr;
	uint8_t		scif_brr;
	uint16_t	scif_scr;
	uint16_t	scif_ssr;
	uint16_t	scif_fcr;
	int		scif_delayed_tx;
	int		scif_console_handle;
	struct interrupt scif_tx_irq;
	struct interrupt scif_rx_irq;

	/*  Bus State Controller:  */
	uint32_t	bsc_bcr1;
	uint16_t	bsc_bcr2;
	uint32_t	bsc_wcr1;
	uint32_t	bsc_wcr2;
	uint32_t	bsc_mcr;
	uint16_t	bsc_rtcsr;
	uint16_t	bsc_rtcor;
	uint16_t	bsc_rfcr;

	/*  GPIO:  */
	uint32_t	pctra;		/*  Port Control Register A  */
	uint32_t	pdtra;		/*  Port Data Register A  */
	uint32_t	pctrb;		/*  Port Control Register B  */
	uint32_t	pdtrb;		/*  Port Data Register B  */

	/*  SD-RAM:  */
	uint16_t	sdmr2;
	uint16_t	sdmr3;

	/*  Timer Management Unit:  */
	struct timer	*sh4_timer;
	struct interrupt timer_irq[4];
	uint32_t	tocr;
	uint32_t	tstr;
	uint32_t	tcnt[N_SH4_TIMERS];
	uint32_t	tcor[N_SH4_TIMERS];
	uint32_t	tcr[N_SH4_TIMERS];
	int		timer_interrupts_pending[N_SH4_TIMERS];
	double		timer_hz[N_SH4_TIMERS];

	/*  RTC:  */
	uint32_t	rtc_reg[14];	/*  Excluding rcr1 and 2  */
	uint8_t		rtc_rcr1;
};


#define	SH4_PSEUDO_TIMER_HZ	100.0


/*
 *  sh4_timer_tick():
 *
 *  This function is called SH4_PSEUDO_TIMER_HZ times per real-world second.
 *  Its job is to update the SH4 timer counters, and if necessary, increase
 *  the number of pending interrupts.
 *
 *  Also, RAM Refresh is also faked here.
 */
static void sh4_timer_tick(struct timer *t, void *extra)
{
	struct sh4_data *d = (struct sh4_data *) extra;
	int i;

	/*  Fake RAM refresh:  */
	d->bsc_rfcr ++;
	if (d->bsc_rtcsr & (RTCSR_CMIE | RTCSR_OVIE)) {
		fatal("sh4: RTCSR_CMIE | RTCSR_OVIE: TODO\n");
		/*  TODO: Implement refresh interrupts etc.  */
		exit(1);
	}

	/*  Timer interrupts:  */
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


static void scif_reassert_interrupts(struct sh4_data *d)
{
	if (d->scif_scr & SCSCR2_RIE) {
		if (d->scif_ssr & SCSSR2_DR)
			INTERRUPT_ASSERT(d->scif_rx_irq);
	} else {
		INTERRUPT_DEASSERT(d->scif_rx_irq);
	}
	if (d->scif_scr & SCSCR2_TIE) {
		if (d->scif_ssr & (SCSSR2_TDFE | SCSSR2_TEND))
			INTERRUPT_ASSERT(d->scif_tx_irq);
	} else {
		INTERRUPT_DEASSERT(d->scif_tx_irq);
	}
}


DEVICE_TICK(sh4)
{
	struct sh4_data *d = (struct sh4_data *) extra;
	int i;

	/*  Serial controller interrupts:  */
	/*  TODO: Which bits go to which interrupt?  */
	if (console_charavail(d->scif_console_handle))
		d->scif_ssr |= SCSSR2_DR;
	else
		d->scif_ssr &= SCSSR2_DR;
	if (d->scif_delayed_tx) {
		if (--d->scif_delayed_tx == 0)
			d->scif_ssr |= SCSSR2_TDFE | SCSSR2_TEND;
	}

	scif_reassert_interrupts(d);

	/*  Timer interrupts:  */
	for (i=0; i<N_SH4_TIMERS; i++)
		if (d->timer_interrupts_pending[i] > 0) {
			INTERRUPT_ASSERT(d->timer_irq[i]);
			d->tcr[i] |= TCR_UNF;
		}
}


DEVICE_ACCESS(sh4_itlb_aa)
{
	uint64_t idata = 0, odata = 0;
	int e = (relative_addr & SH4_ITLB_E_MASK) >> SH4_ITLB_E_SHIFT;

	if (writeflag == MEM_WRITE) {
		int safe_to_invalidate = 0;
		uint32_t old_hi = cpu->cd.sh.itlb_hi[e];
		if ((cpu->cd.sh.itlb_lo[e] & SH4_PTEL_SZ_MASK)==SH4_PTEL_SZ_4K)
			safe_to_invalidate = 1;

		idata = memory_readmax64(cpu, data, len);
		cpu->cd.sh.itlb_hi[e] &=
		    ~(SH4_PTEH_VPN_MASK | SH4_PTEH_ASID_MASK);
		cpu->cd.sh.itlb_hi[e] |= (idata &
		    (SH4_ITLB_AA_VPN_MASK | SH4_ITLB_AA_ASID_MASK));
		cpu->cd.sh.itlb_lo[e] &= ~SH4_PTEL_V;
		if (idata & SH4_ITLB_AA_V)
			cpu->cd.sh.itlb_lo[e] |= SH4_PTEL_V;

		/*  Invalidate if this ITLB entry previously belonged to the
		    currently running process, or if it was shared:  */
		if (cpu->cd.sh.ptel & SH4_PTEL_SH ||
		    (old_hi & SH4_ITLB_AA_ASID_MASK) ==
		    (cpu->cd.sh.pteh & SH4_PTEH_ASID_MASK)) {
			if (safe_to_invalidate)
				cpu->invalidate_translation_caches(cpu,
				    old_hi & ~0xfff, INVALIDATE_VADDR);
			else
				cpu->invalidate_translation_caches(cpu,
				    0, INVALIDATE_ALL);
		}
	} else {
		odata = cpu->cd.sh.itlb_hi[e] &
		    (SH4_ITLB_AA_VPN_MASK | SH4_ITLB_AA_ASID_MASK);
		if (cpu->cd.sh.itlb_lo[e] & SH4_PTEL_V)
			odata |= SH4_ITLB_AA_V;
		memory_writemax64(cpu, data, len, odata);
	}

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
		uint32_t old_lo = cpu->cd.sh.itlb_lo[e];
		int safe_to_invalidate = 0;
		if ((cpu->cd.sh.itlb_lo[e] & SH4_PTEL_SZ_MASK)==SH4_PTEL_SZ_4K)
			safe_to_invalidate = 1;

		idata = memory_readmax64(cpu, data, len);
		cpu->cd.sh.itlb_lo[e] &= ~mask;
		cpu->cd.sh.itlb_lo[e] |= (idata & mask);

		/*  Invalidate if this ITLB entry belongs to the
		    currently running process, or if it was shared:  */
		if (old_lo & SH4_PTEL_SH ||
		    (cpu->cd.sh.itlb_hi[e] & SH4_ITLB_AA_ASID_MASK) ==
		    (cpu->cd.sh.pteh & SH4_PTEH_ASID_MASK)) {
			if (safe_to_invalidate)
				cpu->invalidate_translation_caches(cpu,
				    cpu->cd.sh.itlb_hi[e] & ~0xfff,
				    INVALIDATE_VADDR);
			else
				cpu->invalidate_translation_caches(cpu,
				    0, INVALIDATE_ALL);
		}
	} else {
		odata = cpu->cd.sh.itlb_lo[e] & mask;
		memory_writemax64(cpu, data, len, odata);
	}

	return 1;
}


DEVICE_ACCESS(sh4_utlb_aa)
{
	uint64_t idata = 0, odata = 0;
	int i, e = (relative_addr & SH4_UTLB_E_MASK) >> SH4_UTLB_E_SHIFT;
	int a = relative_addr & SH4_UTLB_A;

	if (writeflag == MEM_WRITE) {
		int n_hits = 0;
		int safe_to_invalidate = 0;
		uint32_t vaddr_to_invalidate = 0;

		idata = memory_readmax64(cpu, data, len);
		if (a) {
			for (i=-SH_N_ITLB_ENTRIES; i<SH_N_UTLB_ENTRIES; i++) {
				uint32_t lo, hi;
				uint32_t mask = 0xfffff000;
				int sh;

				if (i < 0) {
					lo = cpu->cd.sh.itlb_lo[
					    i + SH_N_ITLB_ENTRIES];
					hi = cpu->cd.sh.itlb_hi[
					    i + SH_N_ITLB_ENTRIES];
				} else {
					lo = cpu->cd.sh.utlb_lo[i];
					hi = cpu->cd.sh.utlb_hi[i];
				}

				sh = lo & SH4_PTEL_SH;
				if (!(lo & SH4_PTEL_V))
					continue;

				switch (lo & SH4_PTEL_SZ_MASK) {
				case SH4_PTEL_SZ_1K:  mask = 0xfffffc00; break;
				case SH4_PTEL_SZ_64K: mask = 0xffff0000; break;
				case SH4_PTEL_SZ_1M:  mask = 0xfff00000; break;
				}

				if ((hi & mask) != (idata & mask))
					continue;

				if ((lo & SH4_PTEL_SZ_MASK) ==
				    SH4_PTEL_SZ_4K) {
					safe_to_invalidate = 1;
					vaddr_to_invalidate = hi & mask;
				}

				if (!sh && (hi & SH4_PTEH_ASID_MASK) !=
				    (cpu->cd.sh.pteh & SH4_PTEH_ASID_MASK))
					continue;

				if (i < 0) {
					cpu->cd.sh.itlb_lo[i] &= ~SH4_PTEL_V;
					if (idata & SH4_UTLB_AA_V)
						cpu->cd.sh.itlb_lo[i] |=
						    SH4_PTEL_V;
				} else {
					cpu->cd.sh.utlb_lo[i] &=
					    ~(SH4_PTEL_D | SH4_PTEL_V);
					if (idata & SH4_UTLB_AA_D)
						cpu->cd.sh.utlb_lo[i] |=
						    SH4_PTEL_D;
					if (idata & SH4_UTLB_AA_V)
						cpu->cd.sh.utlb_lo[i] |=
						    SH4_PTEL_V;
				}

				if (i >= 0)
					n_hits ++;
			}

			if (n_hits > 1)
				sh_exception(cpu,
				    EXPEVT_RESET_TLB_MULTI_HIT, 0, 0);
		} else {
			if ((cpu->cd.sh.utlb_lo[e] & SH4_PTEL_SZ_MASK) ==
			    SH4_PTEL_SZ_4K) {
				safe_to_invalidate = 1;
				vaddr_to_invalidate =
				    cpu->cd.sh.utlb_hi[e] & ~0xfff;
			}

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

		if (safe_to_invalidate)
			cpu->invalidate_translation_caches(cpu,
			    vaddr_to_invalidate, INVALIDATE_VADDR);
		else
			cpu->invalidate_translation_caches(cpu, 0,
			    INVALIDATE_ALL);
	} else {
		odata = cpu->cd.sh.utlb_hi[e] &
		    (SH4_UTLB_AA_VPN_MASK | SH4_UTLB_AA_ASID_MASK);
		if (cpu->cd.sh.utlb_lo[e] & SH4_PTEL_D)
			odata |= SH4_UTLB_AA_D;
		if (cpu->cd.sh.utlb_lo[e] & SH4_PTEL_V)
			odata |= SH4_UTLB_AA_V;
		memory_writemax64(cpu, data, len, odata);
	}

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
		uint32_t old_lo = cpu->cd.sh.utlb_lo[e];
		int safe_to_invalidate = 0;
		if ((cpu->cd.sh.utlb_lo[e] & SH4_PTEL_SZ_MASK)==SH4_PTEL_SZ_4K)
			safe_to_invalidate = 1;

		idata = memory_readmax64(cpu, data, len);
		cpu->cd.sh.utlb_lo[e] &= ~mask;
		cpu->cd.sh.utlb_lo[e] |= (idata & mask);

		/*  Invalidate if this UTLB entry belongs to the
		    currently running process, or if it was shared:  */
		if (old_lo & SH4_PTEL_SH ||
		    (cpu->cd.sh.utlb_hi[e] & SH4_ITLB_AA_ASID_MASK) ==
		    (cpu->cd.sh.pteh & SH4_PTEH_ASID_MASK)) {
			if (safe_to_invalidate)
				cpu->invalidate_translation_caches(cpu,
				    cpu->cd.sh.utlb_hi[e] & ~0xfff,
				    INVALIDATE_VADDR);
			else
				cpu->invalidate_translation_caches(cpu,
				    0, INVALIDATE_ALL);
		}
	} else {
		odata = cpu->cd.sh.utlb_lo[e] & mask;
		memory_writemax64(cpu, data, len, odata);
	}

	return 1;
}


DEVICE_ACCESS(sh4)
{
	struct sh4_data *d = (struct sh4_data *) extra;
	uint64_t idata = 0, odata = 0;
	int timer_nr = 0, dma_channel = 0;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);

	relative_addr += SH4_REG_BASE;

	/*  SD-RAM access uses address only:  */
	if (relative_addr >= 0xff900000 && relative_addr <= 0xff97ffff) {
		/*  Possibly not 100% correct... TODO  */
		int v = (relative_addr >> 2) & 0xffff;
		if (relative_addr & 0x00040000)
			d->sdmr3 = v;
		else
			d->sdmr2 = v;
		debug("[ sh4: sdmr%i set to 0x%04"PRIx16" ]\n",
		    relative_addr & 0x00040000? 3 : 2, v);
		return 1;
	}


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
				/*
				 *  TODO: Don't invalidate everything,
				 *  only those pages that belonged to the
				 *  old asid.
				 */
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
				int i;
				for (i = 0; i < SH_N_ITLB_ENTRIES; i++)
					cpu->cd.sh.itlb_lo[i] &=
					    ~SH4_PTEL_V;

				for (i = 0; i < SH_N_UTLB_ENTRIES; i++)
					cpu->cd.sh.utlb_lo[i] &=
					    ~SH4_PTEL_V;

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
	/*  UBC: User Break Controller  */

	case 0xff200008:    /*  SH4_BBRA  */
		/*  TODO  */
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
				INTERRUPT_DEASSERT(d->timer_irq[timer_nr]);
				if (d->timer_interrupts_pending[timer_nr] > 0)
					d->timer_interrupts_pending[timer_nr]--;
			}

			d->tcr[timer_nr] = idata;
		}
		break;


	/*************************************************/
	/*  DMAC: DMA Controller                         */

	case SH4_SAR3:
		dma_channel ++;
	case SH4_SAR2:
		dma_channel ++;
	case SH4_SAR1:
		dma_channel ++;
	case SH4_SAR0:
		dma_channel ++;
		if (writeflag == MEM_READ)
			odata = cpu->cd.sh.dmac_sar[dma_channel];
		else
			cpu->cd.sh.dmac_sar[dma_channel] = idata;
		break;

	case SH4_DAR3:
		dma_channel ++;
	case SH4_DAR2:
		dma_channel ++;
	case SH4_DAR1:
		dma_channel ++;
	case SH4_DAR0:
		dma_channel ++;
		if (writeflag == MEM_READ)
			odata = cpu->cd.sh.dmac_dar[dma_channel];
		else
			cpu->cd.sh.dmac_dar[dma_channel] = idata;
		break;

	case SH4_DMATCR3:
		dma_channel ++;
	case SH4_DMATCR2:
		dma_channel ++;
	case SH4_DMATCR1:
		dma_channel ++;
	case SH4_DMATCR0:
		dma_channel ++;
		if (writeflag == MEM_READ)
			odata = cpu->cd.sh.dmac_tcr[dma_channel] & 0x00ffffff;
		else {
			if (idata & ~0x00ffffff) {
				fatal("[ SH4 DMA: Attempt to set top 8 "
				    "bits of the count register? 0x%08"
				    PRIx32" ]\n", (uint32_t) idata);
				exit(1);
			}

			/*  Special case: writing 0 to the count register
			    means 16777216:  */
			if (idata == 0)
				idata = 0x01000000;
			cpu->cd.sh.dmac_tcr[dma_channel] = idata;
		}
		break;

	case SH4_CHCR3:
		dma_channel ++;
	case SH4_CHCR2:
		dma_channel ++;
	case SH4_CHCR1:
		dma_channel ++;
	case SH4_CHCR0:
		dma_channel ++;
		if (writeflag == MEM_READ)
			odata = cpu->cd.sh.dmac_chcr[dma_channel];
		else {
			/*  IP.BIN sets this to 0x12c0, and I want to know if
			    some other guest OS uses other values.  */
			if (idata != 0x12c0) {
				fatal("[ SH4 DMA: Attempt to set chcr "
				    "to 0x%08"PRIx32" ]\n", (uint32_t) idata);
				exit(1);
			}

			cpu->cd.sh.dmac_chcr[dma_channel] = idata;
		}
		break;


	/*************************************************/
	/*  BSC: Bus State Controller                    */

	case SH4_BCR1:
		if (writeflag == MEM_WRITE)
			d->bsc_bcr1 = idata & 0x033efffd;
		else {
			odata = d->bsc_bcr1;
			if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
				odata |= BCR1_LITTLE_ENDIAN;
		}
		break;

	case SH4_BCR2:
		if (len != sizeof(uint16_t)) {
			fatal("Non-16-bit SH4_BCR2 access?\n");
			exit(1);
		}
		if (writeflag == MEM_WRITE)
			d->bsc_bcr2 = idata & 0x3ffd;
		else
			odata = d->bsc_bcr2;
		break;

	case SH4_WCR1:
		if (writeflag == MEM_WRITE)
			d->bsc_wcr1 = idata & 0x77777777;
		else
			odata = d->bsc_wcr1;
		break;

	case SH4_WCR2:
		if (writeflag == MEM_WRITE)
			d->bsc_wcr2 = idata & 0xfffeefff;
		else
			odata = d->bsc_wcr2;
		break;

	case SH4_MCR:
		if (writeflag == MEM_WRITE)
			d->bsc_mcr = idata & 0xf8bbffff;
		else
			odata = d->bsc_mcr;
		break;

	case SH4_RTCSR:
		/*
		 *  Refresh Time Control/Status Register. Called RTCSR in
		 *  NetBSD, but RTSCR in the SH7750 manual?
		 */
		if (writeflag == MEM_WRITE) {
			idata &= 0x00ff;
			if (idata & RTCSR_CMF) {
				idata = (idata & ~RTCSR_CMF)
				    | (d->bsc_rtcsr & RTCSR_CMF);
			}
			d->bsc_rtcsr = idata & 0x00ff;
		} else
			odata = d->bsc_rtcsr;
		break;

	case SH4_RTCOR:
		/*  Refresh Time Constant Register (8 bits):  */
		if (writeflag == MEM_WRITE)
			d->bsc_rtcor = idata & 0x00ff;
		else
			odata = d->bsc_rtcor & 0x00ff;
		break;

	case SH4_RFCR:
		/*  Refresh Count Register (10 bits):  */
		if (writeflag == MEM_WRITE)
			d->bsc_rfcr = idata & 0x03ff;
		else
			odata = d->bsc_rfcr & 0x03ff;
		break;


	/*******************************************/
	/*  GPIO:  General-purpose I/O controller  */

	case SH4_PCTRA:
		if (writeflag == MEM_WRITE)
			d->pctra = idata;
		else
			odata = d->pctra;
		break;

	case SH4_PDTRA:
		if (writeflag == MEM_WRITE) {
			debug("[ sh4: pdtra: write: TODO ]\n");
			d->pdtra = idata;
		} else {
			debug("[ sh4: pdtra: read: TODO ]\n");
			odata = d->pdtra;
		}
		break;

	case SH4_PCTRB:
		if (writeflag == MEM_WRITE)
			d->pctrb = idata;
		else
			odata = d->pctrb;
		break;

	case SH4_PDTRB:
		if (writeflag == MEM_WRITE) {
			debug("[ sh4: pdtrb: write: TODO ]\n");
			d->pdtrb = idata;
		} else {
			debug("[ sh4: pdtrb: read: TODO ]\n");
			odata = d->pdtrb;
		}
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

	case SH4_IPRD:
		if (writeflag == MEM_READ)
			odata = cpu->cd.sh.intc_iprd;
		else
			cpu->cd.sh.intc_iprd = idata;
		break;


	/*************************************************/
	/*  SCIF: Serial Controller Interface with FIFO  */

	case SH4_SCIF_BASE + SCIF_SMR:
		if (writeflag == MEM_WRITE) {
			d->scif_smr = idata;
		} else {
			odata = d->scif_smr;
		}
		break;

	case SH4_SCIF_BASE + SCIF_BRR:
		if (writeflag == MEM_WRITE) {
			d->scif_brr = idata;
		} else {
			odata = d->scif_brr;
		}
		break;

	case SH4_SCIF_BASE + SCIF_SCR:
		if (writeflag == MEM_WRITE) {
			d->scif_scr = idata;
			scif_reassert_interrupts(d);
		} else {
			odata = d->scif_scr;
		}
		break;

	case SH4_SCIF_BASE + SCIF_FTDR:
		if (writeflag == MEM_WRITE) {
			console_putchar(d->scif_console_handle, idata);
			d->scif_delayed_tx = 1;
		}
		break;

	case SH4_SCIF_BASE + SCIF_SSR:
		if (writeflag == MEM_READ) {
			odata = d->scif_ssr;
		} else {
			d->scif_ssr &= ~idata;
			scif_reassert_interrupts(d);
		}
		break;

	case SH4_SCIF_BASE + SCIF_FRDR:
		{
			int x = console_readchar(d->scif_console_handle);
			if (x == 13)
				x = 10;
			odata = x < 0? 0 : x;
			d->scif_ssr &= ~SCSSR2_DR;
		}
		break;

	case SH4_SCIF_BASE + SCIF_FCR:
		if (writeflag == MEM_WRITE) {
			d->scif_fcr = idata;
		} else {
			odata = d->scif_fcr;
		}
		break;

	case SH4_SCIF_BASE + SCIF_FDR:
		odata = console_charavail(d->scif_console_handle);
		break;


	/*************************************************/

	case SH4_RSECCNT:
	case SH4_RMINCNT:
	case SH4_RHRCNT:
	case SH4_RWKCNT:
	case SH4_RDAYCNT:
	case SH4_RMONCNT:
	case SH4_RYRCNT:
	case SH4_RSECAR:
	case SH4_RMINAR:
	case SH4_RHRAR:
	case SH4_RWKAR:
	case SH4_RDAYAR:
	case SH4_RMONAR:
		if (writeflag == MEM_WRITE) {
			d->rtc_reg[(relative_addr - 0xffc80000) / 4] = idata;
		} else {
			/*  TODO: Update rtc_reg based on host's date/time.  */
			odata = d->rtc_reg[(relative_addr - 0xffc80000) / 4];
		}
		break;

	case SH4_RCR1:
		if (writeflag == MEM_READ)
			odata = d->rtc_rcr1;
		else {
			d->rtc_rcr1 = idata;
			if (idata & 0x18) {
				fatal("SH4: TODO: RTC interrupt enable\n");
				exit(1);
			}
		}
		break;


	/*************************************************/

	default:if (writeflag == MEM_READ) {
			fatal("[ sh4: read from addr 0x%x ]\n",
			    (int)relative_addr);
		} else {
			fatal("[ sh4: write to addr 0x%x: 0x%x ]\n",
			    (int)relative_addr, (int)idata);
		}
#ifdef SH4_DEGUG
//		exit(1);
#endif
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


DEVINIT(sh4)
{
	char tmp[200];
	struct machine *machine = devinit->machine;
	struct sh4_data *d = malloc(sizeof(struct sh4_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct sh4_data));

	d->scif_console_handle = console_start_slave(devinit->machine,
	    "SH4 SCIF", 1);

	snprintf(tmp, sizeof(tmp), "%s.irq[0x%x]",
	    devinit->interrupt_path, SH4_INTEVT_SCIF_RXI);
	INTERRUPT_CONNECT(tmp, d->scif_rx_irq);
	snprintf(tmp, sizeof(tmp), "%s.irq[0x%x]",
	    devinit->interrupt_path, SH4_INTEVT_SCIF_TXI);
	INTERRUPT_CONNECT(tmp, d->scif_tx_irq);

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

	snprintf(tmp, sizeof(tmp), "emul[0].machine[0].cpu[0].irq[0x%x]",
	    SH_INTEVT_TMU0_TUNI0);
	if (!interrupt_handler_lookup(tmp, &d->timer_irq[0])) {
		fatal("Could not find interrupt '%s'.\n", tmp);
		exit(1);
	}
	snprintf(tmp, sizeof(tmp), "emul[0].machine[0].cpu[0].irq[0x%x]",
	    SH_INTEVT_TMU1_TUNI1);
	if (!interrupt_handler_lookup(tmp, &d->timer_irq[1])) {
		fatal("Could not find interrupt '%s'.\n", tmp);
		exit(1);
	}
	snprintf(tmp, sizeof(tmp), "emul[0].machine[0].cpu[0].irq[0x%x]",
	    SH_INTEVT_TMU2_TUNI2);
	if (!interrupt_handler_lookup(tmp, &d->timer_irq[2])) {
		fatal("Could not find interrupt '%s'.\n", tmp);
		exit(1);
	}

	/*  Bus State Controller initial values:  */
	d->bsc_bcr2 = 0x3ffc;
	d->bsc_wcr1 = 0x77777777;
	d->bsc_wcr2 = 0xfffeefff;

	return 1;
}

