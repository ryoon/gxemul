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
 *  $Id: cpu_alpha_palcode.c,v 1.7 2006-06-01 18:02:54 debug Exp $
 *
 *  Alpha PALcode-related functionality.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "misc.h"


#ifndef ENABLE_ALPHA  


#include "cpu_alpha.h"


void alpha_palcode_name(uint32_t palcode, char *buf, size_t buflen)
{ buf[0]='\0'; }
void alpha_palcode(struct cpu *cpu, uint32_t palcode) { }


#else   /*  ENABLE_ALPHA  */


#include "console.h"
#include "cpu.h"
#include "machine.h"
#include "memory.h"
#include "symbol.h"


/*
 *  alpha_palcode_name():
 *
 *  Return the name of a PALcode number, as a string.
 */
void alpha_palcode_name(uint32_t palcode, char *buf, size_t buflen)
{
	switch (palcode) {
	case 0x10: snprintf(buf, buflen, "PAL_OSF1_rdmces"); break;
	case 0x11: snprintf(buf, buflen, "PAL_OSF1_wrmces"); break;
	case 0x2b: snprintf(buf, buflen, "PAL_OSF1_wrfen"); break;
	case 0x2d: snprintf(buf, buflen, "PAL_OSF1_wrvptptr"); break;
	case 0x30: snprintf(buf, buflen, "PAL_OSF1_swpctx"); break;
	case 0x31: snprintf(buf, buflen, "PAL_OSF1_wrval"); break;
	case 0x32: snprintf(buf, buflen, "PAL_OSF1_rdval"); break;
	case 0x33: snprintf(buf, buflen, "PAL_OSF1_tbi"); break;
	case 0x34: snprintf(buf, buflen, "PAL_OSF1_wrent"); break;
	case 0x35: snprintf(buf, buflen, "PAL_OSF1_swpipl"); break;
	case 0x36: snprintf(buf, buflen, "PAL_OSF1_rdps"); break;
	case 0x37: snprintf(buf, buflen, "PAL_OSF1_wrkgp"); break;
	case 0x38: snprintf(buf, buflen, "PAL_OSF1_wrusp"); break;
	case 0x39: snprintf(buf, buflen, "PAL_OSF1_wrperfmon"); break;
	case 0x3a: snprintf(buf, buflen, "PAL_OSF1_rdusp"); break;
	case 0x3c: snprintf(buf, buflen, "PAL_OSF1_whami"); break;
	case 0x3d: snprintf(buf, buflen, "PAL_OSF1_retsys"); break;
	case 0x3f: snprintf(buf, buflen, "PAL_OSF1_rti"); break;
	case 0x81: snprintf(buf, buflen, "PAL_bugchk"); break;
	case 0x83: snprintf(buf, buflen, "PAL_OSF1_callsys"); break;
	case 0x86: snprintf(buf, buflen, "PAL_OSF1_imb"); break;
	case 0x92: snprintf(buf, buflen, "PAL_OSF1_urti"); break;
	case 0x3fffffe: snprintf(buf, buflen, "GXemul_PROM"); break;
	default:snprintf(buf, buflen, "UNKNOWN 0x%x", palcode);
	}
}


/*
 *  alpha_prom_call():
 */
void alpha_prom_call(struct cpu *cpu)
{
	uint64_t addr;

	switch (cpu->cd.alpha.r[ALPHA_A0]) {
	case 0x02:
		/*  puts: a1 = channel, a2 = ptr to buf, a3 = len  */
		for (addr = cpu->cd.alpha.r[ALPHA_A2];
		     addr < cpu->cd.alpha.r[ALPHA_A2] +
		     cpu->cd.alpha.r[ALPHA_A3]; addr ++) {
			unsigned char ch;
			cpu->memory_rw(cpu, cpu->mem, addr, &ch, sizeof(ch),
			    MEM_READ, CACHE_DATA | NO_EXCEPTIONS);
			console_putchar(cpu->machine->main_console_handle, ch);
		}
		cpu->cd.alpha.r[ALPHA_V0] = cpu->cd.alpha.r[ALPHA_A3];
		break;
	case 0x22:
		/*  getenv  */
		fatal("[ Alpha PALcode: GXemul PROM call 0x22: TODO ]\n");
		break;
	default:fatal("[ Alpha PALcode: GXemul PROM call, a0=0x%"PRIx64" ]\n",
		    (uint64_t) cpu->cd.alpha.r[ALPHA_A0]);
		cpu->running = 0;
	}

	/*  Return from the PROM call.  */
	cpu->pc = cpu->cd.alpha.r[ALPHA_RA];
}


/*
 *  alpha_palcode():
 *
 *  Execute an Alpha PALcode instruction. (Most of these correspond to
 *  OSF1 palcodes, used by for example NetBSD/alpha.)
 */
void alpha_palcode(struct cpu *cpu, uint32_t palcode)
{
	switch (palcode) {
	case 0x10:	/*  PAL_OSF1_rdmces  */
		/*  TODO? Return something in v0.  */
		break;
	case 0x11:	/*  PAL_OSF1_wrmces  */
		/*  TODO? Set something to a0.  */
		break;
	case 0x2b:	/*  PAL_OSF1_wrfen  */
		/*  Floating point enable: a0 = 1 or 0.  */
		/*  TODO  */
		break;
	case 0x2d:	/*  PAL_OSF1_wrvptptr  */
		/*  a0 = value  */
		cpu->cd.alpha.wrvptptr = cpu->cd.alpha.r[ALPHA_A0];
		break;
	case 0x30:	/*  PAL_OSF1_swpctx  */
		/*  TODO  */
		/*  Swap context  */
		break;
	case 0x31:	/*  PAL_OSF1_wrval  */
		/*  a0 = value  */
		cpu->cd.alpha.sysvalue = cpu->cd.alpha.r[ALPHA_A0];
		break;
	case 0x32:	/*  PAL_OSF1_rdval  */
		/*  return: v0 = value  */
		cpu->cd.alpha.r[ALPHA_V0] = cpu->cd.alpha.sysvalue;
		break;
	case 0x33:	/*  PAL_OSF1_tbi  */
		/*  a0 = op, a1 = vaddr  */
		debug("[ Alpha PALcode: PAL_OSF1_tbi: a0=%"PRIi64" a1=0x%"
		    PRIx64" ]\n", (int64_t)cpu->cd.alpha.r[ALPHA_A0],
		    (uint64_t)cpu->cd.alpha.r[ALPHA_A1]);
		/*  TODO  */
		break;
	case 0x34:	/*  PAL_OSF1_wrent (Write System Entry Address)  */
		/*  a0 = new vector, a1 = vector selector  */
		debug("[ Alpha PALcode: PAL_OSF1_tbi: a0=%"PRIi64" a1=0x%"
		    PRIx64" ]\n", (int64_t) cpu->cd.alpha.r[ALPHA_A0],
		    (uint64_t) cpu->cd.alpha.r[ALPHA_A1]);
		/*  TODO  */
		break;
	case 0x35:	/*  PAL_OSF1_swpipl  */
		/*  a0 = new ipl, v0 = return old ipl  */
		cpu->cd.alpha.r[ALPHA_V0] = cpu->cd.alpha.ipl;
		cpu->cd.alpha.ipl = cpu->cd.alpha.r[ALPHA_A0];
		break;
	case 0x36:	/*  PAL_OSF1_rdps  */
		/*  TODO  */
		cpu->cd.alpha.r[ALPHA_V0] = 0;
		break;
	case 0x37:	/*  PAL_OSF1_wrkgp  */
		/*  "clobbers a0, t0, t8-t11" according to comments in
		    NetBSD sources  */

		/*  KGP shoudl be set to a0.  (TODO)  */
		break;
	case 0x3c:	/*  PAL_OSF1_whami  */
		/*  Returns CPU id in v0:  */
		cpu->cd.alpha.r[ALPHA_V0] = cpu->cpu_id;
		break;
	case 0x81:	/*  PAL_bugchk  */
		cpu->running = 0;
		break;
	case 0x83:	/*  PAL_OSF1_syscall  */
		if (cpu->machine->userland_emul != NULL)
			useremul_syscall(cpu, 0);
		else {
			fatal("[ Alpha PALcode: syscall, but no"
			    " syscall handler? ]\n");
			cpu->running = 0;
		}
		break;
	case 0x86:	/*  PAL_OSF1_imb  */
		/*  TODO  */
		break;
	case 0x3fffffe:
		alpha_prom_call(cpu);
		break;
	default:fatal("[ Alpha PALcode 0x%x unimplemented! ]\n", palcode);
		cpu->running = 0;
	}
}


#endif	/*  ENABLE_ALPHA  */
