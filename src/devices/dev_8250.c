/*
 *  Copyright (C) 2004  Anders Gavare.  All rights reserved.
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
 *  $Id: dev_8250.c,v 1.12 2005-01-09 01:55:24 debug Exp $
 *  
 *  8250 serial controller.
 *
 *  TODO:  Actually implement this device.  So far it's just a fake device
 *         to allow Linux to print stuff to the console.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "misc.h"
#include "console.h"
#include "devices.h"


struct dev_8250_data {
	int		reg[8];
	int		irq_enable;
	int		irqnr;
	int		addrmult;

	int		dlab;		/*  Divisor Latch Access bit  */
	int		divisor;
	int		databits;
	char		parity;
	const char	*stopbits;
};


/*
 *  dev_8250_tick():
 *
 */
void dev_8250_tick(struct cpu *cpu, void *extra)
{
#if 0
	/*  This stuff works for 16550.  TODO for 8250  */

	struct dev_8250_data *d = extra;

	d->reg[REG_IID] |= IIR_NOPEND;
	cpu_interrupt_ack(cpu, d->irqnr);

	if (console_charavail())
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


/*
 *  dev_8250_access():
 */
int dev_8250_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *extra)
{
	uint64_t idata = 0, odata = 0;
	struct dev_8250_data *d = extra;

	idata = memory_readmax64(cpu, data, len);
	relative_addr /= d->addrmult;

	if (writeflag == MEM_WRITE && relative_addr == 0)
		console_putchar(idata);
	else
	if (writeflag == MEM_READ && relative_addr == 5)
		odata = 64 + 32;
	else {
#if 0
		if (writeflag == MEM_WRITE)
			fatal("[ 8250: write addr=0x%02x idata = 0x%02x ]\n", relative_addr, idata);
		else
			fatal("[ 8250: read addr=0x%02x ]\n", relative_addr);
#endif
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_8250_init():
 */
void dev_8250_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr,
	int irq_nr, int addrmult)
{
	struct dev_8250_data *d;

	d = malloc(sizeof(struct dev_8250_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct dev_8250_data));
	d->irqnr = irq_nr;
	d->addrmult = addrmult;

	d->dlab = 0;
	d->divisor  = 115200 / 9600;
	d->databits = 8;
	d->parity   = 'N';
	d->stopbits = "1";

	memory_device_register(mem, "8250", baseaddr,
	    DEV_8250_LENGTH * addrmult, dev_8250_access, d, MEM_DEFAULT, NULL);
	cpu_add_tickfunction(cpu, dev_8250_tick, d, 13);
}

