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
 *  $Id: dev_bebox.c,v 1.3 2005-02-23 22:08:19 debug Exp $
 *
 *  Emulation of BeBox motherboard registers. See the following URL for more
 *  information:
 *
 *	http://www.bebox.nu/history.php?s=history/benews/benews27
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "device.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"


struct bebox_data {
	/*  The 5 motherboard registers:  */
	uint32_t	cpu0_intmask;
	uint32_t	cpu1_intmask;
	uint32_t	int_source;
	uint32_t	xpi;
	uint32_t	resets;
};


/*
 *  dev_bebox_access():
 */
int dev_bebox_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len, int writeflag,
	void *extra)
{
	struct bebox_data *d = extra;
	uint64_t idata = 0, odata = 0;

	idata = memory_readmax64(cpu, data, len);

	switch (relative_addr) {
	case 0x3f0:
		if (writeflag == MEM_READ) {
			odata = d->xpi;

			/*  Bit 6 (counted from the left) is cpuid:  */
			odata &= ~0x02000000;
			if (cpu->cpu_id == 1)
				odata |= 0x02000000;
		} else {
			fatal("[ bebox: unimplemented write to 0x3f0:"
			    " 0x%08x ]\n", (int)idata);
		}
		break;
	default:
		if (writeflag==MEM_READ) {
			fatal("[ bebox: unimplemented read from 0x%08lx ]\n",
			    (long)relative_addr);
		} else {
			fatal("[ bebox: unimplemented write to 0x%08lx: 0x"
			    "%08x ]\n", (long)relative_addr, (int)idata);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  devinit_bebox():
 */
int devinit_bebox(struct devinit *devinit)
{
	struct bebox_data *d;

	d = malloc(sizeof(struct bebox_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct bebox_data));

	memory_device_register(devinit->machine->memory, devinit->name,
	    0x7ffff000, 0x500, dev_bebox_access, d, MEM_DEFAULT, NULL);

	return 1;
}

