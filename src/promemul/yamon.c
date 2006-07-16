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
 *  $Id: yamon.c,v 1.4 2006-07-16 07:44:19 debug Exp $
 *
 *  YAMON emulation. (Very basic, only what is needed to get NetBSD booting.)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "console.h"
#include "cpu.h"
#include "cpu_mips.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"

#ifdef ENABLE_MIPS

#include "yamon.h"


/*
 *  yamon_emul():
 *
 *  YAMON emulation (for evbmips).
 */
int yamon_emul(struct cpu *cpu)
{
	uint32_t ofs = (cpu->pc & 0xff) + YAMON_FUNCTION_BASE;
	uint8_t ch;
	int n;

	switch (ofs) {

	case YAMON_PRINT_COUNT_OFS:
		/*
		 *  print count:
		 *	a1 = string
		 *	a2 = count
		 */
		n = 0;
		while (n < (int)cpu->cd.mips.gpr[MIPS_GPR_A2]) {
			cpu->memory_rw(cpu, cpu->mem, cpu->cd.mips.gpr[
			    MIPS_GPR_A1] + n, &ch, sizeof(ch), MEM_READ,
			    CACHE_DATA | NO_EXCEPTIONS);
			console_putchar(cpu->machine->main_console_handle, ch);
			n++;
		}
		break;

	case YAMON_EXIT_OFS:
		/*
		 *  exit
		 */
		debug("[ yamon_emul(): exit ]\n");
		cpu->running = 0;
		break;

	/*  YAMON_FLUSH_CACHE_OFS: TODO  */
	/*  YAMON_PRINT_OFS: TODO  */
	/*  YAMON_REG_CPU_ISR_OFS: TODO  */
	/*  YAMON_DEREG_CPU_ISR_OFS: TODO  */
	/*  YAMON_REG_IC_ISR_OFS: TODO  */
	/*  YAMON_DEREG_IC_ISR_OFS: TODO  */
	/*  YAMON_REG_ESR_OFS: TODO  */
	/*  YAMON_DEREG_ESR_OFS: TODO  */

	case YAMON_GETCHAR_OFS:
		n = console_readchar(cpu->machine->main_console_handle);
		/*  Note: -1 (if no char was available) becomes 0xff:  */
		ch = n;
		cpu->memory_rw(cpu, cpu->mem, cpu->cd.mips.gpr[MIPS_GPR_A1],
		    &ch, sizeof(ch), MEM_WRITE, CACHE_DATA | NO_EXCEPTIONS);
		break;

	case YAMON_SYSCON_READ_OFS:
		/*
		 *  syscon
		 */
		fatal("[ yamon_emul(): syscon: TODO ]\n");

		/*  TODO. For now, return some kind of "failure":  */
		cpu->cd.mips.gpr[MIPS_GPR_V0] = 1;
		break;

	default:cpu_register_dump(cpu->machine, cpu, 1, 0);
		printf("\n");
		fatal("[ yamon_emul(): unimplemented yamon function 0x%"
		    PRIx32" ]\n", ofs);
		cpu->running = 0;
		cpu->dead = 1;
	}

	return 1;
}

#endif	/*  ENABLE_MIPS  */
