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
 *  $Id: machine_netwinder.c,v 1.3 2006-06-24 10:19:19 debug Exp $
 */

#include <stdio.h>
#include <string.h>

#include "bus_isa.h"
#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "machine.h"
#include "machine_interrupts.h"
#include "memory.h"
#include "misc.h"


MACHINE_SETUP(netwinder)
{
	machine->machine_name = "NetWinder";

	if (machine->physical_ram_in_mb > 256)
		fprintf(stderr, "WARNING! Real NetWinders cannot"
		    " have more than 256 MB RAM. Continuing anyway.\n");

	machine->md_int.footbridge_data =
	    device_add(machine, "footbridge addr=0x42000000");
	machine->md_interrupt = isa32_interrupt;
	machine->isa_pic_data.native_irq = 11;

	bus_isa_init(machine, 0, 0x7c000000, 0x80000000, 32, 48);

#if 0
	snprintf(tmpstr, sizeof(tmpstr), "8259 irq=64 addr=0x7c000020");
	machine->isa_pic_data.pic1 = device_add(machine, tmpstr);
	snprintf(tmpstr, sizeof(tmpstr), "8259 irq=64 addr=0x7c0000a0");
	machine->isa_pic_data.pic2 = device_add(machine, tmpstr);

	device_add(machine, "ns16550 irq=36 addr=0x7c0003f8 name2=com0");
	device_add(machine, "ns16550 irq=35 addr=0x7c0002f8 name2=com1");

	dev_vga_init(machine, machine->memory, 0x800a0000ULL, 0x7c0003c0,
	    machine->machine_name);
	j = dev_pckbc_init(machine, machine->memory, 0x7c000060, PCKBC_8042,
	    32 + 1, 32 + 12, machine->use_x11, 0);
	machine->main_console_handle = j;
#endif

	if (machine->use_x11) {
		bus_pci_add(machine, machine->md_int.footbridge_data->pcibus,
		    machine->memory, 0xc0, 8, 0, "igsfb");
	}

	if (!machine->prom_emulation)
		return;

	arm_setup_initial_translation_table(cpu, 0x4000);
}


MACHINE_DEFAULT_CPU(netwinder)
{
	machine->cpu_name = strdup("SA110");
}


MACHINE_DEFAULT_RAM(netwinder)
{
	machine->physical_ram_in_mb = 16;
}


MACHINE_REGISTER(netwinder)
{
	MR_DEFAULT(netwinder, "NetWinder", ARCH_ARM, MACHINE_NETWINDER);

	machine_entry_add_alias(me, "netwinder");

	me->set_default_ram = machine_default_ram_netwinder;
}

