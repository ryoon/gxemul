/*
 *  Copyright (C) 2006  Anders Gavare.  All rights reserved.
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
 *  $Id: machine_qemu.c,v 1.9 2006-12-29 22:05:25 debug Exp $
 *
 *  This file contains semi-bogus machine descriptions for experimental
 *  machines, mimicing those emulated by Fabrice Bellard's QEMU.
 *
 *  See e.g. http://fabrice.bellard.free.fr/qemu/mips-test-0.1.tar.gz
 *  (available from http://fabrice.bellard.free.fr/qemu/download.html).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bus_isa.h"
#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "machine.h"
#include "machine_interrupts.h"
#include "memory.h"
#include "misc.h"


/*****************************************************************************/


MACHINE_SETUP(qemu_arm)
{
	/*
	 *  The ARM machine in QEMU isn't really a bogus machine, I think.
	 *  It is supposed to emulate a specific ARM board. But for now,
	 *  treat it as "QEMU ARM".
	 */

	machine->machine_name = "QEMU ARM";
	cpu->byte_order = EMUL_LITTLE_ENDIAN;

	device_add(machine, "ns16550 addr=0x16000000");

	if (!machine->prom_emulation)
		return;

	/*
	 *  QEMU's ARM emulation mode uses the following hardcoded addresses:
	 *
	 *	0x00000100 = kernel boot arguments
	 *	0x00010000 = kernel load address
	 *	0x00800000 = initrd load address
	 *
	 *  QEMU's arm_boot.c puts these values at 0x100:
	 *
	 *  TODO: Yuck. Don't hardcode these.
	 */

	store_32bit_word(cpu, 0x100, 5);
	store_32bit_word(cpu, 0x104, 0x54410001);
	store_32bit_word(cpu, 0x108, 1);
	store_32bit_word(cpu, 0x10c, 0x1000);
	store_32bit_word(cpu, 0x110, 0);
	store_32bit_word(cpu, 0x114, 4);
	store_32bit_word(cpu, 0x118, 0x54410002);
	store_32bit_word(cpu, 0x11c, machine->physical_ram_in_mb * 1048576);
	store_32bit_word(cpu, 0x120, 0);
	/*  TODO: 0x54410009 for the kernel command line args  */

	/*
	 *  board ids:
	 *	0x25e for "AB", 0x183 for "PB".
	 */

	cpu->cd.arm.r[0] = 0x5e;	/*  low byte of board id  */
	cpu->cd.arm.r[1] = 0x02;	/*  high byte of board id  */
	cpu->cd.arm.r[5] = 0x100;	/*  address of boot args  */
	cpu->cd.arm.r[6] = 0x10000;	/*  address of loaded kernel  */
}


MACHINE_DEFAULT_CPU(qemu_arm)
{
	/*  TODO: ARM926EJ-S  */
	machine->cpu_name = strdup("ARM922T");
}


MACHINE_DEFAULT_RAM(qemu_arm)
{
	machine->physical_ram_in_mb = 64;
}


MACHINE_REGISTER(qemu_arm)
{
	MR_DEFAULT(qemu_arm, "QEMU ARM", ARCH_ARM, MACHINE_QEMU_ARM);
	me->set_default_ram = machine_default_ram_qemu_arm;
	machine_entry_add_alias(me, "qemu_arm");
}


/*****************************************************************************/


MACHINE_SETUP(qemu_mips)
{
	machine->machine_name = "QEMU MIPS";
	cpu->byte_order = EMUL_BIG_ENDIAN;

	/*  An ISA bus, I/O ports at 0x14000000, memory at 0x10000000...  */
	bus_isa_init(machine, machine->path, BUS_ISA_IDE0 | BUS_ISA_IDE1,
	    0x14000000ULL, 0x10000000ULL);

	/*  ... and an ISA interrupt controller, connected to MIPS irq 2:  */

fatal("TODO: Legacy rewrite\n");
abort();
//	machine->md_interrupt = isa8_interrupt;
//	machine->isa_pic_data.native_irq = 2;

	if (!machine->prom_emulation)
		return;

	/*
	 *  Registers at startup do not seem to be defined in QEMU, but
	 *  bootargs and memory size are placed just below 16 MB.
	 *
	 *  Remember to start the emulator with options, e.g.:
	 *
	 *	-o "console=ttyS0 root=/dev/ram rd_start=0x80800000
	 *		rd_size=10000000 init=/bin/sh"
	 */

	store_string(cpu, (int32_t)(0x80000000 + 16*1048576 - 256),
	    machine->boot_string_argument);
	store_32bit_word(cpu, (int32_t)(0x80000000 + 16*1048576 - 260),
	    0x12345678);
	store_32bit_word(cpu, (int32_t)(0x80000000 + 16*1048576 - 264),
	    machine->physical_ram_in_mb * 1048576);
}


MACHINE_DEFAULT_CPU(qemu_mips)
{
	/*  QEMU emulates a MIPS32 rev 1, so 4Kc will do just fine.  */
	machine->cpu_name = strdup("4Kc");
}


MACHINE_DEFAULT_RAM(qemu_mips)
{
	machine->physical_ram_in_mb = 64;
}


MACHINE_REGISTER(qemu_mips)
{
	MR_DEFAULT(qemu_mips, "QEMU MIPS", ARCH_MIPS, MACHINE_QEMU_MIPS);
	me->set_default_ram = machine_default_ram_qemu_mips;
	machine_entry_add_alias(me, "qemu_mips");
}

