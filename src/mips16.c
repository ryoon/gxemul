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
 *  $Id: mips16.c,v 1.4 2003-11-08 08:43:38 debug Exp $
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
	int rs, rd, imm, wlen;
	int x = (instr16[1] << 8) + instr16[0];
	int y = 0x3e << 26;	/*  This should be something 'illegal', so that execution stops  */

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

	/*  move y,X:   0x67 + 3 bits rd + 5 bits rs  */
	if ((x & 0xff00) == 0x6700) {
		rd = (x >> 5) & 0x07;
		rs = (x >> 0) & 0x1f;
		/*  addiu mips16_reg8_to_reg32[rd], reg32[rs], 0  */
		y = (HI6_ADDIU << 26) + (rs << 21) + (mips16_reg8_to_reg32[rd] << 16);
		goto mips16_ret;
	}

	/*  ld y,D(x)  { ld "y,D(x)", 0x3800, 0xf800, WR_y|RD_x, I3 }  */
	if ((x & 0xf800) == 0x3800) {
		wlen = 8;	/*  for ld  */
		rd = (x >> 5) & 0x07;
		rs = (x >> 8) & 0x07;
		if (cpu->mips16_extend)
			imm = (cpu->mips16_extend & 0x7ff) + ((x & 0x1f) << 11);
		else {
			imm = (x & 0x1f) * wlen;
			if (imm >= 0x10)
				imm |= 0xffe0;		/*  sign-extend  */
		}

		y = (HI6_LD << 26) + (mips16_reg8_to_reg32[rd] << 16) + (rs << 21) + imm;
		goto mips16_ret;
	}

	/*  daddiu    "S,K",      0xfb00, 0xff00, WR_SP|RD_SP, I3   */
	if ((x & 0xff00) == 0xfb00) {

		/*  TODO: this is wrong  */

		if (cpu->mips16_extend)
			imm = ((cpu->mips16_extend & 0x7ff) << 5) + (x & 0xff);
		else {
			imm = (x & 0xff) << 3;
			if (imm & (1 << 10))
				imm |= 0xf800;		/*  sign-extend  */
		}

		y = (HI6_DADDIU << 26) + (mips16_sp << 21) + (mips16_sp << 16) + (imm & 0xffff);
		goto mips16_ret;
	}

	/*  fatal("WARNING: unimplemented MIPS16 instruction 0x%04x\n", x);  */

mips16_ret:
	instr[3] = (y >> 24) & 255;
	instr[2] = (y >> 16) & 255;
	instr[1] = (y >> 8) & 255;
	instr[0] = y & 255;
	return 1;
}

