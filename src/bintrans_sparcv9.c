/*
 *  Copyright (C) 2004 by Anders Gavare.  All rights reserved.
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
 *  $Id: bintrans_sparcv9.c,v 1.2 2004-10-08 20:28:05 debug Exp $
 *
 *  UltraSparc specific code for dynamic binary translation.
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
void bintrans_host_cacheinvalidate(void)
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
	unsigned char *a = *addrp;

	*a++ = 0x81; *a++ = 0xc3; *a++ = 0xe0; *a++ = 0x08;  /*  retl  */
	*a++ = 0x01; *a++ = 0x00; *a++ = 0x00; *a++ = 0x00;  /*  nop  */

	*addrp = a;
}


/*
 *  bintrans_write_pcflush():
 *
 *  TODO: Comment.
 */
void bintrans_write_pcflush(unsigned char **addrp, int *pc_increment)
{
	unsigned char *a = *addrp;
	int inc = *pc_increment;
	int ofs = ((size_t)&dummy_cpu.pc) - ((size_t)&dummy_cpu);

	/*  Increment cpu->pc: (assuming %o5 is available for use)  */
	*a++ = 0xda; *a++ = 0x5a; *a++ = 0x20 + (ofs >> 8);
	    *a++ = (ofs & 255);		/*  ldx [ %o0 + ofs ], %o5  */
	*a++ = 0x9a; *a++ = 3; *a++ = 0x60 + (inc >> 8);
	    *a++ = (inc & 255);		/*  add %o5, inc, %o5  */
	*a++ = 0xda; *a++ = 0x72; *a++ = 0x20 + (ofs >> 8);
	    *a++ = (ofs & 255);		/*  stx %o5, [ %o0 + ofs ]  */

	/*  Increment the instruction count:  */
	inc /= 4;	/*  nr of instructions instead of bytes  */
	*a++ = 0xda; *a++ = 0x02; *a++ = 0x60;
	    *a++ = 0x00;		/*  ld [ %o1 ], %o5  */
	*a++ = 0x9a; *a++ = 3; *a++ = 0x60 + (inc >> 8);
	    *a++ = (inc & 255);		/*  add  %o5, inc, %o5  */
	*a++ = 0xda; *a++ = 0x22; *a++ = 0x60;
	    *a++ = 0x00;		/*  st %o5, [ %o1 ]  */

	*pc_increment = 0;
	*addrp = a;
}


/*
 *  bintrans_write_instruction():
 *
 *  TODO: Comment.
 */
int bintrans_write_instruction(unsigned char **addrp, int instr,
	int *pc_increment)
{
	unsigned char *addr = *addrp;
	int res = 0;

	switch (instr) {
	case INSTR_NOP:
		/*  Add nothing, but succeed.  */
		res = 1;
		break;
	default:
		fatal("bintrans_write_instruction(): unimplemented "
		    "instruction %i\n", instr);
		exit(1);
	}

	*addrp = addr;

	return res;
}

