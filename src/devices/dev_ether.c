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
 *  $Id: dev_ether.c,v 1.2 2005-07-12 21:58:37 debug Exp $
 *
 *  Basic "ethernet" network device. This is a simple test device which can
 *  be used to send and receive packets to/from a simulated ethernet network.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "emul.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"
#include "net.h"


#define	DEV_ETHER_MAXBUFLEN	16384

#define	STATUS_RECEIVED		0x01
#define	STATUS_MORE_AVAIL	0x02

struct ether_data {
	unsigned char	buf[DEV_ETHER_MAXBUFLEN];
	int		status;
	int		packet_len;
};


/*
 *  dev_ether_buf_access():
 */
int dev_ether_buf_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	struct ether_data *d = (struct ether_data *) extra;

	if (writeflag == MEM_WRITE)
		memcpy(d->buf + relative_addr, data, len);
	else
		memcpy(data, d->buf + relative_addr, len);
	return 1;
}


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

	idata = memory_readmax64(cpu, data, len);

	switch (relative_addr) {
	case 0x0000:
		if (writeflag == MEM_READ)
			odata = d->status;
		else
			fatal("[ ether: WARNING: write to status ]\n");
		break;
	case 0x0010:
		if (writeflag == MEM_READ)
			odata = d->packet_len;
		else {
			if ((int64_t)idata < 0) {
				fatal("[ ether: ERROR: packet len too"
				    " short (%i bytes) ]\n", (int)idata);
				idata = -1;
			}
			if (idata > DEV_ETHER_MAXBUFLEN) {
				fatal("[ ether: ERROR: packet len too"
				    " large (%i bytes) ]\n", (int)idata);
				idata = DEV_ETHER_MAXBUFLEN;
			}
			d->packet_len = idata;
		}
		break;
	case 0x0020:
		if (writeflag == MEM_READ)
			fatal("[ ether: WARNING: read from command ]\n");
		else {
			switch (idata) {
			default:fatal("[ ether: UNIMPLEMENTED command 0x"
				    "%02x ]\n", idata);
				cpu->running = 0;
			}
		}
		break;
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
	size_t nlen;
	char *n1, *n2;

	nlen = strlen(devinit->name) + 30;
	n1 = malloc(nlen);
	n2 = malloc(nlen);

	if (d == NULL || n1 == NULL || n2 == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct ether_data));
	snprintf(n1, nlen, "%s [data buffer]", devinit->name);
	snprintf(n2, nlen, "%s [control]", devinit->name);

	memory_device_register(devinit->machine->memory, n1,
	    devinit->addr, DEV_ETHER_MAXBUFLEN, dev_ether_buf_access, (void *)d,
	    MEM_BINTRANS_OK | MEM_BINTRANS_WRITE_OK |
	    MEM_READING_HAS_NO_SIDE_EFFECTS, NULL);
	memory_device_register(devinit->machine->memory, n2,
	    devinit->addr + DEV_ETHER_MAXBUFLEN,
	    DEV_ETHER_LENGTH-DEV_ETHER_MAXBUFLEN, dev_ether_access, (void *)d,
	    MEM_DEFAULT, NULL);

	return 1;
}

