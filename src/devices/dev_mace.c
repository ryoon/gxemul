/*
 *  Copyright (C) 2003-2004  Anders Gavare.  All rights reserved.
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
 *  $Id: dev_mace.c,v 1.6 2004-10-17 15:31:39 debug Exp $
 *  
 *  SGI "mace".
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "misc.h"
#include "console.h"
#include "devices.h"


/*
 *  dev_mace_access():
 */
int dev_mace_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	int i;
	struct mace_data *d = extra;

	if (writeflag == MEM_WRITE)
		memcpy(&d->reg[relative_addr], data, len);
	else
		memcpy(data, &d->reg[relative_addr], len);

	switch (relative_addr) {
#if 0
	case 0x14:	/*  Current interrupt assertions  */
	case 0x18:	/*  ???  */
	case 0x1c:	/*  Interrupt mask  */
		/*  don't dump debug info for these  */
		break;
#endif
	default:
		if (writeflag==MEM_READ) {
			debug("[ mace: read from 0x%x, len=%i ]\n", (int)relative_addr, len);
		} else {
			debug("[ mace: write to 0x%x:", (int)relative_addr);
			for (i=0; i<len; i++)
				debug(" %02x", data[i]);
			debug(" (len=%i) ]\n", len);
		}
	}

	return 1;
}


/*
 *  dev_mace_init():
 */
struct mace_data *dev_mace_init(struct memory *mem, uint64_t baseaddr,
	int irqnr)
{
	struct mace_data *d;

	d = malloc(sizeof(struct mace_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct mace_data));
	d->irqnr = irqnr;

	memory_device_register(mem, "mace", baseaddr, DEV_MACE_LENGTH,
	    dev_mace_access, d);

	return d;
}

