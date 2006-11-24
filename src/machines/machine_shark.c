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
 *  $Id: machine_shark.c,v 1.4 2006-11-24 17:29:07 debug Exp $
 */

#include <stdio.h>
#include <string.h>

#include "bus_isa.h"
#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"
#include "of.h"


MACHINE_SETUP(shark)
{
	machine->machine_name = "Digital DNARD (\"Shark\")";

	bus_isa_init(machine, machine->path,
	    BUS_ISA_IDE0, 0x08100000, 0xc0000000, 32, 48);

	if (!machine->prom_emulation)
		return;


	arm_setup_initial_translation_table(cpu,
	    machine->physical_ram_in_mb * 1048576 - 65536);

	/*  TODO: Framebuffer  */
	of_emul_init(machine, NULL, 0xf1000000, 1024, 768);
	of_emul_init_isa(machine);

	/*
	 *  r0 = OpenFirmware entry point.  NOTE: See cpu_arm.c for
	 *  the rest of this semi-ugly hack.
	 */
	cpu->cd.arm.r[0] = cpu->cd.arm.of_emul_addr;
}


MACHINE_DEFAULT_CPU(shark)
{
	machine->cpu_name = strdup("SA110");
}


MACHINE_REGISTER(shark)
{
	MR_DEFAULT(shark, "Digital DNARD (\"Shark\")", ARCH_ARM, MACHINE_SHARK);

	machine_entry_add_alias(me, "shark");
	machine_entry_add_alias(me, "dnard");
}

