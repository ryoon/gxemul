/*
 *  Copyright (C) 2004  Anders Gavare.  All rights reserved.
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
 *  $Id: dev_decbi.c,v 1.5 2005-01-09 01:55:25 debug Exp $
 *  
 *  DEC 5800 BI...
 *
 *  TODO:  Study VAX docs...
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "misc.h"
#include "bireg.h"
#include "devices.h"


struct decbi_data {
	int		csr[NNODEBI];
};


/*
 *  dev_decbi_access():
 */
int dev_decbi_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	uint64_t idata = 0, odata = 0;
	int node_nr;
	struct decbi_data *d = extra;

	idata = memory_readmax64(cpu, data, len);

	relative_addr += BI_NODESIZE;	/*  HACK  */

	node_nr = relative_addr / BI_NODESIZE;
	relative_addr &= (BI_NODESIZE - 1);

	/*  TODO:  This "1" here is the max node number in actual use.  */
	if (node_nr > 1 || node_nr >= NNODEBI)
		return 0;

	switch (relative_addr) {
	case BIREG_DTYPE:
		if (writeflag==MEM_READ) {
			/*
			 *  This is a list of the devices in our BI slots:
			 */
			switch (node_nr) {
			case 1:	odata = BIDT_KDB50; break;		/*  Disk  */
			/*  case 2:	odata = BIDT_DEBNA; break;  */	/*  Ethernet  */
			/*  case 3:	odata = BIDT_MS820; break;  */	/*  Memory  */
			default:
				/*  No device.  */
				odata = 0;
			}

			debug("[ decbi: (node %i) read from BIREG_DTYPE: 0x%x ]\n", node_nr, odata);
		} else {
			debug("[ decbi: (node %i) attempt to write to BIREG_DTYPE: 0x%08x ]\n", node_nr, idata);
		}
		break;
	case BIREG_VAXBICSR:
		if (writeflag==MEM_READ) {
			odata = (d->csr[node_nr] & ~BICSR_NODEMASK) | node_nr;
			debug("[ decbi: (node %i) read from BIREG_VAXBICSR: 0x%x ]\n", node_nr, odata);
		} else {
			d->csr[node_nr] = idata;
			debug("[ decbi: (node %i) attempt to write to BIREG_VAXBICSR: 0x%08x ]\n", node_nr, idata);
		}
		break;
	case 0xf4:
		if (writeflag==MEM_READ) {
			odata = 0xffff;	/*  ?  */
			debug("[ decbi: (node %i) read from 0xf4: 0x%x ]\n", node_nr, odata);
		} else {
			debug("[ decbi: (node %i) attempt to write to 0xf4: 0x%08x ]\n", node_nr, idata);
		}
		break;
	default:
		if (writeflag==MEM_READ) {
			debug("[ decbi: (node %i) read from unimplemented 0x%08lx ]\n", node_nr, (long)relative_addr, odata);
		} else {
			debug("[ decbi: (node %i) write to unimpltemeted  0x%08lx: 0x%08x ]\n", node_nr, (long)relative_addr, idata);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_decbi_init():
 */
void dev_decbi_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr)
{
	struct decbi_data *d;

	d = malloc(sizeof(struct decbi_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct decbi_data));

	memory_device_register(mem, "decbi", baseaddr + 0x2000,
	    DEV_DECBI_LENGTH - 0x2000, dev_decbi_access, d, MEM_DEFAULT, NULL);
}

