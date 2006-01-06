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
 *  $Id: machine_x86.c,v 1.1 2006-01-06 11:41:46 debug Exp $
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


extern int quiet_mode;


MACHINE_SETUP(barex86)
{
	machine->machine_name = "\"Bare\" x86 machine";
	machine->stable = 1;
}


MACHINE_SETUP(x86)
{
	if (machine->machine_subtype == MACHINE_X86_XT)
		machine->machine_name = "PC XT";
	else
		machine->machine_name = "Generic x86 PC";

	machine->md_interrupt = x86_pc_interrupt;

	bus_isa_init(machine, BUS_ISA_IDE0 | BUS_ISA_IDE1 | BUS_ISA_VGA |
	    BUS_ISA_PCKBC_FORCE_USE |
	    (machine->machine_subtype == MACHINE_X86_XT?
	    BUS_ISA_NO_SECOND_PIC : 0) | BUS_ISA_FDC,
	    X86_IO_BASE, 0x00000000, 0, 16);

	if (machine->prom_emulation)
		pc_bios_init(cpu);

	if (!machine->use_x11 && !quiet_mode)
		fprintf(stderr, "-------------------------------------"
		    "------------------------------------------\n"
		    "\n  WARNING! You are emulating a PC without -X. "
		    "You will miss graphical output!\n\n"
		    "-------------------------------------"
		    "------------------------------------------\n");
}


MACHINE_DEFAULT_CPU(barex86)
{
	machine->cpu_name = strdup("AMD64");
}


MACHINE_DEFAULT_CPU(x86)
{
	if (machine->machine_subtype == MACHINE_X86_XT) {
		machine->cpu_name = strdup("8086");
		return;
	}

	machine->cpu_name = strdup("AMD64");
}


MACHINE_DEFAULT_RAM(x86)
{
	if (machine->machine_subtype == MACHINE_X86_XT) {
		machine->physical_ram_in_mb = 1;
		return;
	}

	machine->physical_ram_in_mb = 64;
}


MACHINE_REGISTER(barex86)
{
	MR_DEFAULT(barex86, "Generic \"bare\" X86 machine", ARCH_X86,
	    MACHINE_BAREX86, 1, 0);
	me->aliases[0] = "barex86";
	machine_entry_add(me, ARCH_X86);
}


MACHINE_REGISTER(x86)
{
	MR_DEFAULT(x86, "x86-based PC", ARCH_X86, MACHINE_X86, 2, 2);
	me->aliases[0] = "pc";
	me->aliases[1] = "x86";
	me->subtype[0] = machine_entry_subtype_new("Generic PC",
	    MACHINE_X86_GENERIC, 2);
	me->subtype[0]->aliases[0] = "pc";
	me->subtype[0]->aliases[1] = "generic";
	me->subtype[1] = machine_entry_subtype_new("PC XT", MACHINE_X86_XT, 1);
	me->subtype[1]->aliases[0] = "xt";
	me->set_default_ram = machine_default_ram_x86;
	machine_entry_add(me, ARCH_X86);
}

