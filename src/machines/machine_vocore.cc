/*
 *  Copyright (C) 2018  Anders Gavare.  All rights reserved.
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
 *  COMMENT: VoCore
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


MACHINE_SETUP(vocore)
{
	machine->machine_name = strdup("VoCore");

	machine->emulated_hz = 360000000;

	/*  Some devices mentioned in the Linux kernel (as shown using strings):

		i/palmbus@10000000/spi@b00
		n/palmbus@10000000/spi@b40
		s/palmbus@10000000/uartlite@c00
		palmbus@10000000
		timer@100
		ethernet@10100000
		esw@10110000
		wmac@10180000
		ehci@101c0000
		ohci@101c1000
	*/

	device_add(machine, "palmbus addr=0x10000000");

	if (!machine->prom_emulation)
		return;

	// TODO: uboot emulation?
}


MACHINE_DEFAULT_CPU(vocore)
{
	// According to http://vocore.io/v1d.html: RT5350, 360 MHz, MIPS 24K
	// According to Linux booting on my real machine:
	//	CPU revision is: 0001964c (MIPS 24KEc)
	//	Primary instruction cache 32kB, VIPT, 4-way, linesize 32 bytes.
	//	Primary data cache 16kB, 4-way, VIPT, no aliases, linesize 32 bytes
	machine->cpu_name = strdup("24KEc");
}


MACHINE_DEFAULT_RAM(vocore)
{
	machine->physical_ram_in_mb = 32;
}


MACHINE_REGISTER(vocore)
{
	MR_DEFAULT(vocore, "VoCore", ARCH_MIPS, MACHINE_VOCORE);

	machine_entry_add_alias(me, "vocore");

	me->set_default_ram = machine_default_ram_vocore;
}

