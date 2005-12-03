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
 *  $Id: dev_cpc700.c,v 1.6 2005-12-03 04:14:14 debug Exp $
 *  
 *  IBM CPC700 bridge; PCI and interrupt controller.
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

#include "cpc700reg.h"


/*
 *  dev_cpc700_pci_access():
 *
 *  Passes PCI indirect addr and data accesses onto bus_pci.
 */
int dev_cpc700_pci_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	uint64_t idata = 0, odata = 0;
	int bus, dev, func, reg;
	struct cpc700_data *d = extra;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len|MEM_PCI_LITTLE_ENDIAN);

	switch (relative_addr) {
	case 0:	/*  Address:  */
		bus_pci_decompose_1(idata, &bus, &dev, &func, &reg);
		bus_pci_setaddr(cpu, d->pci_data, bus, dev, func, reg);
		break;

	case 4:	/*  Data:  */
		bus_pci_data_access(cpu, d->pci_data, writeflag == MEM_READ?
		    &odata : &idata, len, writeflag);
		break;
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len|MEM_PCI_LITTLE_ENDIAN, odata);

	return 1;
}


/*
 *  dev_cpc700_int_access():
 *
 *  The interrupt controller.
 */
int dev_cpc700_int_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	struct cpc700_data *d = extra;
	uint64_t idata = 0, odata = 0;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);

	switch (relative_addr) {

	case CPC_UIC_SR:
		/*  Status register (cleared by writing ones):  */
		if (writeflag == MEM_READ)
			odata = d->sr;
		else
			d->sr &= ~idata;
		break;

	case CPC_UIC_SRS:
		/*  Status register set:  */
		if (writeflag == MEM_READ) {
			fatal("[ cpc700_int: read from CPC_UIC_SRS? ]\n");
			odata = d->sr;
		} else
			d->sr = idata;
		break;

	case CPC_UIC_ER:
		/*  Enable register:  */
		if (writeflag == MEM_READ)
			odata = d->er;
		else
			d->er = idata;
		break;

	case CPC_UIC_MSR:
		/*  Masked status:  */
		if (writeflag == MEM_READ)
			odata = d->sr & d->er;
		else
			fatal("[ cpc700_int: write to CPC_UIC_MSR? ]\n");
		break;

	default:if (writeflag == MEM_WRITE) {
			fatal("[ cpc700_int: unimplemented write to "
			    "offset 0x%x: data=0x%x ]\n", (int)
			    relative_addr, (int)idata);
		} else {
			fatal("[ cpc700_int: unimplemented read from "
			    "offset 0x%x ]\n", (int)relative_addr);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_cpc700_init():
 */
struct cpc700_data *dev_cpc700_init(struct machine *machine, struct memory *mem)
{
	struct cpc700_data *d;
	char tmp[300];

	d = malloc(sizeof(struct cpc700_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct cpc700_data));

	/*  Register a PCI bus:  */
	d->pci_data = bus_pci_init(
	    machine,
	    0			/*  pciirq: TODO  */,
	    0,			/*  pci device io offset  */
	    0,			/*  pci device mem offset  */
	    CPC_PCI_IO_BASE,	/*  PCI portbase  */
	    CPC_PCI_MEM_BASE,	/*  PCI membase: TODO  */
	    0,			/*  PCI irqbase: TODO  */
	    0,			/*  ISA portbase: TODO  */
	    0,			/*  ISA membase: TODO  */
	    0);			/*  ISA irqbase: TODO  */

	switch (machine->machine_type) {
	case MACHINE_PMPPC:
		bus_pci_add(machine, d->pci_data, mem, 0, 0, 0,
		    "heuricon_pmppc");
		break;
	default:fatal("!\n! WARNING: cpc700 for non-implemented machine"
		    " type\n!\n");
		exit(1);
	}

	/*  PCI configuration registers:  */
	memory_device_register(mem, "cpc700_pci", CPC_PCICFGADR, 8,
	    dev_cpc700_pci_access, d, DM_DEFAULT, NULL);

	/*  Interrupt controller:  */
	memory_device_register(mem, "cpc700_int", CPC_UIC_BASE, CPC_UIC_SIZE,
	    dev_cpc700_int_access, d, DM_DEFAULT, NULL);

	/*  Two serial ports:  */
	snprintf(tmp, sizeof(tmp), "ns16550 irq=%i addr=0x%llx name2=tty0",
	    31 - CPC_IB_UART_0, (long long)CPC_COM0);
	machine->main_console_handle = (size_t)device_add(machine, tmp);
	snprintf(tmp, sizeof(tmp), "ns16550 irq=%i addr=0x%llx name2=tty1",
	    31 - CPC_IB_UART_1, (long long)CPC_COM1);
	device_add(machine, tmp);

	return d;
}

