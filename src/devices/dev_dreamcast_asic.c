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
 *  $Id: dev_dreamcast_asic.c,v 1.1 2006-10-21 05:49:06 debug Exp $
 *  
 *  Dreamcast ASIC.
 *
 *  NOTE/TODO: This is just a dummy module, so far.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"


#define debug fatal

struct dreamcast_asic_data {
	int		dummy;
};


DEVICE_ACCESS(dreamcast_asic)
{
	/*  struct dreamcast_asic_data *d =
	    (struct dreamcast_asic_data *) extra;  */
	uint64_t idata = 0, odata = 0;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);

	switch (relative_addr) {

case 0:
/*  TODO: Various bits describing events of the PVR, Maple, the
GD-ROM drive, etc. For now, let's just say that lots of random
event occur all the time. :)  */
odata = random();

	default:if (writeflag == MEM_READ) {
			fatal("[ dreamcast_asic: read from addr 0x%x ]\n",
			    (int)relative_addr);
		} else {
			fatal("[ dreamcast_asic: write to addr 0x%x: 0x%x ]\n",
			    (int)relative_addr, (int)idata);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


DEVINIT(dreamcast_asic)
{
	struct machine *machine = devinit->machine;
	struct dreamcast_asic_data *d =
	    malloc(sizeof(struct dreamcast_asic_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct dreamcast_asic_data));

	memory_device_register(machine->memory, devinit->name,
	    0x5f6900, 0x100, dev_dreamcast_asic_access, d, DM_DEFAULT, NULL);

	return 1;
}

