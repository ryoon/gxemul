/*
 *  Copyright (C) 2004-2005  Anders Gavare.  All rights reserved.
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
 *  $Id: dev_ps2_ohci.c,v 1.9 2005-02-21 09:37:43 debug Exp $
 *  
 *  Playstation 2 OHCI USB host controller.
 *
 *  TODO:  Generalize this into a machine independant OHCI?
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "devices.h"
#include "memory.h"
#include "misc.h"


struct ps2_ohci_data {
	int	dummy;
};


/*
 *  dev_ps2_ohci_access():
 */
int dev_ps2_ohci_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	/*  struct ps2_ohci_data *d = extra;  */
	uint64_t idata = 0, odata = 0;

	idata = memory_readmax64(cpu, data, len);

	switch (relative_addr) {
	case 0:
		if (writeflag==MEM_READ) {
#if 0
			/*  to make NetBSD say "OHCI version 1.0"  */
			odata = 0x10;
#endif
			debug("[ ps2_ohci: read from addr 0x%x: 0x%llx ]\n",
			    (int)relative_addr, (long long)odata);
		} else {
			debug("[ ps2_ohci: write to addr 0x%x: 0x%llx ]\n",
			    (int)relative_addr, (long long)idata);
		}
		break;
	default:
		if (writeflag==MEM_READ) {
			debug("[ ps2_ohci: read from addr 0x%x: 0x%llx ]\n",
			    (int)relative_addr, (long long)odata);
		} else {
			debug("[ ps2_ohci: write to addr 0x%x: 0x%llx ]\n",
			    (int)relative_addr, (long long)idata);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_ps2_ohci_init():
 */
void dev_ps2_ohci_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr)
{
	struct ps2_ohci_data *d;

	d = malloc(sizeof(struct ps2_ohci_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct ps2_ohci_data));

	memory_device_register(mem, "ps2_ohci", baseaddr,
	    DEV_PS2_OHCI_LENGTH, dev_ps2_ohci_access, d, MEM_DEFAULT, NULL);
}

