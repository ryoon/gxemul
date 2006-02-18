/*
 *  Copyright (C) 2005-2006  Anders Gavare.  All rights reserved.
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
 *  $Id: dev_algor.c,v 1.1 2006-02-18 17:55:25 debug Exp $
 *
 *  Algor misc. stuff.
 *
 *  TODO: This is hardcoded for P5064 right now. Generalize it to P40xx etc.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"

#include "algor_p5064reg.h"


DEVICE_ACCESS(algor)
{
	struct algor_data *d = extra;
	uint64_t idata = 0, odata = 0;
	char *n = NULL;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);

	relative_addr += d->base_addr;

	switch (relative_addr) {

	case P5064_LOCINT:
		{
			static int x = 0;
			x ++;
			odata = LOCINT_PCIBR;
			if ((x & 0xffff) == 0)
				odata |= LOCINT_RTC;
		}
		break;

	default:if (writeflag == MEM_READ) {
			fatal("[ algor: read from 0x%x ]\n",
			    (int)relative_addr);
		} else {
			fatal("[ algor: write to 0x%x: 0x%llx ]\n",
			    (int)relative_addr, (long long)idata);
		}
	}

	if (n != NULL) {
		if (writeflag == MEM_READ) {
			debug("[ algor: read from %s ]\n", n);
		} else {
			debug("[ algor: write to %s: 0x%llx ]\n",
			    n, (long long)idata);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


DEVINIT(algor)
{
	struct algor_data *d = malloc(sizeof(struct algor_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct algor_data));

	if (devinit->addr != 0x1ff00000) {
		fatal("The Algor base address should be 0x1ff00000.\n");
		exit(1);
	}

	memory_device_register(devinit->machine->memory, devinit->name,
	    devinit->addr, 0x100000, dev_algor_access, d, DM_DEFAULT, NULL);

	d->base_addr = devinit->addr;

	devinit->return_ptr = d;

	return 1;
}

