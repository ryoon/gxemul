/*
 *  Copyright (C) 2005-2006  Anders Gavare.  All rights reserved.
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
 *  $Id: dev_i80321.c,v 1.7 2006-02-04 12:27:13 debug Exp $
 *
 *  Intel i80321 (ARM) core functionality.
 *
 *  TODO: This is mostly just a dummy device.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bus_pci.h"
#include "cpu.h"
#include "device.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"


#include "i80321reg.h"

#define	DEV_I80321_LENGTH		VERDE_PMMR_SIZE

struct i80321_data {
	uint32_t	pci_addr;
	uint64_t	pci_data_to_be_written;
	struct pci_data *pci_bus;

	uint32_t	mcu_reg[VERDE_MCU_SIZE / sizeof(uint32_t)];
};


/*
 *  dev_i80321_access():
 */
DEVICE_ACCESS(i80321)
{
	struct i80321_data *d = extra;
	uint64_t idata = 0, odata = 0;
	char *n = NULL;
	int bus, dev, func, reg;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);

	if (relative_addr >= VERDE_MCU_BASE &&
	    relative_addr <  VERDE_MCU_BASE + VERDE_MCU_SIZE) {
		int regnr = (relative_addr - VERDE_MCU_BASE) / sizeof(uint32_t);
		if (writeflag == MEM_WRITE)
			d->mcu_reg[regnr] = idata;
		else
			odata = d->mcu_reg[regnr];
	}

	switch (relative_addr) {

	/*  Address Translation Unit:  */
	case VERDE_ATU_BASE + ATU_OCCAR:
		/*  PCI address  */
		if (writeflag == MEM_WRITE) {
			d->pci_addr = idata;
			bus_pci_decompose_1(idata, &bus, &dev, &func, &reg);
			bus = 0;	/*  NOTE  */
			bus_pci_setaddr(cpu, d->pci_bus, bus, dev, func, reg);
		} else {
			odata = d->pci_addr;
		}
		break;
	case VERDE_ATU_BASE + ATU_OCCDR:
		/*  PCI data  */
		if (len == 4) {
			bus_pci_data_access(cpu, d->pci_bus, writeflag ==
			    MEM_READ? &odata : &idata, len, writeflag);
		} else if (len == 2) {
			/*  16-bit access, used by FreeBSD (?)  */
			if (writeflag == MEM_READ) {
				bus_pci_data_access(cpu, d->pci_bus, &odata,
				    sizeof(uint32_t), MEM_READ);
			} else {
				d->pci_data_to_be_written = idata & 0xffff;
			}
		} else {
			fatal("i80321: unsupported pci data len %i (X)\n", len);
		}
		break;
	case VERDE_ATU_BASE + ATU_OCCDR + 2:
		/*  PCI data, 16-bit access, used by FreeBSD (?)  */

/*  TODO: 8-bit!  */

		if (len == 2) {
			if (writeflag == MEM_READ) {
				bus_pci_data_access(cpu, d->pci_bus, &odata,
				    sizeof(uint32_t), MEM_READ);
				odata >>= 16;
			} else {
				d->pci_data_to_be_written &= 0xffff;
				d->pci_data_to_be_written |= (idata & 0xffff) << 16;
				bus_pci_data_access(cpu, d->pci_bus,
				    &d->pci_data_to_be_written, sizeof(uint32_t),
				    MEM_WRITE);
			}
		} else {
			fatal("i80321: unsupported pci data len %i (Y)\n", len);
		}
		break;

	/*  Memory Controller Unit:  */
	case VERDE_MCU_BASE + MCU_SDBR:
		n = "MCU_SDBR";
		break;
	case VERDE_MCU_BASE + MCU_SBR0:
		n = "MCU_SBR0";
		break;
	case VERDE_MCU_BASE + MCU_SBR1:
		n = "MCU_SBR1";
		break;

	default:if (writeflag == MEM_READ) {
			fatal("[ i80321: read from 0x%x ]\n",
			    (int)relative_addr);
		} else {
			fatal("[ i80321: write to 0x%x: 0x%llx ]\n",
			    (int)relative_addr, (long long)idata);
		}
	}

	if (n != NULL) {
		if (writeflag == MEM_READ) {
			debug("[ i80321: read from %s ]\n", n);
		} else {
			debug("[ i80321: write to %s: 0x%llx ]\n",
			    n, (long long)idata);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  devinit_i80321():
 */
int devinit_i80321(struct devinit *devinit)
{
	struct i80321_data *d = malloc(sizeof(struct i80321_data));
	uint32_t memsize = devinit->machine->physical_ram_in_mb * 1048576;
	uint32_t base;

	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct i80321_data));

	d->mcu_reg[MCU_SDBR / sizeof(uint32_t)] = base = 0xa0000000;
	d->mcu_reg[MCU_SBR0 / sizeof(uint32_t)] = (base+memsize) >> 25;
	d->mcu_reg[MCU_SBR1 / sizeof(uint32_t)] = (base+memsize) >> 25;

	d->pci_bus = bus_pci_init(devinit->machine,
	    0 /*  TODO: pciirq  */,
	    0 /*  TODO: pci_io_offset  */,
	    0 /*  TODO: pci_mem_offset  */,
	    0x80000000 /*  TODO: pci_portbase  */,
	    0x80010000 /*  TODO: pci_membase  */,
	    0 /*  TODO: pci_irqbase  */,
	    0 /*  TODO: isa_portbase  */,
	    0 /*  TODO: isa_membase  */,
	    0 /*  TODO: isa_irqbase  */);

	memory_device_register(devinit->machine->memory, devinit->name,
	    devinit->addr, DEV_I80321_LENGTH,
	    dev_i80321_access, d, DM_DEFAULT, NULL);

	devinit->return_ptr = d->pci_bus;

	return 1;
}

