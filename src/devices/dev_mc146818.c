/*
 *  Copyright (C) 2003-2004 by Anders Gavare.  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright  
 *     notice, this list of conditions and the following disclaimer in the 
 *     documentation and/or other materials provided with the distribution.
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
 *  $Id: dev_mc146818.c,v 1.39 2004-09-05 03:12:43 debug Exp $
 *  
 *  MC146818 real-time clock, used by many different machines types.
 *
 *  This device contains Date/time, the machine's ethernet address (on
 *  DECstation 3100), and can cause periodic (hardware) interrupts.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "memory.h"
#include "misc.h"
#include "devices.h"

#include "mc146818reg.h"

extern int register_dump;
extern int instruction_trace;
extern int ncpus;
extern struct cpu **cpus;


#define	to_bcd(x)	( (x/10) * 16 + (x%10) )

/* #define MC146818_DEBUG */
#define	TICK_STEPS_SHIFT	9


#define	N_REGISTERS	256
struct mc_data {
	int	access_style;
	int	last_addr;

	int	register_choice;
	int	reg[N_REGISTERS];
	int	addrdiv;

	int	timebase_hz;
	int	interrupt_hz;
	int	irq_nr;

	int	previous_second;

	int	interrupt_every_x_cycles;
	int	cycles_left_until_interrupt;
};


/*
 *  recalc_interrupt_cycle():
 *
 *  If automatic_clock_adjustment is turned on, then emulated_hz is modified
 *  dynamically.  We have to recalculate how often interrupts are to be
 *  triggered.
 */
static void recalc_interrupt_cycle(struct cpu *cpu, struct mc_data *mc_data)
{
	if (mc_data->interrupt_hz > 0)
		mc_data->interrupt_every_x_cycles =
		    cpu->emul->emulated_hz / mc_data->interrupt_hz;
	else
		mc_data->interrupt_every_x_cycles = 0;
}


/*
 *  dev_mc146818_tick():
 */
void dev_mc146818_tick(struct cpu *cpu, void *extra)
{
	struct mc_data *mc_data = extra;

	if (mc_data == NULL)
		return;

	recalc_interrupt_cycle(cpu, mc_data);

	if ((mc_data->reg[MC_REGB*4] & MC_REGB_PIE) &&
	     mc_data->interrupt_every_x_cycles > 0) {
		mc_data->cycles_left_until_interrupt -=
		    (1 << TICK_STEPS_SHIFT);

		if (mc_data->cycles_left_until_interrupt < 0 ||
		    mc_data->cycles_left_until_interrupt >=
		    mc_data->interrupt_every_x_cycles) {
			debug("[ rtc interrupt (every %i cycles) ]\n",
			    mc_data->interrupt_every_x_cycles);
			cpu_interrupt(cpu, mc_data->irq_nr);

			mc_data->reg[MC_REGC*4] |= MC_REGC_PF;

			/*  Reset the cycle countdown:  */
			while (mc_data->cycles_left_until_interrupt < 0)
				mc_data->cycles_left_until_interrupt +=
				    mc_data->interrupt_every_x_cycles;
		}
	}
}


/*
 *  dev_mc146818_pica_access():
 *
 *  It seems like the PICA accesses the mc146818 by writing one byte to
 *  0x90000000070 and then reading or writing another byte at 0x......0004000.
 */
int dev_mc146818_pica_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	struct mc_data *mc_data = extra;

	if (writeflag == MEM_WRITE) {
		mc_data->last_addr = data[0];
		return 1;
	} else {
		data[0] = mc_data->last_addr;
		return 1;
	}
}


/*
 *  dev_mc146818_access():
 */
int dev_mc146818_access(struct cpu *cpu, struct memory *mem,
	uint64_t r, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	struct tm *tmp;
	time_t timet;
	struct mc_data *mc_data = extra;
	int relative_addr = r;

#ifdef MC146818_DEBUG
	if (writeflag == MEM_WRITE) {
		int i;
		fatal("[ mc146818: write to addr=0x%04x: ", relative_addr);
		for (i=0; i<len; i++)
			fatal("%02x ", data[i]);
		fatal("]\n");
	} else
		fatal("[ mc146818: read from addr=0x%04x ]\n", relative_addr);
#endif

	relative_addr /= mc_data->addrdiv;

	/*  Different ways of accessing the registers:  */
	switch (mc_data->access_style) {
	case MC146818_PC_CMOS:
		if (relative_addr == 0x70 || relative_addr == 0x00) {
			if (writeflag == MEM_WRITE) {
				mc_data->last_addr = data[0];
				return 1;
			} else {
				data[0] = mc_data->last_addr;
				return 1;
			}
		} else if (relative_addr == 0x71 || relative_addr == 0x01)
			relative_addr = mc_data->last_addr * 4;
		else {
			fatal("[ mc146818: not accessed as an MC146818_PC_CMOS device! ]\n");
		}
		break;
	case MC146818_ARC_NEC:
		if (relative_addr == 0x01) {
			if (writeflag == MEM_WRITE) {
				mc_data->last_addr = data[0];
				return 1;
			} else {
				data[0] = mc_data->last_addr;
				return 1;
			}
		} else if (relative_addr == 0x00)
			relative_addr = mc_data->last_addr * 4;
		else {
			fatal("[ mc146818: not accessed as an MC146818_ARC_NEC device! ]\n");
		}
		break;
	case MC146818_ARC_PICA:
		/*  See comment for dev_mc146818_pica_access().  */
		relative_addr = mc_data->last_addr * 4;
		break;
	case MC146818_DEC:
	case MC146818_SGI:
		/*
		 *  This device was originally written for DECstation
		 *  emulation, so no changes are neccessary for that access
		 *  style.
		 *
		 *  SGI access bytes 0x0..0xd at offsets 0x0yz..0xdyz, where yz
		 *  should be ignored. It works _almost_ as DEC, if offsets are
		 *  divided by 0x40.
		 */
	default:
		;
	}

	/*
	 *  For some reason, Linux/sgimips relies on the UIP bit to go
	 *  on and off. Without this code, booting Linux takes forever:
	 */
	mc_data->reg[MC_REGA*4] &= ~MC_REGA_UIP;
#if 1
	/*  TODO:  solve this more nicely  */
	if ((random() & 0xff) == 0)
		mc_data->reg[MC_REGA*4] ^= MC_REGA_UIP;
#endif

	/*
	 *  Sprite seens to wants UF interrupt status, once every second, or
	 *  it hangs forever during bootup.  (These do not cause interrupts,
	 *  but it is good enough... Sprite polls this, iirc.)
	 */
	timet = time(NULL);
	tmp = gmtime(&timet);
	mc_data->reg[MC_REGC*4] &= ~MC_REGC_UF;
	if (tmp->tm_sec != mc_data->previous_second) {
		mc_data->reg[MC_REGC*4] |= MC_REGC_UF;
		mc_data->reg[MC_REGC*4] |= MC_REGC_IRQF;
		mc_data->previous_second = tmp->tm_sec;

		/*  For some reason, some Linux/DECstation KN04 kernels want
		    the PF (periodic flag) bit set, even though interrupts
		    are not enabled?  */
		mc_data->reg[MC_REGC*4] |= MC_REGC_PF;
	}

	/*  RTC date/time is in binary, not BCD:  */
	mc_data->reg[MC_REGB*4] |= (1 << 2);

	/*  RTC date/time is always Valid:  */
	mc_data->reg[MC_REGD*4] |= MC_REGD_VRT;

	if (writeflag == MEM_WRITE) {
		/*  WRITE:  */
		switch (relative_addr) {
		case MC_REGA*4:
			if ((data[0] & MC_REGA_DVMASK) == MC_BASE_32_KHz)
				mc_data->timebase_hz = 32000;
			if ((data[0] & MC_REGA_DVMASK) == MC_BASE_1_MHz)
				mc_data->timebase_hz = 1000000;
			if ((data[0] & MC_REGA_DVMASK) == MC_BASE_4_MHz)
				mc_data->timebase_hz = 4000000;
			switch (data[0] & MC_REGA_RSMASK) {
			case MC_RATE_NONE:
				mc_data->interrupt_hz = 0;
				break;
			case MC_RATE_1:
				if (mc_data->timebase_hz == 32000)
					mc_data->interrupt_hz = 256;
				else
					mc_data->interrupt_hz = 32768;
				break;
			case MC_RATE_2:
				if (mc_data->timebase_hz == 32000)
					mc_data->interrupt_hz = 128;
				else
					mc_data->interrupt_hz = 16384;
				break;
			case MC_RATE_8192_Hz:
				mc_data->interrupt_hz = 8192;
				break;
			case MC_RATE_4096_Hz:
				mc_data->interrupt_hz = 4096;
				break;
			case MC_RATE_2048_Hz:
				mc_data->interrupt_hz = 2048;
				break;
			case MC_RATE_1024_Hz:
				mc_data->interrupt_hz = 1024;
				break;
			case MC_RATE_512_Hz:
				mc_data->interrupt_hz = 512;
				break;
			case MC_RATE_256_Hz:
				mc_data->interrupt_hz = 256;
				break;
			case MC_RATE_128_Hz:
				mc_data->interrupt_hz = 128;
				break;
			case MC_RATE_64_Hz:
				mc_data->interrupt_hz = 64;
				break;
			case MC_RATE_32_Hz:
				mc_data->interrupt_hz = 32;
				break;
			case MC_RATE_16_Hz:
				mc_data->interrupt_hz = 16;
				break;
			case MC_RATE_8_Hz:
				mc_data->interrupt_hz = 8;
				break;
			case MC_RATE_4_Hz:
				mc_data->interrupt_hz = 4;
				break;
			case MC_RATE_2_Hz:
				mc_data->interrupt_hz = 2;
				break;
			default:
				/*  debug("[ mc146818: unimplemented MC_REGA RS: %i ]\n", data[0] & MC_REGA_RSMASK);  */
				;
			}

			recalc_interrupt_cycle(cpu, mc_data);

			mc_data->cycles_left_until_interrupt =
				mc_data->interrupt_every_x_cycles;

			mc_data->reg[MC_REGA*4] =
			    data[0] & (MC_REGA_RSMASK | MC_REGA_DVMASK);

			debug("[ rtc set to interrupt every %i:th cycle ]\n",
			    mc_data->interrupt_every_x_cycles);
			return 1;
		case MC_REGB*4:
			if (((data[0] ^ mc_data->reg[MC_REGB*4]) & MC_REGB_PIE))
				mc_data->cycles_left_until_interrupt =
				    mc_data->interrupt_every_x_cycles;
			mc_data->reg[MC_REGB*4] = data[0];
			if (!(data[0] & MC_REGB_PIE)) {
				cpu_interrupt_ack(cpu, mc_data->irq_nr);
				/*  mc_data->cycles_left_until_interrupt = mc_data->interrupt_every_x_cycles;  */
			}
			/*  debug("[ mc146818: write to MC_REGB, data[0] = 0x%02x ]\n", data[0]);  */
			return 1;
		case MC_REGC*4:
			mc_data->reg[MC_REGC*4] = data[0];
			debug("[ mc146818: write to MC_REGC, data[0] = 0x%02x ]\n", data[0]);
			return 1;
		default:
			mc_data->reg[relative_addr] = data[0];
			/*  debug("[ mc146818: unimplemented write to relative_addr = %08lx ]\n", (long)relative_addr);  */
			return 1;
		}
	} else {
		/*  READ:  */
		switch (relative_addr) {
		case MC_REGC*4:	/*  Interrupt ack.  */
		case 0x01:	/*  Station's ethernet address (6 bytes)  */
		case 0x05:
		case 0x09:
		case 0x0d:
		case 0x11:
		case 0x15:
			break;
		case 0x00:
		case 0x08:
		case 0x10:
		case 0x18:
		case 0x1c:
		case 0x20:
		case 0x24:
			/*
			 *  If the SET bit is set, then we don't automatically
			 *  update the values.  Otherwise, we update them by
			 *  reading from the host's clock:
			 */
			if (mc_data->reg[MC_REGB*4] & MC_REGB_SET)
				break;

			timet = time(NULL);
			tmp = gmtime(&timet);
			/*  use to_bcd() for BCD conversion  */
			mc_data->reg[0x00] = (tmp->tm_sec);
			mc_data->reg[0x08] = (tmp->tm_min);
			mc_data->reg[0x10] = (tmp->tm_hour);
			mc_data->reg[0x18] = (tmp->tm_wday + 1);
			mc_data->reg[0x1c] = (tmp->tm_mday);
			mc_data->reg[0x20] = (tmp->tm_mon + 1);
			mc_data->reg[0x24] = (tmp->tm_year);

			switch (mc_data->access_style) {
			case MC146818_ARC_NEC:
				mc_data->reg[0x24] += (0x18 - 104);
				break;
			case MC146818_SGI:
				mc_data->reg[0x24] += (100 - 104);
				/*
				 *  TODO:  The thing above only works for
				 *  NetBSD/sgimips, not for the IP32 PROM. For
				 *  example, it interprets a host date of 'Sun
				 *  Jan 11 19:10:39 CET 2004' as 'January 11
				 *  64, 12:10:13 GMT'.   TODO: Fix this.
				 *
				 *  Perhaps it is a ds17287, not a mc146818.
				 */
				break;
			case MC146818_DEC:
				/*
				 *  DECstations must have 72 or 73 in the
				 *  Year field, or Ultrix screems.  (Weird.)
				 */
				mc_data->reg[0x24] = 72;
			default:
				;
			}
			break;
		default:
			/*  debug("[ mc146818: read from relative_addr = %04lx ]\n", (long)relative_addr);  */
			;
		}

		data[0] = mc_data->reg[relative_addr];

		if (relative_addr == MC_REGC*4) {
			cpu_interrupt_ack(cpu, mc_data->irq_nr);
			/*  mc_data->cycles_left_until_interrupt =
			    mc_data->interrupt_every_x_cycles;  */
			mc_data->reg[MC_REGC * 4] = 0x00;
		}

		return 1;
	}
}


/*
 *  dev_mc146818_init():
 *
 *  This needs to work for both DECstation emulation and other machine types,
 *  so it contains both rtc related stuff and the station's Ethernet address.
 */
void dev_mc146818_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr,
	int irq_nr, int access_style, int addrdiv)
{
	unsigned char ether_address[6];
	int i;
	struct mc_data *mc_data;

	mc_data = malloc(sizeof(struct mc_data));
	if (mc_data == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}

	memset(mc_data, 0, sizeof(struct mc_data));
	mc_data->irq_nr        = irq_nr;
	mc_data->access_style  = access_style;
	mc_data->addrdiv       = addrdiv;

	/*  Station Ethernet Address:  */
	for (i=0; i<6; i++)
		ether_address[i] = 0x10 * (i+1);

	mc_data->reg[0x01] = ether_address[0];
	mc_data->reg[0x05] = ether_address[1];
	mc_data->reg[0x09] = ether_address[2];
	mc_data->reg[0x0d] = ether_address[3];
	mc_data->reg[0x11] = ether_address[4];
	mc_data->reg[0x15] = ether_address[5];
	/*  TODO:  19, 1d, 21, 25 = checksum bytes 1,2,2,1 resp. */
	mc_data->reg[0x29] = ether_address[5];
	mc_data->reg[0x2d] = ether_address[4];
	mc_data->reg[0x31] = ether_address[3];
	mc_data->reg[0x35] = ether_address[2];
	mc_data->reg[0x39] = ether_address[1];
	mc_data->reg[0x3d] = ether_address[1];
	mc_data->reg[0x41] = ether_address[0];
	mc_data->reg[0x45] = ether_address[1];
	mc_data->reg[0x49] = ether_address[2];
	mc_data->reg[0x4d] = ether_address[3];
	mc_data->reg[0x51] = ether_address[4];
	mc_data->reg[0x55] = ether_address[5];
	/*  TODO:  59, 5d = checksum bytes 1,2 resp. */
	mc_data->reg[0x61] = 0xff;
	mc_data->reg[0x65] = 0x00;
	mc_data->reg[0x69] = 0x55;
	mc_data->reg[0x6d] = 0xaa;
	mc_data->reg[0x71] = 0xff;
	mc_data->reg[0x75] = 0x00;
	mc_data->reg[0x79] = 0x55;
	mc_data->reg[0x7d] = 0xaa;

	if (access_style == MC146818_DEC) {
		/*  Battery valid, for DECstations  */
		mc_data->reg[0xf8] = 1;
	}

	if (access_style == MC146818_ARC_PICA)
		memory_device_register(mem, "mc146818_pica", 0x90000000070ULL,
		    1, dev_mc146818_pica_access, (void *)mc_data);

	if (access_style == MC146818_PC_CMOS)
		memory_device_register(mem, "mc146818", baseaddr,
		    2 * addrdiv, dev_mc146818_access, (void *)mc_data);
	else
		memory_device_register(mem, "mc146818", baseaddr,
		    DEV_MC146818_LENGTH * addrdiv, dev_mc146818_access,
		    (void *)mc_data);
	cpu_add_tickfunction(cpu, dev_mc146818_tick, mc_data, TICK_STEPS_SHIFT);
}

