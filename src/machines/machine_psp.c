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
 *  $Id: machine_psp.c,v 1.1 2006-01-10 20:30:05 debug Exp $
 */

#include <stdio.h>
#include <string.h>

#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"


MACHINE_SETUP(psp)
{
	struct vfb_data *fb;

	/*
	 *  The Playstation Portable seems to be a strange beast.
	 *
	 *  http://yun.cup.com/psppg004.html (in Japanese) seems to
	 *  suggest that virtual addresses are not displaced by
	 *  0x80000000 as on normal CPUs, but by 0x40000000?
	 */
	machine->machine_name = "Playstation Portable";
	cpu->byte_order = EMUL_LITTLE_ENDIAN;

	if (!machine->use_x11)
		fprintf(stderr, "-------------------------------------"
		    "------------------------------------------\n"
		    "\n  WARNING! You are emulating a PSP without -X. "
		    "You will miss graphical output!\n\n"
		    "-------------------------------------"
		    "------------------------------------------\n");

	/*  480 x 272 pixels framebuffer (512 bytes per line)  */
	fb = dev_fb_init(machine, machine->memory, 0x04000000, VFB_HPC,
	    480,272, 512,1088, -15, "Playstation Portable");

	/*
	 *  TODO/NOTE: This is ugly, but necessary since GXemul doesn't
	 *  emulate any MIPS CPU without MMU right now.
	 */
	mips_coproc_tlb_set_entry(cpu, 0, 1048576*16, 0x44000000 /*vaddr*/,
	    0x4000000, 0x4000000 + 1048576*16,  1,1,1,1,1, 0, 2, 2);
	mips_coproc_tlb_set_entry(cpu, 1, 1048576*16, 0x8000000 /*vaddr*/,
	    0x0, 0x0 + 1048576*16, 1,1,1,1,1, 0, 2, 2);
	mips_coproc_tlb_set_entry(cpu, 2, 1048576*16,
	    0x9000000 /*vaddr*/, 0x01000000, 0x01000000 + 1048576*16,
	    1,1,1,1,1, 0, 2, 2);
	mips_coproc_tlb_set_entry(cpu, 3, 1048576*16,
	    0x0 /*vaddr*/, 0, 0 + 1048576*16, 1,1,1,1,1, 0, 2, 2);

	cpu->cd.mips.gpr[MIPS_GPR_SP] = 0xfff0;
}


MACHINE_DEFAULT_CPU(psp)
{
	machine->cpu_name = strdup("Allegrex");
}


MACHINE_DEFAULT_RAM(psp)
{
	/*
	 *  According to
	 *  http://wiki.ps2dev.org/psp:memory_map:
	 *      0×08000000 = 8 MB kernel memory
	 *      0×08800000 = 24 MB user memory
	 */
	machine->physical_ram_in_mb = 8 + 24;
}


MACHINE_REGISTER(psp)
{
	MR_DEFAULT(psp, "Playstation Portable", ARCH_MIPS, MACHINE_PSP, 1, 0);
	me->aliases[0] = "psp";
	me->set_default_ram = machine_default_ram_psp;
	machine_entry_add(me, ARCH_MIPS);
}

