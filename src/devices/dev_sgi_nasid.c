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
 *  $Id: dev_sgi_nasid.c,v 1.9 2004-12-18 06:01:15 debug Exp $
 *  
 *  SGI nasid CPU stuff. (This isn't very documented, I'm basing it on
 *  linux/arch/mips/sgi-ip27/ for now.)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "misc.h"
#include "devices.h"


struct sgi_nasid_data {
	int	dummy;
};


/*
 *  dev_sgi_nasid_access():
 */
int dev_sgi_nasid_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
/*	struct sgi_nasid_data *d = (struct sgi_nasid_data *) extra;  */
	uint64_t idata = 0, odata = 0;

	idata = memory_readmax64(cpu, data, len);

	/*  Read from/write to the sgi_nasid:  */
	switch (relative_addr) {
	case 0x1600000:
		odata = 0x1234;		/*  Just testing... :-)  */
					/*  I have no idea about what these CPU id values are  */
		break;
	default:
		if (writeflag == MEM_WRITE)
			debug("[ sgi_nasid: unimplemented write to address 0x%llx, data=0x%02x ]\n", (long long)relative_addr, idata);
		else
			debug("[ sgi_nasid: unimplemented read from address 0x%llx ]\n", (long long)relative_addr);
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_sgi_nasid_init():
 */
void dev_sgi_nasid_init(struct memory *mem, uint64_t baseaddr)
{
	struct sgi_nasid_data *d = malloc(sizeof(struct sgi_nasid_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct sgi_nasid_data));

	memory_device_register(mem, "sgi_nasid", baseaddr,
	    DEV_SGI_NASID_LENGTH, dev_sgi_nasid_access, (void *)d, MEM_DEFAULT, NULL);
}

