/*
 *  Copyright (C) 2004-2005  Anders Gavare.  All rights reserved.
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
 *  $Id: dev_vr41xx.c,v 1.9 2005-01-19 07:46:50 debug Exp $
 *  
 *  VR41xx (actually, VR4122 and VR4131) misc functions.
 *
 *  TODO
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "misc.h"
#include "devices.h"

#include "bcureg.h"


#define	DEV_VR41XX_TICKSHIFT		14

/*  #define debug fatal  */


/*
 *  dev_vr41xx_tick():
 */
void dev_vr41xx_tick(struct cpu *cpu, void *extra)
{
/*	struct vr41xx_data *d = extra;  */

	{
		static int x = 0;
		/*  TODO:  */
		x++;
		if (x > 100 && x&1)
			cpu_interrupt(cpu, 8 + 3);
	}
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
	int revision = 0;

	idata = memory_readmax64(cpu, data, len);
	regnr = relative_addr / sizeof(uint64_t);

	switch (relative_addr) {
	/*  BCU:  0x00 .. 0x1c  */
	case BCUREVID_REG_W:	/*  0x010  */
	case BCU81REVID_REG_W:	/*  0x014  */
		/*
		 *  TODO?  Linux seems to read 0x14. The lowest bits are
		 *  a divisor for PClock, bits 8 and up seem to be a
		 *  divisor for VTClock (relative to PClock?)...
		 */
		switch (d->cpumodel) {
		case 4131:	revision = BCUREVID_RID_4131; break;
		case 4122:	revision = BCUREVID_RID_4122; break;
		case 4121:	revision = BCUREVID_RID_4121; break;
		case 4111:	revision = BCUREVID_RID_4111; break;
		case 4102:	revision = BCUREVID_RID_4102; break;
		case 4101:	revision = BCUREVID_RID_4101; break;
		case 4181:	revision = BCUREVID_RID_4181; break;
		}
		odata = (revision << BCUREVID_RIDSHFT) | 0x020c;
		break;
	case BCU81CLKSPEED_REG_W:	/*  0x018  */
		/*
		 *  TODO: Implement this for ALL cpu types:
		 */
		odata = BCUCLKSPEED_DIVT4 << BCUCLKSPEED_DIVTSHFT;
		break;

	/*  DMAAU:  0x20 .. 0x3c  */

	/*  DCU:  0x40 .. 0x5c  */

	/*  CMU:  0x60 .. 0x7c  */

	/*  ICU:  0x80 .. 0xbc  */
	case 0x80:	/*  Level 1 system interrupt reg 1...  */
		if (writeflag == MEM_READ)
			odata = d->sysint1;
		else {
			/*  TODO: clear-on-write-one?  */
			d->sysint1 &= ~idata;
			d->sysint1 &= 0xffff;
		}
		break;
	case 0x8c:
		if (writeflag == MEM_READ)
			odata = d->msysint1;
		else
			d->msysint1 = idata;
		break;
	case 0xa0:	/*  Level 1 system interrupt reg 2...  */
		if (writeflag == MEM_READ)
			odata = d->sysint2;
		else {
			/*  TODO: clear-on-write-one?  */
			d->sysint2 &= ~idata;
			d->sysint2 &= 0xffff;
		}
		break;
	case 0xa6:
		if (writeflag == MEM_READ)
			odata = d->msysint2;
		else
			d->msysint2 = idata;
		break;

	/*  PMU:  0xc0 .. 0xfc  */
	/*  RTC:  0x100 .. ?  */

	case 0x13e:
		/*  RTC interrupt register...  */
		/*  Ack. timer interrupts?  */
		cpu_interrupt_ack(cpu, 8 + 3);
		break;

	/*  0x180: possibly a "KIU", see NetBSD sources for more info  */

	default:
		if (writeflag == MEM_WRITE)
			debug("[ vr41xx: unimplemented write to address 0x%llx, data=0x%016llx ]\n",
			    (long long)relative_addr, (long long)idata);
		else
			debug("[ vr41xx: unimplemented read from address 0x%llx ]\n",
			    (long long)relative_addr);
	}

	/*  Recalculate interrupt assertions:  */
	cpu_interrupt_ack(cpu, 8 + 32);

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_vr41xx_init():
 */
struct vr41xx_data *dev_vr41xx_init(struct cpu *cpu,
	struct memory *mem, int cpumodel)
{
	uint64_t baseaddr = 0;
	struct vr41xx_data *d = malloc(sizeof(struct vr41xx_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct vr41xx_data));

	d->cpumodel = cpumodel;

	switch (cpumodel) {
	case 4101:
	case 4102:
	case 4111:
	case 4121:
		baseaddr = 0xb000000;
		break;
	case 4181:
		baseaddr = 0xa000000;
		break;
	case 4122:
	case 4131:
		baseaddr = 0xf000000;
		break;
	default:
		printf("Unimplemented VR cpu model\n");
		exit(1);
	}

	memory_device_register(mem, "vr41xx", baseaddr,
	    DEV_VR41XX_LENGTH, dev_vr41xx_access, (void *)d, MEM_DEFAULT, NULL);

	cpu_add_tickfunction(cpu, dev_vr41xx_tick, d, DEV_VR41XX_TICKSHIFT);

	return d;
}

