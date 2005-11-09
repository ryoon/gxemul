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
 *  $Id: dev_dec21143.c,v 1.2 2005-11-09 08:28:27 debug Exp $
 *
 *  DEC 21143 ("Tulip") ethernet.
 *
 *  TODO:  This is just a dummy device, so far.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "device.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"

#include "tulipreg.h"


#define debug fatal


struct dec21143_data {
	int	dummy;
};


/*
 *  dev_dec21143_access():
 *
 *  Memory access.
 */
int dev_dec21143_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	/*  struct dec21143_data *d = extra;  */
	uint64_t idata = 0, odata = 0;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);

	switch (relative_addr) {

	default:if (writeflag == MEM_READ)
			debug("[ dec21143: read from 0x%02x ]\n",
			    (int)relative_addr);
		else
			debug("[ dec21143: write to  0x%02x: 0x%02x ]\n",
			    (int)relative_addr, (int)idata);
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_dec21143_io_access():
 *
 *  I/O port access.
 */
int dev_dec21143_io_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	/*  struct dec21143_data *d = extra;  */
	uint64_t idata = 0, odata = 0;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);

	switch (relative_addr) {
	default:if (writeflag == MEM_READ)
			debug("[ dec21143_io: read from 0x%02x ]\n",
			    (int)relative_addr);
		else
			debug("[ dec21143_io: write to  0x%02x: 0x%02x ]\n",
			    (int)relative_addr, (int)idata);
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  devinit_dec21143():
 */
int devinit_dec21143(struct devinit *devinit)
{
	struct dec21143_data *d;
	char name2[100];

	d = malloc(sizeof(struct dec21143_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct dec21143_data));

	snprintf(name2, sizeof(name2), "%s [i/o]", devinit->name);

	/*  TODO: Don't hardcode the 0x10000 offset.  */

	memory_device_register(devinit->machine->memory, name2, devinit->addr,
	    0x100, dev_dec21143_io_access, d, MEM_DEFAULT, NULL);

	memory_device_register(devinit->machine->memory, devinit->name,
	    devinit->addr + 0x10000, 0x100, dev_dec21143_access, d,
	    MEM_DEFAULT, NULL);

	return 1;
}

