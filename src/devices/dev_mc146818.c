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
 *  $Id: dev_mc146818.c,v 1.6 2004-01-03 03:10:50 debug Exp $
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

#include "misc.h"
#include "devices.h"

#include "mc146818reg.h"

extern int register_dump;
extern int instruction_trace;
extern int bootstrap_cpu;
extern int ncpus;
extern struct cpu **cpus;


#define	to_bcd(x)	( (x/10) * 16 + (x%10) )

/*  #define MC146818_DEBUG  */
#define	TICK_STEPS_SHIFT	6


#define	N_REGISTERS	256
struct mc_data {
	int	pc_style_cmos;
	int	last_addr;

	int	register_choice;
	int	reg[N_REGISTERS];
	int	addrdiv;

	int	timebase_hz;
	int	interrupt_hz;
	int	emulated_ips;
	int	irq_nr;

	int	interrupt_every_x_instructions;
	int	instructions_left_until_interrupt;
};


/*
 *  dev_mc146818_tick():
 */
void dev_mc146818_tick(struct cpu *cpu, void *extra)
{
	struct mc_data *mc_data = extra;

	if (mc_data == NULL)
		return;

	if ((mc_data->reg[MC_REGB*4] & MC_REGB_PIE) && mc_data->interrupt_every_x_instructions > 0) {
		mc_data->instructions_left_until_interrupt -= (1 << TICK_STEPS_SHIFT);
		if (mc_data->instructions_left_until_interrupt < 0 ||
		    mc_data->instructions_left_until_interrupt >= mc_data->interrupt_every_x_instructions) {
			/*  debug("[ rtc interrupt ]\n");  */
			cpu_interrupt(cpus[bootstrap_cpu], mc_data->irq_nr);

			mc_data->reg[MC_REGC*4] |= MC_REGC_PF;

			/*  Reset the instruction countdown:  */
			mc_data->instructions_left_until_interrupt = mc_data->interrupt_every_x_instructions;
		}
	}
}


/*
 *  dev_mc146818_access():
 *
 *  Returns 1 if ok, 0 on error.
 */
int dev_mc146818_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *extra)
{
	struct tm *tmp;
	time_t timet;
	struct mc_data *mc_data = extra;

#ifdef MC146818_DEBUG
	if (writeflag == MEM_WRITE) {
		int i;
		debug("[ mc146818: write to addr=0x%04x: ", relative_addr);
		for (i=0; i<len; i++)
			debug("%02x ", data[i]);
		debug("]\n");
	} else
		debug("[ mc146818: read from addr=0x%04x ]\n", relative_addr);
#endif

	if (writeflag == MEM_WRITE && relative_addr == 0x70) {
		mc_data->last_addr = data[0];
		return 1;
	}

	relative_addr /= mc_data->addrdiv;

	if (relative_addr == 0x71)
		relative_addr = mc_data->last_addr;

	/*
	 *  For some reason, Linux/sgimips relies on the UIP bit to go
	 *  on and off. Without this code, booting Linux takes forever:
	 */
	mc_data->reg[MC_REGA*4] &= ~MC_REGA_UIP;
	if ((random() & 0xfff) == 0)
		mc_data->reg[MC_REGA*4] |= MC_REGA_UIP;

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
			case MC_RATE_NONE:	mc_data->interrupt_hz = 0;
						break;
			case MC_RATE_1:		if (mc_data->timebase_hz == 32000)
							mc_data->interrupt_hz = 256;
						else
							mc_data->interrupt_hz = 32768;
						break;
			case MC_RATE_2:		if (mc_data->timebase_hz == 32000)
							mc_data->interrupt_hz = 128;
						else
							mc_data->interrupt_hz = 16384;
						break;
			case MC_RATE_8192_Hz:	mc_data->interrupt_hz = 8192;	break;
			case MC_RATE_4096_Hz:	mc_data->interrupt_hz = 4096;	break;
			case MC_RATE_2048_Hz:	mc_data->interrupt_hz = 2048;	break;
			case MC_RATE_1024_Hz:	mc_data->interrupt_hz = 1024;	break;
			case MC_RATE_512_Hz:	mc_data->interrupt_hz = 512;	break;
			case MC_RATE_256_Hz:	mc_data->interrupt_hz = 256;	break;
			case MC_RATE_128_Hz:	mc_data->interrupt_hz = 128;	break;
			case MC_RATE_64_Hz:	mc_data->interrupt_hz = 64;	break;
			case MC_RATE_32_Hz:	mc_data->interrupt_hz = 32;	break;
			case MC_RATE_16_Hz:	mc_data->interrupt_hz = 16;	break;
			case MC_RATE_8_Hz:	mc_data->interrupt_hz = 8;	break;
			case MC_RATE_4_Hz:	mc_data->interrupt_hz = 4;	break;
			case MC_RATE_2_Hz:	mc_data->interrupt_hz = 2;	break;
			default:
				/*  debug("[ mc146818: unimplemented MC_REGA RS: %i ]\n", data[0] & MC_REGA_RSMASK);  */
				;
			}

			if (mc_data->interrupt_hz > 0)
				mc_data->interrupt_every_x_instructions = mc_data->emulated_ips / mc_data->interrupt_hz;
			else
				mc_data->interrupt_every_x_instructions = 0;

			mc_data->instructions_left_until_interrupt =
				mc_data->interrupt_every_x_instructions;

			mc_data->reg[MC_REGA*4] = data[0] & (MC_REGA_RSMASK | MC_REGA_DVMASK);

			debug("[ rtc set to interrupt every %i:th instruction ]\n", mc_data->interrupt_every_x_instructions);
			return 1;
		case MC_REGB*4:
			if (((data[0] ^ mc_data->reg[MC_REGB*4]) & MC_REGB_PIE))
				mc_data->instructions_left_until_interrupt =
				    mc_data->interrupt_every_x_instructions;
			mc_data->reg[MC_REGB*4] = data[0];
			if (!(data[0] & MC_REGB_PIE)) {
				cpu_interrupt_ack(cpus[bootstrap_cpu], mc_data->irq_nr);
				/*  mc_data->instructions_left_until_interrupt = mc_data->interrupt_every_x_instructions;  */
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
		case MC_REGC*4:		/*  Interrupt ack.  */
		case 0x01:		/*  Station's ethernet address (6 bytes)  */
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
			timet = time(NULL);
			tmp = gmtime(&timet);	/*  use to_bcd() for BCD conversion  */
			mc_data->reg[0x00] = (tmp->tm_sec);
			mc_data->reg[0x08] = (tmp->tm_min);
			mc_data->reg[0x10] = (tmp->tm_hour);
			mc_data->reg[0x18] = (tmp->tm_wday + 1);	/*  ?  */
			mc_data->reg[0x1c] = (tmp->tm_mday);
			mc_data->reg[0x20] = (tmp->tm_mon + 1);
			mc_data->reg[0x24] = (tmp->tm_year);
			break;
		default:
			/*  debug("[ mc146818: read from relative_addr = %04lx ]\n", (long)relative_addr);  */
			;
		}

		data[0] = mc_data->reg[relative_addr];

		if (relative_addr == MC_REGC*4) {
			cpu_interrupt_ack(cpus[bootstrap_cpu], mc_data->irq_nr);
			/*  mc_data->instructions_left_until_interrupt = mc_data->interrupt_every_x_instructions;  */
			mc_data->reg[MC_REGC * 4] = 0x00;
		}

		return 1;
	}
}


/*
 *  dev_mc146818_init():
 */
void dev_mc146818_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr, int irq_nr, int pc_style_cmos, int addrdiv, int emulated_ips)
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
	mc_data->pc_style_cmos = pc_style_cmos;
	mc_data->emulated_ips  = emulated_ips;
	mc_data->addrdiv       = addrdiv;

	/*  Station Ethernet Address:  */
	for (i=0; i<6; i++)
		ether_address[i] = 0x11 * (i+1);

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

	memory_device_register(mem, "mc146818", baseaddr, DEV_MC146818_LENGTH * addrdiv, dev_mc146818_access, (void *)mc_data);
	cpu_add_tickfunction(cpu, dev_mc146818_tick, mc_data, TICK_STEPS_SHIFT);
}

