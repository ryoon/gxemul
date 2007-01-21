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
 *  $Id: machine_walnut.c,v 1.4 2007-01-21 21:02:57 debug Exp $
 *
 *  TODO: Other evbppc machines?
 */

#include <stdio.h>
#include <string.h>

#include "bus_isa.h"
#include "bus_pci.h"
#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"


MACHINE_SETUP(walnut)
{
	/*
	 *  NetBSD/evbppc (http://www.netbsd.org/Ports/evbppc/)
	 */
	machine->machine_name = "Walnut evaluation board";

	machine->main_console_handle = (size_t)device_add(machine,
	    "ns16550 irq=0 addr=0xef600300");

	/*  OpenBIOS board config data:  */
	dev_ram_init(machine, 0xfffe0b50, 64, DEV_RAM_RAM, 0);
	store_32bit_word(cpu, 0xfffe0b50, 0xfffe0b54);
	store_32bit_word(cpu, 0xfffe0b54, 0x4e800020);  /*  blr  */
	store_32bit_word(cpu, 0xfffe0b74, machine->physical_ram_in_mb << 20);
	store_32bit_word(cpu, 0xfffe0b84, machine->emulated_hz);
	store_32bit_word(cpu, 0xfffe0b88, 33000000);
	store_32bit_word(cpu, 0xfffe0b8c, 66000000);

#if 0
        unsigned char   usr_config_ver[4];
        unsigned char   rom_sw_ver[30];
        unsigned int    mem_size;
        unsigned char   mac_address_local[6];
        unsigned char   mac_address_pci[6];
        unsigned int    processor_speed;
        unsigned int    plb_speed;
        unsigned int    pci_speed;
#endif
}


MACHINE_DEFAULT_CPU(walnut)
{
	machine->cpu_name = strdup("PPC405GP");
}


MACHINE_REGISTER(walnut)
{
	MR_DEFAULT(walnut, "Walnut evaluation board", ARCH_PPC, MACHINE_WALNUT);

	machine_entry_add_alias(me, "evbppc");
	machine_entry_add_alias(me, "walnut");
}

