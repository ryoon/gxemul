/*
 *  Copyright (C) 2005  Anders Gavare.  All rights reserved.
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
 *  $Id: dev_pcic.c,v 1.1 2005-03-12 23:38:57 debug Exp $
 *  
 *  PCMCIA controller. (Called "pcic" by NetBSD.)
 *
 *  TODO
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
#include "net.h"


#define	DEV_PCIC_LENGTH		2

struct pcic_data {
	int		irq_nr;
	int		regnr;
};


/*
 *  dev_pcic_access():
 */
int dev_pcic_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	struct pcic_data *d = (struct pcic_data *) extra;
	uint64_t idata = 0, odata = 0;

	idata = memory_readmax64(cpu, data, len);

	switch (relative_addr) {
	case 0:	/*  Register select:  */
		if (writeflag == MEM_WRITE)
			d->regnr = idata;
		else
			odata = d->regnr;
		break;
	case 1:	/*  Register access:  */
		switch (d->regnr) {
		case 0:		/*  Controller 0 socket A.  */
			/*  0x82 = "Intel 82365SL Revision 0",
			    0x83 = Revision 1.  0x00 = not present.  */
			odata = 0x83;
			break;
		case 0x40:	/*  Controller 0 socket B.  */
			odata = 0x83;
			break;
		case 0x80:	/*  Controller 1 socket A.  */
			odata = 0x83;
			break;
		case 0xc0:	/*  Controller 1 socket B.  */
			odata = 0x00;
			break;
		default:
			if (writeflag == MEM_WRITE) {
				fatal("[ pcic: unimplemented write to "
				    " (regnr %i), data=0x%02x ]\n",
				    d->regnr, (int)idata);
			} else {
				fatal("[ pcic: unimplemented read from "
				    "(regnr %i) ]\n", d->regnr);
			}
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  devinit_pcic():
 */
int devinit_pcic(struct devinit *devinit)
{
	struct pcic_data *d = malloc(sizeof(struct pcic_data));

	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct pcic_data));
	d->irq_nr = devinit->irq_nr;

	memory_device_register(devinit->machine->memory, devinit->name,
	    devinit->addr, DEV_PCIC_LENGTH,
	    dev_pcic_access, (void *)d, MEM_DEFAULT, NULL);

	/*  TODO: find out a good way to specify the address, and the IRQ!
	    dev_wdc_init(devinit->machine, devinit->machine->memory,
	    devinit->addr + 0x20, 0, 0);
	*/

	return 1;
}

