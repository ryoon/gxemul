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
 *  $Id: dev_sgi_mec.c,v 1.1 2004-12-16 04:37:21 debug Exp $
 *  
 *  SGI "mec" ethernet. Used in SGI-IP32.
 *
 *  TODO.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "misc.h"

#include "devices.h"
#include "memory.h"

#include "if_mecreg.h"


/*  #define debug fatal  */

#define	MEC_TICK_SHIFT		14

struct sgi_mec_data {
	uint64_t	reg[DEV_SGI_MEC_LENGTH / sizeof(uint64_t)];

	int		irq_nr;
};


/*
 *  mec_reset():
 */
static void mec_reset(struct sgi_mec_data *d)
{
	memset(d->reg, 0, sizeof(d->reg));
}


/*
 *  mec_control_write():
 */
static void mec_control_write(struct cpu *cpu, struct sgi_mec_data *d,
	uint64_t x)
{
	if (x & MEC_MAC_CORE_RESET) {
		debug("[ sgi_mec: CORE RESET ]\n");
		mec_reset(d);
	}
}


/*
 *  dev_sgi_mec_tick():
 */
void dev_sgi_mec_tick(struct cpu *cpu, void *extra)
{
	/*  struct sgi_mec_data *d = (struct sgi_mec_data *) extra;  */

	/*  TODO  */
}

/*
 *  dev_sgi_mec_access():
 */
int dev_sgi_mec_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	struct sgi_mec_data *d = (struct sgi_mec_data *) extra;
	uint64_t idata = 0, odata = 0;
	int regnr;

	idata = memory_readmax64(cpu, data, len);
	regnr = relative_addr / sizeof(uint64_t);

	/*  Treat all registers as read/write, by default.  */
	if (writeflag == MEM_WRITE)
		d->reg[regnr] = idata;
	else
		odata = d->reg[regnr];

	switch (relative_addr) {
	case MEC_MAC_CONTROL:	/*  0x00  */
		if (writeflag)
			mec_control_write(cpu, d, idata);
		break;
	case MEC_DMA_CONTROL:	/*  0x10  */
		if (writeflag)
			debug("[ sgi_mec: write to DMA CONTROL: 0x%016llx ]\n", (long long)idata);
		break;
	case MEC_STATION:	/*  0xa0  */
		if (writeflag)
			debug("[ sgi_mec: setting the MAC address to "
			    "%02x:%02x:%02x:%02x:%02x:%02x ]\n",
			    (idata >> 40) & 255, (idata >> 32) & 255,
			    (idata >> 24) & 255, (idata >> 16) & 255,
			    (idata >>  8) & 255, (idata >>  0) & 255);
		break;
	case MEC_STATION_ALT:	/*  0xa8  */
		if (writeflag)
			debug("[ sgi_mec: setting the ALTERNATIVE MAC address to "
			    "%02x:%02x:%02x:%02x:%02x:%02x ]\n",
			    (idata >> 40) & 255, (idata >> 32) & 255,
			    (idata >> 24) & 255, (idata >> 16) & 255,
			    (idata >>  8) & 255, (idata >>  0) & 255);
		break;
	default:
		if (writeflag == MEM_WRITE)
			fatal("[ sgi_mec: unimplemented write to address 0x%llx, data=0x%016llx ]\n",
			    (long long)relative_addr, (long long)idata);
		else
			fatal("[ sgi_mec: unimplemented read from address 0x%llx ]\n",
			    (long long)relative_addr);
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_sgi_mec_init():
 */
void dev_sgi_mec_init(struct cpu *cpu, struct memory *mem,
	uint64_t baseaddr, int irq_nr)
{
	struct sgi_mec_data *d = malloc(sizeof(struct sgi_mec_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct sgi_mec_data));
	d->irq_nr = irq_nr;

	mec_reset(d);

	memory_device_register(mem, "sgi_mec", baseaddr,
	    DEV_SGI_MEC_LENGTH, dev_sgi_mec_access, (void *)d,
	    MEM_DEFAULT, NULL);

	cpu_add_tickfunction(cpu, dev_sgi_mec_tick, d, MEC_TICK_SHIFT);
}

