/*
 *  Copyright (C) 2003-2006  Anders Gavare.  All rights reserved.
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
 *  $Id: dev_kn230.c,v 1.14 2006-01-01 13:17:16 debug Exp $
 *  
 *  DEC MIPSMATE 5100 (KN230) stuff.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "device.h"
#include "devices.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"


#define	DEV_KN230_LENGTH		0x1c00000


/*
 *  dev_kn230_access():
 */
DEVICE_ACCESS(kn230)
{
	struct kn230_csr *d = extra;
	uint64_t idata = 0, odata = 0;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);

	switch (relative_addr) {
	case 0:
		if (writeflag==MEM_READ) {
			odata = d->csr;
			/* debug("[ kn230: read from CSR: 0x%08x ]\n",
			    (int)odata); */
		} else {
			/* debug("[ kn230: write to CSR: 0x%08x ]\n",
			    (int)idata); */
		}
		break;
	default:
		if (writeflag==MEM_READ) {
			debug("[ kn230: read from 0x%08lx ]\n",
			    (long)relative_addr);
		} else {
			debug("[ kn230: write to  0x%08lx: 0x%08x ]\n",
			    (long)relative_addr, (int)idata);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);
  
	return 1;
}


/*
 *  devinit_kn230():
 */
int devinit_kn230(struct devinit *devinit)
{
	struct kn230_csr *d;

	d = malloc(sizeof(struct kn230_csr));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct kn230_csr));

	memory_device_register(devinit->machine->memory, devinit->name,
	    devinit->addr, DEV_KN230_LENGTH, dev_kn230_access, d,
	    DM_DEFAULT, NULL);

	devinit->return_ptr = d;

	return 1;
}

