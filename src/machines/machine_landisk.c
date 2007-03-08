/*
 *  Copyright (C) 2006-2007  Anders Gavare.  All rights reserved.
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
 *  $Id: machine_landisk.c,v 1.7 2007-03-08 10:01:50 debug Exp $
 *
 *  I-O DATA LANDISK USL-5P.
 *
 *  This machine consists of:
 *
 *	o)  An SH4 processor, which includes serial console etc,
 *	o)  an IDE controller at address 0x14000000,
 *	o)  and a minimal SH-IPL+G PROM emulation layer (required to make
 *	    OpenBSD/landisk boot).
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

#include "sh4_exception.h"


MACHINE_SETUP(landisk)
{
	char tmpstr[300];

	machine->machine_name = "Landisk USL-5P";

	/*  200 MHz SH4 CPU clock:  */
	if (machine->emulated_hz == 0)
		machine->emulated_hz = 200000000;

	/*  50 MHz SH4 PCLOCK:  */
	machine->cpus[machine->bootstrap_cpu]->cd.sh.pclock = 50000000;

	dev_ram_init(machine, 0x0c000000, 64 * 1048576, DEV_RAM_RAM, 0x0);

	/*  wdc0 at obio0 port 0x14000000-0x1400000f irq 10  */
	snprintf(tmpstr, sizeof(tmpstr), "wdc irq=%s.cpu[%i].irq[0x%x]"
	    " addr_mult=2 addr=0x14000000",
	    machine->path, machine->bootstrap_cpu, SH4_INTEVT_IRQ10);
	device_add(machine, tmpstr);

	if (!machine->prom_emulation)
		return;

	/*
	 *  Ugly hardcoded register contents at bootup:
	 *
	 *  r4 (arg 0) = boot howto flags
	 *  r5 (arg 1) = bootinfo pointer for NetBSD (?) and
	 *               symbol end pointer for OpenBSD (?)
	 *
	 *  TODO: Make nicer.
	 */
	cpu->cd.sh.r[4] = 0;
	cpu->cd.sh.r[5] = 0x8c000000 + 8 * 1048576;	/*  Note/TODO:
				Assuming hardcoded 8 MB kernel size!  */

	sh_ipl_g_emul_init(machine);
}


MACHINE_DEFAULT_CPU(landisk)
{
	/*  Hitachi SH4 7751R, 200 MHz  */
	machine->cpu_name = strdup("SH7751R");
}


MACHINE_DEFAULT_RAM(landisk)
{
	/*  Note: This is the size of the boot ROM area, since the
	    Landisk's RAM isn't located at physical address zero.  */
	machine->physical_ram_in_mb = 2;
}


MACHINE_REGISTER(landisk)
{
	MR_DEFAULT(landisk, "Landisk", ARCH_SH, MACHINE_LANDISK);
	me->set_default_ram = machine_default_ram_landisk;
	machine_entry_add_alias(me, "landisk");
}

