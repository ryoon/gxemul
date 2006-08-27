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
 *  $Id: machine_chip8.c,v 1.3 2006-08-27 13:13:12 debug Exp $
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


MACHINE_SETUP(chip8)
{
	uint8_t fontchar[5];

	machine->machine_name = "CHIP8";

	if (!machine->use_x11)
		fatal("*\n"
		    "*  NOTE: Emulating a CHIP8 machine without -X is\n"
		    "*        pretty meaningless! Continuing anyway...\n"
		    "*\n");

	dev_fb_init(machine, machine->memory, CHIP8_FB_ADDR,
	    VFB_GENERIC,
	    machine->cpus[0]->cd.chip8.xres * machine->x11_scaleup,
	    machine->cpus[0]->cd.chip8.yres * machine->x11_scaleup,
	    machine->cpus[0]->cd.chip8.xres * machine->x11_scaleup,
	    machine->cpus[0]->cd.chip8.yres * machine->x11_scaleup,
	    8, "CHIP8");

	/*
	 *  5 pixels high font:
	 *  (TODO: 10 for superchip)
	 */
	/*  0  */
	fontchar[0] = 0x60;	/*  .XX.  */
	fontchar[1] = 0x90;	/*  X..X  */
	fontchar[2] = 0x90;	/*  X..X  */
	fontchar[3] = 0x90;	/*  X..X  */
	fontchar[4] = 0x60;	/*  .XX.  */
	store_buf(cpu, CHIP8_FONT_ADDR + 5 * 0, (char *)
	    fontchar, sizeof(fontchar));
	/*  1  */
	fontchar[0] = 0x20;	/*  ..X.  */
	fontchar[1] = 0x60;	/*  .XX.  */
	fontchar[2] = 0x20;	/*  ..X.  */
	fontchar[3] = 0x20;	/*  ..X.  */
	fontchar[4] = 0x20;	/*  ..X.  */
	store_buf(cpu, CHIP8_FONT_ADDR + 5 * 1, (char *)
	    fontchar, sizeof(fontchar));
	/*  2  */
	fontchar[0] = 0x60;	/*  .XX.  */
	fontchar[1] = 0x90;	/*  X..X  */
	fontchar[2] = 0x20;	/*  ..X.  */
	fontchar[3] = 0x40;	/*  .X..  */
	fontchar[4] = 0xf0;	/*  XXXX  */
	store_buf(cpu, CHIP8_FONT_ADDR + 5 * 2, (char *)
	    fontchar, sizeof(fontchar));
	/*  3  */
	fontchar[0] = 0xe0;	/*  XXX.  */
	fontchar[1] = 0x10;	/*  ...X  */
	fontchar[2] = 0x60;	/*  .XX.  */
	fontchar[3] = 0x10;	/*  ...X  */
	fontchar[4] = 0xe0;	/*  XXX.  */
	store_buf(cpu, CHIP8_FONT_ADDR + 5 * 3, (char *)
	    fontchar, sizeof(fontchar));
	/*  4  */
	fontchar[0] = 0x90;	/*  X..X  */
	fontchar[1] = 0x90;	/*  X..X  */
	fontchar[2] = 0xf0;	/*  XXXX  */
	fontchar[3] = 0x10;	/*  ...X  */
	fontchar[4] = 0x10;	/*  ...X  */
	store_buf(cpu, CHIP8_FONT_ADDR + 5 * 4, (char *)
	    fontchar, sizeof(fontchar));
	/*  5  */
	fontchar[0] = 0xf0;	/*  XXXX  */
	fontchar[1] = 0x80;	/*  X...  */
	fontchar[2] = 0xe0;	/*  XXX.  */
	fontchar[3] = 0x10;	/*  ...X  */
	fontchar[4] = 0xe0;	/*  XXX.  */
	store_buf(cpu, CHIP8_FONT_ADDR + 5 * 5, (char *)
	    fontchar, sizeof(fontchar));
	/*  6  */
	fontchar[0] = 0x70;	/*  .XXX  */
	fontchar[1] = 0x80;	/*  X...  */
	fontchar[2] = 0xe0;	/*  XXX.  */
	fontchar[3] = 0x90;	/*  X..X  */
	fontchar[4] = 0x60;	/*  .XX.  */
	store_buf(cpu, CHIP8_FONT_ADDR + 5 * 6, (char *)
	    fontchar, sizeof(fontchar));
	/*  7  */
	fontchar[0] = 0xf0;	/*  XXXX  */
	fontchar[1] = 0x10;	/*  ...X  */
	fontchar[2] = 0x20;	/*  ..X.  */
	fontchar[3] = 0x40;	/*  .X..  */
	fontchar[4] = 0x40;	/*  .X..  */
	store_buf(cpu, CHIP8_FONT_ADDR + 5 * 7, (char *)
	    fontchar, sizeof(fontchar));
	/*  8  */
	fontchar[0] = 0x60;	/*  .XX.  */
	fontchar[1] = 0x90;	/*  X..X  */
	fontchar[2] = 0x60;	/*  .XX.  */
	fontchar[3] = 0x90;	/*  X..X  */
	fontchar[4] = 0x60;	/*  .XX.  */
	store_buf(cpu, CHIP8_FONT_ADDR + 5 * 8, (char *)
	    fontchar, sizeof(fontchar));
	/*  9  */
	fontchar[0] = 0x60;	/*  .XX.  */
	fontchar[1] = 0x90;	/*  X..X  */
	fontchar[2] = 0x70;	/*  .XXX  */
	fontchar[3] = 0x10;	/*  ...X  */
	fontchar[4] = 0x60;	/*  .XX.  */
	store_buf(cpu, CHIP8_FONT_ADDR + 5 * 9, (char *)
	    fontchar, sizeof(fontchar));
	/*  A  */
	fontchar[0] = 0x60;	/*  .XX.  */
	fontchar[1] = 0x90;	/*  X..X  */
	fontchar[2] = 0xf0;	/*  XXXX  */
	fontchar[3] = 0x90;	/*  X..X  */
	fontchar[4] = 0x90;	/*  X..X  */
	store_buf(cpu, CHIP8_FONT_ADDR + 5 * 10, (char *)
	    fontchar, sizeof(fontchar));
	/*  B  */
	fontchar[0] = 0xe0;	/*  XXX.  */
	fontchar[1] = 0x90;	/*  X..X  */
	fontchar[2] = 0xe0;	/*  XXX.  */
	fontchar[3] = 0x90;	/*  X..X  */
	fontchar[4] = 0xe0;	/*  XXX.  */
	store_buf(cpu, CHIP8_FONT_ADDR + 5 * 11, (char *)
	    fontchar, sizeof(fontchar));
	/*  C  */
	fontchar[0] = 0x70;	/*  .XXX  */
	fontchar[1] = 0x80;	/*  X...  */
	fontchar[2] = 0x80;	/*  X...  */
	fontchar[3] = 0x80;	/*  X...  */
	fontchar[4] = 0x70;	/*  .XXX  */
	store_buf(cpu, CHIP8_FONT_ADDR + 5 * 12, (char *)
	    fontchar, sizeof(fontchar));
	/*  D  */
	fontchar[0] = 0xe0;	/*  XXX.  */
	fontchar[1] = 0x90;	/*  X..X  */
	fontchar[2] = 0x90;	/*  X..X  */
	fontchar[3] = 0x90;	/*  X..X  */
	fontchar[4] = 0xe0;	/*  XXX.  */
	store_buf(cpu, CHIP8_FONT_ADDR + 5 * 13, (char *)
	    fontchar, sizeof(fontchar));
	/*  E  */
	fontchar[0] = 0xf0;	/*  XXXX  */
	fontchar[1] = 0x80;	/*  X...  */
	fontchar[2] = 0xe0;	/*  XXX.  */
	fontchar[3] = 0x80;	/*  X...  */
	fontchar[4] = 0xf0;	/*  XXXX  */
	store_buf(cpu, CHIP8_FONT_ADDR + 5 * 14, (char *)
	    fontchar, sizeof(fontchar));
	/*  F  */
	fontchar[0] = 0xf0;	/*  XXXX  */
	fontchar[1] = 0x80;	/*  X...  */
	fontchar[2] = 0xe0;	/*  XXX.  */
	fontchar[3] = 0x80;	/*  X...  */
	fontchar[4] = 0x80;	/*  X...  */
	store_buf(cpu, CHIP8_FONT_ADDR + 5 * 15, (char *)
	    fontchar, sizeof(fontchar));

	/*  TODO: Keyboard input device!  */
}


MACHINE_DEFAULT_CPU(chip8)
{
	machine->cpu_name = strdup("CHIP8");
}


MACHINE_DEFAULT_RAM(chip8)
{
	machine->physical_ram_in_mb = 1;
}


MACHINE_REGISTER(chip8)
{
	MR_DEFAULT(chip8, "CHIP8", ARCH_CHIP8, MACHINE_CHIP8);
	me->set_default_ram = machine_default_ram_chip8;
	machine_entry_add_alias(me, "chip8");
}

