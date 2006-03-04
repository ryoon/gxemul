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
 *  $Id: dev_avr.c,v 1.3 2006-03-04 12:38:47 debug Exp $
 *  
 *  AVR I/O and register area.
 *
 *  NOTE: The dyntrans system usually translates in/out instructions so that
 *  cpu->cd.avr.xxx is accessed directly (where xxx is the right i/o register).
 *  This device is only here in case these addresses are reached using load/
 *  stores to low memory addresses.
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


struct avr_data {
	int		dummy;
};


DEVICE_ACCESS(avr)
{
	uint64_t idata = 0, odata = 0;
	/*  struct avr_data *d = extra;  */

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);

	switch (relative_addr) {

	case 0x11:	/*  ddrd  */
		if (writeflag == MEM_WRITE)
			cpu->cd.avr.ddrd = idata;
		else
			odata = cpu->cd.avr.ddrd;
		break;

	case 0x12:	/*  portd  */
		if (writeflag == MEM_WRITE)
			cpu->cd.avr.portd_write = idata;
		else
			odata = cpu->cd.avr.portd_read;
		break;

	case 0x14:	/*  ddrc  */
		if (writeflag == MEM_WRITE)
			cpu->cd.avr.ddrc = idata;
		else
			odata = cpu->cd.avr.ddrc;
		break;

	case 0x17:	/*  ddrb  */
		if (writeflag == MEM_WRITE)
			cpu->cd.avr.ddrb = idata;
		else
			odata = cpu->cd.avr.ddrb;
		break;

	case 0x1a:	/*  ddrc  */
		if (writeflag == MEM_WRITE)
			cpu->cd.avr.ddra = idata;
		else
			odata = cpu->cd.avr.ddra;
		break;

	case 0x3d:	/*  spl  */
		if (writeflag == MEM_WRITE) {
			cpu->cd.avr.sp &= 0xff00;
			cpu->cd.avr.sp |= (idata & 0xff);
		} else {
			odata = cpu->cd.avr.sp & 0xff;
		}
		break;

	case 0x3e:	/*  sph  */
		if (writeflag == MEM_WRITE) {
			cpu->cd.avr.sp &= 0xff;
			cpu->cd.avr.sp |= ((idata & 0xff) << 8);
		} else {
			odata = (cpu->cd.avr.sp >> 8) & 0xff;
		}
		break;

	default:fatal("AVR: addr=%i, len=%i, idata=%i\n", (int)relative_addr,
		    (int)len, (int)idata);
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


DEVINIT(avr)
{
	struct avr_data *d;
	d = malloc(sizeof(struct avr_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct avr_data));

	memory_device_register(devinit->machine->memory, devinit->name,
	    devinit->addr, 0x60, dev_avr_access, d, DM_DEFAULT, NULL);

	return 1;
}

