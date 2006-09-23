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
 *  $Id: machine_evbmips.c,v 1.11 2006-09-23 03:52:10 debug Exp $
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bus_isa.h"
#include "bus_pci.h"
#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "machine.h"
#include "machine_interrupts.h"
#include "memory.h"
#include "misc.h"

#include "maltareg.h"


MACHINE_SETUP(evbmips)
{
	char tmpstr[1000];
	struct pci_data *pci_data;
	int i;

	/*  See http://www.netbsd.org/Ports/evbmips/ for more info.  */

	switch (machine->machine_subtype) {
	case MACHINE_EVBMIPS_MALTA:
	case MACHINE_EVBMIPS_MALTA_BE:
		if (machine->emulated_hz == 0)
			machine->emulated_hz = 33000000;
		cpu->byte_order = EMUL_LITTLE_ENDIAN;
		machine->machine_name = "MALTA (evbmips, little endian)";
		machine->stable = 1;

		if (machine->machine_subtype == MACHINE_EVBMIPS_MALTA_BE) {
			machine->machine_name = "MALTA (evbmips, big endian)";
			cpu->byte_order = EMUL_BIG_ENDIAN;
		}

		machine->md_interrupt = isa8_interrupt;
		machine->isa_pic_data.native_irq = 2;

		bus_isa_init(machine, 0, 0x18000000, 0x10000000, 8, 24);

		snprintf(tmpstr, sizeof(tmpstr), "ns16550 irq=4 addr=0x%x"
		    " name2=tty2 in_use=0", MALTA_CBUSUART);
		device_add(machine, tmpstr);

		pci_data = dev_gt_init(machine, machine->memory, 0x1be00000,
		    8+9, 8+9, 120);

		if (machine->use_x11) {
			if (strlen(machine->boot_string_argument) < 3) {
				fatal("WARNING: remember to use  -o 'console="
				    "tty0'  if you are emulating Linux. (Not"
				    " needed for NetBSD.)\n");
			}
			bus_pci_add(machine, pci_data, machine->memory,
			    0, 8, 0, "s3_virge");
		}

		bus_pci_add(machine, pci_data, machine->memory,
		    0, 9, 0, "piix4_isa");
		bus_pci_add(machine, pci_data, machine->memory,
		    0, 9, 1, "piix4_ide");

		/*  pcn: Not yet, since it is just a bogus device, so far.  */
		/*  bus_pci_add(machine, pci_data, machine->memory,
		    0, 11, 0, "pcn");  */

		device_add(machine, "malta_lcd addr=0x1f000400");
		break;

	case MACHINE_EVBMIPS_MESHCUBE:
		machine->machine_name = "Meshcube";

		/*  See: http://mail-index.netbsd.org/port-evbmips/2006/
		    02/23/0000.html  */

		if (machine->physical_ram_in_mb != 64)
			fprintf(stderr, "WARNING! MeshCubes are supposed to "
			    "have exactly 64 MB RAM. Continuing anyway.\n");
		if (machine->use_x11)
			fprintf(stderr, "WARNING! MeshCube with -X is "
			    "meaningless. Continuing anyway.\n");

		/*  First of all, the MeshCube has an Au1500 in it:  */
		machine->md_interrupt = au1x00_interrupt;
		machine->md_int.au1x00_ic_data = dev_au1x00_init(machine,
		    machine->memory);

		/*
		 *  TODO:  Which non-Au1500 devices, and at what addresses?
		 *
		 *  "4G Systems MTX-1 Board" at ?
		 *	1017fffc, 14005004, 11700000, 11700008, 11900014,
		 *	1190002c, 11900100, 11900108, 1190010c,
		 *	10400040 - 10400074,
		 *	14001000 (possibly LCD?)
		 *	11100028 (possibly ttySx?)
		 *
		 *  "usb_ohci=base:0x10100000,len:0x100000,irq:26"
		 */

		/*  Linux reads this during startup...  */
		device_add(machine, "random addr=0x1017fffc len=4");

		break;

	case MACHINE_EVBMIPS_PB1000:
		machine->machine_name = "PB1000 (evbmips)";
		cpu->byte_order = EMUL_BIG_ENDIAN;

		machine->md_interrupt = au1x00_interrupt;
		machine->md_int.au1x00_ic_data = dev_au1x00_init(machine,
		    machine->memory);
		/*  TODO  */
		break;

	default:fatal("Unimplemented EVBMIPS model.\n");
		exit(1);
	}

	if (!machine->prom_emulation)
		return;


	/*  NetBSD/evbmips wants these: (at least for Malta)  */

	/*  a0 = argc  */
	cpu->cd.mips.gpr[MIPS_GPR_A0] = 2;

	/*  a1 = argv  */
	cpu->cd.mips.gpr[MIPS_GPR_A1] = (int32_t)0x9fc01000;
	store_32bit_word(cpu, (int32_t)0x9fc01000, 0x9fc01040);
	store_32bit_word(cpu, (int32_t)0x9fc01004, 0x9fc01200);
	store_32bit_word(cpu, (int32_t)0x9fc01008, 0);

	machine->bootstr = strdup(machine->boot_kernel_filename);
	machine->bootarg = strdup(machine->boot_string_argument);
	store_string(cpu, (int32_t)0x9fc01040, machine->bootstr);
	store_string(cpu, (int32_t)0x9fc01200, machine->bootarg);

	/*  a2 = (yamon_env_var *)envp  */
	cpu->cd.mips.gpr[MIPS_GPR_A2] = (int32_t)0x9fc01800;

	yamon_machine_setup(machine, cpu->cd.mips.gpr[MIPS_GPR_A2]);

	/*  a3 = memsize  */
	cpu->cd.mips.gpr[MIPS_GPR_A3] = machine->physical_ram_in_mb * 1048576;
	/*  Hm. Linux ignores a3.  */

	/*
	 *  TODO:
	 *	Core ID numbers.
	 *	How much of this is not valid for PBxxxx?
	 *
	 *  See maltareg.h for more info.
	 */
	store_32bit_word(cpu, (int32_t)(0x80000000 + MALTA_REVISION),
	    (1 << 10) + 0x26);

	/*  Call vectors at 0x9fc005xx:  */
	for (i=0; i<0x100; i+=4)
		store_32bit_word(cpu, (int64_t)(int32_t)0x9fc00500 + i,
		    (int64_t)(int32_t)0x9fc00800 + i);

	/*  "Magic trap" PROM instructions at 0x9fc008xx:  */
	for (i=0; i<0x100; i+=4)
		store_32bit_word(cpu, (int64_t)(int32_t)0x9fc00800 + i,
		    0x00c0de0c);
}


MACHINE_DEFAULT_CPU(evbmips)
{
	switch (machine->machine_subtype) {
	case MACHINE_EVBMIPS_MALTA:
	case MACHINE_EVBMIPS_MALTA_BE:
		machine->cpu_name = strdup("5Kc");
		break;
	case MACHINE_EVBMIPS_MESHCUBE:
		machine->cpu_name = strdup("AU1500");
		break;
	case MACHINE_EVBMIPS_PB1000:
		machine->cpu_name = strdup("AU1000");
		break;
	default:fatal("Unimplemented evbmips subtype.\n");
		exit(1);
	}
}


MACHINE_DEFAULT_RAM(evbmips)
{
	/*  MeshCube is always (?) 64 MB, and the others work fine
	    with 64 MB too.  */
	machine->physical_ram_in_mb = 64;
}


MACHINE_REGISTER(evbmips)
{
	MR_DEFAULT(evbmips, "MIPS evaluation boards (evbmips)",
	    ARCH_MIPS, MACHINE_EVBMIPS);

	machine_entry_add_alias(me, "evbmips");

	machine_entry_add_subtype(me, "Malta", MACHINE_EVBMIPS_MALTA,
	    "malta", NULL);

	machine_entry_add_subtype(me, "Malta (Big-Endian)",
	    MACHINE_EVBMIPS_MALTA_BE, "maltabe", NULL);

	machine_entry_add_subtype(me, "MeshCube", MACHINE_EVBMIPS_MESHCUBE,
	    "meshcube", NULL);

	machine_entry_add_subtype(me, "PB1000", MACHINE_EVBMIPS_PB1000,
	    "pb1000", NULL);

	me->set_default_ram = machine_default_ram_evbmips;
}

