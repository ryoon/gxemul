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
 *  $Id: machine_test.c,v 1.2 2006-02-24 00:20:42 debug Exp $
 *
 *  Various "test" machines (bare machines with just a CPU, or a bare machine
 *  plus some experimental devices).
 */

#include <stdio.h>
#include <string.h>

#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"
#include "mp.h"


static void default_test(struct machine *machine, struct cpu *cpu)
{
	char tmpstr[1000];
	snprintf(tmpstr, sizeof(tmpstr), "cons addr=0x%llx irq=0",
	    (long long)DEV_CONS_ADDRESS);
	machine->main_console_handle = (size_t)device_add(machine, tmpstr);

	snprintf(tmpstr, sizeof(tmpstr), "mp addr=0x%llx",
	    (long long)DEV_MP_ADDRESS);
	device_add(machine, tmpstr);

	dev_fb_init(machine, machine->memory, DEV_FB_ADDRESS, VFB_GENERIC,
	    640,480, 640,480, 24, "generic"); 

	snprintf(tmpstr, sizeof(tmpstr), "disk addr=0x%llx",
	    (long long)DEV_DISK_ADDRESS);
	device_add(machine, tmpstr);

	snprintf(tmpstr, sizeof(tmpstr), "ether addr=0x%llx irq=0",
	    (long long)DEV_ETHER_ADDRESS);
	device_add(machine, tmpstr);
}


MACHINE_SETUP(barealpha)
{
	machine->machine_name = "Generic \"bare\" Alpha machine";
	machine->stable = 1;
}


MACHINE_SETUP(testalpha)
{
	machine->machine_name = "Alpha test machine";
	machine->stable = 1;

	/*  TODO: interrupt for Alpha?  */

	default_test(machine, cpu);
}


MACHINE_DEFAULT_CPU(barealpha)
{
	machine->cpu_name = strdup("Alpha");
}


MACHINE_DEFAULT_CPU(testalpha)
{
	machine->cpu_name = strdup("Alpha");
}


MACHINE_REGISTER(barealpha)
{
	MR_DEFAULT(barealpha, "Generic \"bare\" Alpha machine",
	    ARCH_ALPHA, MACHINE_BAREALPHA, 1, 0);
	me->aliases[0] = "barealpha";
	machine_entry_add(me, ARCH_ALPHA);
}


MACHINE_REGISTER(testalpha)
{
	MR_DEFAULT(testalpha, "Test-machine for Alpha",
	    ARCH_ALPHA, MACHINE_TESTALPHA, 1, 0);
	me->aliases[0] = "testalpha";
	machine_entry_add(me, ARCH_ALPHA);
}


MACHINE_SETUP(barearm)
{
	machine->machine_name = "Generic \"bare\" ARM machine";
	machine->stable = 1;
}


MACHINE_SETUP(testarm)
{
	machine->machine_name = "ARM test machine";
	machine->stable = 1;

	/*  TODO: interrupt for ARM?  */

	default_test(machine, cpu);

	/*
	 *  Place a tiny stub at end of memory, and set the link register to
	 *  point to it. This stub halts the machine (making it easy to try
	 *  out simple stand-alone C functions).
	 */
	cpu->cd.arm.r[ARM_SP] = machine->physical_ram_in_mb * 1048576 - 4096;
	cpu->cd.arm.r[ARM_LR] = cpu->cd.arm.r[ARM_SP] + 32;
	store_32bit_word(cpu, cpu->cd.arm.r[ARM_LR] + 0, 0xe3a00201);
	store_32bit_word(cpu, cpu->cd.arm.r[ARM_LR] + 4, 0xe5c00010);
	store_32bit_word(cpu, cpu->cd.arm.r[ARM_LR] + 8, 0xeafffffe);
}


MACHINE_DEFAULT_CPU(barearm)
{
	machine->cpu_name = strdup("SA1110");
}


MACHINE_DEFAULT_CPU(testarm)
{
	machine->cpu_name = strdup("SA1110");
}


MACHINE_REGISTER(barearm)
{
	MR_DEFAULT(barearm, "Generic \"bare\" ARM machine",
	    ARCH_ARM, MACHINE_BAREARM, 1, 0);
	me->aliases[0] = "barearm";
	machine_entry_add(me, ARCH_ARM);
}


MACHINE_REGISTER(testarm)
{
	MR_DEFAULT(testarm, "Test-machine for ARM",
	    ARCH_ARM, MACHINE_TESTARM, 1, 0);
	me->aliases[0] = "testarm";
	machine_entry_add(me, ARCH_ARM);
}



MACHINE_SETUP(bareavr)
{
	machine->machine_name = "Generic \"bare\" AVR machine";
	machine->stable = 1;
}


MACHINE_DEFAULT_CPU(bareavr)
{
	machine->cpu_name = strdup("AVR");
}


MACHINE_REGISTER(bareavr)
{
	MR_DEFAULT(bareavr, "Generic \"bare\" AVR machine",
	    ARCH_AVR, MACHINE_BAREAVR, 1, 0);
	me->aliases[0] = "bareavr";
	machine_entry_add(me, ARCH_AVR);
}


MACHINE_SETUP(barehppa)
{
	machine->machine_name = "Generic \"bare\" HPPA machine";
	machine->stable = 1;
}


MACHINE_SETUP(testhppa)
{
	machine->machine_name = "HPPA test machine";
	machine->stable = 1;

	/*  TODO: interrupt for HPPA?  */

	default_test(machine, cpu);
}


MACHINE_DEFAULT_CPU(barehppa)
{
	machine->cpu_name = strdup("HPPA");
}


MACHINE_DEFAULT_CPU(testhppa)
{
	machine->cpu_name = strdup("HPPA");
}


MACHINE_REGISTER(barehppa)
{
	MR_DEFAULT(barehppa, "Generic \"bare\" HPPA machine",
	    ARCH_HPPA, MACHINE_BAREHPPA, 1, 0);
	me->aliases[0] = "barehppa";
	machine_entry_add(me, ARCH_HPPA);
}


MACHINE_REGISTER(testhppa)
{
	MR_DEFAULT(testhppa, "Test-machine for HPPA",
	    ARCH_HPPA, MACHINE_TESTHPPA, 1, 0);
	me->aliases[0] = "testhppa";
	machine_entry_add(me, ARCH_HPPA);
}


MACHINE_SETUP(barei960)
{
	machine->machine_name = "Generic \"bare\" i960 machine";
	machine->stable = 1;
}


MACHINE_SETUP(testi960)
{
	machine->machine_name = "i960 test machine";
	machine->stable = 1;

	/*  TODO: interrupt for i960?  */

	default_test(machine, cpu);
}


MACHINE_DEFAULT_CPU(barei960)
{
	machine->cpu_name = strdup("i960");
}


MACHINE_DEFAULT_CPU(testi960)
{
	machine->cpu_name = strdup("i960");
}


MACHINE_REGISTER(barei960)
{
	MR_DEFAULT(barei960, "Generic \"bare\" i960 machine",
	    ARCH_I960, MACHINE_BAREI960, 1, 0);
	me->aliases[0] = "barei960";
	machine_entry_add(me, ARCH_I960);
}


MACHINE_REGISTER(testi960)
{
	MR_DEFAULT(testi960, "Test-machine for i960",
	    ARCH_I960, MACHINE_TESTI960, 1, 0);
	me->aliases[0] = "testi960";
	machine_entry_add(me, ARCH_I960);
}


MACHINE_SETUP(bareia64)
{
	machine->machine_name = "Generic \"bare\" IA64 machine";
	machine->stable = 1;
}


MACHINE_SETUP(testia64)
{
	machine->machine_name = "IA64 test machine";
	machine->stable = 1;

	/*  TODO: interrupt for IA64?  */

	default_test(machine, cpu);
}


MACHINE_DEFAULT_CPU(bareia64)
{
	machine->cpu_name = strdup("IA64");
}


MACHINE_DEFAULT_CPU(testia64)
{
	machine->cpu_name = strdup("IA64");
}


MACHINE_REGISTER(bareia64)
{
	MR_DEFAULT(bareia64, "Generic \"bare\" IA64 machine",
	    ARCH_IA64, MACHINE_BAREIA64, 1, 0);
	me->aliases[0] = "bareia64";
	machine_entry_add(me, ARCH_IA64);
}


MACHINE_REGISTER(testia64)
{
	MR_DEFAULT(testia64, "Test-machine for IA64",
	    ARCH_IA64, MACHINE_TESTIA64, 1, 0);
	me->aliases[0] = "testia64";
	machine_entry_add(me, ARCH_IA64);
}


MACHINE_SETUP(barem68k)
{
	machine->machine_name = "Generic \"bare\" M68K machine";
	machine->stable = 1;
}


MACHINE_SETUP(testm68k)
{
	machine->machine_name = "M68K test machine";
	machine->stable = 1;

	/*  TODO: interrupt for M68K?  */

	default_test(machine, cpu);
}


MACHINE_DEFAULT_CPU(barem68k)
{
	machine->cpu_name = strdup("68020");
}


MACHINE_DEFAULT_CPU(testm68k)
{
	machine->cpu_name = strdup("68020");
}


MACHINE_REGISTER(barem68k)
{
	MR_DEFAULT(barem68k, "Generic \"bare\" M68K machine",
	    ARCH_M68K, MACHINE_BAREM68K, 1, 0);
	me->aliases[0] = "barem68k";
	machine_entry_add(me, ARCH_M68K);
}


MACHINE_REGISTER(testm68k)
{
	MR_DEFAULT(testm68k, "Test-machine for M68K",
	    ARCH_M68K, MACHINE_TESTM68K, 1, 0);
	me->aliases[0] = "testm68k";
	machine_entry_add(me, ARCH_M68K);
}


MACHINE_SETUP(baremips)
{
	machine->machine_name = "Generic \"bare\" MIPS machine";
	machine->stable = 1;
	cpu->byte_order = EMUL_BIG_ENDIAN;
}


MACHINE_SETUP(testmips)
{
	/*
	 *  A MIPS test machine (which happens to work with the
	 *  code in my master's thesis).  :-)
	 *
	 *  IRQ map:
	 *      7       CPU counter
	 *      6       SMP IPIs
	 *      5       not used yet
	 *      4       not used yet
	 *      3       ethernet  
	 *      2       serial console
	 */

	char tmpstr[1000];

	machine->machine_name = "MIPS test machine";
	machine->stable = 1;
	cpu->byte_order = EMUL_BIG_ENDIAN;

	snprintf(tmpstr, sizeof(tmpstr), "cons addr=0x%llx irq=2",
	    (long long)DEV_CONS_ADDRESS);
	machine->main_console_handle = (size_t)device_add(machine, tmpstr);

	snprintf(tmpstr, sizeof(tmpstr), "mp addr=0x%llx",
	    (long long)DEV_MP_ADDRESS);
	device_add(machine, tmpstr);

	dev_fb_init(machine, machine->memory, DEV_FB_ADDRESS, VFB_GENERIC,
	    640,480, 640,480, 24, "testmips generic"); 

	snprintf(tmpstr, sizeof(tmpstr), "disk addr=0x%llx",
	    (long long)DEV_DISK_ADDRESS);
	device_add(machine, tmpstr);

	snprintf(tmpstr, sizeof(tmpstr), "ether addr=0x%llx irq=3",
	    (long long)DEV_ETHER_ADDRESS);
	device_add(machine, tmpstr);
}


MACHINE_DEFAULT_CPU(baremips)
{
	machine->cpu_name = strdup("R4000");
}


MACHINE_DEFAULT_CPU(testmips)
{
	machine->cpu_name = strdup("R4000");
}


MACHINE_REGISTER(baremips)
{
	MR_DEFAULT(baremips, "Generic \"bare\" MIPS machine",
	    ARCH_MIPS, MACHINE_BAREMIPS, 1, 0);
	me->aliases[0] = "baremips";
	machine_entry_add(me, ARCH_MIPS);
}


MACHINE_REGISTER(testmips)
{
	MR_DEFAULT(testmips, "Test-machine for MIPS",
	    ARCH_MIPS, MACHINE_TESTMIPS, 1, 0);
	me->aliases[0] = "testmips";
	machine_entry_add(me, ARCH_MIPS);
}


MACHINE_SETUP(bareppc)
{
	machine->machine_name = "Generic \"bare\" PPC machine";
	machine->stable = 1;
}


MACHINE_SETUP(testppc)
{
	machine->machine_name = "PPC test machine";
	machine->stable = 1;

	/*  TODO: interrupt for PPC?  */

	default_test(machine, cpu);
}


MACHINE_DEFAULT_CPU(bareppc)
{
	machine->cpu_name = strdup("PPC970");
}


MACHINE_DEFAULT_CPU(testppc)
{
	machine->cpu_name = strdup("PPC970");
}


MACHINE_REGISTER(bareppc)
{
	MR_DEFAULT(bareppc, "Generic \"bare\" PPC machine",
	    ARCH_PPC, MACHINE_BAREPPC, 1, 0);
	me->aliases[0] = "bareppc";
	machine_entry_add(me, ARCH_PPC);
}


MACHINE_REGISTER(testppc)
{
	MR_DEFAULT(testppc, "Test-machine for PPC",
	    ARCH_PPC, MACHINE_TESTPPC, 1, 0);
	me->aliases[0] = "testppc";
	machine_entry_add(me, ARCH_PPC);
}


MACHINE_SETUP(baresh)
{
	machine->machine_name = "Generic \"bare\" SH machine";
	machine->stable = 1;
}


MACHINE_SETUP(testsh)
{
	machine->machine_name = "SH test machine";
	machine->stable = 1;

	/*  TODO: interrupt for SH?  */

	default_test(machine, cpu);
}


MACHINE_DEFAULT_CPU(baresh)
{
	machine->cpu_name = strdup("SH");
}


MACHINE_DEFAULT_CPU(testsh)
{
	machine->cpu_name = strdup("SH");
}


MACHINE_REGISTER(baresh)
{
	MR_DEFAULT(baresh, "Generic \"bare\" SH machine",
	    ARCH_SH, MACHINE_BARESH, 1, 0);
	me->aliases[0] = "baresh";
	machine_entry_add(me, ARCH_SH);
}


MACHINE_REGISTER(testsh)
{
	MR_DEFAULT(testsh, "Test-machine for SH",
	    ARCH_SH, MACHINE_TESTSH, 1, 0);
	me->aliases[0] = "testsh";
	machine_entry_add(me, ARCH_SH);
}


MACHINE_SETUP(baresparc)
{
	machine->machine_name = "Generic \"bare\" SPARC machine";
	machine->stable = 1;
}


MACHINE_SETUP(testsparc)
{
	machine->machine_name = "SPARC test machine";
	machine->stable = 1;

	/*  TODO: interrupt for SPARC?  */

	default_test(machine, cpu);
}


MACHINE_DEFAULT_CPU(baresparc)
{
	machine->cpu_name = strdup("SPARCv9");
}


MACHINE_DEFAULT_CPU(testsparc)
{
	machine->cpu_name = strdup("SPARCv9");
}


MACHINE_REGISTER(baresparc)
{
	MR_DEFAULT(baresparc, "Generic \"bare\" SPARC machine",
	    ARCH_SPARC, MACHINE_BARESPARC, 1, 0);
	me->aliases[0] = "baresparc";
	machine_entry_add(me, ARCH_SPARC);
}


MACHINE_REGISTER(testsparc)
{
	MR_DEFAULT(testsparc, "Test-machine for SPARC",
	    ARCH_SPARC, MACHINE_TESTSPARC, 1, 0);
	me->aliases[0] = "testsparc";
	machine_entry_add(me, ARCH_SPARC);
}

