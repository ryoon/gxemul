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
 *  $Id: dev_ns16550.c,v 1.15 2004-06-13 09:15:25 debug Exp $
 *  
 *  NS16550 serial controller.
 *
 *  TODO: fifo.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "misc.h"
#include "console.h"
#include "devices.h"

#include "comreg.h"


/*  #define DISABLE_FIFO  */


struct ns_data {
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
 *  dev_ns16550_tick():
 *
 */
void dev_ns16550_tick(struct cpu *cpu, void *extra)
{
	struct ns_data *d = extra;

	d->reg[com_iir] |= IIR_NOPEND;
	cpu_interrupt_ack(cpu, d->irqnr);

	if (console_charavail())
		d->reg[com_iir] |= IIR_RXRDY;
	else
		d->reg[com_iir] &= ~IIR_RXRDY;

	if (d->reg[com_mcr] & MCR_IENABLE) {
		if (d->irq_enable & IER_ETXRDY && d->reg[com_iir] & IIR_TXRDY) {
			cpu_interrupt(cpu, d->irqnr);
			d->reg[com_iir] &= ~IIR_NOPEND;
		}

		if (d->irq_enable & IER_ERXRDY && d->reg[com_iir] & IIR_RXRDY) {
			cpu_interrupt(cpu, d->irqnr);
			d->reg[com_iir] &= ~IIR_NOPEND;
		}
	}
}


/*
 *  dev_ns16550_access():
 *
 *  Returns 1 if ok, 0 on error.
 */
int dev_ns16550_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *extra)
{
	uint64_t idata = 0, odata=0;
	int i;
	struct ns_data *d = extra;

	idata = memory_readmax64(cpu, data, len);

	/*  Always ready to transmit:  */
	d->reg[com_lsr] |= LSR_TXRDY | LSR_TSRE;
	d->reg[com_lsr] &= ~LSR_RXRDY;
	d->reg[com_msr] = MSR_DCD | MSR_DSR | MSR_CTS;

#ifdef DISABLE_FIFO
	/*  FIFO turned off:  */
	d->reg[com_iir] &= 0x0f;
#endif

	if (console_charavail()) {
		d->reg[com_lsr] |= LSR_RXRDY;
	}

	relative_addr /= d->addrmult;

	switch (relative_addr) {
	case com_data:	/*  com_data or com_dlbl  */
		/*  Read/write of the Divisor value:  */
		if (d->dlab) {
			if (writeflag == MEM_WRITE) {
				/*  Set the low byte of the divisor:  */
				d->divisor &= ~0xff;
				d->divisor |= (idata & 0xff);
			} else {
				odata = d->divisor & 0xff;
			}
			break;
		}

		/*  Read write of data:  */
		if (writeflag == MEM_WRITE) {
			/*  Ugly hack: don't show form feeds:  */
			if (idata != 12)
				console_putchar(idata);

			if (d->reg[com_mcr] & MCR_LOOPBACK)
				console_makeavail(idata);

			d->reg[com_iir] |= IIR_TXRDY;
			dev_ns16550_tick(cpu, d);
			return 1;
		} else {
			odata = console_readchar();
			dev_ns16550_tick(cpu, d);
		}
		break;
	case com_ier:	/*  interrupt enable AND high byte of the divisor  */
		/*  Read/write of the Divisor value:  */
		if (d->dlab) {
			if (writeflag == MEM_WRITE) {
				/*  Set the high byte of the divisor:  */
				d->divisor &= ~0xff00;
				d->divisor |= ((idata & 0xff) << 8);
				debug("[ ns16550 speed set to %i bps ]\n", 115200 / d->divisor);
			} else {
				odata = (d->divisor & 0xff00) >> 8;
			}
			break;
		}

		/*  IER:  */
		if (writeflag == MEM_WRITE) {
			if (idata != 0)		/*  <-- to supress Linux' behaviour  */
				debug("[ ns16550 write to ier: 0x%02x ]\n", idata);
			d->irq_enable = idata;
/* cpu_interrupt_ack(cpu, d->irqnr); */
			dev_ns16550_tick(cpu, d);
		} else {
			odata = d->reg[relative_addr];
		}
		break;
	case com_iir:	/*  interrupt identification (r), fifo control (w)  */
		if (writeflag == MEM_WRITE) {
			debug("[ ns16550 write to fifo control ]\n");
			d->reg[relative_addr] = idata;
		} else {
			odata = d->reg[relative_addr];
			debug("[ ns16550 read from iir: 0x%02x ]\n", odata);
d->reg[com_iir] &= ~IIR_TXRDY;
dev_ns16550_tick(cpu, d);
		}
		break;
	case com_lsr:
		if (writeflag == MEM_WRITE) {
			debug("[ ns16550 write to lsr ]\n");
			d->reg[relative_addr] = idata;
		} else {
			odata = d->reg[relative_addr];
		}
		break;
	case com_msr:
		if (writeflag == MEM_WRITE) {
			debug("[ ns16550 write to msr ]\n");
			d->reg[relative_addr] = idata;
		} else {
			odata = d->reg[relative_addr];
		}
		break;
	case com_lctl:
		if (writeflag == MEM_WRITE) {
			d->reg[relative_addr] = idata;
			switch (idata & 0x7) {
			case 0:	d->databits = 5; d->stopbits = "1"; break;
			case 1:	d->databits = 6; d->stopbits = "1"; break;
			case 2:	d->databits = 7; d->stopbits = "1"; break;
			case 3:	d->databits = 8; d->stopbits = "1"; break;
			case 4:	d->databits = 5; d->stopbits = "1.5"; break;
			case 5:	d->databits = 6; d->stopbits = "2"; break;
			case 6:	d->databits = 7; d->stopbits = "2"; break;
			case 7:	d->databits = 8; d->stopbits = "2"; break;
			}
			switch ((idata & 0x38) / 0x8) {
			case 0:	d->parity = 'N'; break;		/*  none  */
			case 1:	d->parity = 'O'; break;		/*  odd  */
			case 2:	d->parity = '?'; break;
			case 3:	d->parity = 'E'; break;		/*  even  */
			case 4:	d->parity = '?'; break;
			case 5:	d->parity = 'Z'; break;		/*  zero  */
			case 6:	d->parity = '?'; break;
			case 7:	d->parity = 'o'; break;		/*  one  */
			}

			d->dlab = idata & 0x80? 1 : 0;

			debug("[ ns16550 write to lctl: 0x%02x (%s%ssetting mode %i%c%s) ]\n",
			    (int)idata,
			    d->dlab? "Divisor Latch access, " : "",
			    idata&0x40? "sending BREAK, " : "",
			    d->databits, d->parity, d->stopbits);
		} else {
			odata = d->reg[relative_addr];
			debug("[ ns16550 read from lctl: 0x%02x ]\n", odata);
		}
		break;
	case com_mcr:
		if (writeflag == MEM_WRITE) {
			d->reg[relative_addr] = idata;
			debug("[ ns16550 write to mcr: 0x%02x ]\n", idata);
		} else {
			odata = d->reg[relative_addr];
			debug("[ ns16550 read from mcr: 0x%02x ]\n", odata);
		}
		break;
	default:
		if (writeflag==MEM_READ) {
			debug("[ ns16550 read from reg %i ]\n", (int)relative_addr);
			odata = d->reg[relative_addr];
		} else {
			debug("[ ns16550 write to reg %i:", (int)relative_addr);
			for (i=0; i<len; i++)
				debug(" %02x", data[i]);
			debug(" ]\n");
			d->reg[relative_addr] = idata;
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_ns16550_init():
 */
void dev_ns16550_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr, int irq_nr, int addrmult)
{
	struct ns_data *d;

	d = malloc(sizeof(struct ns_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct ns_data));
	d->irqnr = irq_nr;
	d->addrmult = addrmult;

	d->dlab = 0;
	d->divisor  = 115200 / 9600;
	d->databits = 8;
	d->parity   = 'N';
	d->stopbits = "1";

	memory_device_register(mem, "ns16550", baseaddr, DEV_NS16550_LENGTH * addrmult, dev_ns16550_access, d);
	cpu_add_tickfunction(cpu, dev_ns16550_tick, d, 9);	/*  every 512:th cycle  */
}

