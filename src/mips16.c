/*
 *  Copyright (C) 2003 by Anders Gavare.  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright  
 *     notice, this list of conditions and the following disclaimer in the 
 *     documentation and/or other materials provided with the distribution.
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
 *  $Id: mips16.c,v 1.3 2003-11-07 10:32:53 debug Exp $
 *
 *  MIPS16 encoding support, 16-bit to 32-bit instruction translation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

#include "misc.h"


/*  MIPS16 register numbers:  */
static int mips16_reg8_to_reg32[8] = { 16, 17, 2, 3, 4, 5, 6, 7 };
static int mips16_sp = 29;
static int mips16_t = 24;


/*
 *  mips16_to_32():
 *
 *  Translate a 16-bit MIPS16 instruction word into a normal
 *  32-bit instruction word.  instr16[1..0] ==> instr[3..0].
 *
 *  Returns 1 if there is a resulting 32-bit instruction,
 *  0 if this is an extend.
 */
int mips16_to_32(struct cpu *cpu, unsigned char *instr16, unsigned char *instr)
{
	int x = (instr16[1] << 8) + instr16[0];
	int y = 0;	/*  TODO: This should be something 'illegal', so that execution stops  */

	/*  Translate 16-bit x into 32-bit y:  */

	/*  extend:  */
	if ((x & 0xf800) == 0xf000) {
		/*  TODO: what happens if an extend is interrupted?  */
		cpu->mips16_extend = x;		/*  save x until later  */
		return 0;
	}

	/*  nop:  */
	if ((x & 0xffff) == 0x6500) {
		y = 0x00000000;		/*  nop  */
		goto mips16_ret;
	}

	fatal("WARNING: unimplemented MIPS16 instruction 0x%04x\n", x);

mips16_ret:
	instr[3] = (y >> 24) & 255;
	instr[2] = (y >> 16) & 255;
	instr[1] = (y >> 8) & 255;
	instr[0] = y & 255;
	return 1;
}

