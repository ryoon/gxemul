/*
 *  Copyright (C) 2004 by Anders Gavare.  All rights reserved.
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
 *  $Id: dev_sgi_ip20.c,v 1.5 2004-09-05 05:06:55 debug Exp $
 *  
 *  SGI IP20 stuff.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "misc.h"
#include "devices.h"


extern int quiet_mode;


/*
 *  dev_sgi_ip20_access():
 */
int dev_sgi_ip20_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	/*  struct sgi_ip20_data *d = (struct sgi_ip20_data *) extra;  */
	uint64_t idata = 0, odata = 0;

	idata = memory_readmax64(cpu, data, len);

	switch (relative_addr) {
	case 0x38:
		if (writeflag == MEM_WRITE) {
			debug("[ sgi_ip20: write to address 0x%x, data=0x%02x ]\n", relative_addr, idata);
		} else {
			/*
			 *  TODO:
			 *
			 *  I haven't had time to figure out what values should
			 *  be returned by this device.  Simple bit patterns
			 *  don't seem to be enough, but using random() is
			 *  obviously pretty bad.  This must be fixed some day.
			 */

			/*  instruction_trace = 1;  quiet_mode = 0;  */
			odata = random() & 0xff;

			debug("[ sgi_ip20: read from address 0x%x: 0x%x ]\n", relative_addr, (int)odata);
		}
		break;
	default:
		if (writeflag == MEM_WRITE) {
			debug("[ sgi_ip20: unimplemented write to address 0x%x, data=0x%02x ]\n", relative_addr, idata);
		} else {
			debug("[ sgi_ip20: unimplemented read from address 0x%x ]\n", relative_addr);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_sgi_ip20_init():
 */
struct sgi_ip20_data *dev_sgi_ip20_init(struct cpu *cpu, struct memory *mem,
	uint64_t baseaddr)
{
	struct sgi_ip20_data *d = malloc(sizeof(struct sgi_ip20_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct sgi_ip20_data));

	/*
	 *  This device is detected as int0 by NetBSD 2.0_BETA, so I call it
	 *  "sgi_ip20_int".
	 */
	memory_device_register(mem, "sgi_ip20_int", baseaddr,
	    DEV_SGI_IP20_LENGTH, dev_sgi_ip20_access, (void *)d);

	return d;
}

