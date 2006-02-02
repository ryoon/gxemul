/*
 *  Copyright (C) 2003-2006  Anders Gavare.  All rights reserved.
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
 *  $Id: machine_cobalt.c,v 1.2 2006-02-02 19:30:14 debug Exp $
 */

#include <stdio.h>
#include <string.h>

#include "bus_pci.h"
#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "machine.h"
#include "machine_interrupts.h"
#include "memory.h"
#include "misc.h"


MACHINE_SETUP(cobalt)
{
	char tmpstr[500];
	struct pci_data *pci_data;
	struct memory *mem = machine->memory;

	cpu->byte_order = EMUL_LITTLE_ENDIAN;
	machine->machine_name = "Cobalt";
	machine->stable = 1;

	/*
	 *  Interrupts seem to be the following:
	 *  (according to http://www.funet.fi/pub/Linux/PEOPLE/Linus/v2.4/
	 *  patch-html/patch-2.4.19/linux-2.4.19_arch_mips_cobalt_irq.c.html)
	 *
	 *	2	Galileo chip (timer)
	 *	3	Tulip 0 + NCR SCSI
	 *	4	Tulip 1
	 *	5	16550 UART (serial console)
	 *	6	VIA southbridge PIC
	 *	7	PCI  (Note: Not used. The PCI controller
	 *		interrupts at ISA interrupt 9.)
	 */

	/*  ISA interrupt controllers:  */
	snprintf(tmpstr, sizeof(tmpstr), "8259 irq=24 addr=0x10000020");
	machine->isa_pic_data.pic1 = device_add(machine, tmpstr);
	snprintf(tmpstr, sizeof(tmpstr), "8259 irq=24 addr=0x100000a0");
	machine->isa_pic_data.pic2 = device_add(machine, tmpstr);
	machine->md_interrupt = isa8_interrupt;
	machine->isa_pic_data.native_irq = 6;

	dev_mc146818_init(machine, mem, 0x10000070, 0, MC146818_PC_CMOS, 4);

	machine->main_console_handle = (size_t) device_add(machine,
	    "ns16550 irq=5 addr=0x1c800000 name2=tty0 in_use=1");

	/*  TODO: bus_isa_init() ?  */

#if 0
	device_add(machine,
	    "ns16550 irq=0 addr=0x1f000010 name2=tty1 in_use=0");
#endif

	/*
	 *  According to NetBSD/cobalt:
	 *
	 *  pchb0 at pci0 dev 0 function 0: Galileo GT-64111 System Controller,
	 *	rev 1   (NOTE: added by dev_gt_init())
	 *  tlp0 at pci0 dev 7 function 0: DECchip 21143 Ethernet, pass 4.1
	 *  Symbios Logic 53c860 (SCSI mass storage, revision 0x02) at pci0
	 *	dev 8
	 *  pcib0 at pci0 dev 9 function 0, VIA Technologies VT82C586 (Apollo
	 *	VP) PCI-ISA Bridge, rev 37
	 *  pciide0 at pci0 dev 9 function 1: VIA Technologies VT82C586 (Apollo
	 *	VP) ATA33 cr
	 *  tlp1 at pci0 dev 12 function 0: DECchip 21143 Ethernet, pass 4.1
	 *
	 *  The PCI controller interrupts at ISA interrupt 9.
	 */
	pci_data = dev_gt_init(machine, mem, 0x14000000, 2, 8 + 9, 11);
	bus_pci_add(machine, pci_data, mem, 0,  7, 0, "dec21143");
	/*  bus_pci_add(machine, pci_data, mem, 0,  8, 0, "symbios_860");
	    PCI_VENDOR_SYMBIOS, PCI_PRODUCT_SYMBIOS_860  */
	bus_pci_add(machine, pci_data, mem, 0,  9, 0, "vt82c586_isa");
	bus_pci_add(machine, pci_data, mem, 0,  9, 1, "vt82c586_ide");
	bus_pci_add(machine, pci_data, mem, 0, 12, 0, "dec21143");

	if (!machine->prom_emulation)
		return;


	/*
	 *  NetBSD/cobalt expects memsize in a0, but it seems that what
	 *  it really wants is the end of memory + 0x80000000.
	 *
	 *  The bootstring is stored 512 bytes before the end of
	 *  physical ram.
	 */
	cpu->cd.mips.gpr[MIPS_GPR_A0] =
	    machine->physical_ram_in_mb * 1048576 + 0xffffffff80000000ULL;
	machine->bootstr = "root=/dev/hda1 ro";
	/*  bootstr = "nfsroot=/usr/cobalt/";  */
	/*  TODO: bootarg, and/or automagic boot device detection  */
	store_string(cpu, cpu->cd.mips.gpr[MIPS_GPR_A0] - 512,
	    machine->bootstr);
}


MACHINE_DEFAULT_CPU(cobalt)
{
	machine->cpu_name = strdup("RM5200");
}


MACHINE_REGISTER(cobalt)
{
	MR_DEFAULT(cobalt, "Cobalt", ARCH_MIPS, MACHINE_COBALT, 1, 0);
	me->aliases[0] = "cobalt";
	machine_entry_add(me, ARCH_MIPS);
}

