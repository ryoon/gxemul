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
 *  $Id: machine_db64360.c,v 1.4 2006-12-30 13:31:01 debug Exp $
 */

#include <stdio.h>
#include <string.h>

#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"


MACHINE_SETUP(db64360)
{
	int i;

	/*  For playing with PMON2000 for PPC:  */
	machine->machine_name = "DB64360";

	machine->main_console_handle = (size_t)device_add(machine,
	    "ns16550 irq=0 addr=0x1d000020 addr_mult=4");

	if (!machine->prom_emulation)
		return;

	for (i=0; i<32; i++)
		cpu->cd.ppc.gpr[i] =
		    0x12340000 + (i << 8) + 0x55;
}


MACHINE_DEFAULT_CPU(db64360)
{
	machine->cpu_name = strdup("PPC750");
}


MACHINE_DEFAULT_RAM(db64360)
{
	machine->physical_ram_in_mb = 64;
}


MACHINE_REGISTER(db64360)
{
	MR_DEFAULT(db64360, "DB64360", ARCH_PPC, MACHINE_DB64360);

	machine_entry_add_alias(me, "db64360");

	me->set_default_ram = machine_default_ram_db64360;
}

