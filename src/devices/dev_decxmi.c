/*
 *  Copyright (C) 2003-2004 by Anders Gavare.  All rights reserved.
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
 *  $Id: dev_decxmi.c,v 1.1 2004-02-24 00:16:51 debug Exp $
 *  
 *  DEC 5800 XMI (this has to do with SMP...)
 *
 *  TODO:  This hardware is not very easy to find docs about.
 *  Perhaps VAX docs will suffice.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "misc.h"
#include "xmireg.h"
#include "devices.h"


struct decxmi_data {
	int		dummy;
};


/*
 *  dev_decxmi_access():
 *
 *  Returns 1 if ok, 0 on error.
 */
int dev_decxmi_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *extra)
{
	uint64_t idata = 0, odata = 0;
	int node_nr;

	idata = memory_readmax64(cpu, data, len);

	node_nr = relative_addr / XMI_NODESIZE;
	relative_addr &= (XMI_NODESIZE - 1);

	switch (relative_addr) {
	case XMI_TYPE:
		if (writeflag == MEM_READ) {
			odata = XMIDT_ISIS;
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

	memory_device_register(mem, "decxmi", baseaddr, DEV_DECXMI_LENGTH, dev_decxmi_access, d);
}

