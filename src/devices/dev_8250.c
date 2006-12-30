/*
 *  Copyright (C) 2004-2007  Anders Gavare.  All rights reserved.
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
 *  $Id: dev_8250.c,v 1.25 2006-12-30 13:30:57 debug Exp $
 *  
 *  8250 serial controller.
 *
 *  TODO:  Actually implement this device.  So far it's just a fake device
 *         to allow Linux to print stuff to the console.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "console.h"
#include "device.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"


struct dev_8250_data {
	int		console_handle;
	char		*name;

	int		irq_enable;
	int		irqnr;
	int		in_use;
	int		addrmult;

	int		reg[8];

	int		dlab;		/*  Divisor Latch Access bit  */
	int		divisor;
	int		databits;
	char		parity;
	const char	*stopbits;
};

#define	DEV_8250_LENGTH		8
#define	DEV_8250_TICKSHIFT	15


DEVICE_TICK(8250)
{
#if 0
	/*  This stuff works for 16550.  TODO for 8250  */

	struct dev_8250_data *d = extra;

	d->reg[REG_IID] |= IIR_NOPEND;
	cpu_interrupt_ack(cpu, d->irqnr);

	if (console_charavail(d->console_handle))
		d->reg[REG_IID] |= IIR_RXRDY;
	else
		d->reg[REG_IID] &= ~IIR_RXRDY;

	if (d->reg[REG_MCR] & MCR_IENABLE) {
		if (d->irq_enable & IER_ETXRDY && d->reg[REG_IID] & IIR_TXRDY) {
			cpu_interrupt(cpu, d->irqnr);
			d->reg[REG_IID] &= ~IIR_NOPEND;
		}

		if (d->irq_enable & IER_ERXRDY && d->reg[REG_IID] & IIR_RXRDY) {
			cpu_interrupt(cpu, d->irqnr);
			d->reg[REG_IID] &= ~IIR_NOPEND;
		}
	}
#endif
}


DEVICE_ACCESS(8250)
{
	uint64_t idata = 0, odata = 0;
	struct dev_8250_data *d = extra;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);

	relative_addr /= d->addrmult;

	if (writeflag == MEM_WRITE && relative_addr == 0) {
		console_putchar(d->console_handle, idata);
	} else if (writeflag == MEM_READ && relative_addr == 5) {
		odata = 64 + 32;
	} else {
#if 0
		if (writeflag == MEM_WRITE)
			fatal("[ 8250: write addr=0x%02x idata = 0x%02x ]\n",
			    (int)relative_addr, (int)idata);
		else
			fatal("[ 8250: read addr=0x%02x ]\n", relative_addr);
#endif
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


DEVINIT(8250)
{
	size_t nlen;
	char *name;
	struct dev_8250_data *d;

	d = malloc(sizeof(struct dev_8250_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct dev_8250_data));
	d->irqnr    = devinit->irq_nr;
	d->addrmult = devinit->addr_mult;
	d->in_use   = devinit->in_use;
	d->dlab     = 0;
	d->divisor  = 115200 / 9600;
	d->databits = 8;
	d->parity   = 'N';
	d->stopbits = "1";
	d->name = devinit->name2 != NULL? devinit->name2 : "";
	d->console_handle =
	    console_start_slave(devinit->machine, devinit->name2 != NULL?
	    devinit->name2 : devinit->name, d->in_use);

	nlen = strlen(devinit->name) + 10;
	if (devinit->name2 != NULL)
		nlen += strlen(devinit->name2);
	name = malloc(nlen);
	if (name == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	if (devinit->name2 != NULL && devinit->name2[0])
		snprintf(name, nlen, "%s [%s]", devinit->name, devinit->name2);
	else
		snprintf(name, nlen, "%s", devinit->name);

	memory_device_register(devinit->machine->memory, name,
	    devinit->addr, DEV_8250_LENGTH * devinit->addr_mult,
	    dev_8250_access, d, DM_DEFAULT, NULL);
	machine_add_tickfunction(devinit->machine, dev_8250_tick, d,
	    DEV_8250_TICKSHIFT, 0.0);

	return 1;
}

