/*
 *  Copyright (C) 2007  Anders Gavare.  All rights reserved.
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
 *  $Id: machine_mmeye.c,v 1.1 2007-05-26 22:26:31 debug Exp $
 *
 *  mmEye machine.
 *
 *  TODO:
 *	GXemul doesn't support SH3 emulation yet, only SH4, so this
 *	doesn't really work.
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
#include "sh4_scireg.h"


MACHINE_SETUP(mmeye)
{
	machine->machine_name = "mmEye";

	/*  100 MHz SH3 CPU clock:  */
	if (machine->emulated_hz == 0)
		machine->emulated_hz = 100000000;

	/*  Note: 16 MB RAM at 0x0c000000, not at 0x00000000.  */
	dev_ram_init(machine, 0x0c000000, 16 * 1048576, DEV_RAM_RAM, 0x0);

	/*  Serial console:  */
	device_add(machine, "ns16550 addr=0x04000000");

	if (!machine->prom_emulation)
		return;

}


MACHINE_DEFAULT_CPU(mmeye)
{
	/*  Hitachi SH3 SH7708R, 100 MHz  */
	machine->cpu_name = strdup("SH7708R");
}


MACHINE_DEFAULT_RAM(mmeye)
{
	/*  Note: This is the size of the boot ROM area, since the
	    mmEye's RAM isn't located at physical address zero.  */
	machine->physical_ram_in_mb = 2;
}


MACHINE_REGISTER(mmeye)
{
	MR_DEFAULT(mmeye, "mmEye", ARCH_SH, MACHINE_MMEYE);
	me->set_default_ram = machine_default_ram_mmeye;

	machine_entry_add_alias(me, "mmeye");
}

