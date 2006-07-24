/*
 *  Copyright (C) 2005-2006  Anders Gavare.  All rights reserved.
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
 *  $Id: dev_8253.c,v 1.14 2006-07-24 19:08:17 debug Exp $
 *
 *  Intel 8253/8254 Programmable Interval Timer
 *
 *  TODO: The timers don't really count down. Fix this when there is a generic
 *  clock framework; also split counter[] into reset value and current value.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "emul.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"

#include "i8253reg.h"


/*  #define debug fatal  */

#define	DEV_8253_LENGTH		4
#define	TICK_SHIFT		14


struct pit8253_data {
	int		in_use;

	int		irq0_nr;
	int		counter_select;
	uint8_t		mode_byte;

	int		mode[3];
	int		counter[3];
};


DEVICE_TICK(8253)
{
	struct pit8253_data *d = (struct pit8253_data *) extra;

	if (!d->in_use)
		return;

	switch (d->mode[0] & 0x0e) {

	case I8253_TIMER_INTTC:
		/*  TODO: Correct frequency!  */
		cpu_interrupt(cpu, d->irq0_nr);
		break;

	case I8253_TIMER_RATEGEN:
		break;

	default:fatal("[ 8253: unimplemented mode 0x%x ]\n", d->mode[0] & 0x0e);
		exit(1);
	}
}


DEVICE_ACCESS(8253)
{
	struct pit8253_data *d = (struct pit8253_data *) extra;
	uint64_t idata = 0, odata = 0;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);

	d->in_use = 1;

	/*  TODO: ack somewhere else  */
	cpu_interrupt_ack(cpu, d->irq0_nr);

	switch (relative_addr) {

	case I8253_TIMER_CNTR0:
	case I8253_TIMER_CNTR1:
	case I8253_TIMER_CNTR2:
		if (writeflag == MEM_WRITE) {
			switch (d->mode_byte & 0x30) {
			case I8253_TIMER_LSB:
			case I8253_TIMER_16BIT:
				d->counter[relative_addr] &= 0xff00;
				d->counter[relative_addr] |= (idata & 0xff);
				break;
			case I8253_TIMER_MSB:
				d->counter[relative_addr] &= 0x00ff;
				d->counter[relative_addr] |= ((idata&0xff)<<8);
				debug("[ 8253: counter %i set to %i (%i Hz) "
				    "]\n", relative_addr, d->counter[
				    relative_addr], (int)(I8253_TIMER_FREQ /
				    (float)d->counter[relative_addr] + 0.5));
				break;
			default:fatal("[ 8253: huh? writing to counter"
				    " %i but neither from msb nor lsb? ]\n",
				    relative_addr);
			}
		} else {
			switch (d->mode_byte & 0x30) {
			case I8253_TIMER_LSB:
			case I8253_TIMER_16BIT:
				odata = d->counter[relative_addr] & 0xff;
				break;
			case I8253_TIMER_MSB:
				odata = (d->counter[relative_addr] >> 8) & 0xff;
				break;
			default:fatal("[ 8253: huh? reading from counter"
				    " %i but neither from msb nor lsb? ]\n",
				    relative_addr);
			}
		}

		/*  Switch from LSB to MSB, if accessing as 16-bit word:  */
		if ((d->mode_byte & 0x30) == I8253_TIMER_16BIT)
			d->mode_byte &= ~I8253_TIMER_LSB;

		break;

	case I8253_TIMER_MODE:
		if (writeflag == MEM_WRITE) {
			d->mode_byte = idata;

			d->counter_select = idata >> 6;
			if (d->counter_select > 2) {
				debug("[ 8253: attempt to select counter 3,"
				    " which doesn't exist. ]\n");
				d->counter_select = 0;
			}

			d->mode[d->counter_select] = idata & 0x0e;

			debug("[ 8253: select=%i mode=0x%x ",
			    d->counter_select, d->mode[d->counter_select]);
			if (idata & 0x30) {
				switch (idata & 0x30) {
				case I8253_TIMER_LSB:
					debug("LSB ");
					break;
				case I8253_TIMER_16BIT:
					debug("LSB+");
				case I8253_TIMER_MSB:
					debug("MSB ");
				}
			}
			debug("]\n");

			if (idata & I8253_TIMER_BCD) {
				fatal("[ 8253: BCD not yet implemented ]\n");
				exit(1);
			}
		} else {
			debug("[ 8253: read; can this actually happen? ]\n");
			odata = d->mode_byte;
		}
		break;

	default:if (writeflag == MEM_WRITE) {
			fatal("[ 8253: unimplemented write to address 0x%x"
			    " data=0x%02x ]\n", (int)relative_addr, (int)idata);
		} else {
			fatal("[ 8253: unimplemented read from address 0x%x "
			    "]\n", (int)relative_addr);
		}
		exit(1);
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


DEVINIT(8253)
{
	struct pit8253_data *d = malloc(sizeof(struct pit8253_data));

	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct pit8253_data));
	d->irq0_nr = devinit->irq_nr;
	d->in_use = devinit->in_use;

	/*  Don't cause interrupt, by default.  */
	d->mode[0] = I8253_TIMER_RATEGEN;
	d->mode[1] = I8253_TIMER_RATEGEN;
	d->mode[2] = I8253_TIMER_RATEGEN;

	memory_device_register(devinit->machine->memory, devinit->name,
	    devinit->addr, DEV_8253_LENGTH, dev_8253_access, (void *)d,
	    DM_DEFAULT, NULL);

	machine_add_tickfunction(devinit->machine, dev_8253_tick,
	    d, TICK_SHIFT, 0.0);

	return 1;
}

