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
 *  $Id: dev_ram.c,v 1.5 2004-03-23 02:30:55 debug Exp $
 *  
 *  A generic RAM (memory) device.  Can also be used to mirror/alias another
 *  part of RAM.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "misc.h"
#include "console.h"
#include "devices.h"


/*  #define RAM_DEBUG  */

struct ram_data {
	int		mode;
	uint64_t	otheraddress;

	/*  If mode = DEV_RAM_RAM:  */
	unsigned char	*data;
	uint64_t	length;
};


/*
 *  dev_ram_access():
 *
 *  Returns 1 if ok, 0 on error.
 */
int dev_ram_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *extra)
{
	struct ram_data *d = extra;

#ifdef RAM_DEBUG
	if (writeflag==MEM_READ) {
		debug("[ ram: read from 0x%x, len=%i ]\n", (int)relative_addr, len);
	} else {
		int i;
		debug("[ ram: write to 0x%x:", (int)relative_addr);
		for (i=0; i<len; i++)
			debug(" %02x", data[i]);
		debug(" (len=%i) ]\n", len);
	}
#endif

	switch (d->mode) {
	case DEV_RAM_MIRROR:
		/*  TODO:  how about caches?  */
		return memory_rw(cpu, mem, d->otheraddress + relative_addr, data, len, writeflag, PHYSICAL | NO_EXCEPTIONS);
	case DEV_RAM_RAM:
		if (writeflag == MEM_WRITE)
			memcpy(&d->data[relative_addr], data, len);
		else
			memcpy(data, &d->data[relative_addr], len);
		break;
	default:
		fatal("dev_ram_access(): unknown mode %i\n", d->mode);
		exit(1);
	}

	return 1;
}


/*
 *  dev_ram_init():
 */
void dev_ram_init(struct memory *mem, uint64_t baseaddr, uint64_t length, int mode, uint64_t otheraddress)
{
	struct ram_data *d;

	d = malloc(sizeof(struct ram_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}

	memset(d, 0, sizeof(struct ram_data));

	d->mode         = mode;
	d->otheraddress = otheraddress;

	switch (d->mode) {
	case DEV_RAM_MIRROR:
		break;
	case DEV_RAM_RAM:
		d->length = length;
		d->data = malloc(length);
		if (d->data == NULL) {
			fprintf(stderr, "out of memory\n");
			exit(1);
		}
		memset(d->data, 0, length);
		break;
	default:
		fatal("dev_ram_access(): unknown mode %i\n", d->mode);
		exit(1);
	}

	memory_device_register(mem, "ram", baseaddr, length, dev_ram_access, d);
}

