/*
 *  Copyright (C) 2005  Anders Gavare.  All rights reserved.
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
 *  $Id: cpu_alpha_palcode.c,v 1.2 2005-07-21 09:30:22 debug Exp $
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
	case 0x3b: snprintf(buf, buflen, "PAL_OSF1_whami"); break;
	case 0x3c: snprintf(buf, buflen, "PAL_OSF1_retsys"); break;
	case 0x3d: snprintf(buf, buflen, "PAL_OSF1_rti"); break;
	case 0x3f: snprintf(buf, buflen, "PAL_OSF1_callsys"); break;
	case 0x86: snprintf(buf, buflen, "PAL_OSF1_imb"); break;
	case 0x92: snprintf(buf, buflen, "PAL_OSF1_urti"); break;
	default:snprintf(buf, buflen, "UNKNOWN 0x%x", palcode);
	}
}


/*
 *  alpha_palcode():
 *
 *  Execute an Alpha PALcode instruction.
 */
void alpha_palcode(struct cpu *cpu, uint32_t palcode)
{
	switch (palcode) {
	case 0x2b:	/*  PAL_OSF1_wrfen  */
		/*  Floating point enable: a0 = 1 or 0.  */
		/*  TODO  */
		break;
	case 0x33:	/*  PAL_OSF1_tbi  */
		/*  a0 = op, a1 = vaddr  */
		debug("[ Alpha PALcode: PAL_OSF1_tbi: a0=%lli a1=0x%llx ]\n",
		    (signed long long)cpu->cd.alpha.r[ALPHA_A0],
		    (long long)cpu->cd.alpha.r[ALPHA_A1]);
		/*  TODO  */
		break;
	case 0x35:	/*  PAL_OSF1_swpipl  */
		/*  a0 = new ipl, v0 = return old ipl  */
		cpu->cd.alpha.r[ALPHA_V0] = cpu->cd.alpha.ipl;
		cpu->cd.alpha.ipl = cpu->cd.alpha.r[ALPHA_A0];
		break;
	case 0x37:	/*  PAL_OSF1_wrkgp  */
		/*  "clobbers a0, t0, t8-t11" according to comments in
		    NetBSD sources  */

		/*  KGP shoudl be set to a0.  (TODO)  */
		break;
	case 0x86:	/*  PAL_OSF1_imb  */
		/*  TODO  */
		break;
	default:fatal("[ Alpha PALcode 0x%x unimplemented! ]\n", palcode);
		cpu->running = 0;
	}
}


#endif	/*  ENABLE_ALPHA  */
