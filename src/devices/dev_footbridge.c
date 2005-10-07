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
 *  $Id: dev_footbridge.c,v 1.22 2005-10-07 15:10:02 debug Exp $
 *
 *  Footbridge. Used in Netwinder and Cats.
 *
 *  TODO: Most things. For example:
 *
 *	o)  Add actual support for the fcom serial port.
 *	o)  FIQs.
 *	o)  Lots of other things.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bus_pci.h"
#include "console.h"
#include "cpu.h"
#include "device.h"
#include "devices.h"	/*  for struct footbridge_data  */
#include "machine.h"
#include "memory.h"
#include "misc.h"


#include "dc21285reg.h"

#define	DEV_FOOTBRIDGE_TICK_SHIFT	14
#define	DEV_FOOTBRIDGE_LENGTH		0x400


/*
 *  dev_footbridge_tick():
 *
 *  The 4 footbridge timers should decrease every now and then, and cause
 *  interrupts. Periodic interrupts restart as soon as they are acknowledged,
 *  non-periodic interrupts need to be "reloaded" to restart.
 */
void dev_footbridge_tick(struct cpu *cpu, void *extra)
{
	int i;
	struct footbridge_data *d = (struct footbridge_data *) extra;

	for (i=0; i<N_FOOTBRIDGE_TIMERS; i++) {
		int amount = 1 << DEV_FOOTBRIDGE_TICK_SHIFT;
		if (d->timer_control[i] & TIMER_FCLK_16)
			amount >>= 4;
		else if (d->timer_control[i] & TIMER_FCLK_256)
			amount >>= 8;

		if (d->timer_tick_countdown[i] == 0)
			continue;

		if (d->timer_value[i] > amount)
			d->timer_value[i] -= amount;
		else
			d->timer_value[i] = 0;

		if (d->timer_value[i] == 0) {
			d->timer_tick_countdown[i] --;
			if (d->timer_tick_countdown[i] > 0)
				continue;

			if (d->timer_control[i] & TIMER_ENABLE)
				cpu_interrupt(cpu, IRQ_TIMER_1 + i);
			d->timer_tick_countdown[i] = 0;
		}
	}
}


/*
 *  dev_footbridge_isa_access():
 *
 *  NetBSD seems to read 0x79000000 to find out which ISA interrupt occurred,
 *  a quicker way than dealing with legacy 0x20/0xa0 ISA ports.
 */
int dev_footbridge_isa_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	/*  struct footbridge_data *d = extra;  */
	uint64_t idata = 0, odata = 0;
	int x;

	idata = memory_readmax64(cpu, data, len);

	if (writeflag == MEM_WRITE)
		fatal("[ footbridge_isa: WARNING/TODO: write! ]\n");

	/*
	 *  NetBSD seems to want a value of 0x20 + x, where x is the highest
	 *  priority ISA interrupt which is currently asserted and not masked.
	 */

	for (x=0; x<16; x++) {
		if (x == 2)
			continue;
		if (x < 8 && (cpu->machine->isa_pic_data.pic1->irr &
		    ~cpu->machine->isa_pic_data.pic1->ier &
		    (1 << x)))
			break;
		if (x >= 8 && (cpu->machine->isa_pic_data.pic2->irr &
		    ~cpu->machine->isa_pic_data.pic2->ier &
		    (1 << (x&7))))
			break;
	}

	if (x == 16)
		fatal("_\n_  SPORADIC but INVALID ISA interrupt\n_\n");

	odata = 0x20 + (x & 15);

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_footbridge_pci_access():
 *
 *  The Footbridge PCI configuration space is not implemented as "address +
 *  data port" pair, but instead a 24-bit (16 MB) chunk of physical memory
 *  decodes as the address. This function translates that into bus_pci_access
 *  calls.
 */
int dev_footbridge_pci_access(struct cpu *cpu, struct memory *mem,
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

	if (bus == 255) {
		fatal("[ footbridge DEBUG ERROR: bus 255 unlikely,"
		    " pc (might not be updated) = 0x%08x ]\n", (int)cpu->pc);
		exit(1);
	}

	debug("[ footbridge_pci: %s bus %i, device %i, function "
	    "%i, register %i ]\n", writeflag == MEM_READ? "read from"
	    : "write to", bus, device, function, regnr);

	if (d->pcibus == NULL) {
		fatal("dev_footbridge_pci_access(): no PCI bus?\n");
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
 *
 *  The DC21285 registers.
 */
int dev_footbridge_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	struct footbridge_data *d = extra;
	uint64_t idata = 0, odata = 0;
	int timer_nr = 0;

	idata = memory_readmax64(cpu, data, len);

	if (relative_addr >= TIMER_1_LOAD && relative_addr <= TIMER_4_CLEAR) {
		timer_nr = (relative_addr >> 5) & (N_FOOTBRIDGE_TIMERS - 1);
		relative_addr &= ~0x060;
	}

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

	case UART_DATA:
		if (writeflag == MEM_WRITE)
			console_putchar(d->console_handle, idata);
		break;

	case UART_RX_STAT:
		/*  TODO  */
		odata = 0;
		break;

	case UART_FLAGS:
		odata = UART_TX_EMPTY;
		break;

	case IRQ_STATUS:
		if (writeflag == MEM_READ)
			odata = d->irq_status & d->irq_enable;
		else {
			fatal("[ WARNING: footbridge write to irq status? ]\n");
			exit(1);
		}
		break;

	case IRQ_RAW_STATUS:
		if (writeflag == MEM_READ)
			odata = d->irq_status;
		else {
			fatal("[ footbridge write to irq_raw_status ]\n");
			exit(1);
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
			odata = d->fiq_status & d->fiq_enable;
		else {
			fatal("[ WARNING: footbridge write to fiq status? ]\n");
			exit(1);
		}
		break;

	case FIQ_RAW_STATUS:
		if (writeflag == MEM_READ)
			odata = d->fiq_status;
		else {
			fatal("[ footbridge write to fiq_raw_status ]\n");
			exit(1);
		}
		break;

	case FIQ_ENABLE_SET:
		if (writeflag == MEM_WRITE)
			d->fiq_enable |= idata;
		break;

	case FIQ_ENABLE_CLEAR:
		if (writeflag == MEM_WRITE)
			d->fiq_enable &= ~idata;
		break;

	case TIMER_1_LOAD:
		if (writeflag == MEM_READ)
			odata = d->timer_load[timer_nr];
		else {
			d->timer_value[timer_nr] =
			    d->timer_load[timer_nr] = idata & TIMER_MAX_VAL;
			debug("[ footbridge: timer %i (1-based), value %i ]\n",
			    timer_nr + 1, (int)d->timer_value[timer_nr]);
			d->timer_tick_countdown[timer_nr] = 1;
			cpu_interrupt_ack(cpu, IRQ_TIMER_1 + timer_nr);
		}
		break;

	case TIMER_1_VALUE:
		if (writeflag == MEM_READ) {
			dev_footbridge_tick(cpu, d);
			odata = d->timer_value[timer_nr];
		} else
			d->timer_value[timer_nr] = idata & TIMER_MAX_VAL;
		break;

	case TIMER_1_CONTROL:
		if (writeflag == MEM_READ)
			odata = d->timer_control[timer_nr];
		else {
			d->timer_control[timer_nr] = idata;
			if (idata & TIMER_FCLK_16 &&
			    idata & TIMER_FCLK_256) {
				fatal("TODO: footbridge timer: "
				    "both 16 and 256?\n");
				exit(1);
			}
			if (idata & TIMER_ENABLE) {
				d->timer_value[timer_nr] =
				    d->timer_load[timer_nr];
				d->timer_tick_countdown[timer_nr] = 1;
			}
			cpu_interrupt_ack(cpu, IRQ_TIMER_1 + timer_nr);
		}
		break;

	case TIMER_1_CLEAR:
		if (d->timer_control[timer_nr] & TIMER_MODE_PERIODIC) {
			d->timer_value[timer_nr] = d->timer_load[timer_nr];
			d->timer_tick_countdown[timer_nr] = 1;
		}
		cpu_interrupt_ack(cpu, IRQ_TIMER_1 + timer_nr);
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
	uint64_t pci_addr = 0x7b000000;
	int i;

	d = malloc(sizeof(struct footbridge_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct footbridge_data));

	/*  DC21285 register access:  */
	memory_device_register(devinit->machine->memory, devinit->name,
	    devinit->addr, DEV_FOOTBRIDGE_LENGTH,
	    dev_footbridge_access, d, MEM_DEFAULT, NULL);

	/*  ISA interrupt status word:  */
	memory_device_register(devinit->machine->memory, "footbridge_isa",
	    0x79000000, 8, dev_footbridge_isa_access, d, MEM_DEFAULT, NULL);

	/*  The "fcom" console:  */
	d->console_handle = console_start_slave(devinit->machine, "fcom");

	/*  A PCI bus:  */
	d->pcibus = bus_pci_init(devinit->irq_nr);

	/*  ... with some default devices for known machine types:  */
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

	/*  PCI configuration space:  */
	memory_device_register(devinit->machine->memory,
	    "footbridge_pci", pci_addr, 0x1000000,
	    dev_footbridge_pci_access, d, MEM_DEFAULT, NULL);

	/*  Timer ticks:  */
	for (i=0; i<N_FOOTBRIDGE_TIMERS; i++) {
		d->timer_control[i] = TIMER_MODE_PERIODIC;
		d->timer_load[i] = TIMER_MAX_VAL;
	}
	machine_add_tickfunction(devinit->machine,
	    dev_footbridge_tick, d, DEV_FOOTBRIDGE_TICK_SHIFT);

	devinit->return_ptr = d;
	return 1;
}

