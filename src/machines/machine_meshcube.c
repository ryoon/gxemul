/*
 *  Copyright (C) 2004-2006  Anders Gavare.  All rights reserved.
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
 *  $Id: machine_meshcube.c,v 1.1 2006-01-10 20:30:05 debug Exp $
 */

#include <stdio.h>
#include <string.h>

#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "machine.h"
#include "machine_interrupts.h"
#include "memory.h"
#include "misc.h"


MACHINE_SETUP(meshcube)
{
	machine->machine_name = "Meshcube";

	if (machine->physical_ram_in_mb != 64)
		fprintf(stderr, "WARNING! MeshCubes are supposed to have "
		    "exactly 64 MB RAM. Continuing anyway.\n");
	if (machine->use_x11)
		fprintf(stderr, "WARNING! MeshCube with -X is meaningless. "
		    "Continuing anyway.\n");

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

	device_add(machine, "random addr=0x1017fffc len=4");

	if (!machine->prom_emulation)
		return;


	/*
	 *  TODO:  A Linux kernel wants "memsize" from somewhere... I
	 *  haven't found any docs on how it is used though.
	 */

	cpu->cd.mips.gpr[MIPS_GPR_A0] = 1;

	cpu->cd.mips.gpr[MIPS_GPR_A1] = 0xa0001000ULL;
	store_32bit_word(cpu, cpu->cd.mips.gpr[MIPS_GPR_A1], 0xa0002000ULL);
	store_string(cpu, 0xa0002000ULL, "something=somethingelse");

	cpu->cd.mips.gpr[MIPS_GPR_A2] = 0xa0003000ULL;
	store_string(cpu, 0xa0002000ULL, "hello=world\n");
}


MACHINE_DEFAULT_CPU(meshcube)
{
	/*  TODO:  Should be AU1500, but Linux doesn't like
		the absence of caches in the emulator  */
	machine->cpu_name = strdup("R4400");
}


MACHINE_DEFAULT_RAM(meshcube)
{
	machine->physical_ram_in_mb = 64;
}


MACHINE_REGISTER(meshcube)
{
	MR_DEFAULT(meshcube, "Meshcube", ARCH_MIPS, MACHINE_MESHCUBE, 1, 0);
	me->aliases[0] = "meshcube";
	me->set_default_ram = machine_default_ram_meshcube;
	machine_entry_add(me, ARCH_MIPS);
}

