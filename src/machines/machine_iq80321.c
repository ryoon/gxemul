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
 *  $Id: machine_iq80321.c,v 1.10 2006-02-04 12:27:14 debug Exp $
 */

#include <stdio.h>
#include <string.h>

#include "bus_pci.h"
#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"


MACHINE_SETUP(iq80321)
{
	struct pci_data *pci;

	/*
	 *  Intel IQ80321. See http://sources.redhat.com/ecos/
	 *	docs-latest/redboot/iq80321.html
	 *  for more details about the memory map.
	 */

	machine->machine_name = "Intel IQ80321";

	cpu->cd.arm.coproc[6] = arm_coproc_i80321_6;

	device_add(machine, "ns16550 irq=0 addr=0xfe800000 in_use=1");

#if 0
{
int i;
for (i=0; i<1048576*16; i+=4)
	store_32bit_word(cpu, i, 0x70000000 + i);
}
#endif

#if 0
	/*  Used by "Redboot":  */
	dev_ram_init(machine, 0xa800024, 4, DEV_RAM_RAM, 0);
	store_32bit_word(cpu, 0xa800024, 0x7fff);
	device_add(machine,
	    "ns16550 irq=0 addr=0x0d800000 addr_mult=4 in_use=0");
	device_add(machine,
	    "ns16550 irq=0 addr=0x0d800020 addr_mult=4 in_use=0");
#endif

	/*  0xa0000000 = physical ram, 0xc0000000 = uncached  */
	dev_ram_init(machine, 0xa0000000, 0x20000000, DEV_RAM_MIRROR, 0x0);
	dev_ram_init(machine, 0xc0000000, 0x20000000, DEV_RAM_MIRROR, 0x0);

	/*  0xe0000000 and 0xff000000 = cache flush regions  */
	dev_ram_init(machine, 0xe0000000, 0x100000, DEV_RAM_RAM, 0x0);
	dev_ram_init(machine, 0xff000000, 0x100000, DEV_RAM_RAM, 0x0);

	device_add(machine, "iq80321_7seg addr=0xfe840000");

	pci = device_add(machine, "i80321 addr=0xffffe000");

	/*
	 *  PCI device 0: should be "Intel i82546EB 1000BASE-T Ethernet" (TODO)
	 *  PCI device 1: "Intel 31244 Serial ATA Controller"
	 */
	bus_pci_add(machine, pci, machine->memory, 0, 0, 0, "dec21143");
	bus_pci_add(machine, pci, machine->memory, 0, 1, 0, "i31244");

	if (!machine->prom_emulation)
		return;

	arm_setup_initial_translation_table(cpu, 0x4000);
	arm_translation_table_set_l1(cpu, 0xa0000000, 0xa0000000);
	arm_translation_table_set_l1(cpu, 0xc0000000, 0xa0000000);
	arm_translation_table_set_l1(cpu, 0xe0000000, 0xe0000000);
	arm_translation_table_set_l1(cpu, 0xf0000000, 0xf0000000);
}


MACHINE_DEFAULT_CPU(iq80321)
{
	machine->cpu_name = strdup("80321_600_B0");
}


MACHINE_REGISTER(iq80321)
{
	MR_DEFAULT(iq80321, "Intel IQ80321", ARCH_ARM, MACHINE_IQ80321,
	    1, 0);
	me->aliases[0] = "iq80321";
	machine_entry_add(me, ARCH_ARM);
}

