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
 *  $Id: dev_sgi_cpuinfo.c,v 1.1 2004-01-05 06:30:51 debug Exp $
 *  
 *  SGI cpuinfo CPU stuff. (This isn't very documented, I'm basing it on
 *  linux/arch/mips/sgi-ip27/ for now.)
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "misc.h"
#include "devices.h"


struct sgi_cpuinfo_data {
	int	dummy;
};


/*
 *  dev_sgi_cpuinfo_access():
 *
 *  Returns 1 if ok, 0 on error.
 */
int dev_sgi_cpuinfo_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *extra)
{
	struct sgi_cpuinfo_data *d = (struct sgi_cpuinfo_data *) extra;
	int regnr;
	int idata = 0, odata=0, odata_set=0, i;

	/*  Switch byte order for incoming data, if neccessary:  */
	if (cpu->byte_order == EMUL_BIG_ENDIAN)
		for (i=0; i<len; i++) {
			idata <<= 8;
			idata |= data[i];
		}
	else
		for (i=len-1; i>=0; i--) {
			idata <<= 8;
			idata |= data[i];
		}

	if (writeflag == MEM_READ)
		odata_set = 1;

	/*  Read from/write to the sgi_cpuinfo:  */
	switch (relative_addr) {

	case 0x1600000:
		odata = 0x1234;		/*  Just testing... :-)  */
					/*  I have no idea about what these CPU id values are  */
		break;

	default:
		if (writeflag == MEM_WRITE)
			debug("[ sgi_cpuinfo: unimplemented write to address 0x%x, data=0x%02x ]\n", relative_addr, idata);
		else
			debug("[ sgi_cpuinfo: unimplemented read from address 0x%x ]\n", relative_addr);
	}

	if (odata_set) {
		if (cpu->byte_order == EMUL_LITTLE_ENDIAN) {
			for (i=0; i<len; i++)
				data[i] = (odata >> (i*8)) & 255;
		} else {
			for (i=0; i<len; i++)
				data[len - 1 - i] = (odata >> (i*8)) & 255;
		}
	}

	return 1;
}


/*
 *  dev_sgi_cpuinfo_init():
 */
void dev_sgi_cpuinfo_init(struct memory *mem, uint64_t baseaddr)
{
	struct sgi_cpuinfo_data *d = malloc(sizeof(struct sgi_cpuinfo_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct sgi_cpuinfo_data));

	memory_device_register(mem, "sgi_cpuinfo", baseaddr, DEV_SGI_CPUINFO_LENGTH, dev_sgi_cpuinfo_access, (void *)d);
}

