/*
 *  Copyright (C) 2004-2005  Anders Gavare.  All rights reserved.
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
 *  $Id: dev_zs.c,v 1.22 2005-11-13 00:14:10 debug Exp $
 *  
 *  Zilog serial controller, used by (at least) the SGI emulation mode.
 *
 *  TODO:  Implement this correctly.  The values in here are too
 *  hardcoded, and the controller should be able to handle 2 serial lines.
 *
 *  Right now it only barely works with NetSBD/sgimips.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "console.h"
#include "cpu.h"
#include "devices.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"


#define	ZS_TICK_SHIFT		14

struct zs_data {
	int		irq_nr;
	int		console_handle;
	int		addrmult;

	int		reg_select;

	int		tx_done;
};


/*  From NetBSD:  */
#define	ZSRR0_RX_READY		1
#define	ZSRR0_TX_READY		4


/*
 *  dev_zs_tick():
 */
void dev_zs_tick(struct cpu *cpu, void *extra)
{
	struct zs_data *d = (struct zs_data *) extra;

	if (console_charavail(d->console_handle) || d->tx_done)
		cpu_interrupt(cpu, d->irq_nr);
	else
		cpu_interrupt_ack(cpu, d->irq_nr);
}


/*
 *  dev_zs_access():
 */
int dev_zs_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr,
	unsigned char *data, size_t len, int writeflag, void *extra)
{
	struct zs_data *d = extra;
	uint64_t idata = 0, odata = 0;
	int port_nr;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);

	relative_addr /= d->addrmult;

	port_nr = relative_addr / 8;

/*  TODO:  The zs controller has 2 ports...  */
/*	relative_addr &= 7;  */

	switch (relative_addr) {
	case 3:
		if (writeflag==MEM_READ) {
			odata = ZSRR0_TX_READY;
			if (console_charavail(d->console_handle))
				odata |= ZSRR0_RX_READY;
			/*  debug("[ zs: read from 0x%08lx: 0x%08x ]\n",
			    (long)relative_addr, (int)odata);  */
		} else {
			/*  Hm...  TODO  */
			if (d->reg_select == 0) {
				d->reg_select = idata;
			} else {
				switch (d->reg_select) {
				case 8:	console_putchar(d->console_handle,
					    idata & 255);
					break;
				default:
					debug("[ zs: write to (unimplemented)"
					    " register 0x%02x: 0x%08x ]\n",
					    d->reg_select, (int)idata);
				}
				d->reg_select = 0;
			}
			/*  debug("[ zs: write to  0x%08lx: 0x%08x ]\n",
			    (long)relative_addr, (int)idata);  */
		}
		break;
	case 7:
		if (writeflag==MEM_READ) {
			if (console_charavail(d->console_handle))
				odata = console_readchar(d->console_handle);
			else
				odata = 0;
			/*  debug("[ zs: read from 0x%08lx: 0x%08x ]\n",
			    (long)relative_addr, (int)odata);  */
		} else {
			/*  debug("[ zs: write to  0x%08lx: 0x%08x ]\n",
			    (long)relative_addr, (int)idata);  */
			console_putchar(d->console_handle, idata & 255);
			d->tx_done = 1;
		}
		break;

/*  hehe, perhaps 0xb and 0xf are the second channel :-)  */

	case 0xb:
		if (writeflag==MEM_READ) {
			odata = 0;
#if 0
	/*  TODO: Weird. Linux needs 4 here, NetBSD wants 0.  */
	odata = 4;
#endif
			if (d->tx_done)
				odata |= 2;
			if (console_charavail(d->console_handle))
				odata |= 4;
			d->tx_done = 0;
			debug("[ zs: read from 0x%08lx: 0x%08x ]\n",
			    (long)relative_addr, (int)odata);
		} else {
			debug("[ zs: write to  0x%08lx: 0x%08x ]\n",
			    (long)relative_addr, (int)idata);
		}
		break;

	/*  0xf is used by Linux:  */
	case 0xf:
		if (writeflag==MEM_READ) {
			if (console_charavail(d->console_handle))
				odata = console_readchar(d->console_handle);
			else
				odata = 0;
			/*  debug("[ zs: read from 0x%08lx: 0x%08x ]\n",
			    (long)relative_addr, (int)odata);  */
		} else {
			/*  debug("[ zs: write to  0x%08lx: 0x%08x ]\n",
			    (long)relative_addr, (int)idata);  */
			console_putchar(d->console_handle, idata & 255);
			d->tx_done = 1;
		}
		break;
	default:
		if (writeflag==MEM_READ) {
			debug("[ zs: read from 0x%08lx ]\n",
			    (long)relative_addr);
		} else {
			debug("[ zs: write to  0x%08lx: 0x%08x ]\n",
			    (long)relative_addr, (int)idata);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	dev_zs_tick(cpu, extra);

	return 1;
}


/*
 *  dev_zs_init():
 */
int dev_zs_init(struct machine *machine, struct memory *mem,
	uint64_t baseaddr, int irq_nr, int addrmult, char *name)
{
	struct zs_data *d;

	d = malloc(sizeof(struct zs_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct zs_data));
	d->irq_nr   = irq_nr;
	d->addrmult = addrmult;
	d->console_handle = console_start_slave(machine, name);

	memory_device_register(mem, "zs", baseaddr, DEV_ZS_LENGTH * addrmult,
	    dev_zs_access, d, DM_DEFAULT, NULL);

	machine_add_tickfunction(machine, dev_zs_tick, d, ZS_TICK_SHIFT);

	return d->console_handle;
}

