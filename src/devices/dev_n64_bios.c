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
 *  $Id: dev_n64_bios.c,v 1.1 2004-01-04 21:40:11 debug Exp $
 *  
 *  Nintendo 64 devices between 0x03f00000 and 0x05000000.
 *  (Not really a BIOS.)
 *
 *  TODO:  I have no idea about what this is.  :-/
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "misc.h"
#include "console.h"
#include "devices.h"


struct n64_bios_data {
	unsigned char	reg[DEV_N64_BIOS_LENGTH];
	uint64_t	baseaddr;
};


/*
 *  dev_n64_bios_access():
 *
 *  Returns 1 if ok, 0 on error.
 */
int dev_n64_bios_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *extra)
{
	int i;
	struct n64_bios_data *d = extra;

	if (writeflag == MEM_WRITE)
		memcpy(&d->reg[relative_addr], data, len);
	else
		memcpy(data, &d->reg[relative_addr], len);

	/*  After this point, addresses are absolute.  */
	relative_addr += d->baseaddr;

	switch (relative_addr) {
	default:
		if (writeflag==MEM_READ) {
			debug("[ n64_bios: read from 0x%x, len=%i ]\n", (int)relative_addr, len);
		} else {
			debug("[ n64_bios: write to 0x%x:", (int)relative_addr);
			for (i=0; i<len; i++)
				debug(" %02x", data[i]);
			debug(" (len=%i) ]\n", len);
		}
	}

	return 1;
}


/*
 *  dev_n64_bios_init():
 */
void dev_n64_bios_init(struct memory *mem, uint64_t baseaddr)
{
	struct n64_bios_data *d;

	d = malloc(sizeof(struct n64_bios_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct n64_bios_data));
	d->baseaddr = baseaddr;

	memory_device_register(mem, "n64_bios", baseaddr, DEV_N64_BIOS_LENGTH, dev_n64_bios_access, d);
}

