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
 *  $Id: dev_pvr.c,v 1.3 2006-10-17 10:53:06 debug Exp $
 *  
 *  PowerVR CLX2 (Graphics controller used in the Dreamcast)
 *
 *  TODO: Almost everything
 *	x)  Change resolution during runtime? (PAL/NTSC/???)
 *	x)  Change framebuffer layout in memory during runtime
 *		(bits per pixel, location, etc)
 *	x)  3D "Tile Accelerator" engine
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

#include "dreamcast_pvr.h"


/*  #define debug fatal  */

struct pvr_data {
	struct vfb_data		*fb;
};


DEVICE_ACCESS(pvr)
{
	/*  struct pvr_data *d = (struct pvr_data *) extra;  */
	uint64_t idata = 0, odata = 0;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);

	switch (relative_addr) {

	case PVRREG_SYNC_STAT:
		/*  Ugly hack, but it works:  */
		odata = random();
		break;

	default:if (writeflag == MEM_READ) {
			fatal("[ pvr: read from addr 0x%x ]\n",
			    (int)relative_addr);
		} else {
			fatal("[ pvr: write to addr 0x%x: 0x%x ]\n",
			    (int)relative_addr, (int)idata);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


DEVINIT(pvr)
{
	struct machine *machine = devinit->machine;
	struct pvr_data *d = malloc(sizeof(struct pvr_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct pvr_data));

	memory_device_register(machine->memory, devinit->name,
	    PVRREG_REGSTART, 0x2000, dev_pvr_access, d, DM_DEFAULT, NULL);

#if 1
	/*  640x480 16-bit framebuffer:  */
	d->fb = dev_fb_init(machine, machine->memory, PVRREG_FBSTART,
	    VFB_HPC, 640,480, 640,480, 16, "Dreamcast PVR");
#else
	/*  IP.BIN experiment.  */
	d->fb = dev_fb_init(machine, machine->memory, PVRREG_FBSTART + 0x200000,
	    VFB_GENERIC, 640,480, 640,480, 32, "Dreamcast PVR");
#endif

	return 1;
}

