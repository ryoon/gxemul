/*
 *  Copyright (C) 2003-2004 by Anders Gavare.  All rights reserved.
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
 *  $Id: dev_crime.c,v 1.9 2004-01-10 05:40:36 debug Exp $
 *  
 *  SGI "crime".
 *
 *  TODO:  This is hardcoded for bigendian, but since it will probably only
 *         be used for SGI emulation, and no little-endian machines, this is
 *         not that much of a problem.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "misc.h"
#include "console.h"
#include "devices.h"

#include "crimereg.h"

struct crime_data {
	unsigned char	reg[DEV_CRIME_LENGTH];
};


#define	CRIME_TICKSHIFT		9


/*
 *  dev_crime_tick():
 *
 *  TODO:  This function simply updates CRIME_TIME by 1 for each tick.
 *  This is probably not correct.
 */
void dev_crime_tick(struct cpu *cpu, void *extra)
{
	int i, j;
	struct crime_data *d = extra;

	/*  Increase CRIME_TIME by 1<<CRIME_TICKSHIFT steps for each tick:  */
	for (i=0; i<(1<<CRIME_TICKSHIFT); i++) {
		j = 7;
		while (j >= 0) {
			if ((++ d->reg[CRIME_TIME + j]) == 0)
				j --;
			else
				break;
		}
	}
}


/*
 *  dev_crime_access():
 *
 *  Returns 1 if ok, 0 on error.
 */
int dev_crime_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *extra)
{
	int i;
	struct crime_data *d = extra;

	if (writeflag == MEM_WRITE)
		memcpy(&d->reg[relative_addr], data, len);
	else
		memcpy(data, &d->reg[relative_addr], len);

	switch (relative_addr) {
	case 0x18:
	case 0x1c:
	case 0x34:
	case CRIME_TIME:
	case CRIME_TIME+4:
		/*  don't dump debug info for these  */
		break;
	default:
		if (writeflag==MEM_READ) {
			debug("[ crime: read from 0x%x, len=%i ]\n", (int)relative_addr, len);
		} else {
			debug("[ crime: write to 0x%x:", (int)relative_addr);
			for (i=0; i<len; i++)
				debug(" %02x", data[i]);
			debug(" (len=%i) ]\n", len);
		}
	}

	return 1;
}


/*
 *  dev_crime_init():
 */
void dev_crime_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr)
{
	struct crime_data *d;

	d = malloc(sizeof(struct crime_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct crime_data));

	/*  Set crime version/revision:  */
	d->reg[4] = 0x00; d->reg[5] = 0x00; d->reg[6] = 0x00; d->reg[7] = 0x11;

	/*  Amount of memory.  Bit 8 of bank control set ==> 128MB instead of 32MB per bank (?)  */
	/*  When the bank control registers contain the same value as the previous one, that
		bank is not valid. (?)  */
	d->reg[CRM_MEM_BANK_CTRL0 + 6] |= 1;

	d->reg[CRM_MEM_BANK_CTRL1 + 6] |= 1;
	d->reg[CRM_MEM_BANK_CTRL2 + 6] |= 1;
	d->reg[CRM_MEM_BANK_CTRL3 + 6] |= 1;
	d->reg[CRM_MEM_BANK_CTRL4 + 6] |= 1;
	d->reg[CRM_MEM_BANK_CTRL5 + 6] |= 1;
	d->reg[CRM_MEM_BANK_CTRL6 + 6] |= 1;
	d->reg[CRM_MEM_BANK_CTRL7 + 6] |= 1;

	memory_device_register(mem, "crime", baseaddr, DEV_CRIME_LENGTH, dev_crime_access, d);
	cpu_add_tickfunction(cpu, dev_crime_tick, d, CRIME_TICKSHIFT);
}

