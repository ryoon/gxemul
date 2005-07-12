/*
 *  Copyright (C) 2005  Anders Gavare.  All rights reserved.
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
 *  $Id: dev_ether.c,v 1.1 2005-07-12 08:49:13 debug Exp $
 *
 *  Basic "ethernet" network device. This is a simple test device which can
 *  be used to send and receive packets to/from a simulated ethernet network.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "device.h"
#include "emul.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"
#include "net.h"


#define	DEV_ETHER_LEN		1024
#define	DEV_ETHER_MAXBUFLEN	65536

struct ether_data {
	unsigned char	buf[DEV_ETHER_MAXBUFLEN];
};


/*
 *  dev_ether_access():
 */
int dev_ether_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	struct ether_data *d = (struct ether_data *) extra;
	uint64_t idata = 0, odata = 0;
	int i;

	if (relative_addr + len - 1 < sizeof(d->buf)) {
		relative_addr -= 512;
		if (writeflag == MEM_WRITE)
			memcpy(d->buf + relative_addr, data, len);
		else
			memcpy(data, d->buf + relative_addr, len);
		return 1;
	}

	idata = memory_readmax64(cpu, data, len);

	switch (relative_addr) {
	default:if (writeflag == MEM_WRITE) {
			fatal("[ ether: unimplemented write to "
			    "offset 0x%x: data=0x%x ]\n", (int)
			    relative_addr, (int)idata);
		} else {
			fatal("[ ether: unimplemented read from "
			    "offset 0x%x ]\n", (int)relative_addr);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  devinit_ether():
 */
int devinit_ether(struct devinit *devinit)
{
	struct ether_data *d = malloc(sizeof(struct ether_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct ether_data));

	memory_device_register(devinit->machine->memory, devinit->name,
	    devinit->addr, DEV_ETHER_LEN, dev_ether_access, (void *)d,
	    MEM_DEFAULT, NULL);

	return 1;
}

