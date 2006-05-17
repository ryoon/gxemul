/*
 *  Copyright (C) 2006  Anders Gavare.  All rights reserved.
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
 *  $Id: machine_sparc.c,v 1.2 2006-05-17 20:27:31 debug Exp $
 *
 *  SPARC machines.
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


MACHINE_SETUP(sparc)
{
	switch (machine->machine_subtype) {

	case MACHINE_SPARC_SS5:
		machine->machine_name = "SUN SPARCstation 5";

		break;

	case MACHINE_SPARC_SS20:
		machine->machine_name = "SUN SPARCstation 20";

		break;

	case MACHINE_SPARC_ULTRA1:
		machine->machine_name = "SUN Ultra1";

		break;

	case MACHINE_SPARC_ULTRA60:
		machine->machine_name = "SUN Ultra60";

		break;

	default:fatal("Unimplemented SPARC machine subtype %i\n",
		    machine->machine_subtype);
		exit(1);
	}

	if (!machine->prom_emulation)
		return;

}


MACHINE_DEFAULT_CPU(sparc)
{
	switch (machine->machine_subtype) {

	case MACHINE_SPARC_SS5:
		machine->cpu_name = strdup("MB86907");
		break;

	case MACHINE_SPARC_SS20:
		machine->cpu_name = strdup("TMS390Z50");
		break;

	case MACHINE_SPARC_ULTRA1:
		machine->cpu_name = strdup("UltraSPARC");
		break;

	case MACHINE_SPARC_ULTRA60:
		machine->cpu_name = strdup("UltraSPARC-II");
		break;

	default:fatal("Unimplemented SPARC machine subtype %i\n",
		    machine->machine_subtype);
		exit(1);
	}
}


MACHINE_DEFAULT_RAM(sparc)
{
	machine->physical_ram_in_mb = 64;
}


MACHINE_REGISTER(sparc)
{
	MR_DEFAULT(sparc, "SPARC", ARCH_SPARC, MACHINE_SPARC, 1, 4);

	me->aliases[0] = "sparc";

	me->subtype[0] = machine_entry_subtype_new(
	    "SUN SPARCstation 5", MACHINE_SPARC_SS5, 1);
	me->subtype[0]->aliases[0] = "ss5";

	me->subtype[1] = machine_entry_subtype_new(
	    "SUN SPARCstation 20", MACHINE_SPARC_SS20, 1);
	me->subtype[1]->aliases[0] = "ss20";

	me->subtype[2] = machine_entry_subtype_new(
	    "SUN Ultra1", MACHINE_SPARC_ULTRA1, 1);
	me->subtype[2]->aliases[0] = "ultra1";

	me->subtype[3] = machine_entry_subtype_new(
	    "SUN Ultra60", MACHINE_SPARC_ULTRA60, 1);
	me->subtype[3]->aliases[0] = "ultra60";

	me->set_default_ram = machine_default_ram_sparc;

	machine_entry_add(me, ARCH_SPARC);
}

