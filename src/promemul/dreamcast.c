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
 *  $Id: dreamcast.c,v 1.7 2006-10-21 05:49:06 debug Exp $
 *
 *  Dreamcast PROM emulation.
 *
 *  NOTE: This is basically just a dummy module, for now.
 *
 *  See http://mc.pp.se/dc/syscalls.html for a description of what the
 *  PROM syscalls do. (The symbolic names in this module are the same as on
 *  that page.)
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


#define	DREAMCAST_ROMFONT_BASE		0xa0002000


/*
 *  dreamcast_romfont_init()
 *
 *  Initialize the ROM font.
 */
static void dreamcast_romfont_init(struct machine *machine)
{
	struct cpu *cpu = machine->cpus[0];
	int i, y, v;
	uint64_t d = DREAMCAST_ROMFONT_BASE;

	/*  TODO: A real font.  */

	/*  288 narrow glyphs (12 x 24 pixels):  */
	for (i=0; i<288; i++) {
		for (y=0; y<24; y+=2) {
			if (y <= 1 || y >= 22)
				v = 0;
			else
				v = random();
			store_byte(cpu, d++, v & 0x3f);
			store_byte(cpu, d++, v & 0xc3);
			store_byte(cpu, d++, v & 0xfc);
		}
	}

	/*  7078 wide glyphs (24 x 24 pixels):  */
	for (i=0; i<7078; i++) {
		for (y=0; y<24; y++) {
			if (y <= 1 || y >= 22)
				v = 0;
			else
				v = random();
			store_byte(cpu, d++, v & 0x3f);
			store_byte(cpu, d++, v);
			store_byte(cpu, d++, v & 0xfc);
		}
	}

	/*  129 VME icons (32 x 32 pixels):  */
	for (i=0; i<129; i++) {
		for (y=0; y<32; y++) {
			if (y <= 1 || y >= 30)
				v = 0;
			else
				v = random();
			store_byte(cpu, d++, v & 0x3f);
			store_byte(cpu, d++, v);
			store_byte(cpu, d++, v);
			store_byte(cpu, d++, v & 0xfc);
		}
	}
}


/*
 *  dreamcast_machine_setup():
 *
 *  Initializes pointers to Dreamcast PROM syscalls.
 */
void dreamcast_machine_setup(struct machine *machine)
{
	int i;
	struct cpu *cpu = machine->cpus[0];

	for (i=0xb0; i<=0xfc; i+=sizeof(uint32_t)) {
		/*  Store pointer to PROM routine...  */
		store_32bit_word(cpu, 0x8c000000 + i, 0x8c000100 + i);

		/*  ... which contains only 1 instruction, a special
		    0x00ff opcode which triggers PROM emulation:  */
		store_16bit_word(cpu, 0x8c000100 + i, 0x00ff);
	}

	/*  PROM reboot, in case someone jumps to 0xa0000000:  */
	store_16bit_word(cpu, 0xa0000000, 0x00ff);

	dreamcast_romfont_init(machine);
}


/*
 *  dreamcast_emul():
 */
int dreamcast_emul(struct cpu *cpu)
{
	int addr = cpu->pc & 0xff;
	int r1 = cpu->cd.sh.r[1];
	int r6 = cpu->cd.sh.r[6];
	int r7 = cpu->cd.sh.r[7];

	switch (addr) {

	case 0x00:
		/*  Special case: Reboot  */
		fatal("[ dreamcast reboot ]\n");
		cpu->running = 0;
		break;

	case 0xb0:
		/*  SYSINFO  */
		switch (r7) {
		default:fatal("[ SYSINFO: Unimplemented r7=%i ]\n", r7);
			goto bad;
		}
		break;

	case 0xb4:
		/*  ROMFONT  */
		switch (r1) {
		case 0:	/*  ROMFONT_ADDRESS  */
			cpu->cd.sh.r[0] = DREAMCAST_ROMFONT_BASE;
			break;
		default:fatal("[ ROMFONT: Unimplemented r1=%i ]\n", r1);
			goto bad;
		}
		break;

	case 0xb8:
		/*  FLASHROM  */
		switch (r7) {
		default:fatal("[ FLASHROM: Unimplemented r7=%i ]\n", r7);
			goto bad;
		}
		break;

	case 0xbc:
		switch ((int32_t)r6) {
		case 0:	/*  GD-ROM emulation  */
			switch (r7) {
			case 0:	/*  GDROM_SEND_COMMAND  */
				/*  TODO  */
				cpu->cd.sh.r[0] = -1;
				break;
			case 1:	/*  GDROM_CHECK_COMMAND  */
				/*  TODO  */
				cpu->cd.sh.r[0] = 0;
				break;
			case 2:	/*  GDROM_MAINLOOP  */
				/*  TODO  */
				break;
			case 3:	/*  GDROM_INIT  */
				/*  TODO: Do something here?  */
				break;
			default:fatal("[ GDROM: Unimplemented r7=%i ]\n", r7);
				goto bad;
			}
			break;
		default:fatal("[ 0xbc: Unimplemented r6=0x%x ]\n", r6);
			goto bad;
		}
		break;

	case 0xe0:
		/*  0x8c0000e0 is used by KallistOS arch_menu().  */
		fatal("[ Boot menu? TODO ]\n");
		goto bad;

	default:goto bad;
	}

	/*  Return from subroutine:  */
	cpu->pc = cpu->cd.sh.pr;

	return 1;

bad:
	cpu_register_dump(cpu->machine, cpu, 1, 0);
	printf("\n");
	fatal("[ dreamcast_emul(): unimplemented dreamcast PROM call ]\n");
	cpu->running = 0;
	return 1;
}


#endif	/*  ENABLE_SH  */
