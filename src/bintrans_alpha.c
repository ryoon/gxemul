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
 *  $Id: bintrans_alpha.c,v 1.21 2004-11-09 04:28:42 debug Exp $
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


unsigned char bintrans_alpha_ret[4] = {
	0x01, 0x80, 0xfa, 0x6b		/*  ret   */
};

#if 0
	0x00, 0x00, 0xfe, 0x2f,		/*  unop  */
	0x1f, 0x04, 0xff, 0x47,		/*  nop   */
	0x00, 0x00, 0xfe, 0x2f,		/*  unop  */
#endif


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
void bintrans_host_cacheinvalidate(unsigned char *p, size_t len)
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
void bintrans_write_pcflush(unsigned char **addrp, int *pc_inc,
	int flag_pc, int flag_ninstr)
{
	unsigned char *a = *addrp;
	int inc = *pc_inc;
	int ofs = ((size_t)&dummy_cpu.pc) - ((size_t)&dummy_cpu);

	if (inc == 0)
		return;

	if (flag_pc) {
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
	}

	if (flag_ninstr) {
		/*
		 *  Also increment the "number of executed instructions", which
		 *  is an int.
		 *
		 *   0:   44 44 30 a0     ldl     t0,17476(a0)
		 *   4:   89 07 21 20     lda     t0,1929(t0)
		 *   8:   44 44 30 b0     stl     t0,17476(a0)
		 */
		ofs = ((size_t)&dummy_cpu.bintrans_instructions_executed)
		    - ((size_t)&dummy_cpu);
		inc /= 4;	/*  nr of instructions instead of bytes  */
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xa0;
		*a++ = (inc & 255); *a++ = (inc >> 8); *a++ = 0x21; *a++ = 0x20;
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb0;
	}

	*pc_inc = 0;
	*addrp = a;
}


/*
 *  bintrans_write_instruction__addiu():
 */
int bintrans_write_instruction__addiu(unsigned char **addrp,
	int *pc_inc, int rt, int rs, int imm)
{
	unsigned char *a;
	unsigned int uimm;
	int ofs;

	/*
	 *  ldl/stl = 32-bit, ldq/stq = 64-bit
	 *
	 *  1) Load 32-bit (rs)
	 *  2) Use lda t0,imm(t0) to mimic addiu
	 *  3) sign-extend 32->64 bits
	 *  4) Store 64-bit (rt)
	 *
	 *  44 04 30 a0     ldl     t0,1092(a0)		load rs
	 *  34 12 21 20     lda     t0,imm(t0)		addiu
	 *  01 00 3f 40     addl    t0,zero,t0		64->32 bit cast
	 *  1f 04 ff 5f     fnop
	 *  88 08 30 b4     stq     t0,2184(a0)		store rt (64-bit)
	 */
	a = *addrp;

	uimm = imm & 0xffff;
	ofs = ((size_t)&dummy_cpu.gpr[rs]) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xa0;
	*a++ = (uimm & 255); *a++ = (uimm >> 8); *a++ = 0x21; *a++ = 0x20;

	*a++ = 0x01; *a++ = 0x00; *a++ = 0x3f; *a++ = 0x40;
	*a++ = 0x1f; *a++ = 0x04; *a++ = 0xff; *a++ = 0x5f;

	ofs = ((size_t)&dummy_cpu.gpr[rt]) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb4;

	*addrp = a;
	return 1;
}


/*
 *  bintrans_write_instruction__andi():
 */
int bintrans_write_instruction__andi(unsigned char **addrp,
	int *pc_inc, int rt, int rs, int imm)
{
	unsigned char *a;
	unsigned int uimm;
	int ofs;

	a = *addrp;
	uimm = imm & 0xffff;

	if (uimm & 0x8000) {
		/*
		 *  "Negative":
		 *  00 80 3f 20     lda     t0,-32768
		 *  1f 04 ff 5f     fnop
		 *  88 08 50 a4     ldq     t1,2184(a0)
		 *  01 00 21 24     ldah    t0,1(t0)
		 *  02 00 41 44     and     t1,t0,t1
		 *  1f 04 ff 5f     fnop
		 *  88 08 50 b4     stq     t1,2184(a0)
		 */
		ofs = ((size_t)&dummy_cpu.gpr[rs]) - (size_t)&dummy_cpu;
		*a++ = (uimm & 255); *a++ = (uimm >> 8); *a++ = 0x3f; *a++ = 0x20;
		*a++ = 0x1f; *a++ = 0x04; *a++ = 0xff; *a++ = 0x5f;
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x50; *a++ = 0xa4;

		*a++ = 0x01; *a++ = 0x00; *a++ = 0x21; *a++ = 0x24;	/*  ldah  */
		*a++ = 0x02; *a++ = 0x00; *a++ = 0x41; *a++ = 0x44;	/*  and  */
		*a++ = 0x1f; *a++ = 0x04; *a++ = 0xff; *a++ = 0x5f;	/*  fnop  */

		ofs = ((size_t)&dummy_cpu.gpr[rt]) - (size_t)&dummy_cpu;
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x50; *a++ = 0xb4;
	} else {
		/*
		 *  Positive:
 		 *   34 12 5f 20     lda     t1,4660
 		 *   88 08 30 a4     ldq     t0,2184(a0)
 		 *   01 00 22 44     and     t0,t1,t0
 		 *   1f 04 ff 5f     fnop
 		 *   88 08 30 b4     stq     t0,2184(a0)
		 */
		ofs = ((size_t)&dummy_cpu.gpr[rs]) - (size_t)&dummy_cpu;
		*a++ = (uimm & 255); *a++ = (uimm >> 8); *a++ = 0x5f; *a++ = 0x20;
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xa4;

		*a++ = 0x01; *a++ = 0x00; *a++ = 0x22; *a++ = 0x44;	/*  and  */
		*a++ = 0x1f; *a++ = 0x04; *a++ = 0xff; *a++ = 0x5f;	/*  fnop  */

		ofs = ((size_t)&dummy_cpu.gpr[rt]) - (size_t)&dummy_cpu;
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb4;
	}

	*addrp = a;
	return 1;
}


/*
 *  bintrans_write_instruction__ori():
 */
int bintrans_write_instruction__ori(unsigned char **addrp,
	int *pc_inc, int rt, int rs, int imm)
{
	unsigned char *a;
	unsigned int uimm;
	int ofs;

	a = *addrp;
	uimm = imm & 0xffff;

	if (uimm & 0x8000) {
		/*
		 *  "Negative":
		 *  00 80 3f 20     lda     t0,-32768
		 *  1f 04 ff 5f     fnop
		 *  88 08 50 a4     ldq     t1,2184(a0)
		 *  01 00 21 24     ldah    t0,1(t0)
		 *  02 04 41 44     or      t1,t0,t1
		 *  1f 04 ff 5f     fnop
		 *  88 08 50 b4     stq     t1,2184(a0)
		 */
		ofs = ((size_t)&dummy_cpu.gpr[rs]) - (size_t)&dummy_cpu;
		*a++ = (uimm & 255); *a++ = (uimm >> 8); *a++ = 0x3f; *a++ = 0x20;
		*a++ = 0x1f; *a++ = 0x04; *a++ = 0xff; *a++ = 0x5f;
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x50; *a++ = 0xa4;

		*a++ = 0x01; *a++ = 0x00; *a++ = 0x21; *a++ = 0x24;	/*  ldah  */
		*a++ = 0x02; *a++ = 0x04; *a++ = 0x41; *a++ = 0x44;	/*  or  */
		*a++ = 0x1f; *a++ = 0x04; *a++ = 0xff; *a++ = 0x5f;	/*  fnop  */

		ofs = ((size_t)&dummy_cpu.gpr[rt]) - (size_t)&dummy_cpu;
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x50; *a++ = 0xb4;
	} else {
		/*
		 *  Positive:
 		 *   34 12 5f 20     lda     t1,4660
 		 *   88 08 30 a4     ldq     t0,2184(a0)
 		 *   01 04 22 44     or      t0,t1,t0
 		 *   1f 04 ff 5f     fnop
 		 *   88 08 30 b4     stq     t0,2184(a0)
		 */
		ofs = ((size_t)&dummy_cpu.gpr[rs]) - (size_t)&dummy_cpu;
		*a++ = (uimm & 255); *a++ = (uimm >> 8); *a++ = 0x5f; *a++ = 0x20;
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xa4;

		*a++ = 0x01; *a++ = 0x04; *a++ = 0x22; *a++ = 0x44;	/*  or  */
		*a++ = 0x1f; *a++ = 0x04; *a++ = 0xff; *a++ = 0x5f;	/*  fnop  */

		ofs = ((size_t)&dummy_cpu.gpr[rt]) - (size_t)&dummy_cpu;
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb4;
	}

	*addrp = a;
	return 1;
}


/*
 *  bintrans_write_instruction__xori():
 */
int bintrans_write_instruction__xori(unsigned char **addrp,
	int *pc_inc, int rt, int rs, int imm)
{
	unsigned char *a;
	unsigned int uimm;
	int ofs;

	a = *addrp;
	uimm = imm & 0xffff;

	if (uimm & 0x8000) {
		/*
		 *  "Negative":
		 *  00 80 3f 20     lda     t0,-32768
		 *  1f 04 ff 5f     fnop
		 *  88 08 50 a4     ldq     t1,2184(a0)
		 *  01 00 21 24     ldah    t0,1(t0)
		 *  02 08 41 44     xor     t1,t0,t1
		 *  1f 04 ff 5f     fnop
		 *  88 08 50 b4     stq     t1,2184(a0)
		 */
		ofs = ((size_t)&dummy_cpu.gpr[rs]) - (size_t)&dummy_cpu;
		*a++ = (uimm & 255); *a++ = (uimm >> 8); *a++ = 0x3f; *a++ = 0x20;
		*a++ = 0x1f; *a++ = 0x04; *a++ = 0xff; *a++ = 0x5f;
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x50; *a++ = 0xa4;

		*a++ = 0x01; *a++ = 0x00; *a++ = 0x21; *a++ = 0x24;	/*  ldah  */
		*a++ = 0x02; *a++ = 0x08; *a++ = 0x41; *a++ = 0x44;	/*  xor  */
		*a++ = 0x1f; *a++ = 0x04; *a++ = 0xff; *a++ = 0x5f;	/*  fnop  */

		ofs = ((size_t)&dummy_cpu.gpr[rt]) - (size_t)&dummy_cpu;
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x50; *a++ = 0xb4;
	} else {
		/*
		 *  Positive:
 		 *   34 12 5f 20     lda     t1,4660
 		 *   88 08 30 a4     ldq     t0,2184(a0)
 		 *   01 08 22 44     xor     t0,t1,t0
 		 *   1f 04 ff 5f     fnop
 		 *   88 08 30 b4     stq     t0,2184(a0)
		 */
		ofs = ((size_t)&dummy_cpu.gpr[rs]) - (size_t)&dummy_cpu;
		*a++ = (uimm & 255); *a++ = (uimm >> 8); *a++ = 0x5f; *a++ = 0x20;
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xa4;

		*a++ = 0x01; *a++ = 0x08; *a++ = 0x22; *a++ = 0x44;	/*  xor  */
		*a++ = 0x1f; *a++ = 0x04; *a++ = 0xff; *a++ = 0x5f;	/*  fnop  */

		ofs = ((size_t)&dummy_cpu.gpr[rt]) - (size_t)&dummy_cpu;
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb4;
	}

	*addrp = a;
	return 1;
}


/*
 *  bintrans_write_instruction__addu():
 */
int bintrans_write_instruction__addu(unsigned char **addrp,
	int *pc_inc, int rd, int rs, int rt)
{
	unsigned char *a;
	int ofs;

	/*
	 *  88 08 50 a0     ldl     t1,2184(a0)
	 *  18 09 30 a0     ldl     t0,2328(a0)
	 *  01 00 22 40     addl    t0,t1,t0
	 *  1f 04 ff 5f     fnop
	 *  88 08 30 b4     stq     t0,2184(a0)
	 */
	a = *addrp;

	ofs = ((size_t)&dummy_cpu.gpr[rs]) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x50; *a++ = 0xa0;

	ofs = ((size_t)&dummy_cpu.gpr[rt]) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xa0;

	*a++ = 0x01; *a++ = 0x00; *a++ = 0x22; *a++ = 0x40;
	*a++ = 0x1f; *a++ = 0x04; *a++ = 0xff; *a++ = 0x5f;

	ofs = ((size_t)&dummy_cpu.gpr[rd]) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb4;

	*addrp = a;
	return 1;
}


/*
 *  bintrans_write_instruction__subu():
 */
int bintrans_write_instruction__subu(unsigned char **addrp,
	int *pc_inc, int rd, int rs, int rt)
{
	unsigned char *a;
	int ofs;

	/*
	 *  90 08 30 a0     ldl     t0,2192(a0)
	 *  98 08 50 a0     ldl     t1,2200(a0)
	 *  21 01 22 40     subl    t0,t1,t0
	 *  1f 04 ff 5f     fnop
	 *  88 08 30 b4     stq     t0,2184(a0)
	 */
	a = *addrp;

	ofs = ((size_t)&dummy_cpu.gpr[rs]) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xa0;

	ofs = ((size_t)&dummy_cpu.gpr[rt]) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x50; *a++ = 0xa0;

	*a++ = 0x21; *a++ = 0x01; *a++ = 0x22; *a++ = 0x40;
	*a++ = 0x1f; *a++ = 0x04; *a++ = 0xff; *a++ = 0x5f;

	ofs = ((size_t)&dummy_cpu.gpr[rd]) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb4;

	*addrp = a;
	return 1;
}


/*
 *  bintrans_write_instruction__and():
 */
int bintrans_write_instruction__and(unsigned char **addrp,
	int *pc_inc, int rd, int rs, int rt)
{
	unsigned char *a;
	int ofs;

	/*
	 *  88 08 50 a4     ldq     t1,2184(a0)
	 *  18 09 30 a4     ldq     t0,2328(a0)
	 *  01 00 22 44     and     t0,t1,t0
	 *  1f 04 ff 5f     fnop
	 *  88 08 30 b4     stq     t0,2184(a0)
	 */
	a = *addrp;

	ofs = ((size_t)&dummy_cpu.gpr[rs]) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x50; *a++ = 0xa4;

	ofs = ((size_t)&dummy_cpu.gpr[rt]) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xa4;

	*a++ = 0x01; *a++ = 0x00; *a++ = 0x22; *a++ = 0x44;
	*a++ = 0x1f; *a++ = 0x04; *a++ = 0xff; *a++ = 0x5f;

	ofs = ((size_t)&dummy_cpu.gpr[rd]) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb4;

	*addrp = a;
	return 1;
}


/*
 *  bintrans_write_instruction__or():
 */
int bintrans_write_instruction__or(unsigned char **addrp,
	int *pc_inc, int rd, int rs, int rt)
{
	unsigned char *a;
	int ofs;

	/*
	 *  88 08 50 a4     ldq     t1,2184(a0)
	 *  18 09 30 a4     ldq     t0,2328(a0)
	 *  01 04 22 44     or      t0,t1,t0
	 *  1f 04 ff 5f     fnop
	 *  88 08 30 b4     stq     t0,2184(a0)
	 */
	a = *addrp;

	ofs = ((size_t)&dummy_cpu.gpr[rs]) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x50; *a++ = 0xa4;

	ofs = ((size_t)&dummy_cpu.gpr[rt]) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xa4;

	*a++ = 0x01; *a++ = 0x04; *a++ = 0x22; *a++ = 0x44;
	*a++ = 0x1f; *a++ = 0x04; *a++ = 0xff; *a++ = 0x5f;

	ofs = ((size_t)&dummy_cpu.gpr[rd]) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb4;

	*addrp = a;
	return 1;
}


/*
 *  bintrans_write_instruction__nor():
 */
int bintrans_write_instruction__nor(unsigned char **addrp,
	int *pc_inc, int rd, int rs, int rt)
{
	unsigned char *a;
	int ofs;

	/*
	 *  88 08 50 a4     ldq     t1,2184(a0)
	 *  18 09 30 a4     ldq     t0,2328(a0)
	 *  01 04 22 44     or      t0,t1,t0
	 *  01 05 e1 47     not     t0,t0
	 *  88 08 30 b4     stq     t0,2184(a0)
	 */
	a = *addrp;

	ofs = ((size_t)&dummy_cpu.gpr[rs]) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x50; *a++ = 0xa4;

	ofs = ((size_t)&dummy_cpu.gpr[rt]) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xa4;

	*a++ = 0x01; *a++ = 0x04; *a++ = 0x22; *a++ = 0x44;
	*a++ = 0x01; *a++ = 0x05; *a++ = 0xe1; *a++ = 0x47;

	ofs = ((size_t)&dummy_cpu.gpr[rd]) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb4;

	*addrp = a;
	return 1;
}


/*
 *  bintrans_write_instruction__xor():
 */
int bintrans_write_instruction__xor(unsigned char **addrp,
	int *pc_inc, int rd, int rs, int rt)
{
	unsigned char *a;
	int ofs;

	/*
	 *  88 08 50 a4     ldq     t1,2184(a0)
	 *  18 09 30 a4     ldq     t0,2328(a0)
	 *  01 08 22 44     xor     t0,t1,t0
	 *  1f 04 ff 5f     fnop
	 *  88 08 30 b4     stq     t0,2184(a0)
	 */
	a = *addrp;

	ofs = ((size_t)&dummy_cpu.gpr[rs]) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x50; *a++ = 0xa4;

	ofs = ((size_t)&dummy_cpu.gpr[rt]) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xa4;

	*a++ = 0x01; *a++ = 0x08; *a++ = 0x22; *a++ = 0x44;
	*a++ = 0x1f; *a++ = 0x04; *a++ = 0xff; *a++ = 0x5f;

	ofs = ((size_t)&dummy_cpu.gpr[rd]) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb4;

	*addrp = a;
	return 1;
}


/*
 *  bintrans_write_instruction__slt():
 */
int bintrans_write_instruction__slt(unsigned char **addrp,
	int *pc_inc, int rd, int rs, int rt)
{
	unsigned char *a;
	int ofs;

	/*
	 *  48 04 30 a0     ldl     t0,1096(a0)
	 *  4c 04 50 a0     ldl     t1,1100(a0)
	 *  a1 09 22 40     cmplt   t0,t1,t0
	 *  1f 04 ff 5f     fnop
	 *  88 08 30 b4     stq     t0,2184(a0)
	 */
	a = *addrp;

	ofs = ((size_t)&dummy_cpu.gpr[rs]) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xa0;

	ofs = ((size_t)&dummy_cpu.gpr[rt]) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x50; *a++ = 0xa0;

	*a++ = 0xa1; *a++ = 0x09; *a++ = 0x22; *a++ = 0x40;
	*a++ = 0x1f; *a++ = 0x04; *a++ = 0xff; *a++ = 0x5f;

	ofs = ((size_t)&dummy_cpu.gpr[rd]) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb4;

	*addrp = a;
	return 1;
}


/*
 *  bintrans_write_instruction__sltu():
 */
int bintrans_write_instruction__sltu(unsigned char **addrp,
	int *pc_inc, int rd, int rs, int rt)
{
	unsigned char *a;
	int ofs;

	/*
	 *  48 04 30 a0     ldl     t0,1096(a0)
	 *  4c 04 50 a0     ldl     t1,1100(a0)
	 *  a1 03 22 40     cmpult  t0,t1,t0
	 *  1f 04 ff 5f     fnop
	 *  88 08 30 b4     stq     t0,2184(a0)
	 */
	a = *addrp;

	ofs = ((size_t)&dummy_cpu.gpr[rs]) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xa0;

	ofs = ((size_t)&dummy_cpu.gpr[rt]) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x50; *a++ = 0xa0;

	*a++ = 0xa1; *a++ = 0x03; *a++ = 0x22; *a++ = 0x40;
	*a++ = 0x1f; *a++ = 0x04; *a++ = 0xff; *a++ = 0x5f;

	ofs = ((size_t)&dummy_cpu.gpr[rd]) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb4;

	*addrp = a;
	return 1;
}


/*
 *  bintrans_write_instruction__sll():
 */
int bintrans_write_instruction__sll(unsigned char **addrp,
	int *pc_inc, int rd, int rt, int sa)
{
	unsigned char *a;
	int ofs;

	/*
	 *  88 08 30 a4     ldq     t0,2184(a0)
	 *  21 f7 23 48     sll     t0,0x1f,t0
	 *  01 00 3f 40     addl    t0,zero,t0		64->32 bit cast
	 *  88 08 30 b4     stq     t0,2184(a0)
	 */
	a = *addrp;

	ofs = ((size_t)&dummy_cpu.gpr[rt]) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xa4;

	if (sa != 0) {
		/*  Note: bits of sa are distributed among two different bytes.  */
		*a++ = 0x21; *a++ = 0x17 + ((sa & 7) << 5); *a++ = 0x20 + (sa >> 3); *a++ = 0x48;
		*a++ = 0x01; *a++ = 0x00; *a++ = 0x3f; *a++ = 0x40;	/*  addl  */
	}

	ofs = ((size_t)&dummy_cpu.gpr[rd]) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb4;

	*addrp = a;
	return 1;
}


/*
 *  bintrans_write_instruction__sra():
 */
int bintrans_write_instruction__sra(unsigned char **addrp,
	int *pc_inc, int rd, int rt, int sa)
{
	unsigned char *a;
	int ofs;

	/*
	 *  88 08 30 a4     ldq     t0,2184(a0)
	 *  81 f7 23 48     sra     t0,0x1f,t0
	 *  88 08 30 b4     stq     t0,2184(a0)
	 */
	a = *addrp;

	ofs = ((size_t)&dummy_cpu.gpr[rt]) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xa4;

	if (sa != 0) {
		/*  Note: bits of sa are distributed among two different bytes.  */
		*a++ = 0x81; *a++ = 0x17 + ((sa & 7) << 5); *a++ = 0x20 + (sa >> 3); *a++ = 0x48;
	}

	ofs = ((size_t)&dummy_cpu.gpr[rd]) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb4;

	*addrp = a;
	return 1;
}


/*
 *  bintrans_write_instruction__srl():
 */
int bintrans_write_instruction__srl(unsigned char **addrp,
	int *pc_inc, int rd, int rt, int sa)
{
	unsigned char *a;
	int ofs;

	/*
	 *  88 08 30 a0     ldl     t0,2184(a0)
	 *  21 f6 21 48     zapnot  t0,0xf,t0		use only lowest 32 bits
	 *  81 f6 23 48     srl     t0,0x1f,t0
	 *  01 00 3f 40     addl    t0,zero,t0		re-extend to 64-bit
	 *  88 08 30 b4     stq     t0,2184(a0)
	 */
	a = *addrp;
	ofs = ((size_t)&dummy_cpu.gpr[rt]) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xa0;

	if (sa != 0) {
		*a++ = 0x21; *a++ = 0xf6; *a++ = 0x21; *a++ = 0x48;	/*  zapnot  */
		/*  Note: bits of sa are distributed among two different bytes.  */
		*a++ = 0x81; *a++ = 0x16 + ((sa & 7) << 5); *a++ = 0x20 + (sa >> 3); *a++ = 0x48;
		*a++ = 0x01; *a++ = 0x00; *a++ = 0x3f; *a++ = 0x40;	/*  addl  */
	}

	ofs = ((size_t)&dummy_cpu.gpr[rd]) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb4;

	*addrp = a;
	return 1;
}


/*
 *  bintrans_write_instruction__lui():
 */
int bintrans_write_instruction__lui(unsigned char **addrp,
	int *pc_inc, int rt, int imm)
{
	unsigned char *a;
	int ofs;

	/*
	 *  dc fe 3f 24     ldah    t0,-292
	 *  1f 04 ff 5f     fnop
	 *  88 08 30 b4     stq     t0,2184(a0)
	 */
	a = *addrp;

	*a++ = (imm & 255); *a++ = (imm >> 8) & 255; *a++ = 0x3f; *a++ = 0x24;
	*a++ = 0x1f; *a++ = 0x04; *a++ = 0xff; *a++ = 0x5f;

	ofs = ((size_t)&dummy_cpu.gpr[rt]) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb4;

	*addrp = a;
	return 1;
}


/*
 *  bintrans_write_instruction__lw():
 */
int bintrans_write_instruction__lw(unsigned char **addrp,
	int *pc_inc, int first_load, int first_store, int rt, int imm, int rs, int load_type)
{
	unsigned char *jumppast1, *jumppast1b=NULL, *jumppast1c, *jumppast2;
	unsigned char *a;
	int ofs;

	/*  Flush the PC, but don't include this instruction.  */
	(*pc_inc) -= 4;
	bintrans_write_pcflush(addrp, pc_inc, 1, 1);
	(*pc_inc) += 4;

	a = *addrp;

	/*
	 *  if (cpu->pc_bintrans_data_host_4kpage[0] == NULL)
	 *	skip;
	 *
	 *  98 08 90 a4     ldq     t3,2200(a0)
	 *  01 00 80 e4     beq     t3,c <skip>
	 */
	ofs = ((size_t)&dummy_cpu.pc_bintrans_data_host_4kpage[0]) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x90; *a++ = 0xa4;

	if (1) {  /* first_load && first_store) { */
		jumppast1 = a;
		*a++ = 0x01; *a++ = 0x00; *a++ = 0x80; *a++ = 0xe4;
	}

	if (1) { /* first_store) { */
		switch (load_type) {
		case LOAD_TYPE_SW:
		case LOAD_TYPE_SH:
		case LOAD_TYPE_SB:
			/*
			 *  if (!cpu->pc_bintrans_data_writeflag[0])
			 *	return;
			 *
			 *  00 00 30 a0     ldl     t0,0(a0)
			 *  01 00 20 e4     beq     t0,2c <skip>
			 */
			ofs = ((size_t)&dummy_cpu.pc_bintrans_data_writeflag[0]) - (size_t)&dummy_cpu;
			*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xa0;
			jumppast1b = a;
			*a++ = 0x01; *a++ = 0x00; *a++ = 0x20; *a++ = 0xe4;
			break;
		default:
			;
		}
	}

	/*
	 *  t2 = gpr[rs] + imm;
	 *
	 *  88 08 30 a4     ldq     t0,2184(a0)
	 *  34 12 61 20     lda     t2,4660(t0)
	 */
	ofs = ((size_t)&dummy_cpu.gpr[rs]) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xa4;
	*a++ = (imm & 255); *a++ = (imm >> 8); *a++ = 0x61; *a++ = 0x20;

	switch (load_type) {
	case LOAD_TYPE_LW:
	case LOAD_TYPE_SW:
		/*
		 *  if (t2 & 3)		check for misalignment
		 *	return;
		 *
		 *  02 70 60 44     and     t2,0x3,t1
		 *  01 00 40 e4     beq     t1,20 <ok2>
		 *  01 80 fa 6b     ret
		 */
		*a++ = 0x02; *a++ = 0x70; *a++ = 0x60; *a++ = 0x44;	/*  and  */
		*a++ = 0x01; *a++ = 0x00; *a++ = 0x40; *a++ = 0xe4;	/*  beq  */
		*a++ = 0x01; *a++ = 0x80; *a++ = 0xfa; *a++ = 0x6b;	/*  ret  */
		break;
	case LOAD_TYPE_LHU:
	case LOAD_TYPE_LH:
	case LOAD_TYPE_SH:
		/*
		 *  if (t2 & 1)		check for misalignment
		 *	return;
		 *
		 *  02 30 60 44     and     t2,0x1,t1
		 *  01 00 40 e4     beq     t1,20 <ok2>
		 *  01 80 fa 6b     ret
		 */
		*a++ = 0x02; *a++ = 0x30; *a++ = 0x60; *a++ = 0x44;	/*  and  */
		*a++ = 0x01; *a++ = 0x00; *a++ = 0x40; *a++ = 0xe4;	/*  beq  */
		*a++ = 0x01; *a++ = 0x80; *a++ = 0xfa; *a++ = 0x6b;	/*  ret  */
		break;
	case LOAD_TYPE_LBU:
	case LOAD_TYPE_LB:
	case LOAD_TYPE_SB:
		break;
	default:
		;
	}

	/*
	 *  Same virtual page?
	 *  if (cpu->pc_bintrans_data_virtual_4kpage[0] != (t2 & ~0xfff))
	 *	return;
	 *
	 *  90 08 30 a4     ldq     t0,2192(a0)
	 *  00 f0 5f 20     lda     t1,-4096
	 *  02 00 62 44     and     t2,t1,t1
	 *  1f 04 ff 5f     fnop
	 *  a1 05 41 40     cmpeq   t1,t0,t0
	 *  01 00 20 e4     beq     t0,3c <skip>
	 */
	ofs = ((size_t)&dummy_cpu.pc_bintrans_data_virtual_4kpage[0]) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xa4;
	*a++ = 0x00; *a++ = 0xf0; *a++ = 0x5f; *a++ = 0x20;	/*  lda  */
	*a++ = 0x02; *a++ = 0x00; *a++ = 0x62; *a++ = 0x44;	/*  and  */
	*a++ = 0x1f; *a++ = 0x04; *a++ = 0xff; *a++ = 0x5f;	/*  fnop  */
	*a++ = 0xa1; *a++ = 0x05; *a++ = 0x41; *a++ = 0x40;	/*  cmpeq  */
	jumppast1c = a;
	*a++ = 0x01; *a++ = 0x00; *a++ = 0x20; *a++ = 0xe4;	/*  beq  */

	/*
	 *  t3 = hostpage + pageoffset;
	 *  gpr[rt] = load word from t3
	 *
	 *  ff 0f 3f 20     lda     t0,4095
	 *  03 00 61 44     and     t2,t0,t2
	 *  04 04 83 40     addq    t3,t2,t3
	 *  1f 04 ff 5f     fnop
	 *
	 *  00 00 24 a0     ldl     t0,0(t3)
	 *  00 00 30 b4     stq     t0,rt(a0)
	 */
	*a++ = 0xff; *a++ = 0x0f; *a++ = 0x3f; *a++ = 0x20;	/*  lda  */
	*a++ = 0x03; *a++ = 0x00; *a++ = 0x61; *a++ = 0x44;	/*  and  */
	*a++ = 0x04; *a++ = 0x04; *a++ = 0x83; *a++ = 0x40;	/*  addq  */
	*a++ = 0x1f; *a++ = 0x04; *a++ = 0xff; *a++ = 0x5f;	/*  fnop  */

	ofs = ((size_t)&dummy_cpu.gpr[rt]) - (size_t)&dummy_cpu;

/*  TODO: big endian byte order?  */

	switch (load_type) {
	case LOAD_TYPE_LW:
		*a++ = 0x00; *a++ = 0x00; *a++ = 0x24; *a++ = 0xa0;			/*  ldl from memory  */
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb4;	/*  stq gpr[rt]  */
		break;
	case LOAD_TYPE_LHU:
		*a++ = 0x00; *a++ = 0x00; *a++ = 0x24; *a++ = 0x30;			/*  ldwu from memory  */
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb4;	/*  stq gpr[rt]  */
		break;
	case LOAD_TYPE_LH:
		*a++ = 0x00; *a++ = 0x00; *a++ = 0x24; *a++ = 0x30;			/*  ldwu from memory  */
		*a++ = 0x21; *a++ = 0x00; *a++ = 0xe1; *a++ = 0x73;			/*  sextw   t0,t0  */
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb4;	/*  stq gpr[rt]  */
		break;
	case LOAD_TYPE_LBU:
		*a++ = 0x00; *a++ = 0x00; *a++ = 0x24; *a++ = 0x28;			/*  ldbu from memory  */
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb4;	/*  stq gpr[rt]  */
		break;
	case LOAD_TYPE_LB:
		*a++ = 0x00; *a++ = 0x00; *a++ = 0x24; *a++ = 0x28;			/*  ldbu from memory  */
		*a++ = 0x01; *a++ = 0x00; *a++ = 0xe1; *a++ = 0x73;			/*  sextb   t0,t0  */
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb4;	/*  stq gpr[rt]  */
		break;
	case LOAD_TYPE_SW:
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xa0;	/*  ldl gpr[rt]  */
		*a++ = 0x00; *a++ = 0x00; *a++ = 0x24; *a++ = 0xb0;			/*  stl to memory  */
		break;
	case LOAD_TYPE_SH:
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xa0;	/*  ldl gpr[rt]  */
		*a++ = 0x00; *a++ = 0x00; *a++ = 0x24; *a++ = 0x34;			/*  stw to memory  */
		break;
	case LOAD_TYPE_SB:
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xa0;	/*  ldl gpr[rt]  */
		*a++ = 0x00; *a++ = 0x00; *a++ = 0x24; *a++ = 0x38;			/*  stb to memory  */
		break;
	default:
		;
	}





	/*  jump past the [1] case:  */
	/*  00 00 e0 e7     beq     zero,4c <ok>  */
	jumppast2 = a;
	*a++ = 0x00; *a++ = 0x00; *a++ = 0xe0; *a++ = 0xe7;

	/*  if skipping the [0] case, we should end up here:  */
	*jumppast1  = ((size_t)a - (size_t)jumppast1  - 4) / 4;
	if (jumppast1b != NULL)
		*jumppast1b = ((size_t)a - (size_t)jumppast1b - 4) / 4;
	*jumppast1c = ((size_t)a - (size_t)jumppast1c - 4) / 4;






	/*
	 *  if (cpu->pc_bintrans_data_host_4kpage[1] == NULL)
	 *	return;
	 *
	 *  98 08 90 a4     ldq     t3,2200(a0)
	 *  01 00 80 f4     bne     t3,c <ok1>
	 *  01 80 fa 6b     ret
	 */
	ofs = ((size_t)&dummy_cpu.pc_bintrans_data_host_4kpage[1]) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x90; *a++ = 0xa4;

	if (1) {  /* first_load && first_store) { */
		*a++ = 0x01; *a++ = 0x00; *a++ = 0x80; *a++ = 0xf4;
		*a++ = 0x01; *a++ = 0x80; *a++ = 0xfa; *a++ = 0x6b;
	}

	if (first_store) {
		switch (load_type) {
		case LOAD_TYPE_SW:
		case LOAD_TYPE_SH:
		case LOAD_TYPE_SB:
			/*
			 *  if (!cpu->pc_bintrans_data_writeflag[1])
			 *	return;
			 *
			 *  00 00 30 a0     ldl     t0,0(a0)
			 *  01 00 20 f4     bne     t0,2c <ok>
			 *  01 80 fa 6b     ret
			 */
			ofs = ((size_t)&dummy_cpu.pc_bintrans_data_writeflag[1]) - (size_t)&dummy_cpu;
			*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xa0;
			*a++ = 0x01; *a++ = 0x00; *a++ = 0x20; *a++ = 0xf4;
			*a++ = 0x01; *a++ = 0x80; *a++ = 0xfa; *a++ = 0x6b;
			break;
		default:
			;
		}
	}

	/*
	 *  t2 = gpr[rs] + imm;
	 *
	 *  88 08 30 a4     ldq     t0,2184(a0)
	 *  34 12 61 20     lda     t2,4660(t0)
	 */
	ofs = ((size_t)&dummy_cpu.gpr[rs]) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xa4;
	*a++ = (imm & 255); *a++ = (imm >> 8); *a++ = 0x61; *a++ = 0x20;

	switch (load_type) {
	case LOAD_TYPE_LW:
	case LOAD_TYPE_SW:
		/*
		 *  if (t2 & 3)		check for misalignment
		 *	return;
		 *
		 *  02 70 60 44     and     t2,0x3,t1
		 *  01 00 40 e4     beq     t1,20 <ok2>
		 *  01 80 fa 6b     ret
		 */
		*a++ = 0x02; *a++ = 0x70; *a++ = 0x60; *a++ = 0x44;	/*  and  */
		*a++ = 0x01; *a++ = 0x00; *a++ = 0x40; *a++ = 0xe4;	/*  beq  */
		*a++ = 0x01; *a++ = 0x80; *a++ = 0xfa; *a++ = 0x6b;	/*  ret  */
		break;
	case LOAD_TYPE_LHU:
	case LOAD_TYPE_LH:
	case LOAD_TYPE_SH:
		/*
		 *  if (t2 & 1)		check for misalignment
		 *	return;
		 *
		 *  02 30 60 44     and     t2,0x1,t1
		 *  01 00 40 e4     beq     t1,20 <ok2>
		 *  01 80 fa 6b     ret
		 */
		*a++ = 0x02; *a++ = 0x30; *a++ = 0x60; *a++ = 0x44;	/*  and  */
		*a++ = 0x01; *a++ = 0x00; *a++ = 0x40; *a++ = 0xe4;	/*  beq  */
		*a++ = 0x01; *a++ = 0x80; *a++ = 0xfa; *a++ = 0x6b;	/*  ret  */
		break;
	case LOAD_TYPE_LBU:
	case LOAD_TYPE_LB:
	case LOAD_TYPE_SB:
		break;
	default:
		;
	}

	/*
	 *  Same virtual page?
	 *  if (cpu->pc_bintrans_data_virtual_4kpage[1] != (t2 & ~0xfff))
	 *	return;
	 *
	 *  90 08 30 a4     ldq     t0,2192(a0)
	 *  00 f0 5f 20     lda     t1,-4096
	 *  02 00 62 44     and     t2,t1,t1
	 *  1f 04 ff 5f     fnop
	 *  a1 05 41 40     cmpeq   t1,t0,t0
	 *  01 00 20 f4     bne     t0,3c <ok3>
	 *  01 80 fa 6b     ret
	 */
	ofs = ((size_t)&dummy_cpu.pc_bintrans_data_virtual_4kpage[1]) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xa4;
	*a++ = 0x00; *a++ = 0xf0; *a++ = 0x5f; *a++ = 0x20;	/*  lda  */
	*a++ = 0x02; *a++ = 0x00; *a++ = 0x62; *a++ = 0x44;	/*  and  */
	*a++ = 0x1f; *a++ = 0x04; *a++ = 0xff; *a++ = 0x5f;	/*  fnop  */
	*a++ = 0xa1; *a++ = 0x05; *a++ = 0x41; *a++ = 0x40;	/*  cmpeq  */
	*a++ = 0x01; *a++ = 0x00; *a++ = 0x20; *a++ = 0xf4;	/*  bne  */
	*a++ = 0x01; *a++ = 0x80; *a++ = 0xfa; *a++ = 0x6b;	/*  ret  */

	/*
	 *  t3 = hostpage + pageoffset;
	 *  gpr[rt] = load word from t3
	 *
	 *  ff 0f 3f 20     lda     t0,4095
	 *  03 00 61 44     and     t2,t0,t2
	 *  04 04 83 40     addq    t3,t2,t3
	 *  1f 04 ff 5f     fnop
	 *
	 *  00 00 24 a0     ldl     t0,0(t3)
	 *  00 00 30 b4     stq     t0,rt(a0)
	 */
	*a++ = 0xff; *a++ = 0x0f; *a++ = 0x3f; *a++ = 0x20;	/*  lda  */
	*a++ = 0x03; *a++ = 0x00; *a++ = 0x61; *a++ = 0x44;	/*  and  */
	*a++ = 0x04; *a++ = 0x04; *a++ = 0x83; *a++ = 0x40;	/*  addq  */
	*a++ = 0x1f; *a++ = 0x04; *a++ = 0xff; *a++ = 0x5f;	/*  fnop  */

	ofs = ((size_t)&dummy_cpu.gpr[rt]) - (size_t)&dummy_cpu;

/*  TODO: big endian byte order?  */

	switch (load_type) {
	case LOAD_TYPE_LW:
		*a++ = 0x00; *a++ = 0x00; *a++ = 0x24; *a++ = 0xa0;			/*  ldl from memory  */
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb4;	/*  stq gpr[rt]  */
		break;
	case LOAD_TYPE_LHU:
		*a++ = 0x00; *a++ = 0x00; *a++ = 0x24; *a++ = 0x30;			/*  ldwu from memory  */
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb4;	/*  stq gpr[rt]  */
		break;
	case LOAD_TYPE_LH:
		*a++ = 0x00; *a++ = 0x00; *a++ = 0x24; *a++ = 0x30;			/*  ldwu from memory  */
		*a++ = 0x21; *a++ = 0x00; *a++ = 0xe1; *a++ = 0x73;			/*  sextw   t0,t0  */
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb4;	/*  stq gpr[rt]  */
		break;
	case LOAD_TYPE_LBU:
		*a++ = 0x00; *a++ = 0x00; *a++ = 0x24; *a++ = 0x28;			/*  ldbu from memory  */
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb4;	/*  stq gpr[rt]  */
		break;
	case LOAD_TYPE_LB:
		*a++ = 0x00; *a++ = 0x00; *a++ = 0x24; *a++ = 0x28;			/*  ldbu from memory  */
		*a++ = 0x01; *a++ = 0x00; *a++ = 0xe1; *a++ = 0x73;			/*  sextb   t0,t0  */
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb4;	/*  stq gpr[rt]  */
		break;
	case LOAD_TYPE_SW:
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xa0;	/*  ldl gpr[rt]  */
		*a++ = 0x00; *a++ = 0x00; *a++ = 0x24; *a++ = 0xb0;			/*  stl to memory  */
		break;
	case LOAD_TYPE_SH:
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xa0;	/*  ldl gpr[rt]  */
		*a++ = 0x00; *a++ = 0x00; *a++ = 0x24; *a++ = 0x34;			/*  stw to memory  */
		break;
	case LOAD_TYPE_SB:
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xa0;	/*  ldl gpr[rt]  */
		*a++ = 0x00; *a++ = 0x00; *a++ = 0x24; *a++ = 0x38;			/*  stb to memory  */
		break;
	default:
		;
	}



	/*  if skipping the [1] case, we should end up here:  */
	*jumppast2 = ((size_t)a - (size_t)jumppast2 - 4) / 4;



	*addrp = a;
	return 1;
}


/*
 *  bintrans_write_instruction__jr():
 */
int bintrans_write_instruction__jr(unsigned char **addrp,
	int *pc_inc, int rs)
{
	unsigned char *a;
	int ofs;

	bintrans_write_pcflush(addrp, pc_inc, 0, 1);

	/*
	 *   18 09 30 a4     ldq     t0,rs(a0)
	 *   18 09 30 b4     stq     t0,pc(a0)
	 */
	a = *addrp;

	ofs = ((size_t)&dummy_cpu.gpr[rs]) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xa4;

	ofs = ((size_t)&dummy_cpu.pc) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb4;

	*addrp = a;
	return 2;
}


/*
 *  bintrans_write_instruction__jalr():
 */
int bintrans_write_instruction__jalr(unsigned char **addrp,
	int *pc_inc, int rd, int rs)
{
	unsigned char *a;
	int ofs;

	bintrans_write_pcflush(addrp, pc_inc, 1, 1);

	a = *addrp;

	/*
	 *   gpr[rd] = retaddr
	 *
	 *   18 09 30 a4     ldq     t0,pc(a0)
	 *   18 09 30 b4     stq     t0,rd(a0)
	 */

	if (rd != 0) {
		ofs = ((size_t)&dummy_cpu.pc) - (size_t)&dummy_cpu;
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xa4;

		ofs = ((size_t)&dummy_cpu.gpr[rd]) - (size_t)&dummy_cpu;
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb4;
	}


	/*
	 *   pc = gpr[rs]
	 *
	 *   18 09 30 a4     ldq     t0,rs(a0)
	 *   18 09 30 b4     stq     t0,pc(a0)
	 */

	ofs = ((size_t)&dummy_cpu.gpr[rs]) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xa4;

	ofs = ((size_t)&dummy_cpu.pc) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb4;

	*addrp = a;
	return 2;
}


/*
 *  bintrans_write_instruction__mfmthilo():
 */
int bintrans_write_instruction__mfmthilo(unsigned char **addrp,
	int *pc_inc, int rd, int from_flag, int hi_flag)
{
	unsigned char *a;
	int ofs;

	a = *addrp;

	/*
	 *   gpr[rd] = retaddr
	 *
	 *   18 09 30 a4     ldq     t0,hi(a0)  (or lo)
	 *   18 09 30 b4     stq     t0,rd(a0)
	 *
	 *   (or if from_flag is cleared then move the other way, it's
	 *   actually not rd then, but rs...)
	 */

	if (from_flag) {
		if (rd != 0) {
			/*  mfhi or mflo  */
			if (hi_flag)
				ofs = ((size_t)&dummy_cpu.hi) - (size_t)&dummy_cpu;
			else
				ofs = ((size_t)&dummy_cpu.lo) - (size_t)&dummy_cpu;
			*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xa4;

			ofs = ((size_t)&dummy_cpu.gpr[rd]) - (size_t)&dummy_cpu;
			*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb4;
		}
	} else {
		/*  mthi or mtlo  */
		ofs = ((size_t)&dummy_cpu.gpr[rd]) - (size_t)&dummy_cpu;
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xa4;

		if (hi_flag)
			ofs = ((size_t)&dummy_cpu.hi) - (size_t)&dummy_cpu;
		else
			ofs = ((size_t)&dummy_cpu.lo) - (size_t)&dummy_cpu;
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb4;
	}

	*addrp = a;
	return 1;
}


/*
 *  bintrans_write_instruction__branch():
 */
int bintrans_write_instruction__branch(unsigned char **addrp,
	int *pc_inc, int branch_type, int rt, int rs, int imm,
	unsigned char **potential_chunk_p)
{
	unsigned char *a, *b, *b2;
	int n;
	int ofs;
	uint64_t alpha_addr;
	uint64_t subaddr;

#if 0
	/*  Flush the PC, but don't include this instruction.  */
	(*pc_inc) -= 4;
	bintrans_write_pcflush(addrp, pc_inc, 1, 1);
	(*pc_inc) += 4;
#else
	/*  Flush the PC. The branch instruction is already included
	    in the pc_inc count.  */
	bintrans_write_pcflush(addrp, pc_inc, 1, 1);

	/*  Flush once more, to make sure the instruction in the delay
	    slot is counted, but _don't_ update the PC:  */
	(*pc_inc) = 4;
	bintrans_write_pcflush(addrp, pc_inc, 0, 1);
#endif

	a = *addrp;

	/*
	 *  t0 = gpr[rt]; t1 = gpr[rs];
	 *
	 *  50 00 30 a4     ldq     t0,80(a0)
	 *  58 00 50 a4     ldq     t1,88(a0)
	 */

	ofs = ((size_t)&dummy_cpu.gpr[rt]) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xa4;
	ofs = ((size_t)&dummy_cpu.gpr[rs]) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x50; *a++ = 0xa4;

	/*
	 *  Compare t0 and t1 for equality.
	 *  If the result was false (equal to zero), then skip a lot
	 *  of instructions:
	 *
	 *  a1 05 22 40     cmpeq   t0,t1,t0
	 *  01 00 20 e4     beq     t0,14 <f+0x14>
	 */

	b = NULL;

	if (branch_type == BRANCH_BEQ && rt != rs) {
		*a++ = 0xa1; *a++ = 0x05; *a++ = 0x22; *a++ = 0x40;  /*  cmpeq  */
		b = a;
		*a++ = 0x01; *a++ = 0x00; *a++ = 0x20; *a++ = 0xe4;  /*  beq  */
		/*     ^^^^  --- NOTE: This is automagically updated later on.  */
	}
	if (branch_type == BRANCH_BNE && rt != rs) {
		*a++ = 0xa1; *a++ = 0x05; *a++ = 0x22; *a++ = 0x40;  /*  cmpeq  */
		b = a;
		*a++ = 0x01; *a++ = 0x00; *a++ = 0x20; *a++ = 0xf4;  /*  bne  */
		/*     ^^^^  --- NOTE: This is automagically updated later on.  */
	}

	/*
	 *  Perform the jump by changing pc.
	 *
	 *  44 04 30 a4     ldq     t0,1092(a0)		load pc
	 *  c8 01 5f 20     lda     t1,456
	 *  22 57 40 48     sll     t1,0x2,t1
	 *  01 04 22 40     addq    t0,t1,t0
	 *  88 08 30 b4     stq     t0,2184(a0)		store pc
	 */
	ofs = ((size_t)&dummy_cpu.pc) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xa4;

	*a++ = (imm & 255); *a++ = (imm >> 8); *a++ = 0x5f; *a++ = 0x20;  /*  lda  */
	*a++ = 0x22; *a++ = 0x57; *a++ = 0x40; *a++ = 0x48;  /*  sll  */
	*a++ = 0x01; *a++ = 0x04; *a++ = 0x22; *a++ = 0x40;  /*  addq  */

	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb4;


	if (potential_chunk_p == NULL) {
		/*  Not much we can do here if this wasn't to the same
		    physical page...  */
		*a++ = 0x01; *a++ = 0x80; *a++ = 0xfa; *a++ = 0x6b;	/*  ret  */
	} else {
		/*
		 *  Just to make sure that we don't become too unreliant
		 *  on the main program loop, we need to return every once
		 *  in a while (interrupts etc).
		 *
		 *  Load the "nr of instructions executed" (which is an int)
		 *  and see if it is below a certain threshold. If so, then
		 *  we go on with the fast path (bintrans), otherwise we
		 *  abort by returning.
		 *
		 *  f4 01 5f 20     lda     t1,500  (some low number...)
		 *  50 00 30 a0     ldl     t0,80(a0)
		 *  a1 0d 22 40     cmple   t0,t1,t0
		 *  01 00 20 f4     bne     t0,14 <f+0x14>
		 */
		ofs = ((size_t)&dummy_cpu.bintrans_instructions_executed)
		    - ((size_t)&dummy_cpu);
		*a++ = 0xc0; *a++ = 0x01; *a++ = 0x5f; *a++ = 0x20;	/*  lda  */
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xa0;
		*a++ = 0xa1; *a++ = 0x0d; *a++ = 0x22; *a++ = 0x40;	/*  cmple  */
		*a++ = 0x01; *a++ = 0x00; *a++ = 0x20; *a++ = 0xf4;	/*  bne  */
		*a++ = 0x01; *a++ = 0x80; *a++ = 0xfa; *a++ = 0x6b;	/*  ret  */

		/*
		 *  potential_chunk_p points to an "unsigned char *".
		 *  If this value is non-NULL, then it is a piece of Alpha
		 *  machine language code corresponding to the address
		 *  we're jumping to. Otherwise, those instructions haven't
		 *  been translated yet, so we have to return to the main
		 *  loop.
		 */

		/*  15 bits at a time, which means max 60 bits, but
		    that should be enough. the top 4 bits are probably
		    not used by userland alpha code. (TODO: verify this)  */
		alpha_addr = (size_t)potential_chunk_p;
		subaddr = (alpha_addr >> 45) & 0x7fff;

		/*
		 *  00 00 3f 20     lda     t0,0
		 *  21 f7 21 48     sll     t0,0xf,t0
		 *  34 12 21 20     lda     t0,4660(t0)
		 *  21 f7 21 48     sll     t0,0xf,t0
		 *  34 12 21 20     lda     t0,4660(t0)
		 *  21 f7 21 48     sll     t0,0xf,t0
		 *  34 12 21 20     lda     t0,4660(t0)
		 */

		/*  Start with the topmost 15 bits:  */
		*a++ = (subaddr & 255); *a++ = (subaddr >> 8); *a++ = 0x3f; *a++ = 0x20;
		*a++ = 0x21; *a++ = 0xf7; *a++ = 0x21; *a++ = 0x48;	/*  sll  */

		subaddr = (alpha_addr >> 30) & 0x7fff;
		*a++ = (subaddr & 255); *a++ = (subaddr >> 8); *a++ = 0x21; *a++ = 0x20;
		*a++ = 0x21; *a++ = 0xf7; *a++ = 0x21; *a++ = 0x48;	/*  sll  */

		subaddr = (alpha_addr >> 15) & 0x7fff;
		*a++ = (subaddr & 255); *a++ = (subaddr >> 8); *a++ = 0x21; *a++ = 0x20;
		*a++ = 0x21; *a++ = 0xf7; *a++ = 0x21; *a++ = 0x48;	/*  sll  */

		subaddr = alpha_addr & 0x7fff;
		*a++ = (subaddr & 255); *a++ = (subaddr >> 8); *a++ = 0x21; *a++ = 0x20;

		/*
		 *  Load the chunk pointer into t0.
		 *  If it is NULL (zero), then skip the following jump.
		 *  Jump to t0.
		 */

		*a++ = 0x00; *a++ = 0x00; *a++ = 0x21; *a++ = 0xa4;	/*  ldq t0,0(t0)  */
		*a++ = 0x01; *a++ = 0x00; *a++ = 0x20; *a++ = 0xe4;	/*  beq t0,<skip>  */

		/*  00 00 e1 6b     jmp     (t0)  */
		*a++ = 0x00; *a++ = 0x00; *a++ = 0xe1; *a++ = 0x6b;	/*  jmp (t0)  */


		/*  "Failure", then let's return to the main loop.  */
		*a++ = 0x01; *a++ = 0x80; *a++ = 0xfa; *a++ = 0x6b;	/*  ret  */
	}

	b2 = a;
	n = (size_t)b2 - (size_t)b - 4;
	if (b != NULL)
		*b = n/4;	/*  nr of skipped instructions  */

	*addrp = a;

#if 1
	/*  Flush _again_; this time, it is to update the PC for the
	    instruction in the delay slot, but don't update the count
	    as it is already updated.  */
	(*pc_inc) += 4;
	bintrans_write_pcflush(addrp, pc_inc, 1, 0);
#else
	(*pc_inc) += 4;
#endif

	return 2;
}


/*
 *  bintrans_write_instruction__jal():
 */
int bintrans_write_instruction__jal(unsigned char **addrp,
	int *pc_inc, int imm, int link)
{
	unsigned char *a;
	int ofs;
	uint64_t subimm;

	/*  Flush both the jal and the delay slot instruction:  */
	bintrans_write_pcflush(addrp, pc_inc, 1, 1);

	a = *addrp;

	/*
	 *  gpr[31] = retaddr
	 *
	 *  18 09 30 a4     ldq     t0,pc(a0)
	 *  18 09 30 b4     stq     t0,gpr31(a0)
	 */
	if (link) {
		ofs = ((size_t)&dummy_cpu.pc) - (size_t)&dummy_cpu;
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xa4;

		ofs = ((size_t)&dummy_cpu.gpr[31]) - (size_t)&dummy_cpu;
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb4;
	}

	/*  Set the lowest 28 bits of pc to imm*4:  */

	/*
	 *  imm = 4*imm;
	 *  t0 = pc
	 *  shift right t0 28 steps.
	 *  shift left t0 14 steps.
	 *  OR in 14 bits of part of imm
	 *  shift left 14 steps.
	 *  OR in the lowest 14 bits of imm.
	 *  pc = t0
	 *
	 *  18 09 30 a4     ldq     t0,pc(a0)
	 *  81 96 23 48     srl     t0,0x1c,t0
	 *  21 d7 21 48     sll     t0,0xe,t0
	 *  c8 01 5f 20     lda     t1,456
	 *  01 04 22 44     or      t0,t1,t0
	 *  21 d7 21 48     sll     t0,0xe,t0
	 *  c8 01 5f 20     lda     t1,456
	 *  01 04 22 44     or      t0,t1,t0
	 *  18 09 30 b4     stq     t0,pc(a0)
	 */

	imm *= 4;

	ofs = ((size_t)&dummy_cpu.pc) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xa4;

	*a++ = 0x81; *a++ = 0x96; *a++ = 0x23; *a++ = 0x48;
	*a++ = 0x21; *a++ = 0xd7; *a++ = 0x21; *a++ = 0x48;

	subimm = (imm >> 14) & 0x3fff;
	*a++ = (subimm & 255); *a++ = (subimm >> 8); *a++ = 0x5f; *a++ = 0x20;

	*a++ = 0x01; *a++ = 0x04; *a++ = 0x22; *a++ = 0x44;
	*a++ = 0x21; *a++ = 0xd7; *a++ = 0x21; *a++ = 0x48;

	subimm = imm & 0x3fff;
	*a++ = (subimm & 255); *a++ = (subimm >> 8); *a++ = 0x5f; *a++ = 0x20;

	*a++ = 0x01; *a++ = 0x04; *a++ = 0x22; *a++ = 0x44;

	ofs = ((size_t)&dummy_cpu.pc) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb4;

	*addrp = a;

	return 2;
}

