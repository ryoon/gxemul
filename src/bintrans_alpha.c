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
 *  $Id: bintrans_alpha.c,v 1.14 2004-11-07 13:23:46 debug Exp $
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
void bintrans_write_pcflush(unsigned char **addrp, int *pc_increment,
	int flag_pc, int flag_ninstr)
{
	unsigned char *a = *addrp;
	int inc = *pc_increment;
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

	*pc_increment = 0;
	*addrp = a;
}


/*
 *  bintrans_write_instruction__addiu():
 */
int bintrans_write_instruction__addiu(unsigned char **addrp,
	int *pc_increment, int rt, int rs, int imm)
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
	int *pc_increment, int rt, int rs, int imm)
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
	int *pc_increment, int rt, int rs, int imm)
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
	int *pc_increment, int rt, int rs, int imm)
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
	int *pc_increment, int rd, int rs, int rt)
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
	int *pc_increment, int rd, int rs, int rt)
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
	int *pc_increment, int rd, int rs, int rt)
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
	int *pc_increment, int rd, int rs, int rt)
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
	int *pc_increment, int rd, int rs, int rt)
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
	int *pc_increment, int rd, int rs, int rt)
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
	int *pc_increment, int rd, int rs, int rt)
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
	int *pc_increment, int rd, int rs, int rt)
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
	int *pc_increment, int rd, int rt, int sa)
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

	/*  Note: bits of sa are distributed among two different bytes.  */
	*a++ = 0x21; *a++ = 0x17 + ((sa & 7) << 5); *a++ = 0x20 + (sa >> 3); *a++ = 0x48;

	*a++ = 0x01; *a++ = 0x00; *a++ = 0x3f; *a++ = 0x40;	/*  addl  */

	ofs = ((size_t)&dummy_cpu.gpr[rd]) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb4;

	*addrp = a;
	return 1;
}


/*
 *  bintrans_write_instruction__lui():
 */
int bintrans_write_instruction__lui(unsigned char **addrp,
	int *pc_increment, int rt, int imm)
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
 *  bintrans_write_instruction__jr():
 */
int bintrans_write_instruction__jr(unsigned char **addrp,
	int *pc_increment, int rs)
{
	unsigned char *a;
	int ofs;

	bintrans_write_pcflush(addrp, pc_increment, 0, 1);

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
	int *pc_increment, int rd, int rs)
{
	unsigned char *a;
	int ofs;

	bintrans_write_pcflush(addrp, pc_increment, 1, 1);

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


