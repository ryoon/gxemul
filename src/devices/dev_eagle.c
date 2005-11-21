/*
 *  Copyright (C) 2003-2005  Anders Gavare.  All rights reserved.
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
 *  $Id: dev_eagle.c,v 1.3 2005-11-21 09:17:26 debug Exp $
 *  
 *  Motorola MPC105 "Eagle" host bridge.
 *
 *  TODO: This is just a dummy.
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


struct eagle_data {
	int		pciirq;
	struct pci_data	*pci_data;
};


/*
 *  dev_eagle_access():
 *
 *  Passes accesses to ISA ports 0xcf8 and 0xcfc onto bus_pci_access().
 */
int dev_eagle_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	uint64_t idata = 0, odata = 0;
	struct eagle_data *d = extra;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);

	relative_addr += BUS_PCI_ADDR;

	if (writeflag == MEM_WRITE)
		bus_pci_access(cpu, mem, relative_addr, &idata,
		    len, writeflag, d->pci_data);
	else
		bus_pci_access(cpu, mem, relative_addr, &odata,
		    len, writeflag, d->pci_data);

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_eagle_init():
 */
struct pci_data *dev_eagle_init(struct machine *machine, struct memory *mem,
	int isa_irqbase, int pciirq)
{
	struct eagle_data *d;
	int pci_irqbase = 0;	/*  TODO  */
	uint64_t pci_io_offset, pci_mem_offset;
	uint64_t isa_portbase = 0, isa_membase = 0;
	uint64_t pci_portbase = 0, pci_membase = 0;

	d = malloc(sizeof(struct eagle_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct eagle_data));
	d->pciirq = pciirq;

	pci_io_offset  = 0x80000000ULL;
	pci_mem_offset = 0xc0000000ULL;
	pci_portbase   = 0x00000000ULL;
	pci_membase    = 0x00000000ULL;
	isa_portbase   = 0x80000000ULL;
	isa_membase    = 0xc0000000ULL;

	d->pci_data = bus_pci_init(pciirq,
	    pci_io_offset, pci_mem_offset,
	    pci_portbase, pci_membase, pci_irqbase,
	    isa_portbase, isa_membase, isa_irqbase);

	/*  Add the PCI glue for the controller itself:  */
	bus_pci_add(machine, d->pci_data, mem, 0, 0, 0, "eagle");

	/*  ADDR and DATA configuration ports in ISA space:  */
	memory_device_register(mem, "eagle", isa_portbase + BUS_PCI_ADDR,
	    8, dev_eagle_access, d, DM_DEFAULT, NULL);

	switch (machine->machine_type) {
	case MACHINE_BEBOX:
		bus_pci_add(machine, d->pci_data, mem, 0, 11, 0, "i82378zb");
		break;
	case MACHINE_PREP:
		bus_pci_add(machine, d->pci_data, mem, 0, 11, 0, "ibm_isa");
		break;
	}

	return d->pci_data;
}

