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
 *  $Id: bintrans_i386.c,v 1.7 2004-10-17 13:36:05 debug Exp $
 *
 *  i386 specific code for dynamic binary translation.
 *
 *  See bintrans.c for more information.  Included from bintrans.c.
 */


struct cpu dummy_cpu;


/*
 *  bintrans_host_cacheinvalidate()
 *
 *  Invalidate the host's instruction cache. On i386, this isn't neccessary,
 *  so this is an empty function.
 */
void bintrans_host_cacheinvalidate(unsigned char *p, size_t len)
{
	/*  Do nothing.  */
}


/*
 *  bintrans_chunk_header_len():
 *
 *  TODO: Comment.
 */
size_t bintrans_chunk_header_len(void)
{
	return 3;
}


/*
 *  bintrans_write_chunkhead():
 *
 *  TODO: Comment.
 */
void bintrans_write_chunkhead(unsigned char *p)
{
	*p++ = 0x55;			/*  push %ebp  */
	*p++ = 0x89; *p++ = 0xe5;	/*  mov %esp,%ebp  */
}


/*
 *  bintrans_write_chunkreturn():
 *
 *  TODO: Comment.
 */
void bintrans_write_chunkreturn(unsigned char **addrp)
{
	unsigned char *a = *addrp;

	*a++ = 0xc9;		/*  leave  */
	*a++ = 0xc3;		/*  ret  */

	*addrp = a;
}


/*
 *  bintrans_write_pcflush():
 *
 *  TODO: Comment.
 */
void bintrans_write_pcflush(unsigned char **addrp, int *pc_increment,
	int flag_pc, int flag_ninstr)
{
	unsigned char *a = *addrp;
	int inc = *pc_increment;
	int ofs = ((size_t)&dummy_cpu.pc) - ((size_t)&dummy_cpu);

	if (inc == 0)
		return;

	if (flag_pc) {
		/*  Add inc to cpu->pc.  */
		*a++ = 0x8b; *a++ = 0x45; *a++ = 0x08;	/*  mov 0x8(%ebp),%eax  */
		*a++ = 0x81; *a++ = 0x80; *a++ = ofs & 255;
		    *a++ = (ofs >> 8); *a++ = (ofs >> 16); *a++ = (ofs >> 24);
		    *a++ = (inc & 255); *a++ = (inc >> 8); *a++ = (inc >> 16);
		    *a++ = (inc >> 24);  /*  addl $inc,ofs(%eax)  */
		ofs += 4;
		*a++ = 0x83; *a++ = 0x90; *a++ = ofs & 255; *a++ = (ofs >> 8);
		    *a++ = 0; *a++ = 0; *a++ = 0;  /*  adcl $0,ofs+4(%eax)  */
	}

	if (flag_ninstr) {
		/*  ... and add inc (nr of instructions) to the counter:  */
		ofs = ((size_t)&dummy_cpu.bintrans_instructions_executed)
	            - ((size_t)&dummy_cpu);
		inc /= 4;	/*  nr of instructions instead of bytes  */
		*a++ = 0x8b; *a++ = 0x45; *a++ = 0x08;	/*  mov 0x8(%ebp),%eax  */
		*a++ = 0x81; *a++ = 0x80; *a++ = (ofs & 255); *a++ = (ofs >> 8);
		    *a++ = (ofs >> 16); *a++ = (ofs >> 24);
		    *a++ = (inc & 255); *a++ = (inc >> 8);
		    *a++ = (inc >> 16); *a++ = (inc >> 24);  /*  addl $inc,ofs(%eax)  */
	}

	*pc_increment = 0;
	*addrp = a;
}


/*
 *  bintrans_write_instruction():
 *
 *  TODO: Comment.
 */
int bintrans_write_instruction(unsigned char **addrp, int instr,
	int *pc_increment, uint64_t addr_a, uint64_t addr_b)
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

