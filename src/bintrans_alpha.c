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
 *  $Id: bintrans_alpha.c,v 1.13 2004-10-17 15:31:44 debug Exp $
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
 *  bintrans_write_instruction():
 *
 *  TODO: Comment.
 */
int bintrans_write_instruction(unsigned char **addrp, int instr,
	int *pc_increment, uint64_t addr_a, uint64_t addr_b)
{
	unsigned char *a;
	int ofs;
	int res = 0;
	uint64_t v;

	switch (instr) {
	case INSTR_NOP:
		/*  Add nothing, but succeed.  */
		res = 1;
		break;
	case INSTR_JAL:
		/*  JAL to addr_a, set r31 = addr_b  */
		/*  printf("JAL addr_a=%016llx, addr_b=%016llx\n",
		    addr_a, addr_b);  */
		bintrans_write_pcflush(addrp, pc_increment, 0, 1);
		a = *addrp;

		/*
   0:   34 12 3f 20     lda     t0,4660
   4:   01 00 21 24     ldah    t0,1(t0)
   4:   21 17 22 48     sll     t0,0x10,t0
   8:   dc fe 5f 20     lda     t1,-292
   c:   01 00 42 24     ldah    t1,1(t1)
  10:   01 04 41 44     or      t1,t0,t0
  14:   28 01 30 b4     stq     t0,296(a0)
		 */
		/*  pc = addr_a  */
		ofs = ((size_t)&dummy_cpu.pc) - ((size_t)&dummy_cpu);

		/*  t0 = bits 48..63  */
		v = (addr_a >> 48) & 0xffff;
		/*  lda t0,v  */
		*a++ = (v & 255); *a++ = (v >> 8); *a++ = 0x3f; *a++ = 0x20;
		if (v & 0x8000) {
			/*  ldah t0,1(t0)  */
			*a++ = 0x01; *a++ = 0x00; *a++ = 0x21; *a++ = 0x24;
		}

		/*  sll t0,0x10,t0:  */
		*a++ = 0x21; *a++ = 0x17; *a++ = 0x22; *a++ = 0x48;

		/*  t1 = bits 32..47  */
		v = (addr_a >> 32) & 0xffff;
		/*  lda t1,v  */
		*a++ = (v & 255); *a++ = (v >> 8); *a++ = 0x5f; *a++ = 0x20;
		if (v & 0x8000) {
			/*  ldah t1,1(t1)  */
			*a++ = 0x01; *a++ = 0x00; *a++ = 0x42; *a++ = 0x24;
		}

		/*  or t1,t0,t0:  */
		*a++ = 0x01; *a++ = 0x04; *a++ = 0x41; *a++ = 0x44;

		/*  sll t0,0x10,t0:  */
		*a++ = 0x21; *a++ = 0x17; *a++ = 0x22; *a++ = 0x48;

		/*  t1 = bits 16..31  */
		v = (addr_a >> 16) & 0xffff;
		/*  lda t1,v  */
		*a++ = (v & 255); *a++ = (v >> 8); *a++ = 0x5f; *a++ = 0x20;
		if (v & 0x8000) {
			/*  ldah t1,1(t1)  */
			*a++ = 0x01; *a++ = 0x00; *a++ = 0x42; *a++ = 0x24;
		}

		/*  or t1,t0,t0:  */
		*a++ = 0x01; *a++ = 0x04; *a++ = 0x41; *a++ = 0x44;

		/*  sll t0,0x10,t0:  */
		*a++ = 0x21; *a++ = 0x17; *a++ = 0x22; *a++ = 0x48;

		/*  t1 = bits 0..15  */
		v = addr_a & 0xffff;
		/*  lda t1,v  */
		*a++ = (v & 255); *a++ = (v >> 8); *a++ = 0x5f; *a++ = 0x20;
		if (v & 0x8000) {
			/*  ldah t1,1(t1)  */
			*a++ = 0x01; *a++ = 0x00; *a++ = 0x42; *a++ = 0x24;
		}

		/*  or t1,t0,t0:  */
		*a++ = 0x01; *a++ = 0x04; *a++ = 0x41; *a++ = 0x44;

		/*  stq t0,ofs(a0):  */
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb4;



		/*  r31 = addr_b  */
		ofs = ((size_t)&dummy_cpu.gpr[31]) - ((size_t)&dummy_cpu);

		/*  t0 = bits 48..63  */
		v = (addr_b >> 48) & 0xffff;
		/*  lda t0,v  */
		*a++ = (v & 255); *a++ = (v >> 8); *a++ = 0x3f; *a++ = 0x20;
		if (v & 0x8000) {
			/*  ldah t0,1(t0)  */
			*a++ = 0x01; *a++ = 0x00; *a++ = 0x21; *a++ = 0x24;
		}

		/*  sll t0,0x10,t0:  */
		*a++ = 0x21; *a++ = 0x17; *a++ = 0x22; *a++ = 0x48;

		/*  t1 = bits 32..47  */
		v = (addr_b >> 32) & 0xffff;
		/*  lda t1,v  */
		*a++ = (v & 255); *a++ = (v >> 8); *a++ = 0x5f; *a++ = 0x20;
		if (v & 0x8000) {
			/*  ldah t1,1(t1)  */
			*a++ = 0x01; *a++ = 0x00; *a++ = 0x42; *a++ = 0x24;
		}

		/*  or t1,t0,t0:  */
		*a++ = 0x01; *a++ = 0x04; *a++ = 0x41; *a++ = 0x44;

		/*  sll t0,0x10,t0:  */
		*a++ = 0x21; *a++ = 0x17; *a++ = 0x22; *a++ = 0x48;

		/*  t1 = bits 16..31  */
		v = (addr_b >> 16) & 0xffff;
		/*  lda t1,v  */
		*a++ = (v & 255); *a++ = (v >> 8); *a++ = 0x5f; *a++ = 0x20;
		if (v & 0x8000) {
			/*  ldah t1,1(t1)  */
			*a++ = 0x01; *a++ = 0x00; *a++ = 0x42; *a++ = 0x24;
		}

		/*  or t1,t0,t0:  */
		*a++ = 0x01; *a++ = 0x04; *a++ = 0x41; *a++ = 0x44;

		/*  sll t0,0x10,t0:  */
		*a++ = 0x21; *a++ = 0x17; *a++ = 0x22; *a++ = 0x48;

		/*  t1 = bits 0..15  */
		v = addr_b & 0xffff;
		/*  lda t1,v  */
		*a++ = (v & 255); *a++ = (v >> 8); *a++ = 0x5f; *a++ = 0x20;
		if (v & 0x8000) {
			/*  ldah t1,1(t1)  */
			*a++ = 0x01; *a++ = 0x00; *a++ = 0x42; *a++ = 0x24;
		}

		/*  or t1,t0,t0:  */
		*a++ = 0x01; *a++ = 0x04; *a++ = 0x41; *a++ = 0x44;

		/*  stq t0,ofs(a0):  */
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb4;



		*addrp = a;
		res = 1;
		break;
	default:
		fatal("bintrans_write_instruction(): unimplemented "
		    "instruction %i\n", instr);
		exit(1);
	}

	return res;
}

