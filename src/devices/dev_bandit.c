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
 *  $Id: dev_bandit.c,v 1.1 2005-11-27 16:03:34 debug Exp $
 *  
 *  Bandit PCI controller (as used by MacPPC).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bus_pci.h"
#include "cpu.h"
#include "devices.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"


struct bandit_data {
	int		pciirq;
	struct pci_data	*pci_data;
};


/*
 *  dev_bandit_addr_access():
 */
int dev_bandit_addr_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	struct bandit_data *d = extra;
	if (writeflag == MEM_WRITE) {
		uint64_t idata = memory_readmax64(cpu, data, len
		    | MEM_PCI_LITTLE_ENDIAN);
		uint64_t x = 0;
		int i;
		/*  Convert Bandit PCI address into normal address:  */
		for (i=11; i<32; i++)
			if (idata & (1 << i))
				break;
		if (i < 32)
			x = i << 11;
		/*  Copy function and register nr from idata:  */
		x |= (idata & 0x7ff);
		bus_pci_access(cpu, mem, BUS_PCI_ADDR, &x,
		    len | PCI_ALREADY_NATIVE_BYTEORDER, writeflag, d->pci_data);
	} else {
		uint64_t odata;
		bus_pci_access(cpu, mem, BUS_PCI_ADDR, &odata,
		    len, writeflag, d->pci_data);
		memory_writemax64(cpu, data, len, odata);
		printf("TODO: read from bandit addr\n");
	}
	return 1;
}


/*
 *  dev_bandit_data_access():
 */
int dev_bandit_data_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	struct bandit_data *d = extra;
	if (writeflag == MEM_WRITE) {
		uint64_t idata = memory_readmax64(cpu, data, len);
		bus_pci_access(cpu, mem, BUS_PCI_DATA, &idata,
		    len, writeflag, d->pci_data);
	} else {
		uint64_t odata;
		bus_pci_access(cpu, mem, BUS_PCI_DATA, &odata,
		    len, writeflag, d->pci_data);
		memory_writemax64(cpu, data, len, odata);
	}
	return 1;
}


/*
 *  dev_bandit_init():
 */
struct pci_data *dev_bandit_init(struct machine *machine, struct memory *mem,
	uint64_t addr, int isa_irqbase, int pciirq)
{
	struct bandit_data *d;
	int pci_irqbase = 0;	/*  TODO  */
	uint64_t pci_io_offset, pci_mem_offset;
	uint64_t isa_portbase = 0, isa_membase = 0;
	uint64_t pci_portbase = 0, pci_membase = 0;

	d = malloc(sizeof(struct bandit_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct bandit_data));
	d->pciirq = pciirq;

	pci_io_offset  = 0x00000000ULL;
	pci_mem_offset = 0x00000000ULL;
	pci_portbase   = 0xd0000000ULL;
	pci_membase    = 0xd1000000ULL;
	isa_portbase   = 0xd2000000ULL;
	isa_membase    = 0xd3000000ULL;

	/*  Create a PCI bus:  */
	d->pci_data = bus_pci_init(pciirq,
	    pci_io_offset, pci_mem_offset,
	    pci_portbase, pci_membase, pci_irqbase,
	    isa_portbase, isa_membase, isa_irqbase);

	/*  Add the PCI glue for the controller itself:  */
	bus_pci_add(machine, d->pci_data, mem, 0, 11, 0, "bandit");

	/*  ADDR and DATA configuration ports:  */
	memory_device_register(mem, "bandit_pci_addr", addr + 0x800000,
	    4, dev_bandit_addr_access, d, DM_DEFAULT, NULL);
	memory_device_register(mem, "bandit_pci_data", addr + 0xc00000,
	    8, dev_bandit_data_access, d, DM_DEFAULT, NULL);

	return d->pci_data;
}

