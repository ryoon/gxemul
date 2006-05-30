/*
 *  Copyright (C) 2006  Anders Gavare.  All rights reserved.
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
 *  $Id: dev_lca.c,v 1.1 2006-05-30 19:49:39 debug Exp $
 *
 *  LCA bus (for Alpha machines).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bus_isa.h"
#include "cpu.h"
#include "device.h"
#include "emul.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"


#include "alpha_lcareg.h"

#define	LCA_ISA_BASE	(LCA_PCI_SIO + 0x10000000)
#define	LCA_ISA_MEMBASE	(LCA_PCI_SIO + 0x18000000)


struct lca_data {
	int		dummy;
};


DEVICE_ACCESS(lca_isa)
{
	int ofs, i;
	uint8_t byte;

	relative_addr >>= 5;

	ofs = relative_addr & 3;
	if (ofs > len) {
		fatal("[ ofs=%i len=%i in lca_isa access function. "
		    "aborting ]\n", ofs, len);
		exit(1);
	}

	if (writeflag == MEM_WRITE) {
		byte = data[ofs % len];
		return cpu->memory_rw(cpu, cpu->mem, LCA_ISA_BASE +
		    relative_addr, &byte, 1, writeflag, CACHE_NONE);
	}

	cpu->memory_rw(cpu, cpu->mem, LCA_ISA_BASE + relative_addr,
	    &byte, 1, MEM_READ, CACHE_NONE);

	for (i=0; i<len; i++)
		data[i] = i == ofs? byte : 0x00;

	return 1;
}


DEVINIT(lca)
{
	struct lca_data *d = malloc(sizeof(struct lca_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct lca_data));

	memory_device_register(devinit->machine->memory, "lca_isa",
	    LCA_PCI_SIO, 0x10000 << 5, dev_lca_isa_access, (void *)d,
	    DM_DEFAULT, NULL);

	/*  TODO: IRQs etc.  */
	bus_isa_init(devinit->machine, BUS_ISA_IDE0 | BUS_ISA_IDE1,
	    LCA_ISA_BASE, LCA_ISA_MEMBASE, 32, 48);

	return 1;
}

