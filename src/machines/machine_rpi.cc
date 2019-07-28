/*
 *  Copyright (C) 2013  Anders Gavare.  All rights reserved.
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
 *  COMMENT: Raspberry Pi
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


MACHINE_SETUP(rpi)
{
	machine->machine_name = strdup("Raspberry Pi");

	if (machine->emulated_hz == 0)
		machine->emulated_hz = 700000000;

	if (!machine->prom_emulation)
		return;

	arm_setup_initial_translation_table(cpu, 7 * 1048576 - 32768);
}


MACHINE_DEFAULT_CPU(rpi)
{
	// "700 MHz ARM1176JZF-S core (ARM11 family, ARMv6 instruction set)[3]"
	// according to Wikipedia.
	// NOTE/TODO: This is wrong, but should be enough for
	// starting with simple experiments.
	machine->cpu_name = strdup("ARM1136JSR1");
}


MACHINE_DEFAULT_RAM(rpi)
{
	machine->physical_ram_in_mb = 512;
}


MACHINE_REGISTER(rpi)
{
	MR_DEFAULT(rpi, "Raspberry Pi", ARCH_ARM, MACHINE_RPI);

	machine_entry_add_alias(me, "rpi");

	me->set_default_ram = machine_default_ram_rpi;
}

