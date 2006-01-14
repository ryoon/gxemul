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
 *  $Id: machine_mvmeppc.c,v 1.1 2006-01-14 11:29:38 debug Exp $
 *
 *  MVMEPPC machines (for experimenting with NetBSD/mvmeppc or RTEMS).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bus_isa.h"
#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "diskimage.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"


MACHINE_SETUP(mvmeppc)
{
	struct pci_data *pci_data;

	switch (machine->machine_subtype) {

	case MACHINE_MVMEPPC_1600:
		machine->machine_name = "MVME1600";

		bus_isa_init(machine, BUS_ISA_IDE0 | BUS_ISA_IDE1,
		     0x80000000, 0xc0000000, 32, 48);

		break;

	case MACHINE_MVMEPPC_2100:
		machine->machine_name = "MVME2100";

		/*  0xfe000000 isa bus space  */
		/*  0xfec00000 pci indirect addr  */
		/*  0xfee00000 pci indirect data  */

		/*  TODO: irq  */
		device_add(machine, "ns16550 irq=0 addr=0xffe10000");

		break;

	case MACHINE_MVMEPPC_5500:
		machine->machine_name = "MVME5500";

		/*  GT64260 interrupt and PCI controller:  */
		pci_data = dev_gt_init(machine, machine->memory,
		    0xf1000000, 0 /* TODO: irq */, 0 /* TODO: pciirq */, 260);

		/*  TODO: irq  */
		device_add(machine, "ns16550 irq=0 addr=0xf1120000");

		break;

	default:fatal("Unimplemented MVMEPPC machine subtype %i\n",
		    machine->machine_subtype);
		exit(1);
	}

	if (!machine->prom_emulation)
		return;

	cpu->cd.ppc.gpr[5] = machine->physical_ram_in_mb * 1048576-0x100;
	store_string(cpu, cpu->cd.ppc.gpr[5]+ 44, "PC16550");
	store_16bit_word(cpu, cpu->cd.ppc.gpr[5]+ 76, 0x1600);
#if 0
0         u_int32_t       bi_boothowto;
4         u_int32_t       bi_bootaddr;
8         u_int16_t       bi_bootclun;
10        u_int16_t       bi_bootdlun;
12        char            bi_bootline[BOOTLINE_LEN];  (32)
44        char            bi_consoledev[CONSOLEDEV_LEN]; (16)
60        u_int32_t       bi_consoleaddr;
64        u_int32_t       bi_consolechan;
68        u_int32_t       bi_consolespeed;
72        u_int32_t       bi_consolecflag;
76        u_int16_t       bi_modelnumber;
80        u_int32_t       bi_memsize;
84        u_int32_t       bi_mpuspeed;
88        u_int32_t       bi_busspeed;
92        u_int32_t       bi_clocktps;
#endif
}


MACHINE_DEFAULT_CPU(mvmeppc)
{
	switch (machine->machine_subtype) {

	case MACHINE_MVMEPPC_1600:
		/*  TODO? Check with NetBSD/mvmeppc  */
		machine->cpu_name = strdup("PPC603");
		break;

	case MACHINE_MVMEPPC_2100:
		machine->cpu_name = strdup("PPC603e");
		break;

	case MACHINE_MVMEPPC_5500:
		machine->cpu_name = strdup("PPC750");
		break;

	default:fatal("Unimplemented MVMEPPC machine subtype %i\n",
		    machine->machine_subtype);
		exit(1);
	}
}


MACHINE_DEFAULT_RAM(mvmeppc)
{
	machine->physical_ram_in_mb = 32;
}


MACHINE_REGISTER(mvmeppc)
{
	MR_DEFAULT(mvmeppc, "MVME (PPC)", ARCH_PPC, MACHINE_MVMEPPC, 1, 3);

	me->aliases[0] = "mvmeppc";

	me->subtype[0] = machine_entry_subtype_new(
	    "MVME1600", MACHINE_MVMEPPC_1600, 1);
	me->subtype[0]->aliases[0] = "mvme1600";

	me->subtype[1] = machine_entry_subtype_new(
	    "MVME2100", MACHINE_MVMEPPC_2100, 1);
	me->subtype[1]->aliases[0] = "mvme2100";

	me->subtype[2] = machine_entry_subtype_new(
	    "MVME5500", MACHINE_MVMEPPC_5500, 1);
	me->subtype[2]->aliases[0] = "mvme5500";

	me->set_default_ram = machine_default_ram_mvmeppc;

	machine_entry_add(me, ARCH_PPC);
}

