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
 *  $Id: machine_sandpoint.c,v 1.2 2006-02-05 10:26:36 debug Exp $
 */

#include <stdio.h>
#include <string.h>

#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"


MACHINE_SETUP(sandpoint)
{
	/*
	 *  NetBSD/sandpoint (http://www.netbsd.org/Ports/sandpoint/)
	 */
	machine->machine_name = "Motorola Sandpoint";

	device_add(machine, "ns16550 irq=36 addr=0x800003f8");

	/*  r4 should point to first free byte after the loaded kernel:  */
	cpu->cd.ppc.gpr[4] = 6 * 1048576;

	if (!machine->prom_emulation)
		return;
}


MACHINE_DEFAULT_CPU(sandpoint)
{
	/*
	 *  According to NetBSD's page:
	 *
	 *  "Unity" module has an MPC8240.
	 *  "Altimus" module has an MPC7400 (G4) or an MPC107.
	 */

	machine->cpu_name = strdup("MPC7400");
}


MACHINE_REGISTER(sandpoint)
{
	MR_DEFAULT(sandpoint, "Motorola Sandpoint (PPC)", ARCH_PPC,
	    MACHINE_SANDPOINT, 1, 0);
	me->aliases[0] = "sandpoint";
	machine_entry_add(me, ARCH_PPC);
}

