/*
 *  Copyright (C) 2004  Anders Gavare.  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright  
 *     notice, this list of conditions and the following disclaimer in the 
 *     documentation and/or other materials provided with the distribution.
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
 *  $Id: dev_vr41xx.c,v 1.2 2004-12-22 17:49:57 debug Exp $
 *  
 *  VR41xx misc functions.
 *
 *  TODO
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "misc.h"
#include "devices.h"


#define	DEV_VR41XX_TICKSHIFT		14


/*
 *  dev_vr41xx_tick():
 */
void dev_vr41xx_tick(struct cpu *cpu, void *extra)
{
	struct vr41xx_data *d = extra;

	/*  TODO: Timer interrupts.  */
}


/*
 *  dev_vr41xx_access():
 */
int dev_vr41xx_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	struct vr41xx_data *d = (struct vr41xx_data *) extra;
	uint64_t idata = 0, odata = 0;
	int regnr;

	idata = memory_readmax64(cpu, data, len);
	regnr = relative_addr / sizeof(uint64_t);

	switch (relative_addr) {
	case 0x14:
		/*
		 *  TODO?  Linux seems to read this. The lowest bits are
		 *  a divisor for PClock, bits 8 and up seem to be a
		 *  divisor for VTClock (relative to PClock?)...
		 */
		odata = 0x0000020c;
		break;
	case 0x80:
		if (writeflag == MEM_READ)
			odata = d->sysint1;
		else
			/*  TODO: clear-on-write-one?  */
			d->sysint1 = idata;
		break;
	case 0x8c:
		if (writeflag == MEM_READ)
			odata = d->msysint1;
		else
			d->msysint1 = idata;
		break;
	default:
		if (writeflag == MEM_WRITE)
			fatal("[ vr41xx: unimplemented write to address 0x%llx, data=0x%016llx ]\n",
			    (long long)relative_addr, (long long)idata);
		else
			fatal("[ vr41xx: unimplemented read from address 0x%llx ]\n",
			    (long long)relative_addr);
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_vr41xx_init():
 */
struct vr41xx_data *dev_vr41xx_init(struct cpu *cpu,
	struct memory *mem, uint64_t baseaddr)
{
	struct vr41xx_data *d = malloc(sizeof(struct vr41xx_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct vr41xx_data));

	memory_device_register(mem, "vr41xx", baseaddr,
	    DEV_VR41XX_LENGTH, dev_vr41xx_access, (void *)d, MEM_DEFAULT, NULL);

	cpu_add_tickfunction(cpu, dev_vr41xx_tick, d, DEV_VR41XX_TICKSHIFT);

	return d;
}

