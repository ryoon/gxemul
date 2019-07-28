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
 *  COMMENT: LUNA 88K PROM emulation
 *
 *  For LUNA 88K emulation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "console.h"
#include "cpu.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"

#include "thirdparty/luna88k_board.h"


/*
 *  luna88kprom_init():
 */
void luna88kprom_init(struct machine *machine)
{
	struct cpu *cpu = machine->cpus[0];

        /*
         *  Memory layout according to OpenBSD's locore0.S:
         *
         *      0x00000 - 0x00fff = trap vectors
         *      0x01000 - 0x1ffff = ROM monitor work area
         *      0x20000 - ...     = Boot loader jumps here
	 *
	 *  The boot loader stage before loading OpenBSD's kernel seems
	 *  to be loaded at 0x00700000.
         */
 
        /*  0x00001100: ROM function table. See OpenBSD's machdep.c  */
        store_32bit_word(cpu, 0x1100 + sizeof(uint32_t) * 3, 0x2030);       /*  ROM console getch  */
        store_32bit_word(cpu, 0x1100 + sizeof(uint32_t) * 4, 0x2040);       /*  ROM console putch  */

        store_32bit_word(cpu, 0x2030, M88K_PROM_INSTR);
        store_32bit_word(cpu, 0x2034, 0xf400c001);	/*  jmp (r1)  */

        store_32bit_word(cpu, 0x2040, M88K_PROM_INSTR);
        store_32bit_word(cpu, 0x2044, 0xf400c001);	/*  jmp (r1)  */
  
        /*  0x00001114: Framebuffer depth  */
        store_32bit_word(cpu, 0x1114, machine->x11_md.in_use ? 8 : 0);
}


/*
 *  luna88kprom_emul():
 *
 *  Input:
 *	pc is used to figure out function number
 *	r2 = first argument (for functions that take arguments)
 *
 *  Output:
 *	r2 = result
 */
int luna88kprom_emul(struct cpu *cpu)
{
	int func = (cpu->pc & 0xf0) >> 4;

	switch (func) {

	case 4:
		console_putchar(cpu->machine->main_console_handle, cpu->cd.m88k.r[2]);
		break;

	default:
		cpu_register_dump(cpu->machine, cpu, 1, 0);
		cpu_register_dump(cpu->machine, cpu, 0, 1);
		fatal("[ LUNA88K PROM emulation: unimplemented function 0x%" PRIx32" ]\n", func);
		cpu->running = 0;
		return 0;
	}

	return 1;
}

