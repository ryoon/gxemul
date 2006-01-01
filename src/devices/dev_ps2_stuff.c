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
 *  $Id: dev_ps2_stuff.c,v 1.27 2006-01-01 13:17:17 debug Exp $
 *  
 *  Playstation 2 misc. stuff:
 *
 *	offset 0x0000	timer control
 *	offset 0x8000	DMA controller
 *	offset 0xf000	Interrupt register
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "devices.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"

#include "ee_timerreg.h"
#include "ps2_dmacreg.h"

#define	TICK_STEPS_SHIFT	14

/*  NOTE/TODO: This should be the same as in ps2_gs:  */
#define	DEV_PS2_GIF_FAKE_BASE		0x50000000


/*
 *  dev_ps2_stuff_tick():
 */
void dev_ps2_stuff_tick(struct cpu *cpu, void *extra)
{
	struct ps2_data *d = extra;
	int i;

	/*
	 *  Right now this interrupts every now and then.
	 *  The main interrupt in NetBSD should be 100 Hz. TODO.
	 */
	for (i=0; i<N_PS2_TIMERS; i++) {
		/*  Count-up Enable:   TODO: by how much?  */
		if (d->timer_mode[i] & T_MODE_CUE)
			d->timer_count[i] ++;

		if (d->timer_mode[i] & (T_MODE_CMPE | T_MODE_OVFE)) {
			/*  Zero return:  */
			if (d->timer_mode[i] & T_MODE_ZRET)
				d->timer_count[i] = 0;

			/*  irq 9 is timer0, etc.  */
			cpu_interrupt(cpu, 8 + 9 + i);

			/*  timer 1..3 are "single-shot"? TODO  */
			if (i > 0) {
				d->timer_mode[i] &=
				    ~(T_MODE_CMPE | T_MODE_OVFF);
			}
		}
	}
}


/*
 *  dev_ps2_stuff_access():
 */
DEVICE_ACCESS(ps2_stuff)
{
	uint64_t idata = 0, odata = 0;
	int regnr = 0;
	struct ps2_data *d = extra;
	int timer_nr = 0;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);

	if (relative_addr >= 0x8000 && relative_addr < 0x8000 + DMAC_REGSIZE) {
		regnr = (relative_addr - 0x8000) / 16;
		if (writeflag == MEM_READ)
			odata = d->dmac_reg[regnr];
		else
			d->dmac_reg[regnr] = idata;
	}

	/*
	 *  Timer control:
	 *  The four timers are at offsets 0, 0x800, 0x1000, and 0x1800.
	 */
	if (relative_addr < TIMER_REGSIZE) {
		/*  0, 1, 2, or 3  */
		timer_nr = (relative_addr & 0x1800) >> 11;
		relative_addr &= (TIMER_OFS-1);
	}

	switch (relative_addr) {
	case 0x0000:	/*  timer count  */
		if (writeflag == MEM_READ) {
			odata = d->timer_count[timer_nr];
			if (timer_nr == 0) {
				/*  :-)  TODO: remove this?  */
				d->timer_count[timer_nr] ++;
			}
			debug("[ ps2_stuff: read timer %i count: 0x%llx ]\n",
			    timer_nr, (long long)odata);
		} else {
			d->timer_count[timer_nr] = idata;
			debug("[ ps2_stuff: write timer %i count: 0x%llx ]\n",
			    timer_nr, (long long)idata);
		}
		break;
	case 0x0010:	/*  timer mode  */
		if (writeflag == MEM_READ) {
			odata = d->timer_mode[timer_nr];
			debug("[ ps2_stuff: read timer %i mode: 0x%llx ]\n",
			    timer_nr, (long long)odata);
		} else {
			d->timer_mode[timer_nr] = idata;
			debug("[ ps2_stuff: write timer %i mode: 0x%llx ]\n",
			    timer_nr, (long long)idata);
		}
		break;
	case 0x0020:	/*  timer comp  */
		if (writeflag == MEM_READ) {
			odata = d->timer_comp[timer_nr];
			debug("[ ps2_stuff: read timer %i comp: 0x%llx ]\n",
			    timer_nr, (long long)odata);
		} else {
			d->timer_comp[timer_nr] = idata;
			debug("[ ps2_stuff: write timer %i comp: 0x%llx ]\n",
			    timer_nr, (long long)idata);
		}
		break;
	case 0x0030:	/*  timer hold  */
		if (writeflag == MEM_READ) {
			odata = d->timer_hold[timer_nr];
			debug("[ ps2_stuff: read timer %i hold: 0x%llx ]\n",
			    timer_nr, (long long)odata);
			if (timer_nr >= 2)
				fatal("[ WARNING: ps2_stuff: read from non-"
				    "existant timer %i hold register ]\n");
		} else {
			d->timer_hold[timer_nr] = idata;
			debug("[ ps2_stuff: write timer %i hold: 0x%llx ]\n",
			    timer_nr, (long long)idata);
			if (timer_nr >= 2)
				fatal("[ WARNING: ps2_stuff: write to "
				    "non-existant timer %i hold register ]\n",
				    timer_nr);
		}
		break;

	case 0x8000 + D2_CHCR_REG:
		if (writeflag==MEM_READ) {
			odata = d->dmac_reg[regnr];
			/*  debug("[ ps2_stuff: dmac read from D2_CHCR "
			    "(0x%llx) ]\n", (long long)d->dmac_reg[regnr]);  */
		} else {
			/*  debug("[ ps2_stuff: dmac write to D2_CHCR, "
			    "data 0x%016llx ]\n", (long long) idata);  */
			if (idata & D_CHCR_STR) {
				int length = d->dmac_reg[D2_QWC_REG/0x10] * 16;
				uint64_t from_addr = d->dmac_reg[
				    D2_MADR_REG/0x10];
				uint64_t to_addr   = d->dmac_reg[
				    D2_TADR_REG/0x10];
				unsigned char *copy_buf;

				debug("[ ps2_stuff: dmac [ch2] transfer addr="
				    "0x%016llx len=0x%lx ]\n", (long long)
				    d->dmac_reg[D2_MADR_REG/0x10],
				    (long)length);

				copy_buf = malloc(length);
				if (copy_buf == NULL) {
					fprintf(stderr, "out of memory in "
					    "dev_ps2_stuff_access()\n");
					exit(1);
				}
				cpu->memory_rw(cpu, cpu->mem, from_addr,
				    copy_buf, length, MEM_READ,
				    CACHE_NONE | PHYSICAL);
				cpu->memory_rw(cpu, cpu->mem,
				    d->other_memory_base[DMA_CH_GIF] + to_addr,
				    copy_buf, length, MEM_WRITE,
				    CACHE_NONE | PHYSICAL);
				free(copy_buf);

				/*  Done with the transfer:  */
				d->dmac_reg[D2_QWC_REG/0x10] = 0;
				idata &= ~D_CHCR_STR;

				/*  interrupt DMA channel 2  */
				cpu_interrupt(cpu, 8 + 16 + 2);
			} else
				debug("[ ps2_stuff: dmac [ch2] stopping "
				    "transfer ]\n");
			d->dmac_reg[regnr] = idata;
			return 1;
		}
		break;

	case 0x8000 + D2_QWC_REG:
	case 0x8000 + D2_MADR_REG:
	case 0x8000 + D2_TADR_REG:
		/*  no debug output  */
		break;

	case 0xe010:	/*  dmac interrupt status (and mask,  */
			/*  the upper 16 bits)  */
		if (writeflag == MEM_WRITE) {
			uint32_t oldmask = d->dmac_reg[regnr] & 0xffff0000;
			/*  Clear out those bits that are set in idata:  */
			d->dmac_reg[regnr] &= ~idata;
			d->dmac_reg[regnr] &= 0xffff;
			d->dmac_reg[regnr] |= oldmask;
			if (((d->dmac_reg[regnr] & 0xffff) &
			    ((d->dmac_reg[regnr]>>16) & 0xffff)) == 0) {
				/*  irq 3 is the DMAC  */
				cpu_interrupt_ack(cpu, 3);
			}
		} else {
			/*  Hm... make it seem like the mask bits are (at
			    least as much as) the interrupt assertions:  */
			odata = d->dmac_reg[regnr];
			odata |= (odata << 16);
		}
		break;

	case 0xf000:	/*  interrupt register  */
		if (writeflag == MEM_READ) {
			odata = d->intr;
			debug("[ ps2_stuff: read from Interrupt Register:"
			    " 0x%llx ]\n", (long long)odata);

			/*  TODO: This is _NOT_ correct behavior:  */
			d->intr = 0;
			cpu_interrupt_ack(cpu, 2);
		} else {
			debug("[ ps2_stuff: write to Interrupt Register: "
			    "0x%llx ]\n", (long long)idata);
			/*  Clear out bits that are set in idata:  */
			d->intr &= ~idata;

			if ((d->intr & d->imask) == 0)
				cpu_interrupt_ack(cpu, 2);
		}
		break;

	case 0xf010:	/*  interrupt mask  */
		if (writeflag == MEM_READ) {
			odata = d->imask;
			/*  debug("[ ps2_stuff: read from Interrupt Mask "
			    "Register: 0x%llx ]\n", (long long)odata);  */
		} else {
			/*  debug("[ ps2_stuff: write to Interrupt Mask "
			    "Register: 0x%llx ]\n", (long long)idata);  */
			d->imask = idata;
		}
		break;

	case 0xf230:	/*  sbus interrupt register?  */
		if (writeflag == MEM_READ) {
			odata = d->sbus_smflg;
			debug("[ ps2_stuff: read from SBUS SMFLG:"
			    " 0x%llx ]\n", (long long)odata);
		} else {
			/*  Clear bits on write:  */
			debug("[ ps2_stuff: write to SBUS SMFLG:"
			    " 0x%llx ]\n", (long long)idata);
			d->sbus_smflg &= ~idata;
			/*  irq 1 is SBUS  */
			if (d->sbus_smflg == 0)
				cpu_interrupt_ack(cpu, 8 + 1);
		}
		break;
	default:
		if (writeflag==MEM_READ) {
			debug("[ ps2_stuff: read from addr 0x%x: 0x%llx ]\n",
			    (int)relative_addr, (long long)odata);
		} else {
			debug("[ ps2_stuff: write to addr 0x%x: 0x%llx ]\n",
			    (int)relative_addr, (long long)idata);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_ps2_stuff_init():
 */
struct ps2_data *dev_ps2_stuff_init(struct machine *machine,
	struct memory *mem, uint64_t baseaddr)
{
	struct ps2_data *d;

	d = malloc(sizeof(struct ps2_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct ps2_data));

	d->other_memory_base[DMA_CH_GIF] = DEV_PS2_GIF_FAKE_BASE;

	memory_device_register(mem, "ps2_stuff", baseaddr,
	    DEV_PS2_STUFF_LENGTH, dev_ps2_stuff_access, d, DM_DEFAULT, NULL);
	machine_add_tickfunction(machine,
	    dev_ps2_stuff_tick, d, TICK_STEPS_SHIFT);

	return d;
}

