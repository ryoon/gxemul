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
 *  $Id: dev_pckbc.c,v 1.4 2004-01-06 23:19:52 debug Exp $
 *  
 *  Standard 8042 PC keyboard controller.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "misc.h"
#include "console.h"
#include "devices.h"


#define	MAX_8042_QUEUELEN	256

struct pckbc_data {
	int	reg[DEV_PCKBC_LENGTH];
	int	irqnr;

	unsigned	key_queue[MAX_8042_QUEUELEN];
	int		head, tail;
};


void pckbc_add_key(struct pckbc_data *d, int code)
{
	/*  Add at the head, read at the tail  */
	d->head = (d->head+1) % MAX_8042_QUEUELEN;
	if (d->head == d->tail)
		fatal("pckbc: queue overrun!\n");

	d->key_queue[d->head] = code;
}


int pckbc_get_key(struct pckbc_data *d)
{
	if (d->head == d->tail)
		fatal("pckbc: queue empty!\n");

	d->tail = (d->tail+1) % MAX_8042_QUEUELEN;
	return d->key_queue[d->tail];
}


/*
 *  dev_pckbc_access():
 *
 *  Returns 1 if ok, 0 on error.
 */
int dev_pckbc_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *extra)
{
	uint64_t idata = 0, odata = 0;
	int i, code;
	struct pckbc_data *d = extra;

	idata = memory_readmax64(cpu, data, len);

	/*  TODO:  this is almost 100% dummy  */

	switch (relative_addr) {
	case 0:		/*  data  */
		if (writeflag==MEM_READ) {
			odata = 0;
			if (d->head != d->tail)
				odata = pckbc_get_key(d);
			debug("[ pckbc: read from DATA: 0x%02x ]\n", odata);
		} else {
			debug("[ pckbc: write to DATA:");
			for (i=0; i<len; i++)
				debug(" %02x", data[i]);
			debug(" ]\n");
			d->reg[relative_addr] = idata;
		}
		break;
	case 1:		/*  control  */
		if (writeflag==MEM_READ) {
			odata = 0;

			/*  "Data in buffer" bit  */
			if (d->head != d->tail)
				odata |= 1;

			debug("[ pckbc: read from CTL: 0x%02x ]\n", odata);
		} else {
			debug("[ pckbc: write to CTL:");
			for (i=0; i<len; i++)
				debug(" %02x", data[i]);
			debug(" ]\n");
			d->reg[relative_addr] = idata;

			code = 0xfa; /*  KBR_ACK;  */

			/*  Self-test:  */
			if (idata == 0xaa)
				code = 0x55;

			pckbc_add_key(d, code);
		}
		break;
	default:
		if (writeflag==MEM_READ) {
			debug("[ pckbc: read from unimplemented reg %i ]\n", (int)relative_addr);
			odata = d->reg[relative_addr];
		} else {
			debug("[ pckbc: write to unimplemented reg %i:", (int)relative_addr);
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
 *  dev_pckbc_init():
 */
void dev_pckbc_init(struct memory *mem, uint64_t baseaddr, int irq_nr)
{
	struct pckbc_data *d;

	d = malloc(sizeof(struct pckbc_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct pckbc_data));
	d->irqnr = irq_nr;

	pckbc_add_key(d, 0x00);

	memory_device_register(mem, "pckbc", baseaddr, DEV_PCKBC_LENGTH, dev_pckbc_access, d);
}

