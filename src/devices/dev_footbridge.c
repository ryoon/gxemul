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
 *  $Id: dev_footbridge.c,v 1.10 2005-09-30 13:33:01 debug Exp $
 *
 *  Footbridge. Used in Netwinder and Cats.
 *
 *  TODO.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bus_pci.h"
#include "cpu.h"
#include "device.h"
#include "devices.h"	/*  for struct footbridge_data  */
#include "machine.h"
#include "memory.h"
#include "misc.h"

#include "dc21285reg.h"

#define	DEV_FOOTBRIDGE_TICK_SHIFT	14
#define	DEV_FOOTBRIDGE_LENGTH		0x1000		/*  TODO  */


/*
 *  dev_footbridge_tick():
 */
void dev_footbridge_tick(struct cpu *cpu, void *extra)
{
	struct footbridge_data *d = (struct footbridge_data *) extra;

	d->timer1_value += 100;
	d->timer3_value += 100;
{
	static int x = 0;
	x++;
	if (x > 2399 && d->timer_tick_countdown-- < 0 &&
	    d->irq_enable & 0x10) {
		cpu_interrupt(cpu, 4);
	} else {
		cpu_interrupt_ack(cpu, 4);
	}
}
}


/*
 *  dev_footbridge_pci_cfg_access():
 */
int dev_footbridge_pci_cfg_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	struct footbridge_data *d = extra;
	uint64_t idata = 0, odata = 0;
	int bus, device, function, regnr, res;
	uint64_t pci_word;

	idata = memory_readmax64(cpu, data, len);

	bus      = (relative_addr >> 16) & 0xff;
	device   = (relative_addr >> 11) & 0x1f;
	function = (relative_addr >> 8) & 0x7;
	regnr    = relative_addr & 0xff;

	debug("[ footbridge_pci_cfg: %s bus %i, device %i, function "
	    "%i, register %i ]\n", writeflag == MEM_READ? "read from"
	    : "write to", bus, device, function, regnr);

	if (d->pcibus == NULL) {
		fatal("dev_footbridge_pci_cfg_access(): no PCI bus?\n");
		return 0;
	}

	pci_word = relative_addr & 0x00ffffff;

	res = bus_pci_access(cpu, mem, BUS_PCI_ADDR,
	    &pci_word, MEM_WRITE, d->pcibus);
	if (writeflag == MEM_READ) {
		res = bus_pci_access(cpu, mem, BUS_PCI_DATA,
		    &pci_word, MEM_READ, d->pcibus);
		odata = pci_word;
	} else {
		pci_word = idata;
		res = bus_pci_access(cpu, mem, BUS_PCI_DATA,
		    &pci_word, MEM_WRITE, d->pcibus);
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_footbridge_access():
 */
int dev_footbridge_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	struct footbridge_data *d = extra;
	uint64_t idata = 0, odata = 0;

	idata = memory_readmax64(cpu, data, len);

	switch (relative_addr) {

	case VENDOR_ID:
		odata = 0x1011;  /*  DC21285_VENDOR_ID  */
		break;

	case DEVICE_ID:
		odata = 0x1065;  /*  DC21285_DEVICE_ID  */
		break;

	case REVISION:
		odata = 3;  /*  footbridge revision number  */
		break;

	case IRQ_STATUS:
		if (writeflag == MEM_READ)
			odata = d->irq_status;
		else {
			fatal("[ WARNING: footbridge write to irq status? ]\n");
			exit(1);
			d->irq_status = idata;
			cpu_interrupt(cpu, 64);
		}
		break;

	case IRQ_ENABLE_SET:
		if (writeflag == MEM_WRITE) {
			d->irq_enable |= idata;
			cpu_interrupt(cpu, 64);
		} else {
			fatal("[ WARNING: footbridge read from "
			    "ENABLE SET? ]\n");
			exit(1);
			odata = d->irq_enable;
		}
		break;

	case IRQ_ENABLE_CLEAR:
		if (writeflag == MEM_WRITE) {
			d->irq_enable &= ~idata;
			cpu_interrupt(cpu, 64);
		} else {
			fatal("[ WARNING: footbridge read from "
			    "ENABLE CLEAR? ]\n");
			exit(1);
			odata = d->irq_enable;
		}
		break;

	case FIQ_STATUS:
		if (writeflag == MEM_READ)
			odata = d->fiq_status;
		else
			d->fiq_status = idata;
		break;

	case FIQ_ENABLE_SET:
		if (writeflag == MEM_WRITE)
			d->fiq_enable |= idata;
		break;

	case FIQ_ENABLE_CLEAR:
		if (writeflag == MEM_WRITE)
			d->fiq_enable &= ~idata;
		break;

	case TIMER_1_VALUE:
		d->timer1_value += 100;
		if (writeflag == MEM_READ)
			odata = d->timer1_value;
		else
			d->timer1_value = idata;
		break;

	case TIMER_1_CONTROL:
		if (writeflag == MEM_READ)
			odata = d->timer1_control;
		else
			d->timer1_control = idata;
		break;

	case TIMER_1_CLEAR:
		d->timer_tick_countdown = 2;
		cpu_interrupt_ack(cpu, 4);
		break;

	case TIMER_3_VALUE:
		d->timer3_value += 100;
		if (writeflag == MEM_READ)
			odata = d->timer3_value;
		else
			d->timer3_value = idata;
		break;

	case TIMER_3_CLEAR:
		d->timer3_value = 0;
		break;

	default:if (writeflag == MEM_READ) {
			fatal("[ footbridge: read from 0x%x ]\n",
			    (int)relative_addr);
		} else {
			fatal("[ footbridge: write to 0x%x: 0x%llx ]\n",
			    (int)relative_addr, (long long)idata);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  devinit_footbridge():
 */
int devinit_footbridge(struct devinit *devinit)
{
	struct footbridge_data *d;
	uint64_t pci_cfg_addr = 0x7b000000;

	d = malloc(sizeof(struct footbridge_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct footbridge_data));

	memory_device_register(devinit->machine->memory, devinit->name,
	    devinit->addr, DEV_FOOTBRIDGE_LENGTH,
	    dev_footbridge_access, d, MEM_DEFAULT, NULL);

	d->pcibus = bus_pci_init(devinit->irq_nr);

	switch (devinit->machine->machine_type) {
	case MACHINE_CATS:
		bus_pci_add(devinit->machine, d->pcibus,
		    devinit->machine->memory, 0xc0, 7, 0,
		    pci_ali_m1543_init, pci_ali_m1543_rr);
		bus_pci_add(devinit->machine, d->pcibus,
		    devinit->machine->memory, 0xc0, 16, 0,
		    pci_ali_m5229_init, pci_ali_m5229_rr);
		break;
	case MACHINE_NETWINDER:
		bus_pci_add(devinit->machine, d->pcibus,
		    devinit->machine->memory, 0xc0, 11, 0,
		    pci_symphony_83c553_init, pci_symphony_83c553_rr);
		bus_pci_add(devinit->machine, d->pcibus,
		    devinit->machine->memory, 0xc0, 11, 1,
		    pci_symphony_82c105_init, pci_symphony_82c105_rr);
		break;
	default:fatal("footbridge: unimplemented machine type.\n");
		exit(1);
	}

	memory_device_register(devinit->machine->memory,
	    "footbridge_pci_cfg", pci_cfg_addr, 0x1000000,
	    dev_footbridge_pci_cfg_access, d, MEM_DEFAULT, NULL);

	machine_add_tickfunction(devinit->machine,
	    dev_footbridge_tick, d, DEV_FOOTBRIDGE_TICK_SHIFT);

	devinit->return_ptr = d;
	return 1;
}

