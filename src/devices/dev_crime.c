/*
 *  Copyright (C) 2003-2004  Anders Gavare.  All rights reserved.
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
 *  $Id: dev_crime.c,v 1.21 2004-10-19 03:40:32 debug Exp $
 *  
 *  SGI "crime".
 *
 *  TODO:  This is hardcoded for bigendian, but since it will probably only
 *         be used for SGI emulation, and no little-endian machines, this is
 *         not that much of a problem.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "misc.h"
#include "console.h"
#include "devices.h"

#include "crimereg.h"


#define	CRIME_TICKSHIFT			10
#define	CRIME_SPEED_MUL_FACTOR		1
#define	CRIME_SPEED_DIV_FACTOR		4


/*
 *  dev_crime_tick():
 *
 *  This function simply updates CRIME_TIME each tick.
 *
 *  The names DIV and MUL may be a bit confusing. Increasing the
 *  MUL factor will result in an OS running on the emulated machine
 *  detecting a faster CPU. Increasing the DIV factor will result
 *  in a slower detected CPU.
 *
 *  A R10000 is detected as running at
 *  CRIME_SPEED_FACTOR * 66 MHz. (TODO: this is not correct anymore)
 */
void dev_crime_tick(struct cpu *cpu, void *extra)
{
	int j, carry, old, new;
	uint64_t what_to_add = (1<<CRIME_TICKSHIFT)
	    * CRIME_SPEED_DIV_FACTOR / CRIME_SPEED_MUL_FACTOR;
	struct crime_data *d = extra;

	j = 0;
	carry = 0;
	while (j < 8) {
		old = d->reg[CRIME_TIME + 7 - j];
		new = old + ((what_to_add >> (j * 8)) & 255) + carry;
		d->reg[CRIME_TIME + 7 - j] = new & 255;
		if (new >= 256)
			carry = 1;
		else
			carry = 0;
		j++;
	}
}


/*
 *  dev_crime_access():
 */
int dev_crime_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	int i;
	struct crime_data *d = extra;

	/*
	 *  Set crime version/revision:
	 *
	 *  This might not be the most elegant or correct solution,
	 *  but it seems that the IP32 PROM likes 0x11 for machines
	 *  without graphics, and 0xa1 for machines with graphics.
	 *
	 *  NetBSD 2.0 complains about "unknown" crime for 0x11,
	 *  but I guess that's something one has to live with.
	 *
	 *  (TODO?)
	 */
	d->reg[4] = 0x00; d->reg[5] = 0x00; d->reg[6] = 0x00; d->reg[7] = d->use_fb? 0xa1 : 0x11;

	/*  Amount of memory.  Bit 8 of bank control set ==> 128MB instead of 32MB per bank (?)  */
	/*  When the bank control registers contain the same value as the previous one, that
		bank is not valid. (?)  */
	d->reg[CRM_MEM_BANK_CTRL0 + 6] = 0x0;	/*  lowest bit set = 128MB, clear = 32MB  */
	d->reg[CRM_MEM_BANK_CTRL0 + 7] = 0x0;	/*  address * 32MB  */
	d->reg[CRM_MEM_BANK_CTRL1 + 6] = 0x0;	/*  lowest bit set = 128MB, clear = 32MB  */
	d->reg[CRM_MEM_BANK_CTRL1 + 7] = 0x1;	/*  address * 32MB  */

	if (writeflag == MEM_WRITE)
		memcpy(&d->reg[relative_addr], data, len);
	else
		memcpy(data, &d->reg[relative_addr], len);

	if (relative_addr == 0x18 || relative_addr == 0x1c) {
		/*
		 *  NOTE:  This is super-ugly. I want to force the MIPS interrupt
		 *  assertion bits to be updated, taking the interrupt mask
		 *  into account, so I call cpu_interrupt_ack() with something
		 *  which "hopefully" doesn't do any damage. cpu_interrupt_ack()
		 *  will in turn call the SGI crime interrupt stuff in src/machine.c,
		 *  which takes the interrupt mask into account when asserting or
		 *  deasserting the crime interrupt pin.
		 *
		 *  TODO: How to solve this nicer?
		 */
		cpu_interrupt_ack(cpu, 8);
	}

	switch (relative_addr) {
#if 1
	case 0x10:	/*  Current interrupt status  */
	case 0x14:
	case 0x18:	/*  Current interrupt mask  */
	case 0x1c:
	case 0x34:
#endif
#if 1
	case CRIME_TIME:
	case CRIME_TIME+4:
#endif
		/*  don't dump debug info for these  */
		break;
	default:
		if (writeflag==MEM_READ) {
			debug("[ crime: read from 0x%x, len=%i:", (int)relative_addr, len);
			for (i=0; i<len; i++)
				debug(" %02x", data[i]);
			debug(" ]\n", len);
		} else {
			debug("[ crime: write to 0x%x:", (int)relative_addr);
			for (i=0; i<len; i++)
				debug(" %02x", data[i]);
			debug(" (len=%i) ]\n", len);
		}
	}

	return 1;
}


/*
 *  dev_crime_init():
 */
struct crime_data *dev_crime_init(struct cpu *cpu, struct memory *mem,
	uint64_t baseaddr, int irq_nr, int use_fb)
{
	struct crime_data *d;

	d = malloc(sizeof(struct crime_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct crime_data));
	d->irq_nr = irq_nr;
	d->use_fb = use_fb;

	memory_device_register(mem, "crime", baseaddr, DEV_CRIME_LENGTH,
	    dev_crime_access, d);
	cpu_add_tickfunction(cpu, dev_crime_tick, d, CRIME_TICKSHIFT);

	return d;
}


