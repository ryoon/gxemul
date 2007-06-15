/*
 *  Copyright (C) 2004-2007  Anders Gavare.  All rights reserved.
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
 *  $Id: useremul_freebsd.c,v 1.1 2007-06-15 00:41:21 debug Exp $
 *
 *  FreeBSD userland (syscall) emulation.
 */

#include <stdio.h>
#include <stdlib.h>

#include "cpu.h"
#include "machine.h"
#include "useremul.h"

#include "syscall_freebsd.h"


/*
 *  useremul_freebsd_setup():
 *
 *  Set up an emulated userland environment suitable for running FreeBSD
 *  binaries.
 */
void useremul_freebsd_setup(struct cpu *cpu, int argc, char **host_argv)
{
	switch (cpu->machine->arch) {

	case ARCH_ALPHA:
		/*  According to FreeBSD's /usr/src/lib/csu/alpha/crt1.c:  */
		/*  a0 = char **ap                                         */
		/*  a1 = void (*cleanup)(void)          from shared loader */
		/*  a2 = struct Struct_Obj_Entry *obj   from shared loader */
		/*  a3 = struct ps_strings *ps_strings                     */
		cpu->cd.alpha.r[ALPHA_A0] = 0;
		cpu->cd.alpha.r[ALPHA_A1] = 0;
		cpu->cd.alpha.r[ALPHA_A2] = 0;
		cpu->cd.alpha.r[ALPHA_A3] = 0;

		/*  What is a good stack pointer? TODO  */
		cpu->cd.alpha.r[ALPHA_SP] = 0x120000000ULL +
		    1048576 * cpu->machine->physical_ram_in_mb - 1024;
		break;

	default:
		fatal("The selected architecture is not supported yet for"
		    " FreeBSD syscall emulation.\n");
		exit(1);
	}
}


/*
 *  useremul_freebsd():
 *
 *  FreeBSD/Alpha syscall emulation.
 */
void useremul_freebsd(struct cpu *cpu, uint32_t code)
{
	int syscall_nr = -1;
	int64_t error_code = 0, error_flag = 0;
	uint64_t arg0, arg1, arg2, arg3, arg4, arg5;


	/*
	 *  Retrieve syscall arguments (in registers and/or the stack):
	 */

	switch (cpu->machine->arch) {

	case ARCH_ALPHA:
		syscall_nr = cpu->cd.alpha.r[ALPHA_V0];
		arg0 = cpu->cd.alpha.r[ALPHA_A0];
		arg1 = cpu->cd.alpha.r[ALPHA_A1];
		arg2 = cpu->cd.alpha.r[ALPHA_A2];
		arg3 = cpu->cd.alpha.r[ALPHA_A3];
		arg4 = cpu->cd.alpha.r[ALPHA_A4];
		arg5 = cpu->cd.alpha.r[ALPHA_A5];

		if (syscall_nr == FREEBSD_SYS___syscall) {
			syscall_nr = arg0;
			arg0 = arg1;
			arg1 = arg2;
			arg2 = arg3;
			arg3 = arg4;
			arg4 = arg5;
			/*  TODO: stack arguments  */
		}

		break;
	}


	/*
	 *  Handle the syscall:
	 */

	switch (syscall_nr) {

	case FREEBSD_SYS_exit:
		useremul_syscall_exit(cpu, arg0);
		break;

	case FREEBSD_SYS_sync:
		useremul_syscall_sync(cpu);
		break;

	default:
		fatal("[ UNIMPLEMENTED NetBSD syscall nr %i ]\n",
		    syscall_nr);
		cpu->running = 0;
	}


	/*
	 *  Return:
	 */

	switch (cpu->machine->arch) {

	case ARCH_ALPHA:
		if (error_flag) {
			cpu->cd.alpha.r[ALPHA_A3] = 1;
			cpu->cd.alpha.r[ALPHA_V0] = error_flag;
		} else {
			cpu->cd.alpha.r[ALPHA_A3] = 0;
			cpu->cd.alpha.r[ALPHA_V0] = error_code;
		}
		break;
	}
}

