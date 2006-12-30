/*
 *  Copyright (C) 2005-2007  Anders Gavare.  All rights reserved.
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
 *  $Id: machine_zaurus.c,v 1.6 2006-12-30 13:31:02 debug Exp $
 */

#include <stdio.h>
#include <string.h>

#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"


MACHINE_SETUP(zaurus)
{
	machine->machine_name = "Zaurus";

	dev_ram_init(machine, 0xa0000000, 0x20000000, DEV_RAM_MIRROR, 0x0);
	dev_ram_init(machine, 0xc0000000, 0x20000000, DEV_RAM_MIRROR, 0x0);

	/*  TODO: replace this with the correct device  */
	dev_ram_init(machine, 0x40d00000, 0x1000, DEV_RAM_RAM, 0);

	device_add(machine, "ns16550 irq=0 addr=0x40100000 addr_mult=4");
	device_add(machine, "ns16550 irq=0 addr=0xfd400000 addr_mult=4");


dev_fb_init(machine, machine->memory, 0x44000000,
    VFB_GENERIC, 640,240, 640,240, 16, "PXA2X0 LCD");
         
        
	if (!machine->prom_emulation)
		return;

	/*  TODO: Registers, etc.  */

	arm_setup_initial_translation_table(cpu, 0x4000);
}


MACHINE_DEFAULT_CPU(zaurus)
{
	machine->cpu_name = strdup("PXA27X");
}


MACHINE_DEFAULT_RAM(zaurus)
{
	machine->physical_ram_in_mb = 64;
}


MACHINE_REGISTER(zaurus)
{
	MR_DEFAULT(zaurus, "Zaurus", ARCH_ARM, MACHINE_ZAURUS);

	machine_entry_add_alias(me, "zaurus");

	me->set_default_ram = machine_default_ram_zaurus;
}

