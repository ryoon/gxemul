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
 *  $Id: dev_pcic.c,v 1.2 2005-03-13 10:32:13 debug Exp $
 *
 *  Intel 82365SL PC Card Interface Controller (called "pcic" by NetBSD).
 *
 *  TODO: Lots of stuff.
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

#include "i82365reg.h"


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
	int controller_nr, socket_nr;

	idata = memory_readmax64(cpu, data, len);

	controller_nr = d->regnr & 0x80? 1 : 0;
	socket_nr = d->regnr & 0x40? 1 : 0;

	switch (relative_addr) {
	case 0:	/*  Register select:  */
		if (writeflag == MEM_WRITE)
			d->regnr = idata;
		else
			odata = d->regnr;
		break;
	case 1:	/*  Register access:  */
		switch (d->regnr & 0x3f) {
		case PCIC_IDENT:
			/*  This causes sockets A and B to be present on
			    controller 0, and only socket A on controller 1.  */
			if (controller_nr == 1 && socket_nr == 1)
				odata = 0;
			else
				odata = PCIC_IDENT_IFTYPE_MEM_AND_IO
				    | PCIC_IDENT_REV_I82365SLR1;
			break;
#if 0
/*  TODO: make NetBSD accept card present  */
		case PCIC_IF_STATUS:
			odata = PCIC_IF_STATUS_CARDDETECT_PRESENT;
			break;
#endif
		default:
			if (writeflag == MEM_WRITE) {
				fatal("[ pcic: unimplemented write to "
				    "controller %i socket %c, regnr %i: "
				    "data=0x%02x ]\n", controller_nr,
				    socket_nr? 'B' : 'A',
				    d->regnr & 0x3f, (int)idata);
			} else {
				fatal("[ pcic: unimplemented read from "
				    "controller %i socket %c, regnr %i ]\n",
				    controller_nr, socket_nr? 'B' : 'A',
				    d->regnr & 0x3f);
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

