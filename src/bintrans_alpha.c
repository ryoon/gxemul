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
 *  $Id: bintrans_alpha.c,v 1.7 2004-10-08 19:24:15 debug Exp $
 *
 *  Alpha specific code for dynamic binary translation.
 *
 *  See bintrans.c for more information.  Included from bintrans.c.
 */


struct cpu dummy_cpu;


unsigned char bintrans_alpha_imb[32] = {
	0x86, 0x00, 0x00, 0x00,		/*  imb   */
	0x01, 0x80, 0xfa, 0x6b,		/*  ret   */
	0x1f, 0x04, 0xff, 0x47,		/*  nop   */
	0x00, 0x00, 0xfe, 0x2e,		/*  unop  */
	0x1f, 0x04, 0xff, 0x47,		/*  nop   */
	0x00, 0x00, 0xfe, 0x2e,		/*  unop  */
	0x1f, 0x04, 0xff, 0x47,		/*  nop   */
	0x00, 0x00, 0xfe, 0x2e		/*  unop  */
};


unsigned char bintrans_alpha_ret[16] = {
	0x01, 0x80, 0xfa, 0x6b,		/*  ret   */
	0x00, 0x00, 0xfe, 0x2f,		/*  unop  */
	0x1f, 0x04, 0xff, 0x47,		/*  nop   */
	0x00, 0x00, 0xfe, 0x2f,		/*  unop  */
};


/*
 *  bintrans_host_cacheinvalidate()
 *
 *  Invalidate the host's instruction cache. On Alpha, we do this by
 *  executing an imb instruction.
 *
 *  NOTE:  A simple  asm("imb");  would be enough here, but not all
 *  compilers have such simple constructs, so an entire function has to
 *  be written as bintrans_alpha_imb[] above.
 */
void bintrans_host_cacheinvalidate(void)
{
	/*  Long form of ``asm("imb");''  */

	void (*f)(void);
	f = (void *)&bintrans_alpha_imb[0];
	f();
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
	unsigned char *addr = *addrp;
	int i;

	for (i=0; i<sizeof(bintrans_alpha_ret); i++)
		addr[i] = bintrans_alpha_ret[i];
	addr += sizeof(bintrans_alpha_ret);

	*addrp = addr;
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

	/*
	 *  p[0x918 / 8] += 0x7fff;   (where a0 = p, which is a long long ptr)
	 *
	 *   0:   18 09 30 a4     ldq     t0,2328(a0)
	 *   4:   ff 7f 21 20     lda     t0,32767(t0)
	 *   8:   18 09 30 b4     stq     t0,2328(a0)
	 */
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xa4;
	*a++ = (inc & 255); *a++ = (inc >> 8); *a++ = 0x21; *a++ = 0x20;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb4;

	/*
	 *  Also increment the "number of executed instructions", which
	 *  is an int pointed to by a1.
	 *
	 *   0:   00 00 31 a0     ldl     t0,0(a1)
	 *   4:   ff 7f 21 20     lda     t0,32767(t0)
	 *   8:   00 00 31 b0     stl     t0,0(a1)
	 */
	inc /= 4;	/*  nr of instructions instead of bytes  */
	*a++ = 0x00;        *a++ = 0x00;       *a++ = 0x31; *a++ = 0xa0;
	*a++ = (inc & 255); *a++ = (inc >> 8); *a++ = 0x21; *a++ = 0x20;
	*a++ = 0x00;        *a++ = 0x00;       *a++ = 0x31; *a++ = 0xb0;

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

