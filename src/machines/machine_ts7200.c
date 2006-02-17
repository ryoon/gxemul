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
 *  $Id: machine_ts7200.c,v 1.1 2006-02-17 20:27:21 debug Exp $
 */

#include <stdio.h>
#include <string.h>

#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"


MACHINE_SETUP(ts7200)
{
	machine->machine_name = "TS7200";

	/*  TODO: _NOT_ an ns16550  */
	device_add(machine, "ns16550 irq=0 addr=0x808c0000 addr_mult=4");

	dev_ram_init(machine, 0xc0000000, 0x20000000, DEV_RAM_MIRROR, 0x0);

	if (!machine->prom_emulation)
		return;

	arm_setup_initial_translation_table(cpu, 0x4000);
	arm_translation_table_set_l1(cpu, 0xc0000000, 0xc0000000);
	arm_translation_table_set_l1(cpu, 0xe0000000, 0xe0000000);
	arm_translation_table_set_l1(cpu, 0xf0000000, 0xf0000000);
}


MACHINE_DEFAULT_CPU(ts7200)
{
	machine->cpu_name = strdup("80321_600_B0");
}


MACHINE_REGISTER(ts7200)
{
	MR_DEFAULT(ts7200, "TS7200", ARCH_ARM, MACHINE_TS7200, 1, 0);
	me->aliases[0] = "ts7200";
	machine_entry_add(me, ARCH_ARM);
}

