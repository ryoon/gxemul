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
 *  $Id: bintrans_alpha.c,v 1.65 2004-11-26 20:03:08 debug Exp $
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
 *	t5		pc (64-bit)
 *	t6		bintrans_instructions_executed (32-bit int)
 *	t7		a0 (mips register 4)  (64-bit)
 *	t8		a1 (mips register 5)  (64-bit)
 *	t9		s0 (mips register 16)  (64-bit)
 *	t10		t3 (mips register 11)  (64-bit)  (TODO: bug?)
 *	t11		t4 (mips register 12)  (64-bit)
 *	s0		delay_slot (32-bit int)
 *	s1		delay_jmpaddr (64-bit)
 *	s2		sp (mips register 29)  (64-bit)
 *	s3		ra (mips register 31)  (64-bit)
 *	s4		t0 (mips register 8)  (64-bit)
 *	s5		t1 (mips register 9)  (64-bit)
 *	s6		t2 (mips register 10)  (64-bit)
 *
 *	  a1..a5	  TODO
 */

#define	MIPSREG_PC			-6
#define	MIPSREG_N_INSTRS		-5
#define	MIPSREG_DELAY_SLOT		-4
#define	MIPSREG_DELAY_JMPADDR		-3
#define	MIPSREG_VIRTUAL_PAGE		-2
#define	MIPSREG_HOST_PAGE		-1

/*  Temporaries:  */
#define	ALPHA_T0		1
#define	ALPHA_T1		2
#define	ALPHA_T2		3
#define	ALPHA_T3		4
#define	ALPHA_T4		5
/*  Cached values:  */
#define	ALPHA_T5		6
#define	ALPHA_T6		7
#define	ALPHA_T7		8
#define	ALPHA_S0		9
#define	ALPHA_S1		10
#define	ALPHA_T8		22
#define	ALPHA_T9		23

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
#define ofs_ds	(((size_t)&dummy_cpu.delay_slot) - ((size_t)&dummy_cpu))
#define ofs_ja	(((size_t)&dummy_cpu.delay_jmpaddr) - ((size_t)&dummy_cpu))
#define ofs_sp	(((size_t)&dummy_cpu.gpr[GPR_SP]) - ((size_t)&dummy_cpu))
#define ofs_ra	(((size_t)&dummy_cpu.gpr[GPR_RA]) - ((size_t)&dummy_cpu))
#define ofs_a0	(((size_t)&dummy_cpu.gpr[GPR_A0]) - ((size_t)&dummy_cpu))
#define ofs_a1	(((size_t)&dummy_cpu.gpr[GPR_A1]) - ((size_t)&dummy_cpu))
#define ofs_t0	(((size_t)&dummy_cpu.gpr[GPR_T0]) - ((size_t)&dummy_cpu))
#define ofs_t1	(((size_t)&dummy_cpu.gpr[GPR_T1]) - ((size_t)&dummy_cpu))
#define ofs_t2	(((size_t)&dummy_cpu.gpr[GPR_T2]) - ((size_t)&dummy_cpu))
#define ofs_t3	(((size_t)&dummy_cpu.gpr[GPR_T3]) - ((size_t)&dummy_cpu))
#define ofs_t4	(((size_t)&dummy_cpu.gpr[GPR_T4]) - ((size_t)&dummy_cpu))
#define ofs_s0	(((size_t)&dummy_cpu.gpr[GPR_S0]) - ((size_t)&dummy_cpu))
unsigned char bintrans_alpha_runchunk[200] = {
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
	ofs_a0&255,ofs_a0>>8,0x10,0xa5,	/*  ldq     t7,"a0"(a0)  */
	ofs_a1&255,ofs_a1>>8,0xd0,0xa6,	/*  ldq     t8,"a1"(a0)  */
	ofs_s0&255,ofs_s0>>8,0xf0,0xa6,	/*  ldq     t9,"s0"(a0)  */
	ofs_ds&255,ofs_ds>>8,0x30,0xa1,	/*  ldl     s0,"delay_slot"(a0)  */
	ofs_ja&255,ofs_ja>>8,0x50,0xa5,	/*  ldq     s1,"delay_jmpaddr"(a0)  */
	ofs_sp&255,ofs_sp>>8,0x70,0xa5,	/*  ldq     s2,"gpr[sp]"(a0)  */
	ofs_ra&255,ofs_ra>>8,0x90,0xa5,	/*  ldq     s3,"gpr[ra]"(a0)  */
	ofs_t0&255,ofs_t0>>8,0xb0,0xa5,	/*  ldq     s4,"gpr[t0]"(a0)  */
	ofs_t1&255,ofs_t1>>8,0xd0,0xa5,	/*  ldq     s5,"gpr[t1]"(a0)  */
	ofs_t2&255,ofs_t2>>8,0xf0,0xa5,	/*  ldq     s6,"gpr[t2]"(a0)  */
#if 0
	ofs_t3&255,ofs_t3>>8,0x10,0xa7,	/*  ldq     t10,"gpr[t3]"(a0)  */
#endif
	ofs_t4&255,ofs_t4>>8,0x30,0xa7,	/*  ldq     t11,"gpr[t4]"(a0)  */

	0x00, 0x40, 0x51, 0x6b,		/*  jsr     ra,(a1),<back>  */

	ofs_pc&255,ofs_pc>>8,0xd0,0xb4,	/*  stq     t5,"pc"(a0)  */
	ofs_n&255,ofs_n>>8,0xf0,0xb0,	/*  stl     t6,"bintrans_instructions_executed"(a0)  */
	ofs_a0&255,ofs_a0>>8,0x10,0xb5,	/*  stq     t7,"a0"(a0)  */
	ofs_a1&255,ofs_a1>>8,0xd0,0xb6,	/*  stq     t8,"a1"(a0)  */
	ofs_s0&255,ofs_s0>>8,0xf0,0xb6,	/*  stq     t9,"s0"(a0)  */
	ofs_ds&255,ofs_ds>>8,0x30,0xb1,	/*  stl     s0,"delay_slot"(a0)  */
	ofs_ja&255,ofs_ja>>8,0x50,0xb5,	/*  stq     s1,"delay_jmpaddr"(a0)  */
	ofs_sp&255,ofs_sp>>8,0x70,0xb5,	/*  stq     s2,"gpr[sp]"(a0)  */
	ofs_ra&255,ofs_ra>>8,0x90,0xb5,	/*  stq     s3,"gpr[ra]"(a0)  */
	ofs_t0&255,ofs_t0>>8,0xb0,0xb5,	/*  stq     s4,"gpr[t0]"(a0)  */
	ofs_t1&255,ofs_t1>>8,0xd0,0xb5,	/*  stq     s5,"gpr[t1]"(a0)  */
	ofs_t2&255,ofs_t2>>8,0xf0,0xb5,	/*  stq     s6,"gpr[t2]"(a0)  */
#if 0
	ofs_t3&255,ofs_t3>>8,0x10,0xb7,	/*  stq     t10,"gpr[t3]"(a0)  */
#endif
	ofs_t4&255,ofs_t4>>8,0x30,0xb7,	/*  stq     t11,"gpr[t4]"(a0)  */

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
 *  bintrans_runchunk():
 */
static void bintrans_runchunk(struct cpu *cpu, unsigned char *code)
{
	void (*f)(struct cpu *, unsigned char *);
	f = (void *)&bintrans_alpha_runchunk[0];
	f(cpu, code);
}


/*
 *  bintrans_write_quickjump():
 */
static void bintrans_write_quickjump(unsigned char *quickjump_code,
	uint32_t chunkoffset)
{
	int ofs;
	uint64_t alpha_addr = chunkoffset +
	    (size_t)translation_code_chunk_space;
	unsigned char *a = quickjump_code;

	ofs = (alpha_addr - ((size_t)a+4)) / 4;

	/*  printf("chunkoffset=%i, %016llx %016llx %i\n",
	    chunkoffset, (long long)alpha_addr, (long long)a, ofs);  */

	if (ofs > -0xfffff && ofs < 0xfffff) {
		*a++ = ofs & 255; *a++ = (ofs >> 8) & 255; *a++ = 0xe0 + ((ofs >> 16) & 0x1f); *a++ = 0xc3;	/*  br <chunk>  */
	}
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
 *  bintrans_write_chunkreturn_fail():
 */
static void bintrans_write_chunkreturn_fail(unsigned char **addrp)
{
	uint32_t *a = (uint32_t *) *addrp;
	/*  00 01 3f 24     ldah    t0,256  */
	/*  07 04 27 44     or      t0,t6,t6  */
	*a++ = 0x243f0000 | (BINTRANS_DONT_RUN_NEXT >> 16);
	*a++ = 0x44270407;
	*a++ = 0x6bfa8001;	/*  ret  */
	*addrp = (unsigned char *) a;
}


/*
 *  bintrans_move_MIPS_reg_into_Alpha_reg():
 */
static void bintrans_move_MIPS_reg_into_Alpha_reg(unsigned char **addrp, int mipsreg, int alphareg)
{
	uint32_t *a = (uint32_t *) *addrp;
	int ofs;

	switch (mipsreg) {
	case MIPSREG_PC:
		/*  addq t5,0,alphareg  */
		*a++ = 0x40c01400 | alphareg;
		break;
	case MIPSREG_N_INSTRS:
		/*  addq t6,0,alphareg  */
		*a++ = 0x40e01400 | alphareg;
		break;
	case MIPSREG_DELAY_SLOT:
		/*  addq s0,0,alphareg  */
		*a++ = 0x41201400 | alphareg;
		break;
	case MIPSREG_DELAY_JMPADDR:
		/*  addq s1,0,alphareg  */
		*a++ = 0x41401400 | alphareg;
		break;
	case 0:
		/*  clr alphareg  */
		*a++ = 0x47ff0400 | alphareg;
		break;
	case GPR_A0:
		/*  addq t7,0,alphareg  */
		*a++ = 0x41001400 | alphareg;
		break;
	case GPR_A1:
		/*  addq t8,0,alphareg  */
		*a++ = 0x42c01400 | alphareg;
		break;
	case GPR_T0:
		/*  addq s4,0,alphareg  */
		*a++ = 0x41a01400 | alphareg;
		break;
	case GPR_T1:
		/*  addq s5,0,alphareg  */
		*a++ = 0x41c01400 | alphareg;
		break;
	case GPR_T2:
		/*  addq s6,0,alphareg  */
		*a++ = 0x41e01400 | alphareg;
		break;
#if 0
	case GPR_T3:
		/*  addq t10,0,alphareg  */
		*a++ = 0x43001400 | alphareg;
		break;
#endif
	case GPR_T4:
		/*  addq t11,0,alphareg  */
		*a++ = 0x43201400 | alphareg;
		break;
	case GPR_S0:
		/*  addq t9,0,alphareg  */
		*a++ = 0x42e01400 | alphareg;
		break;
	case GPR_SP:
		/*  addq s2,0,alphareg  */
		*a++ = 0x41601400 | alphareg;
		break;
	case GPR_RA:
		/*  addq s3,0,alphareg  */
		*a++ = 0x41801400 | alphareg;
		break;

	default:
		/*  ldq alphareg,gpr[mipsreg](a0)  */
		ofs = ((size_t)&dummy_cpu.gpr[mipsreg]) - (size_t)&dummy_cpu;
		*a++ = 0xa4100000 | (alphareg << 21) | ofs;
	}
	*addrp = (unsigned char *) a;
}


/*
 *  bintrans_move_Alpha_reg_into_MIPS_reg():
 */
static void bintrans_move_Alpha_reg_into_MIPS_reg(unsigned char **addrp, int alphareg, int mipsreg)
{
	uint32_t *a = (uint32_t *) *addrp;
	int ofs;

	switch (mipsreg) {
	case MIPSREG_PC:
		/*  addq alphareg,0,t5  */
		*a++ = 0x40001406 | (alphareg << 21);
		break;
	case MIPSREG_N_INSTRS:
		/*  addq alphareg,0,t6  */
		*a++ = 0x40001407 | (alphareg << 21);
		break;
	case MIPSREG_DELAY_SLOT:
		/*  addq alphareg,0,s0  */
		*a++ = 0x40001409 | (alphareg << 21);
		break;
	case MIPSREG_DELAY_JMPADDR:
		/*  addq alphareg,0,s1  */
		*a++ = 0x4000140a | (alphareg << 21);
		break;
	case 0:		/*  the zero register  */
		break;
	case GPR_A0:
		/*  addq alphareg,0,t7  */
		*a++ = 0x40001408 | (alphareg << 21);
		break;
	case GPR_A1:
		/*  addq alphareg,0,t8  */
		*a++ = 0x40001416 | (alphareg << 21);
		break;
	case GPR_T0:
		/*  addq alphareg,0,s4  */
		*a++ = 0x4000140d | (alphareg << 21);
		break;
	case GPR_T1:
		/*  addq alphareg,0,s5  */
		*a++ = 0x4000140e | (alphareg << 21);
		break;
	case GPR_T2:
		/*  addq alphareg,0,s6  */
		*a++ = 0x4000140f | (alphareg << 21);
		break;
#if 0
	case GPR_T3:
		/*  addq alphareg,0,t10  */
		*a++ = 0x40001418 | (alphareg << 21);
		break;
#endif
	case GPR_T4:
		/*  addq alphareg,0,t11  */
		*a++ = 0x40001419 | (alphareg << 21);
		break;
	case GPR_S0:
		/*  addq alphareg,0,t9  */
		*a++ = 0x40001417 | (alphareg << 21);
		break;
	case GPR_SP:
		/*  addq alphareg,0,s2  */
		*a++ = 0x4000140b | (alphareg << 21);
		break;
	case GPR_RA:
		/*  addq alphareg,0,s3  */
		*a++ = 0x4000140c | (alphareg << 21);
		break;
	default:
		/*  stq alphareg,gpr[mipsreg](a0)  */
		ofs = ((size_t)&dummy_cpu.gpr[mipsreg]) - (size_t)&dummy_cpu;
		*a++ = 0xb4100000 | (alphareg << 21) | ofs;
	}
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

	if (uimm == 0 && (instruction_type == HI6_ADDIU ||
	    instruction_type == HI6_DADDIU || instruction_type == HI6_ORI)) {
		bintrans_move_MIPS_reg_into_Alpha_reg(&a, rs, ALPHA_T0);
		bintrans_move_Alpha_reg_into_MIPS_reg(&a, ALPHA_T0, rt);
		goto rt0;
	}

	if (load64) {
		/*  ldq t0,rs(a0)  */
		bintrans_move_MIPS_reg_into_Alpha_reg(&a, rs, ALPHA_T0);
	} else {
		/*  ldl t0,rs(a0)  */
		bintrans_move_MIPS_reg_into_Alpha_reg(&a, rs, ALPHA_T0);
		*a++ = 0x01; *a++ = 0x00; *a++ = 0x3f; *a++ = 0x40;	/*  addl t0,0,t0  */
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
	bintrans_move_Alpha_reg_into_MIPS_reg(&a, ALPHA_T0, rt);

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
	unsigned char *a, *unmodified = NULL;
	int load64 = 0, store = 1, ofs;

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
	case SPECIAL_MOVZ:
	case SPECIAL_MOVN:
		load64 = 1;
	}

	switch (instruction_type) {
	case SPECIAL_MULT:
	case SPECIAL_MULTU:
		if (rd != 0)
			return 0;
		store = 0;
		break;
	default:
		if (rd == 0)
			goto rd0;
	}

	a = *addrp;

	if ((instruction_type == SPECIAL_ADDU || instruction_type == SPECIAL_DADDU
	    || instruction_type == SPECIAL_OR) && rt == 0) {
		bintrans_move_MIPS_reg_into_Alpha_reg(&a, rs, ALPHA_T0);
		bintrans_move_Alpha_reg_into_MIPS_reg(&a, ALPHA_T0, rd);
		*addrp = a;
		goto rd0;
	}

	/*  t0 = rs, t1 = rt  */
	if (load64) {
		bintrans_move_MIPS_reg_into_Alpha_reg(&a, rs, ALPHA_T0);
		bintrans_move_MIPS_reg_into_Alpha_reg(&a, rt, ALPHA_T1);
	} else {
		bintrans_move_MIPS_reg_into_Alpha_reg(&a, rs, ALPHA_T0);
		*a++ = 0x01; *a++ = 0x00; *a++ = 0x3f; *a++ = 0x40;	/*  addl t0,0,t0  */
		bintrans_move_MIPS_reg_into_Alpha_reg(&a, rt, ALPHA_T1);
		*a++ = 0x02; *a++ = 0x10; *a++ = 0x40; *a++ = 0x40;	/*  addl t1,0,t1  */
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
	case SPECIAL_MULT:
	case SPECIAL_MULTU:
		if (instruction_type == SPECIAL_MULTU) {
			/*  21 f6 21 48     zapnot  t0,0xf,t0  */
			/*  22 f6 41 48     zapnot  t1,0xf,t1  */
			*a++ = 0x21; *a++ = 0xf6; *a++ = 0x21; *a++ = 0x48;
			*a++ = 0x22; *a++ = 0xf6; *a++ = 0x41; *a++ = 0x48;
		}

		/*  03 04 22 4c     mulq    t0,t1,t2  */
		*a++ = 0x03; *a++ = 0x04; *a++ = 0x22; *a++ = 0x4c;

		/*  01 10 60 40     addl    t2,0,t0  */
		*a++ = 0x01; *a++ = 0x10; *a++ = 0x60; *a++ = 0x40;

		ofs = ((size_t)&dummy_cpu.lo) - (size_t)&dummy_cpu;
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb4;

		/*  81 17 64 48     sra     t2,0x20,t0  */
		*a++ = 0x81; *a++ = 0x17; *a++ = 0x64; *a++ = 0x48;
		*a++ = 0x01; *a++ = 0x00; *a++ = 0x3f; *a++ = 0x40;	/*  addl t0,0,t0  */
		ofs = ((size_t)&dummy_cpu.hi) - (size_t)&dummy_cpu;
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb4;
		break;
	case SPECIAL_MOVZ:
		/*  if rt=0 then rd=rs  ==>  if t1!=0 then t0=unmodified else t0=rd  */
		/*  00 00 40 f4     bne     t1,unmodified  */
		unmodified = a;
		*a++ = 0x00; *a++ = 0x00; *a++ = 0x40; *a++ = 0xf4;
		break;
	case SPECIAL_MOVN:
		/*  if rt!=0 then rd=rs  ==>  if t1=0 then t0=unmodified else t0=rd  */
		/*  00 00 40 e4     beq     t1,unmodified  */
		unmodified = a;
		*a++ = 0x00; *a++ = 0x00; *a++ = 0x40; *a++ = 0xe4;
		break;
	}

	if (store) {
		*a++ = 0x1f; *a++ = 0x04; *a++ = 0xff; *a++ = 0x5f;	/*  fnop  */
		bintrans_move_Alpha_reg_into_MIPS_reg(&a, ALPHA_T0, rd);
	}

	if (unmodified != NULL)
		*unmodified = ((size_t)a - (size_t)unmodified - 4) / 4;

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
		bintrans_move_MIPS_reg_into_Alpha_reg(&a, rt, ALPHA_T0);
		bintrans_move_MIPS_reg_into_Alpha_reg(&a, rs, ALPHA_T1);

		*a++ = 0xa1; *a++ = 0x05; *a++ = 0x22; *a++ = 0x40;  /*  cmpeq  */
		b = a;
		*a++ = 0x01; *a++ = 0x00; *a++ = 0x20; *a++ = 0xe4;  /*  beq  */
		/*     ^^^^  --- NOTE: This is automagically updated later on.  */
	}
	if (instruction_type == HI6_BNE) {
		bintrans_move_MIPS_reg_into_Alpha_reg(&a, rt, ALPHA_T0);
		bintrans_move_MIPS_reg_into_Alpha_reg(&a, rs, ALPHA_T1);

		*a++ = 0xa1; *a++ = 0x05; *a++ = 0x22; *a++ = 0x40;  /*  cmpeq  */
		b = a;
		*a++ = 0x01; *a++ = 0x00; *a++ = 0x20; *a++ = 0xf4;  /*  bne  */
		/*     ^^^^  --- NOTE: This is automagically updated later on.  */
	}
	if (instruction_type == HI6_BLEZ) {
		bintrans_move_MIPS_reg_into_Alpha_reg(&a, rs, ALPHA_T1);

		*a++ = 0xa1; *a++ = 0x1d; *a++ = 0x40; *a++ = 0x40;  /*  cmple t1,0,t0  */
		b = a;
		*a++ = 0x01; *a++ = 0x00; *a++ = 0x20; *a++ = 0xe4;  /*  beq  */
		/*     ^^^^  --- NOTE: This is automagically updated later on.  */
	}
	if (instruction_type == HI6_BGTZ) {
		bintrans_move_MIPS_reg_into_Alpha_reg(&a, rs, ALPHA_T1);

		*a++ = 0xa1; *a++ = 0x1d; *a++ = 0x40; *a++ = 0x40;  /*  cmple t1,0,t0  */
		b = a;
		*a++ = 0x01; *a++ = 0x00; *a++ = 0x20; *a++ = 0xf4;  /*  bne  */
		/*     ^^^^  --- NOTE: This is automagically updated later on.  */
	}
	if (instruction_type == HI6_REGIMM && regimm_type == REGIMM_BLTZ) {
		bintrans_move_MIPS_reg_into_Alpha_reg(&a, rs, ALPHA_T1);

		*a++ = 0xa1; *a++ = 0x19; *a++ = 0x40; *a++ = 0x40;  /*  cmplt t1,0,t0  */
		b = a;
		*a++ = 0x01; *a++ = 0x00; *a++ = 0x20; *a++ = 0xe4;  /*  beq  */
		/*     ^^^^  --- NOTE: This is automagically updated later on.  */
	}
	if (instruction_type == HI6_REGIMM && regimm_type == REGIMM_BGEZ) {
		bintrans_move_MIPS_reg_into_Alpha_reg(&a, rs, ALPHA_T1);

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
	 *  04 00 26 20     lda     t0,4(t5)		add 4
	 *  c8 01 5f 20     lda     t1,456
	 *  22 57 40 48     sll     t1,0x2,t1
	 *  0a 04 22 40     addq    t0,t1,s1		add (imm<<2)
	 */

	*a++ = 0x04; *a++ = 0x00; *a++ = 0x26; *a++ = 0x20;  /*  lda  */
	*a++ = (imm & 255); *a++ = (imm >> 8); *a++ = 0x5f; *a++ = 0x20;  /*  lda  */
	*a++ = 0x22; *a++ = 0x57; *a++ = 0x40; *a++ = 0x48;  /*  sll  */
	*a++ = 0x0a; *a++ = 0x04; *a++ = 0x22; *a++ = 0x40;  /*  addq  */

	/*  02 00 3f 21     lda     s0,TO_BE_DELAYED  */
	*a++ = TO_BE_DELAYED; *a++ = 0x00; *a++ = 0x3f; *a++ = 0x21;

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
	uint32_t *a;

	/*
	 *  Perform the jump by setting cpu->delay_slot = TO_BE_DELAYED
	 *  and cpu->delay_jmpaddr = gpr[rs].
	 */
	bintrans_move_MIPS_reg_into_Alpha_reg(addrp, rs, ALPHA_S1);
	a = (uint32_t *) *addrp;

	/*  02 00 3f 21     lda     s0,TO_BE_DELAYED  */
	*a++ = 0x213f0000 | TO_BE_DELAYED;
	*addrp = (unsigned char *) a;

	if (special == SPECIAL_JALR && rd != 0) {
		/*  gpr[rd] = retaddr    (pc + 8)  */
		a = (uint32_t *) *addrp;
		*a++ = 0x20260008;	/*  lda t0,8(t5)  */
		*addrp = (unsigned char *) a;
		bintrans_move_Alpha_reg_into_MIPS_reg(addrp, ALPHA_T0, rd);
	}

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
	uint64_t subimm;

	a = *addrp;

	/*
	 *  gpr[31] = retaddr
	 *
	 *  08 00 84 20     lda     t3,8(t5)
	 *  18 09 90 b4     stq     t3,gpr31(a0)
	 */
	if (link) {
		*a++ = 8; *a++ = 0; *a++ = 0x86; *a++ = 0x20;	/*  lda t3,8(t5)  */
		bintrans_move_Alpha_reg_into_MIPS_reg(&a, ALPHA_T3, 31);
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
	 *  04 00 26 20     lda     t0,4(t5)	<-- because the jump is from the delay slot
	 *  81 96 23 48     srl     t0,0x1c,t0
	 *  21 d7 21 48     sll     t0,0xe,t0
	 *  c8 01 5f 20     lda     t1,456
	 *  01 04 22 44     or      t0,t1,t0
	 *  21 d7 21 48     sll     t0,0xe,t0
	 *  c8 01 5f 20     lda     t1,456
	 *  0a 04 22 44     or      t0,t1,s1
	 */

	imm *= 4;

	*a++ = 4; *a++ = 0; *a++ = 0x26; *a++ = 0x20;	/*  lda t0,4(t5)  */

	*a++ = 0x81; *a++ = 0x96; *a++ = 0x23; *a++ = 0x48;
	*a++ = 0x21; *a++ = 0xd7; *a++ = 0x21; *a++ = 0x48;

	subimm = (imm >> 14) & 0x3fff;
	*a++ = (subimm & 255); *a++ = (subimm >> 8); *a++ = 0x5f; *a++ = 0x20;

	*a++ = 0x01; *a++ = 0x04; *a++ = 0x22; *a++ = 0x44;
	*a++ = 0x21; *a++ = 0xd7; *a++ = 0x21; *a++ = 0x48;

	subimm = imm & 0x3fff;
	*a++ = (subimm & 255); *a++ = (subimm >> 8); *a++ = 0x5f; *a++ = 0x20;

	*a++ = 0x0a; *a++ = 0x04; *a++ = 0x22; *a++ = 0x44;

	/*  02 00 3f 21     lda     s0,TO_BE_DELAYED  */
	*a++ = TO_BE_DELAYED; *a++ = 0x00; *a++ = 0x3f; *a++ = 0x21;

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
	uint32_t *potential_chunk_p, uint32_t *chunks,
	int only_care_about_chunk_p, int p)
{
	unsigned char *a, *skip=NULL;
	int ofs;
	uint64_t alpha_addr, subaddr;

	a = *addrp;

	if (!only_care_about_chunk_p) {
		/*  Skip all of this if there is no branch:  */
		skip = a;
		*a++ = 0; *a++ = 0; *a++ = 0x20; *a++ = 0xe5;  /*  beq s0,skip  */

		/*
		 *  Perform the jump by setting cpu->delay_slot = 0
		 *  and pc = cpu->delay_jmpaddr.
		 */
		/*  00 00 3f 21     lda     s0,0  */
		*a++ = 0; *a++ = 0; *a++ = 0x3f; *a++ = 0x21;

		bintrans_move_MIPS_reg_into_Alpha_reg(&a, MIPSREG_DELAY_JMPADDR, ALPHA_T0);
		bintrans_move_MIPS_reg_into_Alpha_reg(&a, MIPSREG_PC, ALPHA_T3);
		bintrans_move_Alpha_reg_into_MIPS_reg(&a, ALPHA_T0, MIPSREG_PC);
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

		/*  Don't execute too many instructions. (see comment below)  */
		*a++ = (N_SAFE_BINTRANS_LIMIT-1)&255; *a++ = ((N_SAFE_BINTRANS_LIMIT-1) >> 8)&255;
			*a++ = 0x5f; *a++ = 0x20;	/*  lda t1,0x1fff */
		*a++ = 0xa1; *a++ = 0x0d; *a++ = 0xe2; *a++ = 0x40;	/*  cmple t6,t1,t0  */
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
		bintrans_move_MIPS_reg_into_Alpha_reg(&a, MIPSREG_PC, ALPHA_T2);
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
		 *  a1 0d c2 40     cmple   t6,t1,t0
		 *  01 00 20 f4     bne     t0,14 <f+0x14>
		 */
		if (!only_care_about_chunk_p) {
			*a++ = (N_SAFE_BINTRANS_LIMIT-1)&255; *a++ = ((N_SAFE_BINTRANS_LIMIT-1) >> 8)&255;
				*a++ = 0x5f; *a++ = 0x20;	/*  lda t1,0x1fff */
			*a++ = 0xa1; *a++ = 0x0d; *a++ = 0xe2; *a++ = 0x40;	/*  cmple t6,t1,t0  */
			*a++ = 0x01; *a++ = 0x00; *a++ = 0x20; *a++ = 0xf4;	/*  bne  */
			*a++ = 0x01; *a++ = 0x80; *a++ = 0xfa; *a++ = 0x6b;	/*  ret  */
		}

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
			*a++ = ofs & 255; *a++ = (ofs >> 8) & 255; *a++ = 0xe0 + ((ofs >> 16) & 0x1f); *a++ = 0xc3;	/*  br <chunk>  */
		} else {
			/*  Case 2:  */

			bintrans_register_potential_quick_jump(a, p);

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
	unsigned char *a, *fail;
	int ofs, alignment, load=0;

	/*  TODO: Not yet:  */
	if (instruction_type == HI6_LQ_MDMX || instruction_type == HI6_SQ) {
		bintrans_write_chunkreturn_fail(addrp);
		return 0;
	}

	switch (instruction_type) {
	case HI6_LQ_MDMX:
	case HI6_LD:
	case HI6_LWU:
	case HI6_LW:
	case HI6_LHU:
	case HI6_LH:
	case HI6_LBU:
	case HI6_LB:
		load = 1;
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

	bintrans_move_MIPS_reg_into_Alpha_reg(&a, rs, ALPHA_T0);
	*a++ = (imm & 255); *a++ = (imm >> 8); *a++ = 0x21; *a++ = 0x22;

	alignment = 0;
	switch (instruction_type) {
	case HI6_LQ_MDMX:
	case HI6_SQ:
		alignment = 15;
		break;
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

	/*
	 *  a2 = vaddr_to_hostaddr_table0
	 *  00 00 50 a6     ldq     a2,0(a0)
	 */
	ofs = ((size_t)&dummy_cpu.vaddr_to_hostaddr_table0) - (size_t)&dummy_cpu;
	*a++ = ofs&255; *a++ = ofs>>8; *a++ = 0x50; *a++ = 0xa6;

	if (alignment > 0) {
		/*
		 *  Check alignment:
		 *
		 *  02 30 20 46     and     a1,0x1,t1
		 *  02 70 20 46     and     a1,0x3,t1	(one of these "and"s)
		 *  02 f0 20 46     and     a1,0x7,t1
		 *  02 f0 21 46     and     a1,0xf,t1
		 *  01 00 40 e4     beq     t1,<okalign>
		 *  01 80 fa 6b     ret
		 */
		*a++ = 0x02; *a++ = 0x10 + alignment * 0x20; *a++ = 0x20 + (alignment >> 3); *a++ = 0x46;
		fail = a;
		*a++ = 0x01; *a++ = 0x00; *a++ = 0x40; *a++ = 0xe4;
		*addrp = a;
		bintrans_write_chunkreturn_fail(addrp);
		a = *addrp;
		*fail = ((size_t)a - (size_t)fail - 4) / 4;
	}

	/*
	 *  t1 = 1023;
	 *  t2 = ((a1 >> 22) & t1) * sizeof(void *);
	 *  t3 = ((a1 >> 12) & t1) * sizeof(void *);
	 *  t1 = a1 & 4095;
	 *
	 *  f8 1f 5f 20     lda     t1,1023 * 8
	 *  83 76 22 4a     srl     a1,19,t2
	 *  84 36 21 4a     srl     a1, 9,t3
	 *  03 00 62 44     and     t2,t1,t2
	 */
	*a++ = 0xf8; *a++ = 0x1f; *a++ = 0x5f; *a++ = 0x20;
	*a++ = 0x83; *a++ = 0x76; *a++ = 0x22; *a++ = 0x4a;
	*a++ = 0x84; *a++ = 0x36; *a++ = 0x21; *a++ = 0x4a;
	*a++ = 0x03; *a++ = 0x00; *a++ = 0x62; *a++ = 0x44;

	/*
	 *  a3 = tbl0[t2]  (load entry from tbl0)
	 *  12 04 43 42     addq    a2,t2,a2
	 */
	*a++ = 0x12; *a++ = 0x04; *a++ = 0x43; *a++ = 0x42;

	/*  04 00 82 44     and     t3,t1,t3  */
	*a++ = 0x04; *a++ = 0x00; *a++ = 0x82; *a++ = 0x44;

	/*  00 00 72 a6     ldq     a3,0(a2)  */
	*a++ = 0x00; *a++ = 0x00; *a++ = 0x72; *a++ = 0xa6;

	/*  ff 0f 5f 20     lda     t1,4095  */
	*a++ = 0xff; *a++ = 0x0f; *a++ = 0x5f; *a++ = 0x20;

	/*
	 *  a3 = tbl1[t3]  (load entry from tbl1 (whic is a3))
	 *  13 04 64 42     addq    a3,t3,a3
	 */
	*a++ = 0x13; *a++ = 0x04; *a++ = 0x64; *a++ = 0x42;

	/*  02 00 22 46     and     a1,t1,t1  */
	*a++ = 0x02; *a++ = 0x00; *a++ = 0x22; *a++ = 0x46;

	/*  00 00 73 a6     ldq     a3,0(a3)  */
	*a++ = 0x00; *a++ = 0x00; *a++ = 0x73; *a++ = 0xa6;

	/*
	 *  NULL? Then return failure.
	 *  01 00 60 f6     bne     a3,f8 <okzz>
	 */
	fail = a;
	*a++ = 0x01; *a++ = 0x00; *a++ = 0x60; *a++ = 0xf6;
	bintrans_write_chunkreturn_fail(&a);
	*fail = ((size_t)a - (size_t)fail - 4) / 4;

	/*
	 *  If this is a store, then the lowest bit must be set:
	 */
	if (!load) {
		/*  01 30 60 46     and     a3,0x1,t0  */
		/*  01 00 20 f4     bne     t0,<okzzz>  */
		*a++ = 0x01; *a++ = 0x30; *a++ = 0x60; *a++ = 0x46;
		fail = a;
		*a++ = 0x01; *a++ = 0x00; *a++ = 0x20; *a++ = 0xf4;
		bintrans_write_chunkreturn_fail(&a);
		*fail = ((size_t)a - (size_t)fail - 4) / 4;
	}


	/*
	 *  The rest of this code was written with t3 as the address.
	 *
	 *  fe ff 7f 20     lda     t2,-2	Get rid of the lowest
	 *  04 00 63 46     and     a3,t2,t3	bit, and add
	 *  04 04 82 40     addq    t3,t1,t3	the offset within the page.
	 */
	*a++ = 0xfe; *a++ = 0xff; *a++ = 0x7f; *a++ = 0x20;
	*a++ = 0x04; *a++ = 0x00; *a++ = 0x63; *a++ = 0x46;
	*a++ = 0x04; *a++ = 0x04; *a++ = 0x82; *a++ = 0x40;


	switch (instruction_type) {
	case HI6_LQ_MDMX:
		/*  TODO  */
		break;
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
		bintrans_move_Alpha_reg_into_MIPS_reg(&a, ALPHA_T0, rt);
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
		bintrans_move_Alpha_reg_into_MIPS_reg(&a, ALPHA_T0, rt);
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
		bintrans_move_Alpha_reg_into_MIPS_reg(&a, ALPHA_T0, rt);
		break;
	case HI6_LBU:
	case HI6_LB:
		*a++ = 0x00; *a++ = 0x00; *a++ = 0x24; *a++ = 0x28;			/*  ldbu from memory  */
		if (instruction_type == HI6_LB) {
			*a++ = 0x01; *a++ = 0x00; *a++ = 0xe1; *a++ = 0x73;		/*  sextb   t0,t0  */
		}
		bintrans_move_Alpha_reg_into_MIPS_reg(&a, ALPHA_T0, rt);
		break;

	case HI6_SQ:
		/*  TODO  */
		break;
	case HI6_SD:
		bintrans_move_MIPS_reg_into_Alpha_reg(&a, rt, ALPHA_T0);
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
		bintrans_move_MIPS_reg_into_Alpha_reg(&a, rt, ALPHA_T0);
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
		bintrans_move_MIPS_reg_into_Alpha_reg(&a, rt, ALPHA_T0);
		if (bigendian) {
			*a++ = 0x62; *a++ = 0x31; *a++ = 0x20; *a++ = 0x48;		/*  insbl t0,1,t1  */
			*a++ = 0xc3; *a++ = 0x30; *a++ = 0x20; *a++ = 0x48;		/*  extbl t0,1,t2  */
			*a++ = 0x01; *a++ = 0x04; *a++ = 0x43; *a++ = 0x44;		/*  or t1,t2,t0  */
		}
		*a++ = 0x00; *a++ = 0x00; *a++ = 0x24; *a++ = 0x34;			/*  stw to memory  */
		break;
	case HI6_SB:
		bintrans_move_MIPS_reg_into_Alpha_reg(&a, rt, ALPHA_T0);
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

	/*
	 *  dc fe 3f 24     ldah    t0,-292
	 *  1f 04 ff 5f     fnop
	 *  88 08 30 b4     stq     t0,2184(a0)
	 */
	if (rt != 0) {
		a = (uint32_t *) *addrp;
		*a++ = 0x243f0000 | ((uint32_t)imm & 0xffff);
		*a++ = 0x5fff041f;
		*addrp = (unsigned char *) a;
		bintrans_move_Alpha_reg_into_MIPS_reg(addrp, ALPHA_T0, rt);
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

			bintrans_move_Alpha_reg_into_MIPS_reg(&a, ALPHA_T0, rd);
		}
	} else {
		/*  mthi or mtlo  */
		bintrans_move_MIPS_reg_into_Alpha_reg(&a, rd, ALPHA_T0);

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
 *  bintrans_write_instruction__mfc_mtc():
 */
static int bintrans_write_instruction__mfc_mtc(unsigned char **addrp, int coproc_nr, int flag64bit, int rt, int rd, int mtcflag)
{
	uint32_t *a, *jump;
	int ofs;

	/*
	 *  NOTE: Only a few registers are readable without side effects.
	 */
	if (rt == 0 && mtcflag==0)
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

	if (mtcflag) {
		/*  mtc:  */
		*addrp = (unsigned char *) a;
		bintrans_move_MIPS_reg_into_Alpha_reg(addrp, rt, ALPHA_T0);
		a = (uint32_t *) *addrp;

		if (!flag64bit) {
			*a++ = 0x40201001;	/*  addl t0,0,t0  */
			*a++ = 0x40401002;	/*  addl t1,0,t1  */
		}

		/*
		 *  In the general case:  Only allow mtc if it does NOT
		 *  change the register!!
		 */

		switch (rd) {
		case COP0_INDEX:

		/*  TODO: Some bits are not writable on R3000  */
		case COP0_ENTRYLO0:
		case COP0_ENTRYLO1:

		case COP0_EPC:
			break;

		case COP0_ENTRYHI:
			/*
			 *  Entryhi is ok to write to, as long as the
			 *  ASID isn't changed. (That would require
			 *  cache invalidations etc. Instead of checking
			 *  for MMU3K vs others, we just assume that all the
			 *  lowest 12 bits must be the same.
			 */
			/*  ff 0f bf 20     lda     t4,0x0fff  */
			/*  03 00 25 44     and     t0,t4,t2  */
			/*  04 00 45 44     and     t1,t4,t3  */
			/*  a3 05 64 40     cmpeq   t2,t3,t2  */
			/*  01 00 60 f4     bne     t2,<ok>  */
			*a++ = 0x20bf0fff;
			*a++ = 0x44250003;
			*a++ = 0x44450004;
			*a++ = 0x406405a3;
			jump = a;
			*a++ = 0;	/*  later  */
			*addrp = (unsigned char *) a;
			bintrans_write_chunkreturn_fail(addrp);
			a = (uint32_t *) *addrp;
			*jump = 0xf4600000 | (((size_t)a - (size_t)jump - 4) / 4);
			break;

		case COP0_STATUS:
			/*  Only allow updates to the status register if
			    the interrupt enable bits were changed, but no
			    other bits!  */
			/*  fe 00 bf 20     lda     t4,0x00fe  */
			/*  ff ff a5 24     ldah    t4,-1(t4)  */
			*a++ = 0x20bf00fe;
			*a++ = 0x24a5ffff;

			/*  03 00 25 44     and     t0,t4,t2  */
			/*  04 00 45 44     and     t1,t4,t3  */
			/*  a3 05 64 40     cmpeq   t2,t3,t2  */
			/*  01 00 60 f4     bne     t2,<ok>  */
			*a++ = 0x44250003;
			*a++ = 0x44450004;
			*a++ = 0x406405a3;
			jump = a;
			*a++ = 0;	/*  later  */
			*addrp = (unsigned char *) a;
			bintrans_write_chunkreturn_fail(addrp);
			a = (uint32_t *) *addrp;
			*jump = 0xf4600000 | (((size_t)a - (size_t)jump - 4) / 4);

			/*  If enabling interrupt bits would cause an
			    exception, then don't do it:  */
			ofs = ((size_t)&dummy_cpu.coproc[0]) - (size_t)&dummy_cpu;
			*a++ = 0xa4900000 | (ofs & 0xffff);		/*  ldq t3,coproc[0](a0)  */
			ofs = ((size_t)&dummy_coproc.reg[COP0_CAUSE]) - (size_t)&dummy_coproc;
			*a++ = 0xa4a40000 | (ofs & 0xffff);		/*  ldq t4,reg_rd(t3)  */

			/*  02 00 a1 44     and     t4,t0,t1  */
			/*  83 16 41 48     srl     t1,0x8,t2  */
			/*  04 f0 7f 44     and     t2,0xff,t3  */
			*a++ = 0x44a10002;
			*a++ = 0x48411683;
			*a++ = 0x447ff004;
			/*  01 00 80 e4     beq     t3,<ok>  */
			jump = a;
			*a++ = 0;	/*  later  */
			*addrp = (unsigned char *) a;
			bintrans_write_chunkreturn_fail(addrp);
			a = (uint32_t *) *addrp;
			*jump = 0xe4800000 | (((size_t)a - (size_t)jump - 4) / 4);
			break;

		default:
			/*  a3 05 22 40     cmpeq   t0,t1,t2  */
			/*  01 00 60 f4     bne     t2,<ok>  */
			*a++ = 0x402205a3;
			jump = a;
			*a++ = 0;	/*  later  */
			*addrp = (unsigned char *) a;
			bintrans_write_chunkreturn_fail(addrp);
			a = (uint32_t *) *addrp;
			*jump = 0xf4600000 | (((size_t)a - (size_t)jump - 4) / 4);
		}

		*a++ = 0x40201402;	/*  addq    t0,0,t1  */

		ofs = ((size_t)&dummy_cpu.coproc[0]) - (size_t)&dummy_cpu;
		*a++ = 0xa4300000 | (ofs & 0xffff);		/*  ldq t0,coproc[0](a0)  */
		ofs = ((size_t)&dummy_coproc.reg[rd]) - (size_t)&dummy_coproc;
		*a++ = 0xb4410000 | (ofs & 0xffff);		/*  stq t1,reg_rd(t0)  */
	} else {
		/*  mfc:  */
		if (!flag64bit) {
			*a++ = 0x40401002;		/*  addl t1,0,t1  */
		}

		*addrp = (unsigned char *) a;
		bintrans_move_Alpha_reg_into_MIPS_reg(addrp, ALPHA_T1, rt);
		a = (uint32_t *) *addrp;
	}

	*addrp = (unsigned char *) a;

	bintrans_write_pc_inc(addrp, sizeof(uint32_t), 1, 1);
	return 1;
}


/*
 *  bintrans_write_instruction__tlb_rfe_etc():
 */
static int bintrans_write_instruction__tlb_rfe_etc(unsigned char **addrp,
	int itype)
{
	uint32_t *a;
	int ofs;

	switch (itype) {
	case TLB_TLBWI:
	case TLB_TLBWR:
	case TLB_TLBP:
	case TLB_TLBR:
	case TLB_RFE:
	case TLB_ERET:
		break;
	default:
		return 0;
	}


/*  TODO: ERET modifies the PC register, so we shouldn't
	save and restore it here.  */
if (itype == TLB_ERET)
	return 0;



	a = (uint32_t *) *addrp;

	/*  a0 = pointer to the cpu struct  */

	switch (itype) {
	case TLB_TLBWI:
	case TLB_TLBWR:
		/*  a1 = 0 for indexed, 1 for random  */
		*a++ = 0x223f0000 | (itype == TLB_TLBWR);
		break;
	case TLB_TLBP:
	case TLB_TLBR:
		/*  a1 = 0 for probe, 1 for read  */
		*a++ = 0x223f0000 | (itype == TLB_TLBR);
		break;
	}

	/*  Save a0 and the old return address on the stack:  */
	*a++ = 0x23deff80;		/*  lda sp,-128(sp)  */

	*a++ = 0xb75e0000;		/*  stq ra,0(sp)  */
	*a++ = 0xb61e0008;		/*  stq a0,8(sp)  */
	*a++ = 0xb4de0010;		/*  stq t5,16(sp)  */
	*a++ = 0xb0fe0018;		/*  stl t6,24(sp)  */
	*a++ = 0xb71e0020;		/*  stq t10,32(sp)  */
	*a++ = 0xb73e0028;		/*  stq t11,40(sp)  */
	*a++ = 0xb51e0030;		/*  stq t7,48(sp)  */
	*a++ = 0xb6de0038;		/*  stq t8,56(sp)  */
	*a++ = 0xb6fe0040;		/*  stq t9,64(sp)  */

	switch (itype) {
	case TLB_TLBP:
	case TLB_TLBR:
		ofs = ((size_t)&dummy_cpu.bintrans_fast_tlbpr) - (size_t)&dummy_cpu;
		break;
	case TLB_TLBWR:
	case TLB_TLBWI:
		ofs = ((size_t)&dummy_cpu.bintrans_fast_tlbwri) - (size_t)&dummy_cpu;
		break;
	case TLB_RFE:
		ofs = ((size_t)&dummy_cpu.bintrans_fast_rfe) - (size_t)&dummy_cpu;
		break;
	case TLB_ERET:
		ofs = ((size_t)&dummy_cpu.bintrans_fast_eret) - (size_t)&dummy_cpu;
		break;
	}

	*a++ = 0xa7700000 | ofs;	/*  ldq t12,0(a0)  */

	/*  Call bintrans_fast_tlbwr:  */
	*a++ = 0x6b5b4000;		/*  jsr ra,(t12),<after>  */

	/*  Restore the old return address and a0 from the stack:  */
	*a++ = 0xa75e0000;		/*  ldq ra,0(sp)  */
	*a++ = 0xa61e0008;		/*  ldq a0,8(sp)  */
	*a++ = 0xa4de0010;		/*  ldq t5,16(sp)  */
	*a++ = 0xa0fe0018;		/*  ldl t6,24(sp)  */
	*a++ = 0xa71e0020;		/*  ldq t10,32(sp)  */
	*a++ = 0xa73e0028;		/*  ldq t11,40(sp)  */
	*a++ = 0xa51e0030;		/*  ldq t7,48(sp)  */
	*a++ = 0xa6de0038;		/*  ldq t8,56(sp)  */
	*a++ = 0xa6fe0040;		/*  ldq t9,64(sp)  */

	*a++ = 0x23de0080;		/*  lda sp,128(sp)  */

	*addrp = (unsigned char *) a;

	if (itype != TLB_ERET)
		bintrans_write_pc_inc(addrp, sizeof(uint32_t), 1, 1);

	return 1;
}

