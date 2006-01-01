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
 *  $Id: machine_testmips.c,v 1.1 2006-01-01 12:11:37 debug Exp $
 */

#include <stdio.h>
#include <string.h>

#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"
#include "mp.h"


MACHINE_SETUP(baremips)
{
	machine->machine_name = "Generic \"bare\" MIPS machine";
	machine->stable = 1;
	cpu->byte_order = EMUL_BIG_ENDIAN;
}


MACHINE_SETUP(testmips)
{
	/*
	 *  A MIPS test machine (which happens to work with the
	 *  code in my master's thesis).  :-)
	 *
	 *  IRQ map:
	 *      7       CPU counter
	 *      6       SMP IPIs
	 *      5       not used yet
	 *      4       not used yet
	 *      3       ethernet  
	 *      2       serial console
	 */

	char tmpstr[1000];

	machine->machine_name = "MIPS test machine";
	machine->stable = 1;
	cpu->byte_order = EMUL_BIG_ENDIAN;

	snprintf(tmpstr, sizeof(tmpstr), "cons addr=0x%llx irq=2",
	    (long long)DEV_CONS_ADDRESS);
	machine->main_console_handle = (size_t)device_add(machine, tmpstr);

	snprintf(tmpstr, sizeof(tmpstr), "mp addr=0x%llx",
	    (long long)DEV_MP_ADDRESS);
	device_add(machine, tmpstr);

	dev_fb_init(machine, machine->memory, DEV_FB_ADDRESS, VFB_GENERIC,
	    640,480, 640,480, 24, "testmips generic"); 

	snprintf(tmpstr, sizeof(tmpstr), "disk addr=0x%llx",
	    (long long)DEV_DISK_ADDRESS);
	device_add(machine, tmpstr);

	snprintf(tmpstr, sizeof(tmpstr), "ether addr=0x%llx irq=3",
	    (long long)DEV_ETHER_ADDRESS);
	device_add(machine, tmpstr);
}


MACHINE_DEFAULT_CPU(baremips)
{
	machine->cpu_name = strdup("R4000");
}


MACHINE_DEFAULT_CPU(testmips)
{
	machine->cpu_name = strdup("R4000");
}


MACHINE_REGISTER(baremips)
{
	MR_DEFAULT(baremips, "Generic \"bare\" MIPS machine",
	    ARCH_MIPS, MACHINE_BAREMIPS, 1, 0);
	me->aliases[0] = "baremips";
	machine_entry_add(me, ARCH_MIPS);
}


MACHINE_REGISTER(testmips)
{
	MR_DEFAULT(testmips, "Test-machine for MIPS",
	    ARCH_MIPS, MACHINE_TESTMIPS, 1, 0);
	me->aliases[0] = "testmips";
	machine_entry_add(me, ARCH_MIPS);
}

