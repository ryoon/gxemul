/*
 *  Copyright (C) 2004  Anders Gavare.  All rights reserved.
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
 *  $Id: bintrans_mips.c,v 1.4 2004-11-07 13:23:46 debug Exp $
 *
 *  MIPS specific code (64-bit) for dynamic binary translation.
 *  NOTE: This code will not work on 32-bit MIPS machines.
 *
 *  See bintrans.c for more information.  Included from bintrans.c.
 */


struct cpu dummy_cpu;


/*
 *  bintrans_host_cacheinvalidate()
 *
 *  Invalidate the host's instruction cache.
 *
 *  TODO.
 */
void bintrans_host_cacheinvalidate(unsigned char *p, size_t len)
{
	/*  TODO  */
}


/*
 *  bintrans_chunk_header_len():
 *
 *  TODO: Comment.
 */
size_t bintrans_chunk_header_len(void)
{
	return 0;
}


/*
 *  bintrans_write_chunkhead():
 *
 *  TODO: Comment.
 */
void bintrans_write_chunkhead(unsigned char *p)
{
}


/*
 *  bintrans_write_chunkreturn():
 *
 *  TODO: Comment.
 */
void bintrans_write_chunkreturn(unsigned char **addrp)
{
	uint32_t *a = (uint32_t *) *addrp;

	*a++ = 0x03e00008;	/*  jr ra  */
	*a++ = 0x00000000;	/*  nop  */

	*addrp = (unsigned char *) a;
}


/*
 *  bintrans_write_pcflush():
 *
 *  TODO: Comment.
 */
void bintrans_write_pcflush(unsigned char **addrp, int *pc_increment,
	int flag_pc, int flag_ninstr)
{
	uint32_t *a = (uint32_t *) *addrp;
	int inc = *pc_increment;
	int ofs = ((size_t)&dummy_cpu.pc) - ((size_t)&dummy_cpu);

	if (inc == 0)
		return;

	if (flag_pc) {
		/*  Increment cpu->pc:  */
		*a++ = (0xdc850000 + ofs);	/*  ld a1, ofs(a0)  */
		*a++ = (0x64a30000 + inc);	/*  daddiu v1,a1,inc  */
		*a++ = (0xfc830000 + ofs);	/*  sd v1, ofs(a0)  */
	}

	if (flag_ninstr) {
		/*  Increment the instruction count:  */
		ofs = ((size_t)&dummy_cpu.bintrans_instructions_executed)
	            - ((size_t)&dummy_cpu);
		inc /= 4;	/*  nr of instructions instead of bytes  */
		*a++ = (0x8c850000 + ofs);	/*  lw a1, ofs(a0)  */
		*a++ = (0x24a30000 + inc);	/*  daddiu v1,a1,inc  */
		*a++ = (0xac830000 + ofs);	/*  sw v1, ofs(a0)  */
	}

	*pc_increment = 0;
	*addrp = (unsigned char *) a;
}


/*
 *  bintrans_write_instruction():
 *
 *  TODO: Comment.
 */
int bintrans_write_instruction(unsigned char **addrp, int instr,
	int *pc_increment, uint64_t arg_a, uint64_t arg_b, uint64_t arg_c)
{
	uint32_t *a = (uint32_t *) *addrp;
	int res = 0;

	switch (instr) {
	case INSTR_NOP:
		/*  Add nothing, but succeed.  */
		res = 1;
		break;
	default:
		fatal("bintrans_write_instruction(): unimplemented "
		    "instruction %i\n", instr);
		res = 0;
	}

	*addrp = (unsigned char *) a;

	return res;
}

