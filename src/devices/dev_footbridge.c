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
 *  $Id: dev_footbridge.c,v 1.48 2006-09-30 10:09:19 debug Exp $
 *
 *  Footbridge. Used in Netwinder and Cats.
 *
 *  TODO:
 *	o)  Add actual support for the fcom serial port.
 *	o)  FIQs.
 *	o)  Pretty much everything else as well :)  (This entire thing
 *	    is a quick hack to work primarily with NetBSD and OpenBSD
 *	    as a guest OS.)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bus_pci.h"
#include "console.h"
#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"
#include "timer.h"


#include "dc21285reg.h"

#define	DEV_FOOTBRIDGE_TICK_SHIFT	14
#define	DEV_FOOTBRIDGE_LENGTH		0x400


static void timer_tick0(struct timer *t, void *extra)
{ ((struct footbridge_data *)extra)->pending_timer_interrupts[0] ++; }
static void timer_tick1(struct timer *t, void *extra)
{ ((struct footbridge_data *)extra)->pending_timer_interrupts[1] ++; }
static void timer_tick2(struct timer *t, void *extra)
{ ((struct footbridge_data *)extra)->pending_timer_interrupts[2] ++; }
static void timer_tick3(struct timer *t, void *extra)
{ ((struct footbridge_data *)extra)->pending_timer_interrupts[3] ++; }


static void reload_timer_value(struct cpu *cpu, struct footbridge_data *d,
	int timer_nr)
{
	double freq = (double)cpu->machine->emulated_hz;
	int cycles = d->timer_load[timer_nr];

	if (d->timer_control[timer_nr] & TIMER_FCLK_16)
		cycles <<= 4;
	else if (d->timer_control[timer_nr] & TIMER_FCLK_256)
		cycles <<= 8;
	freq /= (double)cycles;

	d->timer_value[timer_nr] = d->timer_load[timer_nr];
	d->timer_tick_countdown[timer_nr] = 1;

	/*  printf("%i: %i -> %f Hz\n", timer_nr,
	    d->timer_load[timer_nr], freq);  */

	if (d->timer[timer_nr] == NULL) {
		switch (timer_nr) {
		case 0:	d->timer[0] = timer_add(freq, timer_tick0, d); break;
		case 1:	d->timer[1] = timer_add(freq, timer_tick1, d); break;
		case 2:	d->timer[2] = timer_add(freq, timer_tick2, d); break;
		case 3:	d->timer[3] = timer_add(freq, timer_tick3, d); break;
		}
	} else {
		timer_update_frequency(d->timer[timer_nr], freq);
	}
}


/*
 *  dev_footbridge_tick():
 *
 *  The 4 footbridge timers should decrease and cause interrupts. Periodic
 *  interrupts restart as soon as they are acknowledged, non-periodic
 *  interrupts need to be "reloaded" to restart.
 *
 *  TODO: Hm. I thought I had solved this, but it didn't quite work.
 *        This needs to be re-checked against documentation, sometime.
 */
void dev_footbridge_tick(struct cpu *cpu, void *extra)
{
	int i;
	struct footbridge_data *d = (struct footbridge_data *) extra;

	for (i=0; i<N_FOOTBRIDGE_TIMERS; i++) {
		if (d->timer_control[i] & TIMER_ENABLE) {
			if (d->pending_timer_interrupts[i] > 0) {
				d->timer_value[i] = random() % d->timer_load[i];
				cpu_interrupt(cpu, IRQ_TIMER_1 + i);
			}
		}
	}
}


/*
 *  dev_footbridge_isa_access():
 *
 *  Reading the byte at 0x79000000 is a quicker way to figure out which ISA
 *  interrupt has occurred (and acknowledging it at the same time), than
 *  dealing with the legacy 0x20/0xa0 ISA ports.
 */
DEVICE_ACCESS(footbridge_isa)
{
	/*  struct footbridge_data *d = extra;  */
	uint64_t idata = 0, odata = 0;
	int x;

	if (writeflag == MEM_WRITE) {
		idata = memory_readmax64(cpu, data, len);
		fatal("[ footbridge_isa: WARNING/TODO: write! ]\n");
	}

	x = cpu->machine->isa_pic_data.last_int;
	if (x == 0)
		cpu_interrupt_ack(cpu, 32 + x);

	if (x < 8)
		odata = cpu->machine->isa_pic_data.pic1->irq_base + x;
	else
		odata = cpu->machine->isa_pic_data.pic2->irq_base + x - 8;

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  Reset pin at ISA port 0x338, at least in the NetWinder:
 *
 *  TODO: NOT WORKING YET!
 */
DEVICE_ACCESS(footbridge_reset)
{
	uint64_t idata = 0;

	if (writeflag == MEM_WRITE) {
		idata = memory_readmax64(cpu, data, len);
		if (idata & 0x40) {
			debug("[ footbridge_reset: GP16: Halting. ]\n");
			cpu->running = 0;
exit(1);
		}
	}

	return 1;
}


/*
 *  dev_footbridge_pci_access():
 *
 *  The Footbridge PCI configuration space is implemented as a direct memory
 *  space (i.e. not one port for addr and one port for data). This function
 *  translates that into bus_pci calls.
 */
DEVICE_ACCESS(footbridge_pci)
{
	struct footbridge_data *d = extra;
	uint64_t idata = 0, odata = 0;
	int bus, dev, func, reg;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len|MEM_PCI_LITTLE_ENDIAN);

	/*  Decompose the (direct) address into its components:  */
	bus_pci_decompose_1(relative_addr, &bus, &dev, &func, &reg);
	bus_pci_setaddr(cpu, d->pcibus, bus, dev, func, reg);

	if (bus == 255) {
		fatal("[ footbridge DEBUG ERROR: bus 255 unlikely,"
		    " pc (might not be updated) = 0x%08x ]\n", (int)cpu->pc);
		exit(1);
	}

	debug("[ footbridge pci: %s bus %i, device %i, function %i, register "
	    "%i ]\n", writeflag == MEM_READ? "read from" : "write to", bus,
	    dev, func, reg);

	bus_pci_data_access(cpu, d->pcibus, writeflag == MEM_READ?
	    &odata : &idata, len, writeflag);

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len|MEM_PCI_LITTLE_ENDIAN, odata);

	return 1;
}


/*
 *  dev_footbridge_access():
 *
 *  The DC21285 registers.
 */
DEVICE_ACCESS(footbridge)
{
	struct footbridge_data *d = extra;
	uint64_t idata = 0, odata = 0;
	int timer_nr = 0;

	if (writeflag == MEM_WRITE)
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

	case 0x04:
	case 0x0c:
	case 0x10:
	case 0x14:
	case 0x18:
		/*  TODO. Written to by Linux.  */
		break;

	case REVISION:
		odata = 3;  /*  footbridge revision number  */
		break;

	case PCI_ADDRESS_EXTENSION:
		/*  TODO: Written to by Linux.  */
		if (writeflag == MEM_WRITE && idata != 0)
			fatal("[ footbridge: TODO: write to PCI_ADDRESS_"
			    "EXTENSION: 0x%llx ]\n", (long long)idata);
		break;

	case SA_CONTROL:
		/*  Read by Linux:  */
		odata = PCI_CENTRAL_FUNCTION;
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
			odata = d->irq_enable;
			fatal("[ WARNING: footbridge read from "
			    "ENABLE SET? ]\n");
			exit(1);
		}
		break;

	case IRQ_ENABLE_CLEAR:
		if (writeflag == MEM_WRITE) {
			d->irq_enable &= ~idata;
			cpu_interrupt(cpu, 64);
		} else {
			odata = d->irq_enable;
			fatal("[ WARNING: footbridge read from "
			    "ENABLE CLEAR? ]\n");
			exit(1);
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
			d->timer_load[timer_nr] = idata & TIMER_MAX_VAL;
			reload_timer_value(cpu, d, timer_nr);
			/*  debug("[ footbridge: timer %i (1-based), "
			    "value %i ]\n", timer_nr + 1,
			    (int)d->timer_value[timer_nr]);  */
			cpu_interrupt_ack(cpu, IRQ_TIMER_1 + timer_nr);
		}
		break;

	case TIMER_1_VALUE:
		if (writeflag == MEM_READ)
			odata = d->timer_value[timer_nr];
		else
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
				reload_timer_value(cpu, d, timer_nr);
			} else {
				d->pending_timer_interrupts[timer_nr] = 0;
			}
			cpu_interrupt_ack(cpu, IRQ_TIMER_1 + timer_nr);
		}
		break;

	case TIMER_1_CLEAR:
		if (d->timer_control[timer_nr] & TIMER_MODE_PERIODIC) {
			reload_timer_value(cpu, d, timer_nr);
		}

		if (d->pending_timer_interrupts[timer_nr] > 0) {
			d->pending_timer_interrupts[timer_nr] --;
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


DEVINIT(footbridge)
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
	    dev_footbridge_access, d, DM_DEFAULT, NULL);

	/*  ISA interrupt status/acknowledgement:  */
	memory_device_register(devinit->machine->memory, "footbridge_isa",
	    0x79000000, 8, dev_footbridge_isa_access, d, DM_DEFAULT, NULL);

	/*  The "fcom" console:  */
	d->console_handle = console_start_slave(devinit->machine, "fcom", 0);

	/*  A PCI bus:  */
	d->pcibus = bus_pci_init(
	    devinit->machine,
	    devinit->irq_nr,	/*  PCI controller irq  */
	    0x7c000000,		/*  PCI device io offset  */
	    0x80000000,		/*  PCI device mem offset  */
	    0x00000000,		/*  PCI port base  */
	    0x00000000,		/*  PCI mem base  */
	    0,			/*  PCI irq base: TODO  */
	    0x7c000000,		/*  ISA port base  */
	    0x80000000,		/*  ISA mem base  */
	    32);		/*  ISA port base  */

	/*  ... with some default devices for known machine types:  */
	switch (devinit->machine->machine_type) {
	case MACHINE_CATS:
		bus_pci_add(devinit->machine, d->pcibus,
		    devinit->machine->memory, 0xc0, 7, 0, "ali_m1543");
		bus_pci_add(devinit->machine, d->pcibus,
		    devinit->machine->memory, 0xc0, 10, 0, "dec21143");
		bus_pci_add(devinit->machine, d->pcibus,
		    devinit->machine->memory, 0xc0, 16, 0, "ali_m5229");
		break;
	case MACHINE_NETWINDER:
		bus_pci_add(devinit->machine, d->pcibus,
		    devinit->machine->memory, 0xc0, 11, 0, "symphony_83c553");
		bus_pci_add(devinit->machine, d->pcibus,
		    devinit->machine->memory, 0xc0, 11, 1, "symphony_82c105");
		memory_device_register(devinit->machine->memory,
		    "footbridge_reset", 0x7c000338, 1,
		    dev_footbridge_reset_access, d, DM_DEFAULT, NULL);
		break;
	default:fatal("footbridge: unimplemented machine type.\n");
		exit(1);
	}

	/*  PCI configuration space:  */
	memory_device_register(devinit->machine->memory,
	    "footbridge_pci", pci_addr, 0x1000000,
	    dev_footbridge_pci_access, d, DM_DEFAULT, NULL);

	/*  Timer ticks:  */
	for (i=0; i<N_FOOTBRIDGE_TIMERS; i++) {
		d->timer_control[i] = TIMER_MODE_PERIODIC;
		d->timer_load[i] = TIMER_MAX_VAL;
	}
	machine_add_tickfunction(devinit->machine,
	    dev_footbridge_tick, d, DEV_FOOTBRIDGE_TICK_SHIFT, 0.0);

	devinit->return_ptr = d;
	return 1;
}

