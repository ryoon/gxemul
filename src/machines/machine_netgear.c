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
 *  $Id: machine_netgear.c,v 1.2 2006-06-24 10:19:19 debug Exp $
 */

#include <stdio.h>
#include <string.h>

#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"


MACHINE_SETUP(netgear)
{
	machine->machine_name = "NetGear WG602v1";

	if (machine->use_x11)
		fprintf(stderr, "WARNING! NetGear with -X is meaningless. "
		    "Continuing anyway.\n");
	if (machine->physical_ram_in_mb != 16)
		fprintf(stderr, "WARNING! Real NetGear WG602v1 boxes have "
		    "exactly 16 MB RAM. Continuing anyway.\n");

	/*
	 *  Lots of info about the IDT 79RC 32334:  http://www.idt.com/
	 *	products/pages/Integrated_Processors-79RC32334.html
	 */
	device_add(machine, "8250 addr=0x18000800 addr_mult=4 irq=0");
}


MACHINE_DEFAULT_CPU(netgear)
{
	machine->cpu_name = strdup("RC32334");
}


MACHINE_DEFAULT_RAM(netgear)
{
	machine->physical_ram_in_mb = 16;
}


MACHINE_REGISTER(netgear)
{
	MR_DEFAULT(netgear, "NetGear WG602v1", ARCH_MIPS, MACHINE_NETGEAR);

	machine_entry_add_alias(me, "netgear");
	machine_entry_add_alias(me, "wg602v1");

	me->set_default_ram = machine_default_ram_netgear;
}

