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
 *  $Id: machine_avr.c,v 1.5 2006-06-24 10:19:19 debug Exp $
 *
 *  Experimental AVR machines.
 */

#include <stdio.h>
#include <string.h>

#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"


MACHINE_SETUP(bareavr)
{
	char tmpstr[200];
	machine->machine_name = "Generic \"bare\" AVR machine";
	machine->cycle_accurate = 1;
	machine->stable = 1;
	snprintf(tmpstr, sizeof(tmpstr), "avr addr=0x%"PRIx64,
	    (uint64_t) AVR_SRAM_BASE);
	device_add(machine, tmpstr);
}


MACHINE_DEFAULT_CPU(bareavr)
{
	machine->cpu_name = strdup("AVR");
}


MACHINE_DEFAULT_RAM(bareavr)
{
	/*  SRAM starts at 8 MB, and is 4 MB long.  */
	machine->physical_ram_in_mb = 12;
}


MACHINE_REGISTER(bareavr)
{
	MR_DEFAULT(bareavr, "Generic \"bare\" AVR machine",
	    ARCH_AVR, MACHINE_BAREAVR);

	machine_entry_add_alias(me, "bareavr");

	me->set_default_ram = machine_default_ram_bareavr;
}


/*****************************************************************************/


MACHINE_SETUP(avr_pal)
{
	char tmpstr[200];
	machine->machine_name = "AVR connected to a PAL TV";
	machine->cycle_accurate = 1;
	machine->stable = 1;
	snprintf(tmpstr, sizeof(tmpstr), "avr addr=0x%"PRIx64,
	    (uint64_t) AVR_SRAM_BASE);
	device_add(machine, tmpstr);
	device_add(machine, "pal");
}


MACHINE_DEFAULT_CPU(avr_pal)
{
	machine->cpu_name = strdup("AT90S2313");
}


MACHINE_DEFAULT_RAM(avr_pal)
{
	/*  SRAM starts at 8 MB, and is 4 MB long.  */
	machine->physical_ram_in_mb = 12;
}


MACHINE_REGISTER(avr_pal)
{
	MR_DEFAULT(avr_pal, "AVR connected to a PAL TV",
	    ARCH_AVR, MACHINE_AVR_PAL);

	machine_entry_add_alias(me, "avr_pal");

	me->set_default_ram = machine_default_ram_avr_pal;
}


/*****************************************************************************/


MACHINE_SETUP(avr_mahpong)
{
	char tmpstr[200];
	machine->machine_name = "AVR setup for Mahpong";
	machine->cycle_accurate = 1;
	machine->stable = 1;
	snprintf(tmpstr, sizeof(tmpstr), "avr addr=0x%"PRIx64,
	    (uint64_t) AVR_SRAM_BASE);
	device_add(machine, tmpstr);
	device_add(machine, "pal");
}


MACHINE_DEFAULT_CPU(avr_mahpong)
{
	machine->cpu_name = strdup("AT90S8515");
}


MACHINE_DEFAULT_RAM(avr_mahpong)
{
	/*  SRAM starts at 8 MB, and is 4 MB long.  */
	machine->physical_ram_in_mb = 12;
}


MACHINE_REGISTER(avr_mahpong)
{
	MR_DEFAULT(avr_mahpong, "AVR setup for Mahpong",
	    ARCH_AVR, MACHINE_AVR_MAHPONG);

	machine_entry_add_alias(me, "avr_mahpong");

	me->set_default_ram = machine_default_ram_avr_mahpong;
}

