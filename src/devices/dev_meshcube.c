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
 *  $Id: dev_meshcube.c,v 1.1 2004-08-05 22:45:30 debug Exp $
 *  
 *  MeshCube pseudo-device.
 *
 *  This is basically just a huge TODO. :-)
 *
 *  (Semi-serious TODO: this is mostly AU1x00 specific?)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "misc.h"
#include "devices.h"


struct meshcube_data {
	int		dummy;
};


/*
 *  dev_meshcube_tick():
 */
void dev_meshcube_tick(struct cpu *cpu, void *extra)
{
	struct meshcube_data *d = (struct meshcube_data *) extra;

/*	if (random() & 1)
		cpu_interrupt(cpu, 2);
	else
		cpu_interrupt_ack(cpu, 2);
*/

}


/*
 *  dev_meshcube_access():
 */
int dev_meshcube_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr,
	unsigned char *data, size_t len, int writeflag, void *extra)
{
	struct meshcube_data *d = extra;
	uint64_t idata = 0, odata = 0;

	idata = memory_readmax64(cpu, data, len);

	if (writeflag==MEM_READ) {
		debug("[ meshcube: read from 0x%08lx ]\n", (long)relative_addr);
	} else {
		debug("[ meshcube: write to  0x%08lx: 0x%08x ]\n", (long)relative_addr, idata);
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_meshcube_init():
 */
void dev_meshcube_init(struct cpu *cpu, struct memory *mem)
{
	struct meshcube_data *d;

	d = malloc(sizeof(struct meshcube_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct meshcube_data));

/*
	memory_device_register(mem, "meshcube",
		baseaddr, DEV_......_LENGTH * addrmult, dev_meshcube_access, d);
*/

	cpu_add_tickfunction(cpu, dev_meshcube_tick, d, 16);
}

