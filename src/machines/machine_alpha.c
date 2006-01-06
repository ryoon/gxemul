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
 *  $Id: machine_alpha.c,v 1.1 2006-01-06 11:55:52 debug Exp $
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"

#include "alpha_rpb.h"


MACHINE_SETUP(alpha)
{
	switch (machine->machine_subtype) {
	case ST_DEC_3000_300:
		machine->machine_name = "Alpha DEC3000/300";
		break;
	case ST_EB164:
		machine->machine_name = "Alpha EB164";
		break;
	default:fatal("Huh? Unimplemented Alpha machine type %i.\n",
		    machine->machine_subtype);
		exit(1);
	}

	if (machine->prom_emulation) {
		struct rpb rpb;
		struct crb crb;
		struct ctb ctb;

		/*  TODO:  Most of these... They are used by NetBSD/alpha:  */
		/*  a0 = First free Page Frame Number  */
		/*  a1 = PFN of current Level 1 page table  */
		/*  a2 = Bootinfo magic  */
		/*  a3 = Bootinfo pointer  */
		/*  a4 = Bootinfo version  */
		cpu->cd.alpha.r[ALPHA_A0] = 16*1024*1024 / 8192;
		cpu->cd.alpha.r[ALPHA_A1] = 0;
		cpu->cd.alpha.r[ALPHA_A2] = 0;
		cpu->cd.alpha.r[ALPHA_A3] = 0;
		cpu->cd.alpha.r[ALPHA_A4] = 0;

		/*  HWRPB: Hardware Restart Parameter Block  */
		memset(&rpb, 0, sizeof(struct rpb));
		store_64bit_word_in_host(cpu, (unsigned char *)
		    &(rpb.rpb_phys), HWRPB_ADDR);
		strlcpy((char *)&(rpb.rpb_magic), "HWRPB", 8);
		store_64bit_word_in_host(cpu, (unsigned char *)
		    &(rpb.rpb_size), sizeof(struct rpb));
		store_64bit_word_in_host(cpu, (unsigned char *)
		    &(rpb.rpb_page_size), 8192);
		store_64bit_word_in_host(cpu, (unsigned char *)
		    &(rpb.rpb_type), machine->machine_subtype);
		store_64bit_word_in_host(cpu, (unsigned char *)
		    &(rpb.rpb_cc_freq), 100000000);
		store_64bit_word_in_host(cpu, (unsigned char *)
		    &(rpb.rpb_ctb_off), CTB_ADDR - HWRPB_ADDR);
		store_64bit_word_in_host(cpu, (unsigned char *)
		    &(rpb.rpb_crb_off), CRB_ADDR - HWRPB_ADDR);

		/*  CTB: Console Terminal Block  */
		memset(&ctb, 0, sizeof(struct ctb));
		store_64bit_word_in_host(cpu, (unsigned char *)
		    &(ctb.ctb_term_type), machine->use_x11?
		    CTB_GRAPHICS : CTB_PRINTERPORT);

		/*  CRB: Console Routine Block  */
		memset(&crb, 0, sizeof(struct crb));
		store_64bit_word_in_host(cpu, (unsigned char *)
		    &(crb.crb_v_dispatch), CRB_ADDR - 0x100);
		store_64bit_word(cpu, CRB_ADDR - 0x100 + 8, 0x10000);

		/*
		 *  Place a special "hack" palcode call at 0x10000:
		 *  (Hopefully nothing else will be there.)
		 */
		store_32bit_word(cpu, 0x10000, 0x3fffffe);

		store_buf(cpu, HWRPB_ADDR, (char *)&rpb, sizeof(struct rpb));
		store_buf(cpu, CTB_ADDR, (char *)&ctb, sizeof(struct ctb));
		store_buf(cpu, CRB_ADDR, (char *)&crb, sizeof(struct crb));
	}

	switch (machine->machine_subtype) {
	case ST_DEC_3000_300:
		machine->machine_name = "DEC 3000/300";
		machine->main_console_handle = (size_t)device_add(machine,
		    "z8530 addr=0x1b0200000 irq=0 addr_mult=4");
		break;
	case ST_EB164:
		machine->machine_name = "EB164";
		break;
	default:fatal("Unimplemented Alpha machine type %i\n",
		    machine->machine_subtype);
		exit(1);
	}
}


MACHINE_DEFAULT_CPU(alpha)
{
	machine->cpu_name = strdup("Alpha");
}


MACHINE_DEFAULT_RAM(alpha)
{
	machine->physical_ram_in_mb = 64;
}


MACHINE_REGISTER(alpha)
{
	MR_DEFAULT(alpha, "Alpha", ARCH_ALPHA, MACHINE_ALPHA, 1, 2);
	me->aliases[0] = "alpha";
	me->subtype[0] = machine_entry_subtype_new("DEC 3000/300",
	    ST_DEC_3000_300, 1);
	me->subtype[0]->aliases[0] = "3000/300";
	me->subtype[1] = machine_entry_subtype_new("EB164", ST_EB164, 1);
	me->subtype[1]->aliases[0] = "eb164";
	me->set_default_ram = machine_default_ram_alpha;
	machine_entry_add(me, ARCH_ALPHA);
}

