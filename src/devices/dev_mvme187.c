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
 *  $Id: dev_mvme187.c,v 1.2 2007-05-15 12:35:14 debug Exp $
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


#include "mvme187.h"
#include "mvme88k_vme.h"
#include "mvme_memcreg.h"
#include "mvme_pcctworeg.h"


struct mvme187_data {
	struct memcreg		memcreg;

	uint8_t			pcctwo_reg[PCC2_SIZE];
};


DEVICE_ACCESS(pcc2)
{
	uint64_t idata = 0, odata = 0;
	struct mvme187_data *d = extra;

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

	default:fatal("[ pcc2: unimplemented %s offset 0x%x",
		    writeflag == MEM_WRITE? "write to" : "read from",
		    (int) relative_addr);
		if (writeflag == MEM_WRITE)
			fatal(": 0x%x", (int)idata);
		fatal(" ]\n");
//		exit(1);
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


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

	case 0x08:	/*  memconf  */
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
	char tmpstr[300];
	int size_per_memc, r;

	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}

	memset(d, 0, sizeof(struct mvme187_data));


	/*
	 *  Two memory controllers per MVME187 machine:
	 */

	size_per_memc = devinit->machine->physical_ram_in_mb / 2 * 1048576;
	for (r=0; ; r++) {
		if (MEMC_MEMCONF_RTOB(r) > size_per_memc) {
			r--;
			break;
		}
	}

	d->memcreg.memc_chipid = MEMC_CHIPID;
	d->memcreg.memc_chiprev = 1;
	d->memcreg.memc_memconf = r;

	memory_device_register(devinit->machine->memory, devinit->name,
	    MVME187_MEM_CTLR, 0x200, dev_mvme187_memc_access, (void *)d,
	    DM_DEFAULT, NULL);


	/*  PCC2 (Interrupt/bus controller), 0xFFF?????:  */
	d->pcctwo_reg[PCCTWO_CHIPID] = PCC2_ID;
	memory_device_register(devinit->machine->memory, "pcc2",
	    PCC2_BASE, PCC2_SIZE, dev_pcc2_access, (void *)d,
	    DM_DEFAULT, NULL);

	/*  VME2 bus at 0xfff40000:  */
	snprintf(tmpstr, sizeof(tmpstr), "vme addr=0x%x", VME2_BASE);
	device_add(devinit->machine, tmpstr);

	/*  Cirrus Logic serial console at 0xfff45000:  */
	snprintf(tmpstr, sizeof(tmpstr), "clmpcc addr=0x%x", 0xfff45000);
	device_add(devinit->machine, tmpstr);

	/*  MK48T08 clock/nvram at 0xfffc0000:  */
	snprintf(tmpstr, sizeof(tmpstr), "mk48txx addr=0x%x", 0xfffc0000);
	device_add(devinit->machine, tmpstr);

	/*  Instruction and data CMMUs:  */
	snprintf(tmpstr, sizeof(tmpstr),
	    "m8820x addr=0x%x", MVME187_SBC_CMMU_I);
	device_add(devinit->machine, tmpstr);
	snprintf(tmpstr, sizeof(tmpstr),
	    "m8820x addr=0x%x", MVME187_SBC_CMMU_D);
	device_add(devinit->machine, tmpstr);

	return 1;
}

