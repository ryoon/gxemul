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
 *  $Id: bintrans_alpha.c,v 1.35 2004-11-16 20:50:36 debug Exp $
 *
 *  Alpha specific code for dynamic binary translation.
 *
 *  See bintrans.c for more information.  Included from bintrans.c.
 *
 *
 *  Some Alpha registers that are reasonable to use:
 *
 *	t5..t7		6..8		3
 *	s0..s6		9..15		7
 *	a1..a5		17..21		5
 *	t8..t11		22..25		4
 *
 *  These can be "mapped" to MIPS registers in the translated code,
 *  except a0 which points to the cpu struct, and t0..t4 (or so)
 *  which are used by the translated code as temporaries.
 *
 *  3 + 7 + 5 + 4 = 19 available registers. Of course, all (except
 *  s0..s6) must be saved when calling external functions, such as
 *  when doing load/store.
 *
 *  Which are the 19 most commonly used MIPS registers? (This will
 *  include the pc, and the "current number of executed translated
 *  instructions.)
 *
 *  The current allocation is as follows:
 *
 *	Alpha:		MIPS:
 *	------		-----
 *
 *	t5		pc  (64-bit)
 *
 *	t6		bintrans_instructions_executed (32-bit int)
 *
 *	t7..t11,	TODO
 *	  s0..s6,
 *	  a1..a5
 */


struct cpu dummy_cpu;
struct coproc dummy_coproc;


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
static void bintrans_host_cacheinvalidate(unsigned char *p, size_t len)
{
	/*  Long form of ``asm("imb");''  */

	void (*f)(void);
	f = (void *)&bintrans_alpha_imb[0];
	f();
}


/*
 *  lda sp,-128(sp)	some margin
 *  stq ra,0(sp)
 *  stq s0,8(sp)
 *  stq s1,16(sp)
 *  stq s2,24(sp)
 *  stq s3,32(sp)
 *  stq s4,40(sp)
 *  stq s5,48(sp)
 *  stq s6,56(sp)
 *
 *  jsr ra,(a1),<back>
 *  back:
 *
 *  ldq ra,0(sp)
 *  ldq s0,8(sp)
 *  ldq s1,16(sp)
 *  ldq s2,24(sp)
 *  ldq s3,32(sp)
 *  ldq s4,40(sp)
 *  ldq s5,48(sp)
 *  ldq s6,56(sp)
 *  lda sp,128(sp)
 *  ret
 */
#define ofs_pc	(((size_t)&dummy_cpu.pc) - ((size_t)&dummy_cpu))
#define ofs_n	(((size_t)&dummy_cpu.bintrans_instructions_executed) - ((size_t)&dummy_cpu))
unsigned char bintrans_alpha_runchunk[104] = {
	0x80, 0xff, 0xde, 0x23,		/*  lda     sp,-128(sp)  */
	0x00, 0x00, 0x5e, 0xb7,		/*  stq     ra,0(sp)  */
	0x08, 0x00, 0x3e, 0xb5,		/*  stq     s0,8(sp)  */
	0x10, 0x00, 0x5e, 0xb5,		/*  stq     s1,16(sp)  */
	0x18, 0x00, 0x7e, 0xb5,		/*  stq     s2,24(sp)  */
	0x20, 0x00, 0x9e, 0xb5,		/*  stq     s3,32(sp)  */
	0x28, 0x00, 0xbe, 0xb5,		/*  stq     s4,40(sp)  */
	0x30, 0x00, 0xde, 0xb5,		/*  stq     s5,48(sp)  */
	0x38, 0x00, 0xfe, 0xb5,		/*  stq     s6,56(sp)  */
	0x78, 0x00, 0xbe, 0xb7,		/*  stq     gp,120(sp)  */

	ofs_pc&255,ofs_pc>>8,0xd0,0xa4,	/*  ldq     t5,"pc"(a0)  */
	ofs_n&255,ofs_n>>8,0xf0,0xa0,	/*  ldl     t6,"bintrans_instructions_executed"(a0)  */

	0x00, 0x40, 0x51, 0x6b,		/*  jsr     ra,(a1),<back>  */

	ofs_pc&255,ofs_pc>>8,0xd0,0xb4,	/*  stq     t5,"pc"(a0)  */
	ofs_n&255,ofs_n>>8,0xf0,0xb0,	/*  stl     t6,"bintrans_instructions_executed"(a0)  */

	0x00, 0x00, 0x5e, 0xa7,		/*  ldq     ra,0(sp)  */
	0x08, 0x00, 0x3e, 0xa5,		/*  ldq     s0,8(sp)  */
	0x10, 0x00, 0x5e, 0xa5,		/*  ldq     s1,16(sp)  */
	0x18, 0x00, 0x7e, 0xa5,		/*  ldq     s2,24(sp)  */
	0x20, 0x00, 0x9e, 0xa5,		/*  ldq     s3,32(sp)  */
	0x28, 0x00, 0xbe, 0xa5,		/*  ldq     s4,40(sp)  */
	0x30, 0x00, 0xde, 0xa5,		/*  ldq     s5,48(sp)  */
	0x38, 0x00, 0xfe, 0xa5,		/*  ldq     s6,56(sp)  */
	0x78, 0x00, 0xbe, 0xa7,		/*  ldq     gp,120(sp)  */
	0x80, 0x00, 0xde, 0x23,		/*  lda     sp,128(sp)  */
	0x01, 0x80, 0xfa, 0x6b		/*  ret   */
};


/*
 *
 */
static void bintrans_runchunk(struct cpu *cpu, unsigned char *code)
{
	void (*f)(struct cpu *, unsigned char *);
	f = (void *)&bintrans_alpha_runchunk[0];
	f(cpu, code);
}


/*
 *  bintrans_write_chunkreturn():
 */
static void bintrans_write_chunkreturn(unsigned char **addrp)
{
	uint32_t *a = (uint32_t *) *addrp;
	*a++ = 0x6bfa8001;	/*  ret  */
	*addrp = (unsigned char *) a;
}


/*
 *  bintrans_write_pc_inc():
 */
static void bintrans_write_pc_inc(unsigned char **addrp, int pc_inc,
	int flag_pc, int flag_ninstr)
{
	uint32_t *a = (uint32_t *) *addrp;

	if (pc_inc == 0)
		return;

	if (flag_pc) {
		/*  lda t5,pc_inc(t5)  */
		*a++ = 0x20c60000 | pc_inc;
	}

	if (flag_ninstr) {
		pc_inc /= 4;
		/*  lda t6,pc_inc(t6)  */
		*a++ = 0x20e70000 | pc_inc;
	}

	*addrp = (unsigned char *) a;
}


/*
 *  bintrans_write_instruction__addiu_etc():
 */
static int bintrans_write_instruction__addiu_etc(unsigned char **addrp,
	int rt, int rs, int imm, int instruction_type)
{
	unsigned char *a;
	unsigned int uimm;
	int ofs;
	int load64 = 0, sign3264 = 1;

	switch (instruction_type) {
	case HI6_DADDIU:
	case HI6_ORI:
	case HI6_XORI:
	case HI6_SLTI:
	case HI6_SLTIU:
		load64 = 1;
	}

	switch (instruction_type) {
	case HI6_ANDI:
	case HI6_ORI:
	case HI6_XORI:
	case HI6_DADDIU:
		sign3264 = 0;
	}

	a = *addrp;

	if (rt == 0)
		goto rt0;

	uimm = imm & 0xffff;
	ofs = ((size_t)&dummy_cpu.gpr[rs]) - (size_t)&dummy_cpu;

	if (load64) {
		/*  ldq t0,rs(a0)  */
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xa4;
	} else {
		/*  ldl t0,rs(a0)  */
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xa0;
	}

	switch (instruction_type) {
	case HI6_ADDIU:
	case HI6_DADDIU:
		/*  lda t0,imm(t0)  */
		*a++ = (uimm & 255); *a++ = (uimm >> 8); *a++ = 0x21; *a++ = 0x20;
		break;
	case HI6_ANDI:
	case HI6_ORI:
	case HI6_XORI:
		/*  lda t1,4660  */
		*a++ = (uimm & 255); *a++ = (uimm >> 8); *a++ = 0x5f; *a++ = 0x20;
		if (uimm & 0x8000) {
			/*  01 00 42 24  ldah t1,1(t1)	<-- if negative only  */
			*a++ = 0x01; *a++ = 0x00; *a++ = 0x42; *a++ = 0x24;
		}
		switch (instruction_type) {
		case HI6_ANDI:
			*a++ = 0x01; *a++ = 0x00; *a++ = 0x22; *a++ = 0x44;	/*  and t0,t1,t0  */
			break;
		case HI6_ORI:
			*a++ = 0x01; *a++ = 0x04; *a++ = 0x22; *a++ = 0x44;	/*  or t0,t1,t0  */
			break;
		case HI6_XORI:
			*a++ = 0x01; *a++ = 0x08; *a++ = 0x22; *a++ = 0x44;	/*  xor t0,t1,t0  */
			break;
		}
		break;
	case HI6_SLTI:
	case HI6_SLTIU:
		/*  lda t1,4660  */
		*a++ = (uimm & 255); *a++ = (uimm >> 8); *a++ = 0x5f; *a++ = 0x20;
		switch (instruction_type) {
		case HI6_SLTI:
			*a++ = 0xa1; *a++ = 0x09; *a++ = 0x22; *a++ = 0x40;	/*  cmplt  */
			break;
		case HI6_SLTIU:
			*a++ = 0xa1; *a++ = 0x03; *a++ = 0x22; *a++ = 0x40;	/*  cmpult  */
			break;
		}
		break;
	}

	if (sign3264) {
		/*  sign extend, 32->64 bits:  addl t0,zero,t0  */
		*a++ = 0x01; *a++ = 0x00; *a++ = 0x3f; *a++ = 0x40;
	}

	*a++ = 0x1f; *a++ = 0x04; *a++ = 0xff; *a++ = 0x5f;	/*  fnop  */

	/*  stq t0,2184(a0)		store rt (64-bit)  */
	ofs = ((size_t)&dummy_cpu.gpr[rt]) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb4;

rt0:
	*addrp = a;
	bintrans_write_pc_inc(addrp, sizeof(uint32_t), 1, 1);
	return 1;
}


/*
 *  bintrans_write_instruction__addu_etc():
 */
static int bintrans_write_instruction__addu_etc(unsigned char **addrp,
	int rd, int rs, int rt, int sa, int instruction_type)
{
	unsigned char *a;
	int ofs;
	int load64 = 0;

	switch (instruction_type) {
	case SPECIAL_DADDU:
	case SPECIAL_DSUBU:
	case SPECIAL_OR:
	case SPECIAL_AND:
	case SPECIAL_NOR:
	case SPECIAL_XOR:
	case SPECIAL_DSLL:
	case SPECIAL_DSRL:
	case SPECIAL_DSRA:
	case SPECIAL_DSLL32:
	case SPECIAL_DSRL32:
	case SPECIAL_DSRA32:
	case SPECIAL_SLT:
	case SPECIAL_SLTU:
		load64 = 1;
	}

	if (rd == 0)
		goto rd0;

	a = *addrp;

	ofs = ((size_t)&dummy_cpu.gpr[rs]) - (size_t)&dummy_cpu;

	/*  t0 = rs, t1 = rt  */
	if (load64) {
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xa4;
		ofs = ((size_t)&dummy_cpu.gpr[rt]) - (size_t)&dummy_cpu;
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x50; *a++ = 0xa4;
	} else {
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xa0;
		ofs = ((size_t)&dummy_cpu.gpr[rt]) - (size_t)&dummy_cpu;
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x50; *a++ = 0xa0;
	}

	switch (instruction_type) {
	case SPECIAL_ADDU:
		*a++ = 0x01; *a++ = 0x00; *a++ = 0x22; *a++ = 0x40;	/*  addl t0,t1,t0  */
		break;
	case SPECIAL_DADDU:
		*a++ = 0x01; *a++ = 0x04; *a++ = 0x22; *a++ = 0x40;	/*  addq t0,t1,t0  */
		break;
	case SPECIAL_SUBU:
		*a++ = 0x21; *a++ = 0x01; *a++ = 0x22; *a++ = 0x40;	/*  subl t0,t1,t0  */
		break;
	case SPECIAL_DSUBU:
		*a++ = 0x21; *a++ = 0x05; *a++ = 0x22; *a++ = 0x40;	/*  subq t0,t1,t0  */
		break;
	case SPECIAL_AND:
		*a++ = 0x01; *a++ = 0x00; *a++ = 0x22; *a++ = 0x44;	/*  and t0,t1,t0  */
		break;
	case SPECIAL_OR:
		*a++ = 0x01; *a++ = 0x04; *a++ = 0x22; *a++ = 0x44;	/*  or t0,t1,t0  */
		break;
	case SPECIAL_NOR:
		*a++ = 0x01; *a++ = 0x04; *a++ = 0x22; *a++ = 0x44;	/*  or t0,t1,t0  */
		*a++ = 0x01; *a++ = 0x05; *a++ = 0xe1; *a++ = 0x47;	/*  not t0,t0  */
		break;
	case SPECIAL_XOR:
		*a++ = 0x01; *a++ = 0x08; *a++ = 0x22; *a++ = 0x44;	/*  xor t0,t1,t0  */
		break;
	case SPECIAL_SLL:
		*a++ = 0x21; *a++ = 0x17 + ((sa & 7) << 5); *a++ = 0x40 + (sa >> 3); *a++ = 0x48;	/*  sll t1,sa,t0  */
		*a++ = 0x01; *a++ = 0x00; *a++ = 0x3f; *a++ = 0x40;     /*  addl t0,0,t0  */
		break;
	case SPECIAL_SLLV:
		/*  rd = rt << (rs&31)  (logical)     t0 = t1 << (t0&31)  */
		*a++ = 0x01; *a++ = 0xf0; *a++ = 0x23; *a++ = 0x44;     /*  and t0,31,t0  */
		*a++ = 0x21; *a++ = 0x07; *a++ = 0x41; *a++ = 0x48;	/*  sll t1,t0,t0  */
		*a++ = 0x01; *a++ = 0x00; *a++ = 0x3f; *a++ = 0x40;     /*  addl t0,0,t0  */
		break;
	case SPECIAL_DSLL:
		*a++ = 0x21; *a++ = 0x17 + ((sa & 7) << 5); *a++ = 0x40 + (sa >> 3); *a++ = 0x48;	/*  sll t1,sa,t0  */
		break;
	case SPECIAL_DSLL32:
		sa += 32;
		*a++ = 0x21; *a++ = 0x17 + ((sa & 7) << 5); *a++ = 0x40 + (sa >> 3); *a++ = 0x48;	/*  sll t1,sa,t0  */
		break;
	case SPECIAL_SRA:
		*a++ = 0x81; *a++ = 0x17 + ((sa & 7) << 5); *a++ = 0x40 + (sa >> 3); *a++ = 0x48;	/*  sra t1,sa,t0  */
		*a++ = 0x01; *a++ = 0x00; *a++ = 0x3f; *a++ = 0x40;     /*  addl t0,0,t0  */
		break;
	case SPECIAL_SRAV:
		/*  rd = rt >> (rs&31)  (arithmetic)     t0 = t1 >> (t0&31)  */
		*a++ = 0x01; *a++ = 0xf0; *a++ = 0x23; *a++ = 0x44;     /*  and t0,31,t0  */
		*a++ = 0x81; *a++ = 0x07; *a++ = 0x41; *a++ = 0x48;	/*  sra t1,t0,t0  */
		*a++ = 0x01; *a++ = 0x00; *a++ = 0x3f; *a++ = 0x40;     /*  addl t0,0,t0  */
		break;
	case SPECIAL_DSRA:
		*a++ = 0x81; *a++ = 0x17 + ((sa & 7) << 5); *a++ = 0x40 + (sa >> 3); *a++ = 0x48;	/*  sra t1,sa,t0  */
		break;
	case SPECIAL_DSRA32:
		sa += 32;
		*a++ = 0x81; *a++ = 0x17 + ((sa & 7) << 5); *a++ = 0x40 + (sa >> 3); *a++ = 0x48;	/*  sra t1,sa,t0  */
		break;
	case SPECIAL_SRL:
		*a++ = 0x22; *a++ = 0xf6; *a++ = 0x41; *a++ = 0x48;	/*  zapnot t1,0xf,t1 (use only lowest 32 bits)  */
		/*  Note: bits of sa are distributed among two different bytes.  */
		*a++ = 0x81; *a++ = 0x16 + ((sa & 7) << 5); *a++ = 0x40 + (sa >> 3); *a++ = 0x48;
		*a++ = 0x01; *a++ = 0x00; *a++ = 0x3f; *a++ = 0x40;     /*  addl  */
		break;
	case SPECIAL_SRLV:
		/*  rd = rt >> (rs&31)  (logical)     t0 = t1 >> (t0&31)  */
		*a++ = 0x22; *a++ = 0xf6; *a++ = 0x41; *a++ = 0x48;	/*  zapnot t1,0xf,t1 (use only lowest 32 bits)  */
		*a++ = 0x01; *a++ = 0xf0; *a++ = 0x23; *a++ = 0x44;     /*  and t0,31,t0  */
		*a++ = 0x81; *a++ = 0x06; *a++ = 0x41; *a++ = 0x48;	/*  srl t1,t0,t0  */
		*a++ = 0x01; *a++ = 0x00; *a++ = 0x3f; *a++ = 0x40;     /*  addl t0,0,t0  */
		break;
	case SPECIAL_DSRL:
		/*  Note: bits of sa are distributed among two different bytes.  */
		*a++ = 0x81; *a++ = 0x16 + ((sa & 7) << 5); *a++ = 0x40 + (sa >> 3); *a++ = 0x48;
		break;
	case SPECIAL_DSRL32:
		/*  Note: bits of sa are distributed among two different bytes.  */
		sa += 32;
		*a++ = 0x81; *a++ = 0x16 + ((sa & 7) << 5); *a++ = 0x40 + (sa >> 3); *a++ = 0x48;
		break;
	case SPECIAL_SLT:
		*a++ = 0xa1; *a++ = 0x09; *a++ = 0x22; *a++ = 0x40;     /*  cmplt t0,t1,t0  */
		break;
	case SPECIAL_SLTU:
		*a++ = 0xa1; *a++ = 0x03; *a++ = 0x22; *a++ = 0x40;     /*  cmpult t0,t1,t0  */
		break;
	}

	*a++ = 0x1f; *a++ = 0x04; *a++ = 0xff; *a++ = 0x5f;	/*  fnop  */

	ofs = ((size_t)&dummy_cpu.gpr[rd]) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb4;	/*  stq t0,rd(a0)  */

	*addrp = a;
rd0:
	bintrans_write_pc_inc(addrp, sizeof(uint32_t), 1, 1);
	return 1;
}


/*
 *  bintrans_write_instruction__branch():
 */
static int bintrans_write_instruction__branch(unsigned char **addrp,
	int instruction_type, int regimm_type, int rt, int rs, int imm)
{
	unsigned char *a, *b, *b2;
	int n;
	int ofs;

	a = *addrp;

	/*
	 *  t0 = gpr[rt]; t1 = gpr[rs];
	 *
	 *  50 00 30 a4     ldq     t0,80(a0)
	 *  58 00 50 a4     ldq     t1,88(a0)
	 *
	 *  Compare t0 and t1 for equality (BEQ).
	 *  If the result was false (equal to zero), then skip a lot
	 *  of instructions:
	 *
	 *  a1 05 22 40     cmpeq   t0,t1,t0
	 *  01 00 20 e4     beq     t0,14 <f+0x14>
	 */
	b = NULL;
	if (instruction_type == HI6_BEQ && rt != rs) {
		ofs = ((size_t)&dummy_cpu.gpr[rt]) - (size_t)&dummy_cpu;
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xa4;
		ofs = ((size_t)&dummy_cpu.gpr[rs]) - (size_t)&dummy_cpu;
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x50; *a++ = 0xa4;

		*a++ = 0xa1; *a++ = 0x05; *a++ = 0x22; *a++ = 0x40;  /*  cmpeq  */
		b = a;
		*a++ = 0x01; *a++ = 0x00; *a++ = 0x20; *a++ = 0xe4;  /*  beq  */
		/*     ^^^^  --- NOTE: This is automagically updated later on.  */
	}
	if (instruction_type == HI6_BNE) {
		ofs = ((size_t)&dummy_cpu.gpr[rt]) - (size_t)&dummy_cpu;
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xa4;
		ofs = ((size_t)&dummy_cpu.gpr[rs]) - (size_t)&dummy_cpu;
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x50; *a++ = 0xa4;

		*a++ = 0xa1; *a++ = 0x05; *a++ = 0x22; *a++ = 0x40;  /*  cmpeq  */
		b = a;
		*a++ = 0x01; *a++ = 0x00; *a++ = 0x20; *a++ = 0xf4;  /*  bne  */
		/*     ^^^^  --- NOTE: This is automagically updated later on.  */
	}
	if (instruction_type == HI6_BLEZ) {
		ofs = ((size_t)&dummy_cpu.gpr[rs]) - (size_t)&dummy_cpu;
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x50; *a++ = 0xa4;

		*a++ = 0xa1; *a++ = 0x1d; *a++ = 0x40; *a++ = 0x40;  /*  cmple t1,0,t0  */
		b = a;
		*a++ = 0x01; *a++ = 0x00; *a++ = 0x20; *a++ = 0xe4;  /*  beq  */
		/*     ^^^^  --- NOTE: This is automagically updated later on.  */
	}
	if (instruction_type == HI6_BGTZ) {
		ofs = ((size_t)&dummy_cpu.gpr[rs]) - (size_t)&dummy_cpu;
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x50; *a++ = 0xa4;

		*a++ = 0xa1; *a++ = 0x1d; *a++ = 0x40; *a++ = 0x40;  /*  cmple t1,0,t0  */
		b = a;
		*a++ = 0x01; *a++ = 0x00; *a++ = 0x20; *a++ = 0xf4;  /*  bne  */
		/*     ^^^^  --- NOTE: This is automagically updated later on.  */
	}
	if (instruction_type == HI6_REGIMM && regimm_type == REGIMM_BLTZ) {
		ofs = ((size_t)&dummy_cpu.gpr[rs]) - (size_t)&dummy_cpu;
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x50; *a++ = 0xa4;

		*a++ = 0xa1; *a++ = 0x19; *a++ = 0x40; *a++ = 0x40;  /*  cmplt t1,0,t0  */
		b = a;
		*a++ = 0x01; *a++ = 0x00; *a++ = 0x20; *a++ = 0xe4;  /*  beq  */
		/*     ^^^^  --- NOTE: This is automagically updated later on.  */
	}
	if (instruction_type == HI6_REGIMM && regimm_type == REGIMM_BGEZ) {
		ofs = ((size_t)&dummy_cpu.gpr[rs]) - (size_t)&dummy_cpu;
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x50; *a++ = 0xa4;

		*a++ = 0xff; *a++ = 0xff; *a++ = 0x7f; *a++ = 0x20;  /*  lda t2,-1  */
		*a++ = 0xa1; *a++ = 0x0d; *a++ = 0x43; *a++ = 0x40;  /*  cmple t1,t2,t0  */
		b = a;
		*a++ = 0x01; *a++ = 0x00; *a++ = 0x20; *a++ = 0xf4;  /*  bne  */
		/*     ^^^^  --- NOTE: This is automagically updated later on.  */
	}

	/*
	 *  Perform the jump by setting cpu->delay_slot = TO_BE_DELAYED
	 *  and cpu->delay_jmpaddr = pc + 4 + (imm << 2).
	 *
	 *  01 14 c0 40     addq    t5,0,t0		t0 = pc
	 *  04 00 21 20     lda     t0,4(t0)		add 4
	 *  c8 01 5f 20     lda     t1,456
	 *  22 57 40 48     sll     t1,0x2,t1
	 *  01 04 22 40     addq    t0,t1,t0		add (imm<<2)
	 *  88 08 30 b4     stq     t0,2184(a0)		store pc
	 */

	*a++ = 0x01; *a++ = 0x14; *a++ = 0xc0; *a++ = 0x40;  /*  addq    t5,0,t0  */
	*a++ = 0x04; *a++ = 0x00; *a++ = 0x21; *a++ = 0x20;  /*  lda  */
	*a++ = (imm & 255); *a++ = (imm >> 8); *a++ = 0x5f; *a++ = 0x20;  /*  lda  */
	*a++ = 0x22; *a++ = 0x57; *a++ = 0x40; *a++ = 0x48;  /*  sll  */
	*a++ = 0x01; *a++ = 0x04; *a++ = 0x22; *a++ = 0x40;  /*  addq  */

	ofs = ((size_t)&dummy_cpu.delay_jmpaddr) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb4;

	ofs = ((size_t)&dummy_cpu.delay_slot) - (size_t)&dummy_cpu;
	*a++ = TO_BE_DELAYED; *a++ = 0x00; *a++ = 0x3f; *a++ = 0x20;  /*  lda t0,TO_BE_DELAYED */
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb0;	/*  stl  */

	b2 = a;
	n = (size_t)b2 - (size_t)b - 4;
	if (b != NULL)
		*b = n/4;	/*  nr of skipped instructions  */

	*addrp = a;
	bintrans_write_pc_inc(addrp, sizeof(uint32_t), 1, 1);
	return 1;
}


/*
 *  bintrans_write_instruction__jr():
 */
static int bintrans_write_instruction__jr(unsigned char **addrp, int rs, int rd, int special)
{
	unsigned char *a;
	int ofs;

	a = *addrp;

	/*
	 *  Perform the jump by setting cpu->delay_slot = TO_BE_DELAYED
	 *  and cpu->delay_jmpaddr = gpr[rs].
	 */

	ofs = ((size_t)&dummy_cpu.gpr[rs]) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xa4;
	ofs = ((size_t)&dummy_cpu.delay_jmpaddr) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb4;

	ofs = ((size_t)&dummy_cpu.delay_slot) - (size_t)&dummy_cpu;
	*a++ = TO_BE_DELAYED; *a++ = 0x00; *a++ = 0x3f; *a++ = 0x20;  /*  lda t0,TO_BE_DELAYED */
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb0;	/*  stl  */

	if (special == SPECIAL_JALR && rd != 0) {
		/*  gpr[rd] = retaddr    (pc + 8)  */

		*a++ = 0x01; *a++ = 0x14; *a++ = 0xc0; *a++ = 0x40;	/*  addq    t5,0,t0  */
		*a++ = 8; *a++ = 0; *a++ = 0x21; *a++ = 0x20;  /*  lda t0,8(t0)  */
		ofs = ((size_t)&dummy_cpu.gpr[rd]) - (size_t)&dummy_cpu;
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb4;
	}

	*addrp = a;
	bintrans_write_pc_inc(addrp, sizeof(uint32_t), 1, 1);
	return 1;
}


/*
 *  bintrans_write_instruction__jal():
 */
static int bintrans_write_instruction__jal(unsigned char **addrp,
	int imm, int link)
{
	unsigned char *a;
	int ofs;
	uint64_t subimm;

	a = *addrp;

	/*
	 *  gpr[31] = retaddr
	 *
	 *  04 14 c0 40     addq    t5,0,t3	t3 = pc
	 *  08 00 84 20     lda     t3,8(t3)
	 *  18 09 90 b4     stq     t3,gpr31(a0)
	 */
	*a++ = 0x04; *a++ = 0x14; *a++ = 0xc0; *a++ = 0x40;

	/*  NOTE: t3 is used further down again  */

	if (link) {
		*a++ = 8; *a++ = 0; *a++ = 0x84; *a++ = 0x20;	/*  lda t3,8(t3)  */

		ofs = ((size_t)&dummy_cpu.gpr[31]) - (size_t)&dummy_cpu;
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x90; *a++ = 0xb4;
	}

	/*  Set the jmpaddr to top 4 bits of pc + lowest 28 bits of imm*4:  */

	/*
	 *  imm = 4*imm;
	 *  t0 = pc + 4
	 *  shift right t0 28 steps.
	 *  shift left t0 14 steps.
	 *  OR in 14 bits of part of imm
	 *  shift left 14 steps.
	 *  OR in the lowest 14 bits of imm.
	 *  delay_jmpaddr = t0
	 *
	 *  01 14 c0 40     addq    t5,0,t0	t0 = pc
	 *  04 00 21 20     lda     t0,4(t0)	<-- because the jump is from the delay slot
	 *  81 96 23 48     srl     t0,0x1c,t0
	 *  21 d7 21 48     sll     t0,0xe,t0
	 *  c8 01 5f 20     lda     t1,456
	 *  01 04 22 44     or      t0,t1,t0
	 *  21 d7 21 48     sll     t0,0xe,t0
	 *  c8 01 5f 20     lda     t1,456
	 *  01 04 22 44     or      t0,t1,t0
	 *  18 09 30 b4     stq     t0,delay_jmpaddr(a0)
	 */

	imm *= 4;

	*a++ = 0x01; *a++ = 0x14; *a++ = 0xc0; *a++ = 0x40;

	*a++ = 4; *a++ = 0; *a++ = 0x21; *a++ = 0x20;	/*  lda t0,4(t0)  */

	*a++ = 0x81; *a++ = 0x96; *a++ = 0x23; *a++ = 0x48;
	*a++ = 0x21; *a++ = 0xd7; *a++ = 0x21; *a++ = 0x48;

	subimm = (imm >> 14) & 0x3fff;
	*a++ = (subimm & 255); *a++ = (subimm >> 8); *a++ = 0x5f; *a++ = 0x20;

	*a++ = 0x01; *a++ = 0x04; *a++ = 0x22; *a++ = 0x44;
	*a++ = 0x21; *a++ = 0xd7; *a++ = 0x21; *a++ = 0x48;

	subimm = imm & 0x3fff;
	*a++ = (subimm & 255); *a++ = (subimm >> 8); *a++ = 0x5f; *a++ = 0x20;

	*a++ = 0x01; *a++ = 0x04; *a++ = 0x22; *a++ = 0x44;

	ofs = ((size_t)&dummy_cpu.delay_jmpaddr) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb4;

	ofs = ((size_t)&dummy_cpu.delay_slot) - (size_t)&dummy_cpu;
	*a++ = TO_BE_DELAYED; *a++ = 0x00; *a++ = 0x3f; *a++ = 0x20;  /*  lda t0,TO_BE_DELAYED */
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb0;	/*  stl  */

	/*  If the machine continues executing here, it will return
	    to the main loop, which is fine.  */

	*addrp = a;
	bintrans_write_pc_inc(addrp, sizeof(uint32_t), 1, 1);
	return 1;
}


/*
 *  bintrans_write_instruction__delayedbranch():
 */
static int bintrans_write_instruction__delayedbranch(unsigned char **addrp,
	uint32_t *potential_chunk_p, uint32_t *chunks, int only_care_about_chunk_p)
{
	unsigned char *a, *skip=NULL;
	int ofs;
	uint64_t alpha_addr, subaddr;

	a = *addrp;

	if (!only_care_about_chunk_p) {
		/*  Skip all of this if there is no branch:  */
		ofs = ((size_t)&dummy_cpu.delay_slot) - (size_t)&dummy_cpu;
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x70; *a++ = 0xa0;	/*  ldl t2,delay_slot(a0)  */
		skip = a;
		*a++ = 0; *a++ = 0; *a++ = 0x60; *a++ = 0xe4;  /*  beq t2,skip  */

		/*
		 *  Perform the jump by setting cpu->delay_slot = 0
		 *  and pc = cpu->delay_jmpaddr.
		 */
		ofs = ((size_t)&dummy_cpu.delay_slot) - (size_t)&dummy_cpu;
		*a++ = 0; *a++ = 0x00; *a++ = 0x3f; *a++ = 0x20;  /*  lda t0,0 */
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb0;	/*  stl  */

		ofs = ((size_t)&dummy_cpu.delay_jmpaddr) - (size_t)&dummy_cpu;
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xa4;

		*a++ = 0x04; *a++ = 0x14; *a++ = 0xc0; *a++ = 0x40;	/*  addq t5,0,t3  */
		*a++ = 0x06; *a++ = 0x14; *a++ = 0x20; *a++ = 0x40;	/*  addq t0,0,t5  */
	}

	if (potential_chunk_p == NULL) {
		/*  Not much we can do here if this wasn't to the same
		    physical page...  */

		*a++ = 0xfc; *a++ = 0xff; *a++ = 0x84; *a++ = 0x20;	/*  lda t3,-4(t3)  */

		/*
		 *  Compare the old pc (t3) and the new pc (t0). If they are on the
		 *  same virtual page (which means that they are on the same physical
		 *  page), then we can check the right chunk pointer, and if it
		 *  is non-NULL, then we can jump there.  Otherwise just return.
		 *
		 *  00 f0 5f 20     lda     t1,-4096
		 *  01 00 22 44     and     t0,t1,t0
		 *  04 00 82 44     and     t3,t1,t3
		 *  a3 05 24 40     cmpeq   t0,t3,t2
		 *  01 00 60 f4     bne     t2,7c <ok2>
		 *  01 80 fa 6b     ret
		 */
		*a++ = 0x00; *a++ = 0xf0; *a++ = 0x5f; *a++ = 0x20;	/*  lda  */
		*a++ = 0x01; *a++ = 0x00; *a++ = 0x22; *a++ = 0x44;	/*  and  */
		*a++ = 0x04; *a++ = 0x00; *a++ = 0x82; *a++ = 0x44;	/*  and  */
		*a++ = 0xa3; *a++ = 0x05; *a++ = 0x24; *a++ = 0x40;	/*  cmpeq  */
		*a++ = 0x01; *a++ = 0x00; *a++ = 0x60; *a++ = 0xf4;	/*  bne  */
		*a++ = 0x01; *a++ = 0x80; *a++ = 0xfa; *a++ = 0x6b;	/*  ret  */

		/*  Don't execute too many instructions.  */
		*a++ = 0xc0; *a++ = 0x0f; *a++ = 0x5f; *a++ = 0x20;	/*  lda  */
		*a++ = 0x01; *a++ = 0x10; *a++ = 0xe0; *a++ = 0x40;	/*  addl t6,0,t0  */
		*a++ = 0xa1; *a++ = 0x0d; *a++ = 0x22; *a++ = 0x40;	/*  cmple  */
		*a++ = 0x01; *a++ = 0x00; *a++ = 0x20; *a++ = 0xf4;	/*  bne  */
		*a++ = 0x01; *a++ = 0x80; *a++ = 0xfa; *a++ = 0x6b;	/*  ret  */

		/*  15 bits at a time, which means max 60 bits, but
		    that should be enough. the top 4 bits are probably
		    not used by userland alpha code. (TODO: verify this)  */
		alpha_addr = (size_t)chunks;
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
		 *  t2 = pc
		 *  t1 = t2 & 0xfff
		 *  t0 += t1
		 *
		 *  ff 0f 5f 20     lda     t1,4095
		 *  02 00 62 44     and     t2,t1,t1
		 *  01 04 22 40     addq    t0,t1,t0
		 */
		*a++ = 0x03; *a++ = 0x14; *a++ = 0xc0; *a++ = 0x40;	/*  addq t5,0,t2  */
		*a++ = 0xff; *a++ = 0x0f; *a++ = 0x5f; *a++ = 0x20;	/*  lda  */
		*a++ = 0x02; *a++ = 0x00; *a++ = 0x62; *a++ = 0x44;	/*  and  */
		*a++ = 0x01; *a++ = 0x04; *a++ = 0x22; *a++ = 0x40;	/*  addq  */

		/*
		 *  Load the chunk pointer (actually, a 32-bit offset) into t0.
		 *  If it is zero, then skip the following.
		 *  Add cpu->chunk_base_address to t0.
		 *  Jump to t0.
		 */

		*a++ = 0x00; *a++ = 0x00; *a++ = 0x21; *a++ = 0xa0;	/*  ldl t0,0(t0)  */
		*a++ = 0x03; *a++ = 0x00; *a++ = 0x20; *a++ = 0xe4;	/*  beq t0,<skip>  */

		/*  ldl t2,chunk_base_address(a0)  */
		ofs = ((size_t)&dummy_cpu.chunk_base_address) - (size_t)&dummy_cpu;
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x70; *a++ = 0xa4;
		/*  addq t0,t2,t0  */
		*a++ = 0x01; *a++ = 0x04; *a++ = 0x23; *a++ = 0x40;

		/*  00 00 e1 6b     jmp     (t0)  */
		*a++ = 0x00; *a++ = 0x00; *a++ = 0xe1; *a++ = 0x6b;	/*  jmp (t0)  */

		/*  Failure, then return to the main loop.  */
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
		*a++ = 0xc0; *a++ = 0x0f; *a++ = 0x5f; *a++ = 0x20;	/*  lda  */
		*a++ = 0x01; *a++ = 0x10; *a++ = 0xe0; *a++ = 0x40;	/*  addl t6,0,t0  */
		*a++ = 0xa1; *a++ = 0x0d; *a++ = 0x22; *a++ = 0x40;	/*  cmple  */
		*a++ = 0x01; *a++ = 0x00; *a++ = 0x20; *a++ = 0xf4;	/*  bne  */
		*a++ = 0x01; *a++ = 0x80; *a++ = 0xfa; *a++ = 0x6b;	/*  ret  */

		/*
		 *  potential_chunk_p points to an "uint32_t".
		 *  If this value is non-NULL, then it is a piece of Alpha
		 *  machine language code corresponding to the address
		 *  we're jumping to. Otherwise, those instructions haven't
		 *  been translated yet, so we have to return to the main
		 *  loop.  (Actually, we have to add cpu->chunk_base_address,
		 *  because the uint32_t is limited to 32-bit offsets.)
		 *
		 *  Case 1:  The value is non-NULL already at translation
		 *           time. Then we can make a direct (fast) native
		 *           Alpha jump to the code chunk.
		 *
		 *  Case 2:  The value was NULL at translation time, then we
		 *           have to check during runtime.
		 */

		/*  Case 1:  */
		/*  printf("%08x ", *potential_chunk_p);  */
		alpha_addr = *potential_chunk_p + (size_t)translation_code_chunk_space;
		ofs = (alpha_addr - ((size_t)a+4)) / 4;
		/*  printf("%016llx %016llx %i\n", (long long)alpha_addr, (long long)a, ofs);  */

		if ((*potential_chunk_p) != 0 && ofs > -0xfffff && ofs < 0xfffff) {
			*a++ = ofs & 255; *a++ = (ofs >> 8) & 255; *a++ = 0xe0 + (ofs >> 16) & 0x1f; *a++ = 0xc3;	/*  br <chunk>  */
		} else {
			/*  Case 2:  */
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
			*a++ = 0x00; *a++ = 0x00; *a++ = 0x21; *a++ = 0xa0;	/*  ldl t0,0(t0)  */
			*a++ = 0x03; *a++ = 0x00; *a++ = 0x20; *a++ = 0xe4;	/*  beq t0,<skip>  */

			/*  ldl t2,chunk_base_address(a0)  */
			ofs = ((size_t)&dummy_cpu.chunk_base_address) - (size_t)&dummy_cpu;
			*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x70; *a++ = 0xa4;
			/*  addq t0,t2,t0  */
			*a++ = 0x01; *a++ = 0x04; *a++ = 0x23; *a++ = 0x40;

			/*  00 00 e1 6b     jmp     (t0)  */
			*a++ = 0x00; *a++ = 0x00; *a++ = 0xe1; *a++ = 0x6b;	/*  jmp (t0)  */

			/*  "Failure", then let's return to the main loop.  */
			*a++ = 0x01; *a++ = 0x80; *a++ = 0xfa; *a++ = 0x6b;	/*  ret  */
		}
	}

	if (skip != NULL) {
		*skip = ((size_t)a - (size_t)skip - 4) / 4;
		skip ++;
		*skip = (((size_t)a - (size_t)skip - 4) / 4) >> 8;
	}

	*addrp = a;
	return 1;
}


/*
 *  bintrans_write_instruction__loadstore():
 */
static int bintrans_write_instruction__loadstore(unsigned char **addrp,
	int rt, int imm, int rs, int instruction_type, int bigendian)
{
	unsigned char *a;
	int ofs, writeflag, alignment;

	switch (instruction_type) {
	case HI6_LD:
	case HI6_LWU:
	case HI6_LW:
	case HI6_LHU:
	case HI6_LH:
	case HI6_LBU:
	case HI6_LB:
		if (rt == 0)
			return 0;
	}

	a = *addrp;

	/*
	 *  a1 = gpr[rs] + imm;
	 *
	 *  88 08 30 a4     ldq     t0,2184(a0)
	 *  34 12 21 22     lda     a1,4660(t0)
	 */
	ofs = ((size_t)&dummy_cpu.gpr[rs]) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xa4;
	*a++ = (imm & 255); *a++ = (imm >> 8); *a++ = 0x21; *a++ = 0x22;

	writeflag = 0;
	switch (instruction_type) {
	case HI6_SD:
	case HI6_SW:
	case HI6_SH:
	case HI6_SB:
		writeflag = 1;
	}
	*a++ = writeflag; *a++ = 0x00; *a++ = 0x5f; *a++ = 0x22;	/*  lda a2,writeflag  */

	alignment = 0;
	switch (instruction_type) {
	case HI6_LD:
	case HI6_SD:
		alignment = 7;
		break;
	case HI6_LW:
	case HI6_LWU:
	case HI6_SW:
		alignment = 3;
		break;
	case HI6_LH:
	case HI6_LHU:
	case HI6_SH:
		alignment = 1;
		break;
	}
	*a++ = alignment; *a++ = 0x00; *a++ = 0x7f; *a++ = 0x22;	/*  lda a3,alignment  */

	/*  Save a0 and the old return address on the stack:  */
	*a++ = 0x80; *a++ = 0xff; *a++ = 0xde; *a++ = 0x23;	/*  lda sp,-128(sp)  */
	*a++ = 0x00; *a++ = 0x00; *a++ = 0x5e; *a++ = 0xb7;	/*  stq ra,0(sp)  */
	*a++ = 0x08; *a++ = 0x00; *a++ = 0x1e; *a++ = 0xb6;	/*  stq a0,8(sp)  */

	*a++ = 0x10; *a++ = 0x00; *a++ = 0xde; *a++ = 0xb4;	/*  stq t5,16(sp)  */
	*a++ = 0x18; *a++ = 0x00; *a++ = 0xfe; *a++ = 0xb0;	/*  stl t6,24(sp)  */

	ofs = ((size_t)&dummy_cpu.bintrans_fast_vaddr_to_hostaddr) - (size_t)&dummy_cpu;
	*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x70; *a++ = 0xa7;	/*  ldq t12,0(a0)  */

	/*  Call bintrans_fast_vaddr_to_hostaddr:  */
	*a++ = 0x00; *a++ = 0x40; *a++ = 0x5b; *a++ = 0x6b;	/*  jsr ra,(t12),<after>  */

	/*  Restore the old return address and a0 from the stack:  */
	*a++ = 0x00; *a++ = 0x00; *a++ = 0x5e; *a++ = 0xa7;	/*  ldq ra,0(sp)  */
	*a++ = 0x08; *a++ = 0x00; *a++ = 0x1e; *a++ = 0xa6;	/*  ldq a0,8(sp)  */

	*a++ = 0x10; *a++ = 0x00; *a++ = 0xde; *a++ = 0xa4;	/*  ldq t5,16(sp)  */
	*a++ = 0x18; *a++ = 0x00; *a++ = 0xfe; *a++ = 0xa0;	/*  ldl t6,24(sp)  */

	*a++ = 0x80; *a++ = 0x00; *a++ = 0xde; *a++ = 0x23;	/*  lda sp,128(sp)  */

	/*  If the result was NULL, then return (abort):  */
	*a++ = 0x01; *a++ = 0x00; *a++ = 0x00; *a++ = 0xf4;	/*  bne v0,<continue>  */
	*a++ = 0x01; *a++ = 0x80; *a++ = 0xfa; *a++ = 0x6b;	/*  ret  */

	/*  The rest of this code was written with t3 as the index, not v0:  */
	*a++ = 0x04; *a++ = 0x04; *a++ = 0x1f; *a++ = 0x40;	/*  addq v0,zero,t3  */

	ofs = ((size_t)&dummy_cpu.gpr[rt]) - (size_t)&dummy_cpu;

	switch (instruction_type) {
	case HI6_LD:
		*a++ = 0x00; *a++ = 0x00; *a++ = 0x24; *a++ = 0xa4;			/*  ldq t0,0(t3)  */
		if (bigendian) {
			/*  remember original 8 bytes of t0:  */
			*a++ = 0x05; *a++ = 0x04; *a++ = 0x3f; *a++ = 0x40;		/*  addq t0,zero,t4  */

			/*  swap lowest 4 bytes:  */
			*a++ = 0x62; *a++ = 0x71; *a++ = 0x20; *a++ = 0x48;		/*  insbl t0,3,t1  */
			*a++ = 0xc3; *a++ = 0x30; *a++ = 0x20; *a++ = 0x48;		/*  extbl t0,1,t2  */
			*a++ = 0x23; *a++ = 0x17; *a++ = 0x62; *a++ = 0x48;		/*  sll t2,16,t2  */
			*a++ = 0x02; *a++ = 0x04; *a++ = 0x62; *a++ = 0x44;		/*  or t2,t1,t1  */
			*a++ = 0xc3; *a++ = 0x50; *a++ = 0x20; *a++ = 0x48;		/*  extbl t0,2,t2  */
			*a++ = 0x23; *a++ = 0x17; *a++ = 0x61; *a++ = 0x48;		/*  sll t2,8,t2  */
			*a++ = 0x02; *a++ = 0x04; *a++ = 0x62; *a++ = 0x44;		/*  or t2,t1,t1  */
			*a++ = 0xc3; *a++ = 0x70; *a++ = 0x20; *a++ = 0x48;		/*  extbl t0,3,t2  */
			*a++ = 0x01; *a++ = 0x04; *a++ = 0x62; *a++ = 0x44;		/*  or t2,t1,t0  */

			/*  save result in (top 4 bytes of) t1, then t4. get back top bits of t4:  */
			*a++ = 0x22; *a++ = 0x17; *a++ = 0x24; *a++ = 0x48;		/*  sll t0,0x20,t1  */
			*a++ = 0x81; *a++ = 0x16; *a++ = 0xa4; *a++ = 0x48;		/*  srl t4,0x20,t0  */
			*a++ = 0x05; *a++ = 0x14; *a++ = 0x40; *a++ = 0x40;		/*  addq t1,0,t4  */

			/*  swap highest 4 bytes:  */
			*a++ = 0x62; *a++ = 0x71; *a++ = 0x20; *a++ = 0x48;		/*  insbl t0,3,t1  */
			*a++ = 0xc3; *a++ = 0x30; *a++ = 0x20; *a++ = 0x48;		/*  extbl t0,1,t2  */
			*a++ = 0x23; *a++ = 0x17; *a++ = 0x62; *a++ = 0x48;		/*  sll t2,16,t2  */
			*a++ = 0x02; *a++ = 0x04; *a++ = 0x62; *a++ = 0x44;		/*  or t2,t1,t1  */
			*a++ = 0xc3; *a++ = 0x50; *a++ = 0x20; *a++ = 0x48;		/*  extbl t0,2,t2  */
			*a++ = 0x23; *a++ = 0x17; *a++ = 0x61; *a++ = 0x48;		/*  sll t2,8,t2  */
			*a++ = 0x02; *a++ = 0x04; *a++ = 0x62; *a++ = 0x44;		/*  or t2,t1,t1  */
			*a++ = 0xc3; *a++ = 0x70; *a++ = 0x20; *a++ = 0x48;		/*  extbl t0,3,t2  */
			*a++ = 0x01; *a++ = 0x04; *a++ = 0x62; *a++ = 0x44;		/*  or t2,t1,t0  */

			/*  or the results together:  */
			*a++ = 0x01; *a++ = 0x04; *a++ = 0xa1; *a++ = 0x44;		/*  or t4,t0,t0  */
		}
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb4;	/*  stq gpr[rt]  */
		break;
	case HI6_LW:
	case HI6_LWU:
		*a++ = 0x00; *a++ = 0x00; *a++ = 0x24; *a++ = 0xa0;			/*  ldl from memory  */
		if (bigendian) {
			*a++ = 0x62; *a++ = 0x71; *a++ = 0x20; *a++ = 0x48;		/*  insbl t0,3,t1  */
			*a++ = 0xc3; *a++ = 0x30; *a++ = 0x20; *a++ = 0x48;		/*  extbl t0,1,t2  */
			*a++ = 0x23; *a++ = 0x17; *a++ = 0x62; *a++ = 0x48;		/*  sll t2,16,t2  */
			*a++ = 0x02; *a++ = 0x04; *a++ = 0x62; *a++ = 0x44;		/*  or t2,t1,t1  */
			*a++ = 0xc3; *a++ = 0x50; *a++ = 0x20; *a++ = 0x48;		/*  extbl t0,2,t2  */
			*a++ = 0x23; *a++ = 0x17; *a++ = 0x61; *a++ = 0x48;		/*  sll t2,8,t2  */
			*a++ = 0x02; *a++ = 0x04; *a++ = 0x62; *a++ = 0x44;		/*  or t2,t1,t1  */
			*a++ = 0xc3; *a++ = 0x70; *a++ = 0x20; *a++ = 0x48;		/*  extbl t0,3,t2  */
			*a++ = 0x01; *a++ = 0x04; *a++ = 0x62; *a++ = 0x44;		/*  or t2,t1,t0  */
			*a++ = 0x01; *a++ = 0x00; *a++ = 0x3f; *a++ = 0x40;		/*  addl t0,zero,t0 (sign extend) 32->64  */
		}
		if (instruction_type == HI6_LWU) {
			/*  Use only lowest 32 bits:  */
			*a++ = 0x21; *a++ = 0xf6; *a++ = 0x21; *a++ = 0x48;	/*  zapnot t0,0xf,t0  */
		}
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb4;	/*  stq gpr[rt]  */
		break;
	case HI6_LHU:
	case HI6_LH:
		*a++ = 0x00; *a++ = 0x00; *a++ = 0x24; *a++ = 0x30;			/*  ldwu from memory  */
		if (bigendian) {
			*a++ = 0x62; *a++ = 0x31; *a++ = 0x20; *a++ = 0x48;		/*  insbl t0,1,t1  */
			*a++ = 0xc3; *a++ = 0x30; *a++ = 0x20; *a++ = 0x48;		/*  extbl t0,1,t2  */
			*a++ = 0x01; *a++ = 0x04; *a++ = 0x43; *a++ = 0x44;		/*  or t1,t2,t0  */
		}
		if (instruction_type == HI6_LH) {
			*a++ = 0x21; *a++ = 0x00; *a++ = 0xe1; *a++ = 0x73;		/*  sextw   t0,t0  */
		}
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb4;	/*  stq gpr[rt]  */
		break;
	case HI6_LBU:
	case HI6_LB:
		*a++ = 0x00; *a++ = 0x00; *a++ = 0x24; *a++ = 0x28;			/*  ldbu from memory  */
		if (instruction_type == HI6_LB) {
			*a++ = 0x01; *a++ = 0x00; *a++ = 0xe1; *a++ = 0x73;		/*  sextb   t0,t0  */
		}
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb4;	/*  stq gpr[rt]  */
		break;
	case HI6_SD:
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xa4;	/*  ldq gpr[rt]  */
		if (bigendian) {
			/*  remember original 8 bytes of t0:  */
			*a++ = 0x05; *a++ = 0x04; *a++ = 0x3f; *a++ = 0x40;		/*  addq t0,zero,t4  */

			/*  swap lowest 4 bytes:  */
			*a++ = 0x62; *a++ = 0x71; *a++ = 0x20; *a++ = 0x48;		/*  insbl t0,3,t1  */
			*a++ = 0xc3; *a++ = 0x30; *a++ = 0x20; *a++ = 0x48;		/*  extbl t0,1,t2  */
			*a++ = 0x23; *a++ = 0x17; *a++ = 0x62; *a++ = 0x48;		/*  sll t2,16,t2  */
			*a++ = 0x02; *a++ = 0x04; *a++ = 0x62; *a++ = 0x44;		/*  or t2,t1,t1  */
			*a++ = 0xc3; *a++ = 0x50; *a++ = 0x20; *a++ = 0x48;		/*  extbl t0,2,t2  */
			*a++ = 0x23; *a++ = 0x17; *a++ = 0x61; *a++ = 0x48;		/*  sll t2,8,t2  */
			*a++ = 0x02; *a++ = 0x04; *a++ = 0x62; *a++ = 0x44;		/*  or t2,t1,t1  */
			*a++ = 0xc3; *a++ = 0x70; *a++ = 0x20; *a++ = 0x48;		/*  extbl t0,3,t2  */
			*a++ = 0x01; *a++ = 0x04; *a++ = 0x62; *a++ = 0x44;		/*  or t2,t1,t0  */

			/*  save result in (top 4 bytes of) t1, then t4. get back top bits of t4:  */
			*a++ = 0x22; *a++ = 0x17; *a++ = 0x24; *a++ = 0x48;		/*  sll t0,0x20,t1  */
			*a++ = 0x81; *a++ = 0x16; *a++ = 0xa4; *a++ = 0x48;		/*  srl t4,0x20,t0  */
			*a++ = 0x05; *a++ = 0x14; *a++ = 0x40; *a++ = 0x40;		/*  addq t1,0,t4  */

			/*  swap highest 4 bytes:  */
			*a++ = 0x62; *a++ = 0x71; *a++ = 0x20; *a++ = 0x48;		/*  insbl t0,3,t1  */
			*a++ = 0xc3; *a++ = 0x30; *a++ = 0x20; *a++ = 0x48;		/*  extbl t0,1,t2  */
			*a++ = 0x23; *a++ = 0x17; *a++ = 0x62; *a++ = 0x48;		/*  sll t2,16,t2  */
			*a++ = 0x02; *a++ = 0x04; *a++ = 0x62; *a++ = 0x44;		/*  or t2,t1,t1  */
			*a++ = 0xc3; *a++ = 0x50; *a++ = 0x20; *a++ = 0x48;		/*  extbl t0,2,t2  */
			*a++ = 0x23; *a++ = 0x17; *a++ = 0x61; *a++ = 0x48;		/*  sll t2,8,t2  */
			*a++ = 0x02; *a++ = 0x04; *a++ = 0x62; *a++ = 0x44;		/*  or t2,t1,t1  */
			*a++ = 0xc3; *a++ = 0x70; *a++ = 0x20; *a++ = 0x48;		/*  extbl t0,3,t2  */
			*a++ = 0x01; *a++ = 0x04; *a++ = 0x62; *a++ = 0x44;		/*  or t2,t1,t0  */

			/*  or the results together:  */
			*a++ = 0x01; *a++ = 0x04; *a++ = 0xa1; *a++ = 0x44;		/*  or t4,t0,t0  */
		}
		*a++ = 0x00; *a++ = 0x00; *a++ = 0x24; *a++ = 0xb4;			/*  stq to memory  */
		break;
	case HI6_SW:
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xa0;	/*  ldl gpr[rt]  */
		if (bigendian) {
			*a++ = 0x62; *a++ = 0x71; *a++ = 0x20; *a++ = 0x48;		/*  insbl t0,3,t1  */
			*a++ = 0xc3; *a++ = 0x30; *a++ = 0x20; *a++ = 0x48;		/*  extbl t0,1,t2  */
			*a++ = 0x23; *a++ = 0x17; *a++ = 0x62; *a++ = 0x48;		/*  sll t2,16,t2  */
			*a++ = 0x02; *a++ = 0x04; *a++ = 0x62; *a++ = 0x44;		/*  or t2,t1,t1  */
			*a++ = 0xc3; *a++ = 0x50; *a++ = 0x20; *a++ = 0x48;		/*  extbl t0,2,t2  */
			*a++ = 0x23; *a++ = 0x17; *a++ = 0x61; *a++ = 0x48;		/*  sll t2,8,t2  */
			*a++ = 0x02; *a++ = 0x04; *a++ = 0x62; *a++ = 0x44;		/*  or t2,t1,t1  */
			*a++ = 0xc3; *a++ = 0x70; *a++ = 0x20; *a++ = 0x48;		/*  extbl t0,3,t2  */
			*a++ = 0x01; *a++ = 0x04; *a++ = 0x62; *a++ = 0x44;		/*  or t2,t1,t0  */
		}
		*a++ = 0x00; *a++ = 0x00; *a++ = 0x24; *a++ = 0xb0;			/*  stl to memory  */
		break;
	case HI6_SH:
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0x30;	/*  ldwu t0,gpr[rt](a0)  */
		if (bigendian) {
			*a++ = 0x62; *a++ = 0x31; *a++ = 0x20; *a++ = 0x48;		/*  insbl t0,1,t1  */
			*a++ = 0xc3; *a++ = 0x30; *a++ = 0x20; *a++ = 0x48;		/*  extbl t0,1,t2  */
			*a++ = 0x01; *a++ = 0x04; *a++ = 0x43; *a++ = 0x44;		/*  or t1,t2,t0  */
		}
		*a++ = 0x00; *a++ = 0x00; *a++ = 0x24; *a++ = 0x34;			/*  stw to memory  */
		break;
	case HI6_SB:
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xa0;	/*  ldl gpr[rt]  */
		*a++ = 0x00; *a++ = 0x00; *a++ = 0x24; *a++ = 0x38;			/*  stb to memory  */
		break;
	default:
		;
	}

	*addrp = a;
	bintrans_write_pc_inc(addrp, sizeof(uint32_t), 1, 1);
	return 1;
}


/*
 *  bintrans_write_instruction__lui():
 */
static int bintrans_write_instruction__lui(unsigned char **addrp,
	int rt, int imm)
{
	uint32_t *a;
	int ofs;

	/*
	 *  dc fe 3f 24     ldah    t0,-292
	 *  1f 04 ff 5f     fnop
	 *  88 08 30 b4     stq     t0,2184(a0)
	 */
	if (rt != 0) {
		a = (uint32_t *) *addrp;
		ofs = ((size_t)&dummy_cpu.gpr[rt]) - (size_t)&dummy_cpu;
		*a++ = 0x243f0000 | ((uint32_t)imm & 0xffff);
		*a++ = 0x5fff041f;
		*a++ = 0xb4300000 | (ofs & 0xffff);
		*addrp = (unsigned char *) a;
	}

	bintrans_write_pc_inc(addrp, sizeof(uint32_t), 1, 1);

	return 1;
}


/*
 *  bintrans_write_instruction__mfmthilo():
 */
static int bintrans_write_instruction__mfmthilo(unsigned char **addrp,
	int rd, int from_flag, int hi_flag)
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
	bintrans_write_pc_inc(addrp, sizeof(uint32_t), 1, 1);
	return 1;
}


/*
 *  bintrans_write_instruction__rfe():
 */
static int bintrans_write_instruction__rfe(unsigned char **addrp)
{
	uint32_t *a;
	int ofs;

	/*
	 *  cpu->coproc[0]->reg[COP0_STATUS] =
	 *	(cpu->coproc[0]->reg[COP0_STATUS] & ~0x3f) |
	 *      ((cpu->coproc[0]->reg[COP0_STATUS] & 0x3c) >> 2);
	 */

	/*
	 *  ldq t0, coproc[0](a0)
	 *  ldq t1, reg[COP0_STATUS](t0)
	 *  lda t2, 0x...ffc0
	 *  lda t3, 0x3f
	 *  and t1,t2,t2
	 *  and t1,t3,t3
	 *  srl t3,2,t3
	 *  or t2,t3,t3
	 *  stq t3, reg[COP0_STATUS](t0)
	 */

	a = (uint32_t *) *addrp;

	ofs = ((size_t)&dummy_cpu.coproc[0]) - (size_t)&dummy_cpu;
	*a++ = 0xa4300000 | (ofs & 0xffff);		/*  ldq t0,coproc[0](a0)  */

	ofs = ((size_t)&dummy_coproc.reg[COP0_STATUS]) - (size_t)&dummy_coproc;
	*a++ = 0xa4410000 | (ofs & 0xffff);		/*  ldq t1,status(t0)  */

	*a++ = 0x207fffc0;		/*  lda t2,0x...ffc0  */
	*a++ = 0x209f003f;		/*  lda t3,0x3f  */
	*a++ = 0x44430003;		/*  and t1,t2,t2  */
	*a++ = 0x44440004;		/*  and t1,t3,t3  */
	*a++ = 0x48805684;		/*  srl t3,2,t3  */
	*a++ = 0x44640402;		/*  or t2,t3,t1  */

	*a++ = 0xb4410000 | (ofs & 0xffff);		/*  stq t1,status(t0)  */

	*addrp = (unsigned char *) a;

	bintrans_write_pc_inc(addrp, sizeof(uint32_t), 1, 1);
	return 1;
}


/*
 *  bintrans_write_instruction__mfc():
 */
static int bintrans_write_instruction__mfc(unsigned char **addrp, int coproc_nr, int flag64bit, int rt, int rd)
{
	uint32_t *a;
	int ofs;

	/*
	 *  NOTE: Only a few registers are readable without side effects.
	 */
	if (rt == 0)
		return 0;

	if (coproc_nr >= 1)
		return 0;

	if (rd == COP0_RANDOM || rd == COP0_COUNT)
		return 0;


	/*************************************************************
	 *
	 *  TODO: Check for kernel mode, or Coproc X usability bit!
	 *
	 *************************************************************/

	a = (uint32_t *) *addrp;

	ofs = ((size_t)&dummy_cpu.coproc[0]) - (size_t)&dummy_cpu;
	*a++ = 0xa4300000 | (ofs & 0xffff);		/*  ldq t0,coproc[0](a0)  */

	ofs = ((size_t)&dummy_coproc.reg[rd]) - (size_t)&dummy_coproc;
	*a++ = 0xa4410000 | (ofs & 0xffff);		/*  ldq t1,reg_rd(t0)  */

	if (!flag64bit) {
		*a++ = 0x40401002;		/*  addl t1,0,t1  */
	}

	ofs = ((size_t)&dummy_cpu.gpr[rt]) - (size_t)&dummy_cpu;
	*a++ = 0xb4500000 | (ofs & 0xffff);		/*  stq t1,rt(a0)  */

	*addrp = (unsigned char *) a;

	bintrans_write_pc_inc(addrp, sizeof(uint32_t), 1, 1);
	return 1;
}


