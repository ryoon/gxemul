/*
 *  Copyright (C) 2003-2007  Anders Gavare.  All rights reserved.
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
 *  $Id: dev_fdc.c,v 1.19 2006-12-30 13:30:58 debug Exp $
 *  
 *  PC-style floppy controller.
 *
 *  TODO!  (This is just a dummy skeleton right now.)
 *
 *  TODO 2: Make it work nicely with both ARC and PC emulation.
 *
 *  See http://members.tripod.com/~oldboard/assembly/765.html for a
 *  quick overview.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "device.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"


#define	DEV_FDC_LENGTH		6	/*  TODO 8, but collision with wdc  */


struct fdc_data {
	unsigned char	reg[DEV_FDC_LENGTH];
	int		irqnr;
};


/*
 *  dev_fdc_access():
 */
DEVICE_ACCESS(fdc)
{
	uint64_t idata = 0, odata = 0;
	size_t i;
	struct fdc_data *d = extra;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);

	switch (relative_addr) {
	case 0x04:
		break;
	default:if (writeflag==MEM_READ) {
			fatal("[ fdc: read from reg %i ]\n",
			    (int)relative_addr);
			odata = d->reg[relative_addr];
		} else {
			fatal("[ fdc: write to reg %i:", (int)relative_addr);
			for (i=0; i<len; i++)
				fatal(" %02x", data[i]);
			fatal(" ]\n");
			d->reg[relative_addr] = idata;
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


DEVINIT(fdc)
{
	struct fdc_data *d;

	d = malloc(sizeof(struct fdc_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct fdc_data));
	d->irqnr = devinit->irq_nr;

	memory_device_register(devinit->machine->memory, devinit->name,
	    devinit->addr, DEV_FDC_LENGTH, dev_fdc_access, d,
	    DM_DEFAULT, NULL);

	return 1;
}

