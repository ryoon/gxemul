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
 *  $Id: dreamcast.c,v 1.2 2006-10-13 05:02:32 debug Exp $
 *
 *  Dreamcast PROM emulation.
 *
 *  NOTE: This is basically just a dummy module, for now.
 *
 *  See http://mc.pp.se/dc/syscalls.html for a description of what the
 *  PROM syscalls do.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "cpu.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"

#ifdef ENABLE_SH


/*
 *  dreamcast_machine_setup():
 *
 *  Initializes pointers to Dreamcast PROM syscalls.
 */
void dreamcast_machine_setup(struct machine *machine)
{
	int i;
	struct cpu *cpu = machine->cpus[0];

	for (i=0xb0; i<=0xbc; i+=sizeof(uint32_t)) {
		/*  Store pointer to PROM routine...  */
		store_32bit_word(cpu, 0x8c000000 + i, 0x8c000100 + i);

		/*  ... which contains only 1 instruction, a special
		    0x00ff opcode which triggers PROM emulation:  */
		store_16bit_word(cpu, 0x8c000100 + i, 0x00ff);
	}

	/*  PROM reboot, in case someone jumps to 0xa0000000:  */
	store_16bit_word(cpu, 0xa0000000, 0x00ff);
}


/*
 *  dreamcast_emul_gdrom():
 */
int dreamcast_emul_gdrom(struct cpu *cpu)
{
	int index = cpu->cd.sh.r[7];

	switch (index) {

	case 3:	/*  Init  */
		/*  TODO: Do something here?  */
		break;

	default:cpu_register_dump(cpu->machine, cpu, 1, 0);
		printf("\n");
		fatal("[ dreamcast_emul_gdrom(): unimplemented dreamcast gdrom "
		    "function 0x%"PRIx32" ]\n", index);
		cpu->running = 0;
	}

	return 1;
}


/*
 *  dreamcast_emul():
 */
int dreamcast_emul(struct cpu *cpu)
{
	int addr = cpu->pc & 0xff;

	switch (addr) {

	case 0x00:
		/*  Reboot  */
		cpu->running = 0;
		break;

	case 0xbc:
		/*  GD-ROM emulation  */
		dreamcast_emul_gdrom(cpu);
		break;

	default:cpu_register_dump(cpu->machine, cpu, 1, 0);
		printf("\n");
		fatal("[ dreamcast_emul(): unimplemented dreamcast PROM "
		    "address 0x%"PRIx32" ]\n", (uint32_t)cpu->pc);
		cpu->running = 0;
		return 1;
	}

	/*  Return from subroutine:  */
	cpu->pc = cpu->cd.sh.pr;

	return 1;
}


#endif	/*  ENABLE_SH  */
