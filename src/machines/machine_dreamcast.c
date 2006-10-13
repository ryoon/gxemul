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
 *  $Id: machine_dreamcast.c,v 1.10 2006-10-13 06:31:51 debug Exp $
 *
 *  Machine for experimenting with NetBSD/dreamcast.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"


MACHINE_SETUP(dreamcast)
{
	machine->machine_name = "Dreamcast";

	if (machine->emulated_hz == 0)
		machine->emulated_hz = 200000000;

	if (!machine->use_x11)
		fprintf(stderr, "-------------------------------------"
		    "------------------------------------------\n"
		    "\n  WARNING!  You are emulating a Dreamcast without -X."
		    "\n            You will miss graphical output!\n\n"
		    "-------------------------------------"
		    "------------------------------------------\n");

	/*
	 *  Physical address layout on the Dreamcast, according to
	 *  http://www.boob.co.uk/docs/Dreamcast_memory.txt and
	 *  http://www.ludd.luth.se/~jlo/dc/memory.txt:
	 *
	 *  0x00000000 - 0x001fffff	Boot ROM (2 MB)
	 *  0x00200000 - 0x003fffff	Flash (256 KB)
	 *  0x005f8000 - ...		PVR registers
	 *  0x00700000 - ...		SPU registers
	 *  0x00800000 - 0x009fffff	Sound RAM (2 MB)
	 *  0x01000000 - ...		Parallel port registers
	 *  0x02000000 - ...		CD-ROM port registers
	 *  0x05000000 - 0x057fffff	Video RAM (8 MB)
	 *  0x0c000000 - 0x0cffffff	RAM (16 MB)
	 *  0x10000000 - ...		Tile accelerator (?)
	 *  0x10800000 - ...		Write-only mirror of Video RAM
	 *  0x14000000 - ...		G2 (?)
	 */

	dev_ram_init(machine, 0x0c000000, 0x01000000, DEV_RAM_MIRROR, 0x0);

	device_add(machine, "pvr");

	if (!machine->prom_emulation)
		return;

	dreamcast_machine_setup(machine);
}


MACHINE_DEFAULT_CPU(dreamcast)
{
	/*  Hitachi SH4, 200 MHz  */
	machine->cpu_name = strdup("SH4");
}


MACHINE_DEFAULT_RAM(dreamcast)
{
	machine->physical_ram_in_mb = 16;
}


MACHINE_REGISTER(dreamcast)
{
	MR_DEFAULT(dreamcast, "Dreamcast", ARCH_SH, MACHINE_DREAMCAST);
	me->set_default_ram = machine_default_ram_dreamcast;
	machine_entry_add_alias(me, "dreamcast");
}

