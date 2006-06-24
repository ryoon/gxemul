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
 *  $Id: machine_sonynews.c,v 1.2 2006-06-24 10:19:19 debug Exp $
 */

#include <stdio.h>
#include <string.h>

#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"


MACHINE_SETUP(sonynews)
{
	/*
	 *  There are several models, according to
	 *  http://www.netbsd.org/Ports/newsmips/:
	 *
	 *  "R3000 and hyper-bus based models"
	 *	NWS-3470D, -3410, -3460, -3710, -3720
	 *
	 *  "R4000/4400 and apbus based models"
	 *	NWS-5000
	 *
	 *  For example: (found using google)
	 *
	 *    cpu_model = news3700
	 *    SONY NET WORK STATION, Model NWS-3710, Machine ID #30145
	 *    cpu0: MIPS R3000 (0x220) Rev. 2.0 with MIPS R3010 Rev.2.0
	 *    64KB/4B direct-mapped I, 64KB/4B direct-mapped w-thr. D
	 *
	 *  See http://katsu.watanabe.name/doc/sonynews/model.html
	 *  for more details.
	 */

	cpu->byte_order = EMUL_BIG_ENDIAN;
	machine->machine_name = "Sony NeWS (NET WORK STATION)";

	if (machine->prom_emulation) {
		/*  This is just a test.  TODO  */
		int i;
		for (i=0; i<32; i++)
			cpu->cd.mips.gpr[i] =
			    0x01230000 + (i << 8) + 0x55;
	}

	machine->main_console_handle = (size_t)device_add(machine,
	    "z8530 addr=0x1e950000 irq=0 addr_mult=4");
}


MACHINE_DEFAULT_CPU(sonynews)
{
	machine->cpu_name = strdup("R3000");
}


MACHINE_REGISTER(sonynews)
{
	MR_DEFAULT(sonynews, "Sony NeWS (MIPS)", ARCH_MIPS, MACHINE_SONYNEWS);

	machine_entry_add_alias(me, "sonynews");
}

