/*
 *  Copyright (C) 2003 by Anders Gavare.  All rights reserved.
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
 */

/*
 *  dev_ns16550.c  --  ns16550 serial controller
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "misc.h"
#include "comreg.h"


struct ns_data {
	int	reg[8];
	int	irqnr;
};


/*
 *  dev_ns16550_access():
 *
 *  Returns 1 if ok, 0 on error.
 */
int dev_ns16550_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *extra)
{
	int i;
	int idata = 0, odata=0, odata_set=0;
	struct ns_data *d = extra;

	/*  Switch byte order for incoming data, if neccessary:  */
	if (cpu->byte_order == EMUL_BIG_ENDIAN)
		for (i=0; i<len; i++) {
			idata <<= 8;
			idata |= data[i];
		}
	else
		for (i=len-1; i>=0; i--) {
			idata <<= 8;
			idata |= data[i];
		}

	/*  Always ready to transmit:  */
	d->reg[com_lsr] |= LSR_TXRDY;

	d->reg[com_lsr] &= ~LSR_RXRDY;
	if (console_charavail())
		d->reg[com_lsr] |= LSR_RXRDY;

	switch (relative_addr) {
	case com_data:	/*  com_data or com_dlbl  */
		if (writeflag == MEM_WRITE) {
			console_putchar(idata);
			d->reg[com_lsr] |= LSR_TSRE;
			return 1;
		} else {
			odata = console_readchar();
			odata_set = 1;
		}
		break;
	case com_lsr:
		if (writeflag == MEM_WRITE) {
			debug("[ ns16550 write to lsr ]\n");
		} else {
			odata = d->reg[relative_addr];
			odata_set = 1;
		}
		break;
	default:
		if (writeflag==MEM_READ) {
			debug("[ ns16550 read from reg %i ]\n", (int)relative_addr);
			odata = d->reg[relative_addr];
			odata_set = 1;
		} else {
			debug("[ ns16550 write to reg %i:", (int)relative_addr);
			for (i=0; i<len; i++)
				debug(" %02x", data[i]);
			debug(" ]\n");
			d->reg[relative_addr] = idata;
			return 1;
		}
	}

	if (odata_set) {
		if (cpu->byte_order == EMUL_LITTLE_ENDIAN) {
			for (i=0; i<len; i++)
				data[i] = (odata >> (i*8)) & 255;
		} else {
			for (i=0; i<len; i++)
				data[len - 1 - i] = (odata >> (i*8)) & 255;
		}
		return 1;
	}

	return 0;
}


/*
 *  dev_ns16550_init():
 */
void dev_ns16550_init(struct memory *mem, uint64_t baseaddr, int irq_nr)
{
	struct ns_data *d;

	d = malloc(sizeof(struct ns_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct ns_data));
	d->irqnr = irq_nr;

	memory_device_register(mem, "ns16550", baseaddr, DEV_NS16550_LENGTH, dev_ns16550_access, d);
}

