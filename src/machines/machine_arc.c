/*
 *  Copyright (C) 2003-2007  Anders Gavare.  All rights reserved.
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
 *  $Id: machine_arc.c,v 1.11 2007-01-28 13:08:26 debug Exp $
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arcbios.h"
#include "bus_isa.h"
#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "machine.h"
#include "machine_interrupts.h"
#include "memory.h"
#include "misc.h"

#define	MACHINE_NAME_MAXBUF	100


MACHINE_SETUP(arc)
{
	struct pci_data *pci_data;
	void *jazz_data;
	struct memory *mem = machine->memory;
	char tmpstr[1000];
	char tmpstr2[1000];
	int i, j;
	char *eaddr_string = "eaddr=10:20:30:40:50:60";		/*  bogus  */
	unsigned char macaddr[6];

	machine->machine_name = malloc(MACHINE_NAME_MAXBUF);
	if (machine->machine_name == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}

	cpu->byte_order = EMUL_LITTLE_ENDIAN;
	snprintf(machine->machine_name, MACHINE_NAME_MAXBUF, "ARC");

	switch (machine->machine_subtype) {

	case MACHINE_ARC_NEC_RD94:
	case MACHINE_ARC_NEC_R94:
	case MACHINE_ARC_NEC_R96:
		/*
		 *  "NEC-RD94" (NEC RISCstation 2250)
		 *  "NEC-R94" (NEC RISCstation 2200)
		 *  "NEC-R96" (NEC Express RISCserver)
		 *
		 *  http://mirror.aarnet.edu.au/pub/NetBSD/misc/chs/arcdiag.out
		 *  (NEC-R96)
		 */

		switch (machine->machine_subtype) {
		case MACHINE_ARC_NEC_RD94:
			strlcat(machine->machine_name,
			    " (NEC-RD94, NEC RISCstation 2250)",
			    MACHINE_NAME_MAXBUF);
			break;
		case MACHINE_ARC_NEC_R94:
			strlcat(machine->machine_name,
			    " (NEC-R94; NEC RISCstation 2200)",
			    MACHINE_NAME_MAXBUF);
			break;
		case MACHINE_ARC_NEC_R96:
			strlcat(machine->machine_name,
			    " (NEC-R96; NEC Express RISCserver)",
			    MACHINE_NAME_MAXBUF);
			break;
		}

		/*  TODO: interrupt controller!  */

		pci_data = device_add(machine,
		    "rd94 addr=0x80000000, irq=0");

		device_add(machine, "sn addr=0x80001000 irq=0");
		dev_mc146818_init(machine, mem, 0x80004000ULL, 0,
		    MC146818_ARC_NEC, 1);

fatal("TODO: legacy rewrite\n");
abort();
//		i = dev_pckbc_init(machine, mem, 0x80005000ULL, PCKBC_8042,
//		    0, 0, machine->use_x11, 0);

		snprintf(tmpstr, sizeof(tmpstr),
		    "ns16550 irq=3 addr=0x80006000 in_use=%i name2=tty0",
		    machine->use_x11? 0 : 1);
		j = (size_t)device_add(machine, tmpstr);
		snprintf(tmpstr, sizeof(tmpstr),
		    "ns16550 irq=0 addr=0x80007000 in_use=%i name2=tty1", 0);
		device_add(machine, tmpstr);

		if (machine->use_x11)
			machine->main_console_handle = i;
		else
			machine->main_console_handle = j;

		/*  lpt at 0x80008000  */

		device_add(machine, "fdc addr=0x8000c000, irq=0");

		switch (machine->machine_subtype) {
		case MACHINE_ARC_NEC_RD94:
		case MACHINE_ARC_NEC_R94:
			/*  PCI devices:  (NOTE: bus must be 0, device must be
			    3, 4, or 5, for NetBSD to accept interrupts)  */
			bus_pci_add(machine, pci_data, mem, 0, 3, 0,
			    "dec21030");	/*  tga graphics  */
			break;
		case MACHINE_ARC_NEC_R96:
			dev_fb_init(machine, mem, 0x100e00000ULL,
			    VFB_GENERIC, 640,480, 1024,480,
			    8, "necvdfrb");
			break;
		}
		break;

	case MACHINE_ARC_NEC_R98:
		/*
		 *  "NEC-R98" (NEC RISCserver 4200)
		 *
		 *  According to http://mail-index.netbsd.org/port-arc/
		 *	2004/02/01/0001.html:
		 *
		 *  Network adapter at "start: 0x 0 18600000, length:
		 *	0x1000, level: 4, vector: 9"
		 *  Disk at "start: 0x 0 18c103f0, length: 0x1000, level:
		 *	5, vector: 6"
		 *  Keyboard at "start: 0x 0 18c20060, length: 0x1000,
		 *	level: 5, vector: 3"
		 *  Serial at "start: 0x 0 18c103f8, length: 0x1000,
		 *	level: 5, vector: 4"
		 *  Serial at "start: 0x 0 18c102f8, length: 0x1000,
		 *	level: 5, vector: 4"
		 *  Parallel at "start: 0x 0 18c10278, length: 0x1000,
		 *	level: 5, vector: 5"
		 */

		strlcat(machine->machine_name,
		    " (NEC-R98; NEC RISCserver 4200)", MACHINE_NAME_MAXBUF);

		/*
		 *  Windows NT access stuff at these addresses:
		 *
		 *  19980308, 18000210, 18c0a008,
		 *  19022018, 19026010, andso on.
		 */
		break;

	case MACHINE_ARC_JAZZ_PICA:
	case MACHINE_ARC_JAZZ_MAGNUM:
		/*
		 *  "PICA-61"
		 *
		 *  According to NetBSD 1.6.2:
		 *
		 *  jazzio0 at mainbus0
		 *  timer0 at jazzio0 addr 0xe0000228
		 *  mcclock0 at jazzio0 addr 0xe0004000: mc146818 or compatible
		 *  lpt at jazzio0 addr 0xe0008000 intr 0 not configured
		 *  fdc at jazzio0 addr 0xe0003000 intr 1 not configured
		 *  MAGNUM at jazzio0 addr 0xe000c000 intr 2 not configured
		 *  ALI_S3 at jazzio0 addr 0xe0800000 intr 3 not configured
		 *  sn0 at jazzio0 addr 0xe0001000 intr 4: SONIC Ethernet
		 *  sn0: Ethernet address 69:6a:6b:6c:00:00
		 *  asc0 at jazzio0 addr 0xe0002000 intr 5: NCR53C94, target 0
		 *  pckbd at jazzio0 addr 0xe0005000 intr 6 not configured
		 *  pms at jazzio0 addr 0xe0005000 intr 7 not configured
		 *  com0 at jazzio0 addr 0xe0006000 intr 8: ns16550a,
		 *	working fifo
		 *  com at jazzio0 addr 0xe0007000 intr 9 not configured
		 *  jazzisabr0 at mainbus0
		 *  isa0 at jazzisabr0 isa_io_base 0xe2000000 isa_mem_base
		 *	0xe3000000
		 *
		 *  "Microsoft-Jazz", "MIPS Magnum"
		 *
		 *  timer0 at jazzio0 addr 0xe0000228
		 *  mcclock0 at jazzio0 addr 0xe0004000: mc146818 or compatible
		 *  lpt at jazzio0 addr 0xe0008000 intr 0 not configured
		 *  fdc at jazzio0 addr 0xe0003000 intr 1 not configured
		 *  MAGNUM at jazzio0 addr 0xe000c000 intr 2 not configured
		 *  VXL at jazzio0 addr 0xe0800000 intr 3 not configured
		 *  sn0 at jazzio0 addr 0xe0001000 intr 4: SONIC Ethernet
		 *  sn0: Ethernet address 69:6a:6b:6c:00:00
		 *  asc0 at jazzio0 addr 0xe0002000 intr 5: NCR53C94, target 0
		 *  scsibus0 at asc0: 8 targets, 8 luns per target
		 *  pckbd at jazzio0 addr 0xe0005000 intr 6 not configured
		 *  pms at jazzio0 addr 0xe0005000 intr 7 not configured
		 *  com0 at jazzio0 addr 0xe0006000 intr 8: ns16550a,
		 *	working fifo
		 *  com at jazzio0 addr 0xe0007000 intr 9 not configured
		 *  jazzisabr0 at mainbus0
		 *  isa0 at jazzisabr0 isa_io_base 0xe2000000 isa_mem_base
		 *	0xe3000000
		 */

		switch (machine->machine_subtype) {
		case MACHINE_ARC_JAZZ_PICA:
			strlcat(machine->machine_name,
			    " (Microsoft Jazz, Acer PICA-61)",
			    MACHINE_NAME_MAXBUF);
			machine->stable = 1;
			break;
		case MACHINE_ARC_JAZZ_MAGNUM:
			strlcat(machine->machine_name,
			    " (Microsoft Jazz, MIPS Magnum)",
			    MACHINE_NAME_MAXBUF);
			break;
		default:
			fatal("error in machine.c. jazz\n");
			exit(1);
		}

		jazz_data = device_add(machine, "jazz addr=0x80000000");

		/*  Keyboard IRQ is jazz.6, mouse is jazz.7  */
		snprintf(tmpstr, sizeof(tmpstr),
		    "%s.cpu[%i].jazz.6", machine->path,
		    machine->bootstrap_cpu);
		snprintf(tmpstr2, sizeof(tmpstr2),
		    "%s.cpu[%i].jazz.7", machine->path,
		    machine->bootstrap_cpu);
		i = dev_pckbc_init(machine, mem, 0x80005000ULL,
		    PCKBC_JAZZ, tmpstr, tmpstr2,
		    machine->use_x11, 0);

		/*  Serial controllers at JAZZ irq 8 and 9:  */
		snprintf(tmpstr, sizeof(tmpstr),
		    "ns16550 irq=%s.cpu[%i].jazz.8 addr=0x80006000"
		    " in_use=%i name2=tty0", machine->path,
		    machine->bootstrap_cpu, machine->use_x11? 0 : 1);
		j = (size_t)device_add(machine, tmpstr);
		snprintf(tmpstr, sizeof(tmpstr),
		    "ns16550 irq=%s.cpu[%i].jazz.9 addr=0x80007000"
		    " in_use=0 name2=tty1", machine->path,
		    machine->bootstrap_cpu);
		device_add(machine, tmpstr);

		if (machine->use_x11)
			machine->main_console_handle = i;
		else
			machine->main_console_handle = j;

		switch (machine->machine_subtype) {
		case MACHINE_ARC_JAZZ_PICA:
			if (machine->use_x11) {
				dev_vga_init(machine, mem, 0x400a0000ULL,
				    0x600003c0ULL, machine->machine_name);
				arcbios_console_init(machine,
				    0x400b8000ULL, 0x600003c0ULL);
			}
			break;
		case MACHINE_ARC_JAZZ_MAGNUM:
			/*  PROM mirror?  */
			dev_ram_init(machine, 0xfff00000, 0x100000,
			    DEV_RAM_MIRROR | DEV_RAM_MIGHT_POINT_TO_DEVICES,
			    0x1fc00000);

			/*  VXL. TODO  */
			/*  control at 0x60100000?  */
			dev_fb_init(machine, mem, 0x60200000ULL,
			    VFB_GENERIC, 1024,768, 1024,768, 8, "VXL");
			break;
		}

		/*  SN at JAZZ irq 4  */
		snprintf(tmpstr, sizeof(tmpstr),
		    "sn addr=0x80001000 irq=%s.cpu[%i].jazz.4",
		    machine->path, machine->bootstrap_cpu);
		device_add(machine, tmpstr);

		/*  ASC at JAZZ irq 5  */
		snprintf(tmpstr, sizeof(tmpstr), "%s.cpu[%i].jazz.5", 
		    machine->path, machine->bootstrap_cpu);
		dev_asc_init(machine, mem, 0x80002000ULL, tmpstr, NULL,
		    DEV_ASC_PICA, dev_jazz_dma_controller, jazz_data);

		/*  FDC at JAZZ irq 1  */
		snprintf(tmpstr, sizeof(tmpstr),
		    "fdc addr=0x80003000 irq=%s.cpu[%i].jazz.1",
		    machine->path, machine->bootstrap_cpu);
		device_add(machine, tmpstr);

		/*  MC146818 at MIPS irq 2:  */
		snprintf(tmpstr, sizeof(tmpstr), "%s.cpu[%i].2", 
		    machine->path, machine->bootstrap_cpu);
		dev_mc146818_init(machine, mem,
		    0x80004000ULL, tmpstr, MC146818_ARC_JAZZ, 1);

#if 0
Not yet.
		/*  WDC at ISA irq 14  */
		device_add(machine, "wdc addr=0x900001f0, irq=38");
#endif

		break;

	case MACHINE_ARC_JAZZ_M700:
		/*
		 *  "Microsoft-Jazz", "Olivetti M700"
		 *
		 *  Different enough from Pica and Magnum to be
		 *  separate here.
		 *
		 *  http://mail-index.netbsd.org/port-arc/2000/10/18/0001.html
		 */

		strlcat(machine->machine_name, " (Microsoft Jazz, "
		    "Olivetti M700)", MACHINE_NAME_MAXBUF);

		device_add(machine, "jazz addr=0x80000000");

fatal("TODO: Legacy rewrite\n");
abort();

//		dev_mc146818_init(machine, mem,
//		    0x80004000ULL, 2, MC146818_ARC_JAZZ, 1);

		i = 0;		/*  TODO: Yuck!  */
#if 0
		i = dev_pckbc_init(machine, mem, 0x80005000ULL,
		    PCKBC_JAZZ, 8 + 6, 8 + 7, machine->use_x11, 0);
#endif

		snprintf(tmpstr, sizeof(tmpstr), "ns16550 irq=16 addr="
		    "0x80006000 in_use=%i name2=tty0", machine->use_x11? 0 : 1);
		j = (size_t)device_add(machine, tmpstr);
		snprintf(tmpstr, sizeof(tmpstr), "ns16550 irq=17 addr="
		    "0x80007000 in_use=%i name2=tty1", 0);
		device_add(machine, tmpstr);

		if (machine->use_x11)
			machine->main_console_handle = i;
		else
			machine->main_console_handle = j;

		dev_m700_fb_init(machine, mem, 0x180080000ULL, 0x100000000ULL);

		break;

	case MACHINE_ARC_DESKTECH_TYNE:
		/*
		 *  "Deskstation Tyne" (?)
		 *
		 *  TODO
		 *  http://mail-index.netbsd.org/port-arc/2000/10/14/0000.html
		 */

		strlcat(machine->machine_name, " (Deskstation Tyne)",
		    MACHINE_NAME_MAXBUF);

		/*  TODO: IRQs!  */
		bus_isa_init(machine, machine->path, 0, 0x900000000ULL,
		    0x100000000ULL);
#if 0
		snprintf(tmpstr, sizeof(tmpstr), "ns16550 irq=0 addr="
		    "0x9000003f8 in_use=%i name2=tty0", machine->use_x11? 0:1);
		i = (size_t)device_add(machine, tmpstr);
		device_add(machine, "ns16550 irq=0 addr=0x9000002f8 in_use=0"
		    " name2=tty1");
#endif
		device_add(machine, "ns16550 irq=0 addr=0x9000003e8 "
		    "in_use=0 name2=tty2");
		device_add(machine, "ns16550 irq=0 addr=0x9000002e8 "
		    "in_use=0 name2=tty3");
#if 0
		dev_mc146818_init(machine, mem,
		    0x900000070ULL, 2, MC146818_PC_CMOS, 1);
		/*  TODO: irq, etc  */
		device_add(machine, "wdc addr=0x9000001f0, irq=0");
		device_add(machine, "wdc addr=0x900000170, irq=0");

		/*  PC kbd  */
		j = dev_pckbc_init(machine, mem, 0x900000060ULL,
		    PCKBC_8042, 0, 0, machine->use_x11, 0);

		if (machine->use_x11)
			machine->main_console_handle = j;
		else
			machine->main_console_handle = i;
#endif

		if (machine->use_x11) {
			dev_vga_init(machine, mem, 0x1000a0000ULL,
			    0x9000003c0ULL, machine->machine_name);
			arcbios_console_init(machine,
			    0x1000b8000ULL, 0x9000003c0ULL);
		}
		break;

	default:fatal("Unimplemented ARC machine type %i\n",
		    machine->machine_subtype);
		exit(1);
	}

	/*
	 *  NOTE: ARCBIOS shouldn't be used before this point. (The only
	 *  exception is that arcbios_console_init() may be called.)
	 */

	if (!machine->prom_emulation)
		return;

	arcbios_init(machine, 0, 0, eaddr_string, macaddr);
}


MACHINE_DEFAULT_CPU(arc)
{
	switch (machine->machine_subtype) {
	case MACHINE_ARC_JAZZ_PICA:
		machine->cpu_name = strdup("R4000");
		break;
	default:
		machine->cpu_name = strdup("R4400");
	}
}


MACHINE_DEFAULT_RAM(arc)
{
	machine->physical_ram_in_mb = 64;
}


MACHINE_REGISTER(arc)
{
	MR_DEFAULT(arc, "ARC", ARCH_MIPS, MACHINE_ARC);

	me->set_default_ram = machine_default_ram_arc;

	machine_entry_add_alias(me, "arc");

	machine_entry_add_subtype(me, "Acer PICA-61", MACHINE_ARC_JAZZ_PICA,
	    "pica-61", "acer pica", "pica", NULL);

	machine_entry_add_subtype(me, "Deskstation Tyne",
	    MACHINE_ARC_DESKTECH_TYNE,
	    "deskstation tyne", "desktech", "tyne", NULL);

	machine_entry_add_subtype(me, "Jazz Magnum", MACHINE_ARC_JAZZ_MAGNUM,
	    "magnum", "jazz magnum", NULL);

	machine_entry_add_subtype(me, "NEC-R94", MACHINE_ARC_NEC_R94,
	    "nec-r94", "r94", NULL);

	machine_entry_add_subtype(me, "NEC-RD94", MACHINE_ARC_NEC_RD94,
	    "nec-rd94", "rd94", NULL);

	machine_entry_add_subtype(me, "NEC-R96", MACHINE_ARC_NEC_R96,
	    "nec-r96", "r96", NULL);

	machine_entry_add_subtype(me, "NEC-R98", MACHINE_ARC_NEC_R98,
	    "nec-r98", "r98", NULL);

	machine_entry_add_subtype(me, "Olivetti M700", MACHINE_ARC_JAZZ_M700,
	    "olivetti", "m700", NULL);
}

