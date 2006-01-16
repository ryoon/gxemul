/*
 *  Copyright (C) 2006  Anders Gavare.  All rights reserved.
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
 *  $Id: dev_nvram.c,v 1.2 2006-01-16 01:45:28 debug Exp $
 *
 *  NVRAM reached through ISA port 0x74-0x77.
 *  (See dev_pccmos.c for the traditional PC-style CMOS/RTC device.)
 *
 *  TODO: Perhaps implement flags for which parts of ram that are actually
 *        implemented, and warn when accesses occur to other parts?
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


#define	DEV_NVRAM_LENGTH		4

struct nvram_data {
	uint16_t	reg_select;
	unsigned char	ram[65536];
};


/*
 *  dev_nvram_access():
 */
DEVICE_ACCESS(nvram)
{
	struct nvram_data *d = (struct nvram_data *) extra;
	uint64_t idata = 0, odata = 0;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);

	switch (relative_addr) {

	case 0:	if (writeflag == MEM_WRITE) {
			d->reg_select &= ~0xff;
			d->reg_select |= (idata & 0xff);
		} else {
			odata = d->reg_select & 0xff;
		}
		break;

	case 1:	if (writeflag == MEM_WRITE) {
			d->reg_select &= ~0xff00;
			d->reg_select |= ((idata & 0xff) << 8);
		} else {
			odata = (d->reg_select >> 8) & 0xff;
		}
		break;

	case 3:	if (writeflag == MEM_WRITE) {
			d->ram[d->reg_select] = idata;
		} else {
			odata = d->ram[d->reg_select];
		}
		break;

	default:fatal("[ nvram: unimplemented access to offset %i ]\n",
		    (int)relative_addr);
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  devinit_nvram():
 */
int devinit_nvram(struct devinit *devinit)
{
	struct nvram_data *d = malloc(sizeof(struct nvram_data));

	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct nvram_data));

	if (strcmp(devinit->name2, "mvme1600") == 0) {
		/*
		 *  MVME1600 boards have a board ID at 0x1ef8 - 0x1ff7,
		 *  with the following layout:  (see NetBSD/mvmeppc for details)
		 *
		 *  0x1ef8   4 bytes	version
		 *  0x1efc  12 bytes	serial
		 *  0x1f08  16 bytes	id
		 *  0x1f18  16 bytes	pwa
		 *  0x1f28   4 bytes	reserved
		 *  0x1f2c   6 bytes	ethernet address
		 *  0x1f32   2 bytes	reserved
		 *  0x1f34   2 bytes	scsi id
		 *  0x1f36   3 bytes	speed_mpu
		 *  0x1f39   3 bytes	speed_bus
		 *  0x1f3c 187 bytes	reserved
		 *  0x1ff7   1 byte	cksum
		 *
		 *  Example of values from a real machine (according to Google):
		 *  Model: MVME1603-051, Serial: 2451669, PWA: 01-W3066F01E
		 */

		/*  serial:  */
		memcpy(&d->ram[0x1efc], "1234", 5);  /*  includes nul  */

		/*  id:  */
		memcpy(&d->ram[0x1f08], "MVME1600", 9);  /*  includes nul  */

		/*  pwa:  */
		memcpy(&d->ram[0x1f18], "0", 2);  /*  includes nul  */

		/*  speed_mpu:  */
		memcpy(&d->ram[0x1f36], "33", 3);  /*  includes nul  */

		/*  speed_bus:  */
		memcpy(&d->ram[0x1f39], "33", 3);  /*  includes nul  */
	} else {
		fatal("Unimplemented NVRAM type '%s'\n", devinit->name2);
		exit(1);
	}

	memory_device_register(devinit->machine->memory, devinit->name,
	    devinit->addr, DEV_NVRAM_LENGTH, dev_nvram_access, (void *)d,
	    DM_DEFAULT, NULL);

	return 1;
}

