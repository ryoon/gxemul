/*
 *  Copyright (C) 2007  Anders Gavare.  All rights reserved.
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
 *  $Id: dev_pcc2.c,v 1.4 2007-06-15 19:57:33 debug Exp $
 *
 *  COMMENT: PCC2 bus (used in MVME machine)
 *
 *  TODO
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "device.h"
#include "emul.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"


#include "mvme_pcctworeg.h"


/*  #define	debug fatal  */

struct pcc2_data {
	uint8_t			pcctwo_reg[PCC2_SIZE];
};


DEVICE_ACCESS(pcc2)
{
	uint64_t idata = 0, odata = 0;
	struct pcc2_data *d = extra;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);

	if (writeflag == MEM_READ)
		odata = d->pcctwo_reg[relative_addr];

	switch (relative_addr) {

	case PCCTWO_CHIPID:
	case PCCTWO_CHIPREV:
		if (writeflag == MEM_WRITE) {
			fatal("TODO: write to PCCTWO_CHIPID or CHIPREV?\n");
			exit(1);
		}
		break;

	case PCCTWO_GENCTL:
	case PCCTWO_VECBASE:
		if (writeflag == MEM_WRITE)
			d->pcctwo_reg[relative_addr] = idata;
		break;

	case PCCTWO_MASK:
		if (writeflag == MEM_WRITE) {
			d->pcctwo_reg[relative_addr] = idata;
			/*  TODO: Re-Assert interrupts!  */
		}
		break;

	default:debug("[ pcc2: unimplemented %s offset 0x%x",
		    writeflag == MEM_WRITE? "write to" : "read from",
		    (int) relative_addr);
		if (writeflag == MEM_WRITE)
			debug(": 0x%x", (int)idata);
		debug(" ]\n");
		/*  exit(1);  */
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


DEVINIT(pcc2)
{
	struct pcc2_data *d;

	CHECK_ALLOCATION(d = malloc(sizeof(struct pcc2_data)));
	memset(d, 0, sizeof(struct pcc2_data));

	d->pcctwo_reg[PCCTWO_CHIPID] = PCC2_ID;

	memory_device_register(devinit->machine->memory, "pcc2",
	    devinit->addr, PCC2_SIZE, dev_pcc2_access, (void *)d,
	    DM_DEFAULT, NULL);

	return 1;
}

