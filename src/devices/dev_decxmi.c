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
 *  $Id: dev_decxmi.c,v 1.10 2005-01-09 01:55:25 debug Exp $
 *  
 *  DEC 5800 XMI (this has to do with SMP...)
 *
 *  TODO:  This hardware is not very easy to find docs about.
 *  Perhaps VAX docs will suffice.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "devices.h"
#include "emul.h"
#include "memory.h"
#include "misc.h"

#include "xmireg.h"


struct decxmi_data {
	uint32_t		reg_0xc[NNODEXMI];
};


/*
 *  dev_decxmi_access():
 */
int dev_decxmi_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	uint64_t idata = 0, odata = 0;
	int node_nr;
	struct decxmi_data *d = extra;

	idata = memory_readmax64(cpu, data, len);

	node_nr = relative_addr / XMI_NODESIZE;
	relative_addr &= (XMI_NODESIZE - 1);

	if (node_nr >= cpu->emul->ncpus+1 || node_nr >= NNODEXMI)
		return 0;

	switch (relative_addr) {
	case XMI_TYPE:
		if (writeflag == MEM_READ) {
			/*
			 *  The first node is an XMI->BI adapter node, and then
			 *  there are n CPU nodes.
			 */
			odata = XMIDT_ISIS;
			if (node_nr == 0)
				odata = XMIDT_DWMBA;

			debug("[ decxmi: (node %i) read from XMI_TYPE: 0x%08x ]\n", node_nr, odata);
		} else
			debug("[ decxmi: (node %i) write to XMI_TYPE: 0x%08x ]\n", node_nr, idata);
		break;
	case XMI_BUSERR:
		if (writeflag == MEM_READ) {
			odata = 0;
			debug("[ decxmi: (node %i) read from XMI_BUSERR: 0x%08x ]\n", node_nr, odata);
		} else
			debug("[ decxmi: (node %i) write to XMI_BUSERR: 0x%08x ]\n", node_nr, idata);
		break;
	case XMI_FAIL:
		if (writeflag == MEM_READ) {
			odata = 0;
			debug("[ decxmi: (node %i) read from XMI_FAIL: 0x%08x ]\n", node_nr, odata);
		} else
			debug("[ decxmi: (node %i) write to XMI_FAIL: 0x%08x ]\n", node_nr, idata);
		break;
	case 0xc:
		if (writeflag == MEM_READ) {
			odata = d->reg_0xc[node_nr];
			debug("[ decxmi: (node %i) read from REG 0xC: 0x%08x ]\n", node_nr, (int)odata);
		} else {
			d->reg_0xc[node_nr] = idata;
			debug("[ decxmi: (node %i) write to REG 0xC: 0x%08x ]\n", node_nr, (int)idata);
		}
		break;
	default:
		if (writeflag==MEM_READ) {
			debug("[ decxmi: (node %i) read from unimplemented 0x%08lx ]\n", node_nr, (long)relative_addr, odata);
		} else {
			debug("[ decxmi: (node %i) write to unimpltemeted  0x%08lx: 0x%08x ]\n", node_nr, (long)relative_addr, idata);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_decxmi_init():
 */
void dev_decxmi_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr)
{
	struct decxmi_data *d;

	d = malloc(sizeof(struct decxmi_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct decxmi_data));

	memory_device_register(mem, "decxmi", baseaddr, DEV_DECXMI_LENGTH,
	    dev_decxmi_access, d, MEM_DEFAULT, NULL);
}

