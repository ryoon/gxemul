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
 *  $Id: dev_mvme187.c,v 1.1 2007-05-12 10:32:05 debug Exp $
 *
 *  MVME187-specific devices and control registers.
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


#include "mvme_memcreg.h"

struct mvme187_data {
	struct memcreg	memcreg;
};


DEVICE_ACCESS(mvme187_memc)
{
	uint64_t idata = 0, odata = 0;
	struct mvme187_data *d = extra;
	int controller = 0;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);

	if (relative_addr & 0x100) {
		controller = 1;
		relative_addr &= ~0x100;
	}

	switch (relative_addr) {

	case 0x08:
		if (writeflag == MEM_READ) {
			odata = ((uint8_t*)&d->memcreg)[relative_addr];
		} else {
			fatal("mvme187_memc: Write to relative_addr %i not yet"
			    " implemented!\n");
			exit(1);
		}
		break;

	default:fatal("[ mvme187_memc: unimplemented %s offset 0x%x",
		    writeflag == MEM_WRITE? "write to" : "read from",
		    (int) relative_addr);
		if (writeflag == MEM_WRITE)
			fatal(": 0x%x", (int)idata);
		fatal(" ]\n");
		exit(1);
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


DEVINIT(mvme187)
{
	struct mvme187_data *d = malloc(sizeof(struct mvme187_data));
	int size_per_memc, r;

	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}

	memset(d, 0, sizeof(struct mvme187_data));

	d->memcreg.memc_chipid = MEMC_CHIPID;
	d->memcreg.memc_chiprev = 1;

	/*  Two memory controllers per MVME187 machine:  */
	size_per_memc = devinit->machine->physical_ram_in_mb / 2 * 1048576;
	for (r=0; ; r++) {
		if (MEMC_MEMCONF_RTOB(r) > size_per_memc) {
			r--;
			break;
		}
	}

	d->memcreg.memc_memconf = r;

	memory_device_register(devinit->machine->memory, devinit->name,
	    0xfff43000, 0x200, dev_mvme187_memc_access, (void *)d,
	    DM_DEFAULT, NULL);

	return 1;
}

