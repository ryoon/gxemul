/*
 *  Copyright (C) 2004 by Anders Gavare.  All rights reserved.
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
 *  $Id: dev_zs.c,v 1.7 2004-06-12 17:12:09 debug Exp $
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

#include "memory.h"
#include "misc.h"
#include "console.h"
#include "devices.h"


struct zs_data {
	int		irq_nr;
	int		addrmult;

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

	if (console_charavail() || d->tx_done)
		cpu_interrupt(cpu, d->irq_nr);
	else
		cpu_interrupt_ack(cpu, d->irq_nr);
}


/*
 *  dev_zs_access():
 *
 *  Returns 1 if ok, 0 on error.
 */
int dev_zs_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *extra)
{
	struct zs_data *d = extra;
	uint64_t idata = 0, odata = 0;

	idata = memory_readmax64(cpu, data, len);
	relative_addr /= d->addrmult;

	switch (relative_addr) {
	case 3:
		if (writeflag==MEM_READ) {
			odata = ZSRR0_TX_READY;
			if (console_charavail())
				odata |= ZSRR0_RX_READY;
			/*  debug("[ zs: read from 0x%08lx: 0x%08x ]\n", (long)relative_addr, odata);  */
		} else {
			debug("[ zs: write to  0x%08lx: 0x%08x ]\n", (long)relative_addr, idata);
		}
		break;
	case 7:
		if (writeflag==MEM_READ) {
			if (console_charavail())
				odata = console_readchar();
			else
				odata = 0;
			/*  debug("[ zs: read from 0x%08lx: 0x%08x ]\n", (long)relative_addr, odata);  */
		} else {
			/*  debug("[ zs: write to  0x%08lx: 0x%08x ]\n", (long)relative_addr, idata);  */
			console_putchar(idata & 255);
			d->tx_done = 1;
		}
		break;
	case 0xb:
		if (writeflag==MEM_READ) {
			odata = 0;
			if (d->tx_done)
				odata |= 2;
			if (console_charavail())
				odata |= 4;
			d->tx_done = 0;
			debug("[ zs: read from 0x%08lx: 0x%08x ]\n", (long)relative_addr, odata);
		} else {
			debug("[ zs: write to  0x%08lx: 0x%08x ]\n", (long)relative_addr, idata);
		}
		break;
	default:
		if (writeflag==MEM_READ) {
			debug("[ zs: read from 0x%08lx ]\n", (long)relative_addr);
		} else {
			debug("[ zs: write to  0x%08lx: 0x%08x ]\n", (long)relative_addr, idata);
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
void dev_zs_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr, int irq_nr, int addrmult)
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

	memory_device_register(mem, "zs", baseaddr, DEV_ZS_LENGTH * addrmult, dev_zs_access, d);
	cpu_add_tickfunction(cpu, dev_zs_tick, d, 10);
}

