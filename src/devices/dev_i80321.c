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
 *  $Id: dev_i80321.c,v 1.9 2006-02-09 20:02:59 debug Exp $
 *
 *  Intel i80321 (ARM) core functionality.
 *
 *  TODO: This is mostly just a dummy device.
 *  TODO 2: This is hardcoded for little endian emulation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bus_pci.h"
#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"


#include "i80321reg.h"

#define	DEV_I80321_LENGTH		VERDE_PMMR_SIZE


/*
 *  dev_i80321_access():
 */
DEVICE_ACCESS(i80321)
{
	struct i80321_data *d = extra;
	uint64_t idata = 0, odata = 0;
	char *n = NULL;
	int i, bus, dev, func, reg;

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
	case VERDE_ATU_BASE + ATU_OCCDR + 1:
	case VERDE_ATU_BASE + ATU_OCCDR + 2:
	case VERDE_ATU_BASE + ATU_OCCDR + 3:
		/*  PCI data  */
		if (writeflag == MEM_READ) {
			uint64_t tmp;
			bus_pci_data_access(cpu, d->pci_bus, &tmp,
			    sizeof(uint32_t), MEM_READ);
			switch (relative_addr) {
			case VERDE_ATU_BASE + ATU_OCCDR + 1:
				odata = tmp >> 8; break;
			case VERDE_ATU_BASE + ATU_OCCDR + 2:
				odata = tmp >> 16; break;
			case VERDE_ATU_BASE + ATU_OCCDR + 3:
				odata = tmp >> 24; break;
			default:odata = tmp;
			}
		} else {
			uint64_t tmp;
			int r = relative_addr - (VERDE_ATU_BASE + ATU_OCCDR);
			bus_pci_data_access(cpu, d->pci_bus, &tmp,
			    sizeof(uint32_t), MEM_READ);
			for (i=0; i<len; i++) {
				uint8_t b = idata >> (i*8);
				tmp &= ~(0xff << ((r+i)*8));
				tmp |= b << ((r+i)*8);
			}
			bus_pci_data_access(cpu, d->pci_bus, &tmp,
			    sizeof(uint32_t), MEM_WRITE);
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


DEVINIT(i80321)
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

	devinit->return_ptr = d;

	return 1;
}

