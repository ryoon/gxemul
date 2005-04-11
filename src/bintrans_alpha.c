/*
 *  Copyright (C) 2004-2005  Anders Gavare.  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright  
 *     notice, this list of conditions and the following disclaimer in the 
 *     documentation and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
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
 *  $Id: bintrans_alpha.c,v 1.117 2005-04-11 21:22:13 debug Exp $
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
 *	t10		table0 cached (for load/store)
 *	t11		v0 (mips register 2)  (64-bit)
 *	s0		delay_slot (32-bit int)
 *	s1		delay_jmpaddr (64-bit)
 *	s2		sp (mips register 29)  (64-bit)
 *	s3		ra (mips register 31)  (64-bit)
 *	s4		t0 (mips register 8)  (64-bit)
 *	s5		t1 (mips register 9)  (64-bit)
 *	s6		t2 (mips register 10)  (64-bit)
 */

#define	MIPSREG_PC			-3
#define	MIPSREG_DELAY_SLOT		-2
#define	MIPSREG_DELAY_JMPADDR		-1

#define	ALPHA_T0		1
#define	ALPHA_T1		2
#define	ALPHA_T2		3
#define	ALPHA_T3		4
#define	ALPHA_T4		5
#define	ALPHA_T5		6
#define	ALPHA_T6		7
#define	ALPHA_T7		8
#define	ALPHA_S0		9
#define	ALPHA_S1		10
#define	ALPHA_S2		11
#define	ALPHA_S3		12
#define	ALPHA_S4		13
#define	ALPHA_S5		14
#define	ALPHA_S6		15
#define	ALPHA_A0		16
#define	ALPHA_A1		17
#define	ALPHA_A2		18
#define	ALPHA_A3		19
#define	ALPHA_A4		20
#define	ALPHA_A5		21
#define	ALPHA_T8		22
#define	ALPHA_T9		23
#define	ALPHA_T10		24
#define	ALPHA_T11		25
#define	ALPHA_ZERO		31

static int map_MIPS_to_Alpha[32] = {
	ALPHA_ZERO, -1, ALPHA_T11, -1,		/*  0 .. 3  */
	ALPHA_T7, ALPHA_T8, -1, -1,		/*  4 .. 7  */
	ALPHA_S4, ALPHA_S5, ALPHA_S6, -1,	/*  8 .. 11  */
	-1, -1, -1, -1,				/*  12 .. 15  */
	ALPHA_T9, -1, -1, -1,			/*  16 .. 19  */
	-1, -1, -1, -1,				/*  20 .. 23  */
	-1, -1, -1, -1,				/*  24 .. 27  */
	-1, ALPHA_S2, -1, ALPHA_S3,		/*  28 .. 31  */
};


struct cpu dummy_cpu;
struct mips_coproc dummy_coproc;
struct vth32_table dummy_vth32_table;

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
/*  note: offsetof (in stdarg.h) could possibly be used, but I'm not sure
    if it will take care of the compiler problems...  */
#define ofs_pc	(((size_t)&dummy_cpu.pc) - ((size_t)&dummy_cpu))
#define ofs_pc_last	(((size_t)&dummy_cpu.cd.mips.pc_last) - ((size_t)&dummy_cpu))
#define ofs_n	(((size_t)&dummy_cpu.cd.mips.bintrans_instructions_executed) - ((size_t)&dummy_cpu))
#define ofs_ds	(((size_t)&dummy_cpu.cd.mips.delay_slot) - ((size_t)&dummy_cpu))
#define ofs_ja	(((size_t)&dummy_cpu.cd.mips.delay_jmpaddr) - ((size_t)&dummy_cpu))
#define ofs_sp	(((size_t)&dummy_cpu.cd.mips.gpr[MIPS_GPR_SP]) - ((size_t)&dummy_cpu))
#define ofs_ra	(((size_t)&dummy_cpu.cd.mips.gpr[MIPS_GPR_RA]) - ((size_t)&dummy_cpu))
#define ofs_a0	(((size_t)&dummy_cpu.cd.mips.gpr[MIPS_GPR_A0]) - ((size_t)&dummy_cpu))
#define ofs_a1	(((size_t)&dummy_cpu.cd.mips.gpr[MIPS_GPR_A1]) - ((size_t)&dummy_cpu))
#define ofs_t0	(((size_t)&dummy_cpu.cd.mips.gpr[MIPS_GPR_T0]) - ((size_t)&dummy_cpu))
#define ofs_t1	(((size_t)&dummy_cpu.cd.mips.gpr[MIPS_GPR_T1]) - ((size_t)&dummy_cpu))
#define ofs_t2	(((size_t)&dummy_cpu.cd.mips.gpr[MIPS_GPR_T2]) - ((size_t)&dummy_cpu))
#define ofs_v0	(((size_t)&dummy_cpu.cd.mips.gpr[MIPS_GPR_V0]) - ((size_t)&dummy_cpu))
#define ofs_s0	(((size_t)&dummy_cpu.cd.mips.gpr[MIPS_GPR_S0]) - ((size_t)&dummy_cpu))
#define ofs_tbl0 (((size_t)&dummy_cpu.cd.mips.vaddr_to_hostaddr_table0) - ((size_t)&dummy_cpu))
#define ofs_c0	((size_t)&dummy_vth32_table.bintrans_chunks[0] - (size_t)&dummy_vth32_table)
#define ofs_cb (((size_t)&dummy_cpu.cd.mips.chunk_base_address) - (size_t)&dummy_cpu)


static uint32_t bintrans_alpha_loadstore_32bit[19] = {
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
	0x205f1ff8,
	0x4a227683,
	0x4a213684,
	0x44620003,

	/*
	 *  t10 is vaddr_to_hostaddr_table0
	 *
	 *  a3 = tbl0[t2]  (load entry from tbl0)
	 *  12 04 03 43     addq    t10,t2,a2
	 */
	0x43030412,

	/*  04 00 82 44     and     t3,t1,t3  */
	0x44820004,

	/*  00 00 72 a6     ldq     a3,0(a2)  */
	0xa6720000,

	/*  ff 0f 5f 20     lda     t1,4095  */
	0x205f0fff,

	/*
	 *  a3 = tbl1[t3]  (load entry from tbl1 (which is a3))
	 *  13 04 64 42     addq    a3,t3,a3
	 */
	0x42640413,

	/*  02 00 22 46     and     a1,t1,t1  */
	0x46220002,

	/*  00 00 73 a6     ldq     a3,0(a3)  */
	0xa6730000,

	/*  NULL? Then return failure at once.  */
	/*  beq a3, return  */
	0xe6600004,

	/*  01 30 60 46     and     a3,0x1,t0  */
	0x46603001,

	/*  Get rid of the lowest bit:  */
	/*  33 05 61 42     subq    a3,t0,a3  */
	0x42610533,

	/*  The rest of the load/store code was written with t3 as the address.  */

	/*  Add the offset within the page:  */
	/*  04 04 62 42     addq    a3,t1,t3  */
	0x42620404,

	0x6be50000,		/*  jmp (t4)  */

	/*  return:  */
	0x243f0000 | (BINTRANS_DONT_RUN_NEXT >> 16),	/*  ldah  t0,256  */
	0x44270407,					/*  or      t0,t6,t6  */
	0x6bfa8001					/*  ret  */
};

static void (*bintrans_runchunk)(struct cpu *, unsigned char *);

static void (*bintrans_jump_to_32bit_pc)(struct cpu *);

static void (*bintrans_loadstore_32bit)
    (struct cpu *) = (void *)bintrans_alpha_loadstore_32bit;


/*
 *  bintrans_write_quickjump():
 */
static void bintrans_write_quickjump(struct memory *mem,
	unsigned char *quickjump_code, uint32_t chunkoffset)
{
	int ofs;
	uint64_t alpha_addr = chunkoffset +
	    (size_t)mem->translation_code_chunk_space;
	uint32_t *a = (uint32_t *)quickjump_code;

	ofs = (alpha_addr - ((size_t)a+4)) / 4;

	/*  printf("chunkoffset=%i, %016llx %016llx %i\n",
	    chunkoffset, (long long)alpha_addr, (long long)a, ofs);  */

	if (ofs > -0xfffff && ofs < 0xfffff) {
		*a++ = 0xc3e00000 | (ofs & 0x1fffff);	/*  br <chunk>  */
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
	int ofs, alpha_mips_reg;

	switch (mipsreg) {
	case MIPSREG_PC:
		/*  addq t5,0,alphareg  */
		*a++ = 0x40c01400 | alphareg;
		break;
	case MIPSREG_DELAY_SLOT:
		/*  addq s0,0,alphareg  */
		*a++ = 0x41201400 | alphareg;
		break;
	case MIPSREG_DELAY_JMPADDR:
		/*  addq s1,0,alphareg  */
		*a++ = 0x41401400 | alphareg;
		break;
	default:
		alpha_mips_reg = map_MIPS_to_Alpha[mipsreg];
		if (alpha_mips_reg < 0) {
			ofs = ((size_t)&dummy_cpu.cd.mips.gpr[mipsreg]) - (size_t)&dummy_cpu;
			/*  ldq alphareg,gpr[mipsreg](a0)  */
			*a++ = 0xa4100000 | (alphareg << 21) | ofs;
		} else {
			/*  addq alpha_mips_reg,0,alphareg  */
			*a++ = 0x40001400 | (alpha_mips_reg << 21) | alphareg;
		}
	}
	*addrp = (unsigned char *) a;
}


/*
 *  bintrans_move_Alpha_reg_into_MIPS_reg():
 */
static void bintrans_move_Alpha_reg_into_MIPS_reg(unsigned char **addrp, int alphareg, int mipsreg)
{
	uint32_t *a = (uint32_t *) *addrp;
	int ofs, alpha_mips_reg;

	switch (mipsreg) {
	case MIPSREG_PC:
		/*  addq alphareg,0,t5  */
		*a++ = 0x40001406 | (alphareg << 21);
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
	default:
		alpha_mips_reg = map_MIPS_to_Alpha[mipsreg];
		if (alpha_mips_reg < 0) {
			/*  stq alphareg,gpr[mipsreg](a0)  */
			ofs = ((size_t)&dummy_cpu.cd.mips.gpr[mipsreg]) - (size_t)&dummy_cpu;
			*a++ = 0xb4100000 | (alphareg << 21) | ofs;
		} else {
			/*  addq alphareg,0,alpha_mips_reg  */
			*a++ = 0x40001400 | (alphareg << 21) | alpha_mips_reg;
		}
	}
	*addrp = (unsigned char *) a;
}


/*
 *  bintrans_write_pc_inc():
 */
static void bintrans_write_pc_inc(unsigned char **addrp)
{
	uint32_t *a = (uint32_t *) *addrp;

	/*  lda t6,1(t6)  */
	*a++ = 0x20e70001;

	/*  lda t5,4(t5)  */
	*a++ = 0x20c60004;

	*addrp = (unsigned char *) a;
}


/*
 *  bintrans_write_instruction__addiu_etc():
 */
static int bintrans_write_instruction__addiu_etc(unsigned char **addrp,
	int rt, int rs, int imm, int instruction_type)
{
	uint32_t *a;
	unsigned int uimm;
	int alpha_rs, alpha_rt;

	/*  TODO: overflow detection for ADDI and DADDI  */
	switch (instruction_type) {
	case HI6_ADDI:
	case HI6_DADDI:
		return 0;
	}

	a = (uint32_t *) *addrp;

	if (rt == 0)
		goto rt0;

	uimm = imm & 0xffff;

	alpha_rs = map_MIPS_to_Alpha[rs];
	alpha_rt = map_MIPS_to_Alpha[rt];

	if (uimm == 0 && (instruction_type == HI6_ADDI ||
 	    instruction_type == HI6_ADDIU || instruction_type == HI6_DADDI ||
	    instruction_type == HI6_DADDIU || instruction_type == HI6_ORI)) {
		if (alpha_rs >= 0 && alpha_rt >= 0) {
			/*  addq rs,0,rt  */
			*a++ = 0x40001400 | (alpha_rs << 21) | alpha_rt;
		} else {
			*addrp = (unsigned char *) a;
			bintrans_move_MIPS_reg_into_Alpha_reg(addrp, rs, ALPHA_T0);
			bintrans_move_Alpha_reg_into_MIPS_reg(addrp, ALPHA_T0, rt);
			a = (uint32_t *) *addrp;
		}
		goto rt0;
	}

	if (alpha_rs < 0) {
		/*  ldq t0,"rs"(a0)  */
		*addrp = (unsigned char *) a;
		bintrans_move_MIPS_reg_into_Alpha_reg(addrp, rs, ALPHA_T0);
		a = (uint32_t *) *addrp;
		alpha_rs = ALPHA_T0;
	}

	if (alpha_rt < 0)
		alpha_rt = ALPHA_T0;

	/*  Place the result of the calculation in alpha_rt:  */

	switch (instruction_type) {
	case HI6_ADDIU:
	case HI6_DADDIU:
	case HI6_ADDI:
	case HI6_DADDI:
		if (uimm < 256) {
			if (instruction_type == HI6_ADDI ||
			    instruction_type == HI6_ADDIU) {
				/*  addl rs,uimm,rt  */
				*a++ = 0x40001000 | (alpha_rs << 21)
				    | (uimm << 13) | alpha_rt;
			} else {
				/*  addq rs,uimm,rt  */
				*a++ = 0x40001400 | (alpha_rs << 21)
				    | (uimm << 13) | alpha_rt;
			}
		} else {
			/*  lda rt,imm(rs)  */
			*a++ = 0x20000000 | (alpha_rt << 21) | (alpha_rs << 16) | uimm;
			if (instruction_type == HI6_ADDI ||
			    instruction_type == HI6_ADDIU) {
				/*  sign extend, 32->64 bits:  addl t0,zero,t0  */
				*a++ = 0x40001000 | (alpha_rt << 21) | alpha_rt;
			}
		}
		break;
	case HI6_ANDI:
	case HI6_ORI:
	case HI6_XORI:
		if (uimm >= 256) {
			/*  lda t1,4660  */
			*a++ = 0x205f0000 | uimm;
			if (uimm & 0x8000) {
				/*  01 00 42 24  ldah t1,1(t1)	<-- if negative only  */
				*a++ = 0x24420001;
			}
		}

		switch (instruction_type) {
		case HI6_ANDI:
			if (uimm < 256) {
				/*  and rs,uimm,rt  */
				*a++ = 0x44001000 | (alpha_rs << 21)
				    | (uimm << 13) | alpha_rt;
			} else {
				/*  and rs,t1,rt  */
				*a++ = 0x44020000 | (alpha_rs << 21) | alpha_rt;
			}
			break;
		case HI6_ORI:
			if (uimm < 256) {
				/*  or rs,uimm,rt  */
				*a++ = 0x44001400 | (alpha_rs << 21)
				    | (uimm << 13) | alpha_rt;
			} else {
				/*  or rs,t1,rt  */
				*a++ = 0x44020400 | (alpha_rs << 21) | alpha_rt;
			}
			break;
		case HI6_XORI:
			if (uimm < 256) {
				/*  xor rs,uimm,rt  */
				*a++ = 0x44001800 | (alpha_rs << 21)
				    | (uimm << 13) | alpha_rt;
			} else {
				/*  xor rs,t1,rt  */
				*a++ = 0x44020800 | (alpha_rs << 21) | alpha_rt;
			}
			break;
		}
		break;
	case HI6_SLTI:
	case HI6_SLTIU:
		/*  lda t1,4660  */
		*a++ = 0x205f0000 | uimm;

		switch (instruction_type) {
		case HI6_SLTI:
			/*  cmplt rs,t1,rt  */
			*a++ = 0x400209a0 | (alpha_rs << 21) | alpha_rt;
			break;
		case HI6_SLTIU:
			/*  cmpult rs,t1,rt  */
			*a++ = 0x400203a0 | (alpha_rs << 21) | alpha_rt;
			break;
		}
		break;
	}

	if (alpha_rt == ALPHA_T0) {
		*a++ = 0x5fff041f;	/*  fnop  */
		*addrp = (unsigned char *) a;
		bintrans_move_Alpha_reg_into_MIPS_reg(addrp, ALPHA_T0, rt);
		a = (uint32_t *) *addrp;
	}

rt0:
	*addrp = (unsigned char *) a;
	bintrans_write_pc_inc(addrp);
	return 1;
}


/*
 *  bintrans_write_instruction__addu_etc():
 */
static int bintrans_write_instruction__addu_etc(unsigned char **addrp,
	int rd, int rs, int rt, int sa, int instruction_type)
{
	unsigned char *a, *unmodified = NULL;
	int load64 = 0, store = 1, ofs, alpha_rd = ALPHA_T0;

	alpha_rd = map_MIPS_to_Alpha[rd];
	if (alpha_rd < 0)
		alpha_rd = ALPHA_T0;

	switch (instruction_type) {
	case SPECIAL_DIV:
	case SPECIAL_DIVU:
		return 0;
	}

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
		if (!load64) {
			*a++ = 0x01; *a++ = 0x00; *a++ = 0x3f; *a++ = 0x40;	/*  addl t0,0,t0  */
		}
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
		*a++ = alpha_rd; *a++ = 0x00; *a++ = 0x22; *a++ = 0x40;	/*  addl t0,t1,rd  */
		break;
	case SPECIAL_DADDU:
		*a++ = alpha_rd; *a++ = 0x04; *a++ = 0x22; *a++ = 0x40;	/*  addq t0,t1,rd  */
		break;
	case SPECIAL_SUBU:
		*a++ = 0x20 + alpha_rd; *a++ = 0x01; *a++ = 0x22; *a++ = 0x40;	/*  subl t0,t1,t0  */
		break;
	case SPECIAL_DSUBU:
		*a++ = 0x20 + alpha_rd; *a++ = 0x05; *a++ = 0x22; *a++ = 0x40;	/*  subq t0,t1,t0  */
		break;
	case SPECIAL_AND:
		*a++ = alpha_rd; *a++ = 0x00; *a++ = 0x22; *a++ = 0x44;	/*  and t0,t1,t0  */
		break;
	case SPECIAL_OR:
		*a++ = alpha_rd; *a++ = 0x04; *a++ = 0x22; *a++ = 0x44;	/*  or t0,t1,t0  */
		break;
	case SPECIAL_NOR:
		*a++ = 0x01; *a++ = 0x04; *a++ = 0x22; *a++ = 0x44;	/*  or t0,t1,t0  */
		*a++ = alpha_rd; *a++ = 0x05; *a++ = 0xe1; *a++ = 0x47;	/*  not t0,t0  */
		break;
	case SPECIAL_XOR:
		*a++ = alpha_rd; *a++ = 0x08; *a++ = 0x22; *a++ = 0x44;	/*  xor t0,t1,t0  */
		break;
	case SPECIAL_SLL:
		*a++ = 0x21; *a++ = 0x17 + ((sa & 7) << 5); *a++ = 0x40 + (sa >> 3); *a++ = 0x48;	/*  sll t1,sa,t0  */
		*a++ = alpha_rd; *a++ = 0x00; *a++ = 0x3f; *a++ = 0x40;     /*  addl t0,0,t0  */
		break;
	case SPECIAL_SLLV:
		/*  rd = rt << (rs&31)  (logical)     t0 = t1 << (t0&31)  */
		*a++ = 0x01; *a++ = 0xf0; *a++ = 0x23; *a++ = 0x44;     /*  and t0,31,t0  */
		*a++ = 0x21; *a++ = 0x07; *a++ = 0x41; *a++ = 0x48;	/*  sll t1,t0,t0  */
		*a++ = alpha_rd; *a++ = 0x00; *a++ = 0x3f; *a++ = 0x40;     /*  addl t0,0,t0  */
		break;
	case SPECIAL_DSLL:
		*a++ = 0x20 + alpha_rd; *a++ = 0x17 + ((sa & 7) << 5); *a++ = 0x40 + (sa >> 3); *a++ = 0x48;	/*  sll t1,sa,t0  */
		break;
	case SPECIAL_DSLL32:
		sa += 32;
		*a++ = 0x20 + alpha_rd; *a++ = 0x17 + ((sa & 7) << 5); *a++ = 0x40 + (sa >> 3); *a++ = 0x48;	/*  sll t1,sa,t0  */
		break;
	case SPECIAL_SRA:
		*a++ = 0x81; *a++ = 0x17 + ((sa & 7) << 5); *a++ = 0x40 + (sa >> 3); *a++ = 0x48;	/*  sra t1,sa,t0  */
		*a++ = alpha_rd; *a++ = 0x00; *a++ = 0x3f; *a++ = 0x40;     /*  addl t0,0,t0  */
		break;
	case SPECIAL_SRAV:
		/*  rd = rt >> (rs&31)  (arithmetic)     t0 = t1 >> (t0&31)  */
		*a++ = 0x01; *a++ = 0xf0; *a++ = 0x23; *a++ = 0x44;     /*  and t0,31,t0  */
		*a++ = 0x81; *a++ = 0x07; *a++ = 0x41; *a++ = 0x48;	/*  sra t1,t0,t0  */
		*a++ = alpha_rd; *a++ = 0x00; *a++ = 0x3f; *a++ = 0x40;     /*  addl t0,0,t0  */
		break;
	case SPECIAL_DSRA:
		*a++ = 0x80 + alpha_rd; *a++ = 0x17 + ((sa & 7) << 5); *a++ = 0x40 + (sa >> 3); *a++ = 0x48;	/*  sra t1,sa,t0  */
		break;
	case SPECIAL_DSRA32:
		sa += 32;
		*a++ = 0x80 + alpha_rd; *a++ = 0x17 + ((sa & 7) << 5); *a++ = 0x40 + (sa >> 3); *a++ = 0x48;	/*  sra t1,sa,t0  */
		break;
	case SPECIAL_SRL:
		*a++ = 0x22; *a++ = 0xf6; *a++ = 0x41; *a++ = 0x48;	/*  zapnot t1,0xf,t1 (use only lowest 32 bits)  */
		/*  Note: bits of sa are distributed among two different bytes.  */
		*a++ = 0x81; *a++ = 0x16 + ((sa & 7) << 5); *a++ = 0x40 + (sa >> 3); *a++ = 0x48;
		*a++ = alpha_rd; *a++ = 0x00; *a++ = 0x3f; *a++ = 0x40;     /*  addl  */
		break;
	case SPECIAL_SRLV:
		/*  rd = rt >> (rs&31)  (logical)     t0 = t1 >> (t0&31)  */
		*a++ = 0x22; *a++ = 0xf6; *a++ = 0x41; *a++ = 0x48;	/*  zapnot t1,0xf,t1 (use only lowest 32 bits)  */
		*a++ = 0x01; *a++ = 0xf0; *a++ = 0x23; *a++ = 0x44;     /*  and t0,31,t0  */
		*a++ = 0x81; *a++ = 0x06; *a++ = 0x41; *a++ = 0x48;	/*  srl t1,t0,t0  */
		*a++ = alpha_rd; *a++ = 0x00; *a++ = 0x3f; *a++ = 0x40;     /*  addl t0,0,t0  */
		break;
	case SPECIAL_DSRL:
		/*  Note: bits of sa are distributed among two different bytes.  */
		*a++ = 0x80 + alpha_rd; *a++ = 0x16 + ((sa & 7) << 5); *a++ = 0x40 + (sa >> 3); *a++ = 0x48;
		break;
	case SPECIAL_DSRL32:
		/*  Note: bits of sa are distributed among two different bytes.  */
		sa += 32;
		*a++ = 0x80 + alpha_rd; *a++ = 0x16 + ((sa & 7) << 5); *a++ = 0x40 + (sa >> 3); *a++ = 0x48;
		break;
	case SPECIAL_SLT:
		*a++ = 0xa0 + alpha_rd; *a++ = 0x09; *a++ = 0x22; *a++ = 0x40;     /*  cmplt t0,t1,t0  */
		break;
	case SPECIAL_SLTU:
		*a++ = 0xa0 + alpha_rd; *a++ = 0x03; *a++ = 0x22; *a++ = 0x40;     /*  cmpult t0,t1,t0  */
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

		ofs = ((size_t)&dummy_cpu.cd.mips.lo) - (size_t)&dummy_cpu;
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb4;

		/*  81 17 64 48     sra     t2,0x20,t0  */
		*a++ = 0x81; *a++ = 0x17; *a++ = 0x64; *a++ = 0x48;
		*a++ = 0x01; *a++ = 0x00; *a++ = 0x3f; *a++ = 0x40;	/*  addl t0,0,t0  */
		ofs = ((size_t)&dummy_cpu.cd.mips.hi) - (size_t)&dummy_cpu;
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb4;
		break;
	case SPECIAL_MOVZ:
		/*  if rt=0 then rd=rs  ==>  if t1!=0 then t0=unmodified else t0=rd  */
		/*  00 00 40 f4     bne     t1,unmodified  */
		unmodified = a;
		*a++ = 0x00; *a++ = 0x00; *a++ = 0x40; *a++ = 0xf4;
		alpha_rd = ALPHA_T0;
		break;
	case SPECIAL_MOVN:
		/*  if rt!=0 then rd=rs  ==>  if t1=0 then t0=unmodified else t0=rd  */
		/*  00 00 40 e4     beq     t1,unmodified  */
		unmodified = a;
		*a++ = 0x00; *a++ = 0x00; *a++ = 0x40; *a++ = 0xe4;
		alpha_rd = ALPHA_T0;
		break;
	}

	if (store && alpha_rd == ALPHA_T0) {
		bintrans_move_Alpha_reg_into_MIPS_reg(&a, ALPHA_T0, rd);
	}

	if (unmodified != NULL)
		*unmodified = ((size_t)a - (size_t)unmodified - 4) / 4;

	*addrp = a;
rd0:
	bintrans_write_pc_inc(addrp);
	return 1;
}


/*
 *  bintrans_write_instruction__branch():
 */
static int bintrans_write_instruction__branch(unsigned char **addrp,
	int instruction_type, int regimm_type, int rt, int rs, int imm)
{
	uint32_t *a, *b, *c = NULL;
	int alpha_rs, alpha_rt, likely = 0, ofs;

	alpha_rs = map_MIPS_to_Alpha[rs];
	alpha_rt = map_MIPS_to_Alpha[rt];

	switch (instruction_type) {
	case HI6_BEQL:
	case HI6_BNEL:
	case HI6_BLEZL:
	case HI6_BGTZL:
		likely = 1;
	}

	/*
	 *  t0 = gpr[rt]; t1 = gpr[rs];
	 *
	 *  50 00 30 a4     ldq     t0,80(a0)
	 *  58 00 50 a4     ldq     t1,88(a0)
	 */

	switch (instruction_type) {
	case HI6_BEQ:
	case HI6_BNE:
	case HI6_BEQL:
	case HI6_BNEL:
		if (alpha_rt < 0) {
			bintrans_move_MIPS_reg_into_Alpha_reg(addrp, rt, ALPHA_T0);
			alpha_rt = ALPHA_T0;
		}
	}

	if (alpha_rs < 0) {
		bintrans_move_MIPS_reg_into_Alpha_reg(addrp, rs, ALPHA_T1);
		alpha_rs = ALPHA_T1;
	}

	a = (uint32_t *) *addrp;

	/*
	 *  Compare alpha_rt (t0) and alpha_rs (t1) for equality (BEQ).
	 *  If the result was false (equal to zero), then skip a lot
	 *  of instructions:
	 *
	 *  a1 05 22 40     cmpeq   t0,t1,t0
	 *  01 00 20 e4     beq     t0,14 <f+0x14>
	 */
	b = NULL;
	if ((instruction_type == HI6_BEQ ||
	     instruction_type == HI6_BEQL) && rt != rs) {
		/*  cmpeq rt,rs,t0  */
		*a++ = 0x400005a1 | (alpha_rt << 21) | (alpha_rs << 16);
		b = a;
		*a++ = 0xe4200001;	/*  beq  */
	}
	if (instruction_type == HI6_BNE || instruction_type == HI6_BNEL) {
		/*  cmpeq rt,rs,t0  */
		*a++ = 0x400005a1 | (alpha_rt << 21) | (alpha_rs << 16);
		b = a;
		*a++ = 0xf4200001;	/*  bne  */
	}
	if (instruction_type == HI6_BLEZ || instruction_type == HI6_BLEZL) {
		/*  cmple rs,0,t0  */
		*a++ = 0x40001da1 | (alpha_rs << 21);
		b = a;
		*a++ = 0xe4200001;	/*  beq  */
	}
	if (instruction_type == HI6_BGTZ || instruction_type == HI6_BGTZL) {
		/*  cmple rs,0,t0  */
		*a++ = 0x40001da1 | (alpha_rs << 21);
		b = a;
		*a++ = 0xf4200001;	/*  bne  */
	}
	if (instruction_type == HI6_REGIMM && regimm_type == REGIMM_BLTZ) {
		/*  cmplt rs,0,t0  */
		*a++ = 0x400019a1 | (alpha_rs << 21);
		b = a;
		*a++ = 0xe4200001;	/*  beq  */
	}
	if (instruction_type == HI6_REGIMM && regimm_type == REGIMM_BGEZ) {
		*a++ = 0x207fffff;	/*  lda t2,-1  */
		/*  cmple rs,t2,t0  */
		*a++ = 0x40030da1 | (alpha_rs << 21);
		b = a;
		*a++ = 0xf4200001;	/*  bne  */
	}

	/*
	 *  Perform the jump by setting cpu->delay_slot = TO_BE_DELAYED
	 *  and cpu->delay_jmpaddr = pc + 4 + (imm << 2).
	 *
	 *  04 00 26 20     lda     t0,4(t5)		add 4
	 *  c8 01 5f 20     lda     t1,456
	 *  4a 04 41 40     s4addq  t1,t0,s1		s1 = (t1<<2) + t0
	 */

	*a++ = 0x20260004;			/*  lda t0,4(t5)  */
	*a++ = 0x205f0000 | (imm & 0xffff);	/*  lda  */
	*a++ = 0x4041044a;			/*  s4addq  */

	/*  02 00 3f 21     lda     s0,TO_BE_DELAYED  */
	*a++ = 0x213f0000 | TO_BE_DELAYED;

	/*
	 *  Special case:  "likely"-branches:
	 */
	if (likely) {
		c = a;
		*a++ = 0xc3e00001;	/*  br delayed_ok  */

		if (b != NULL)
			*((unsigned char *)b) = ((size_t)a - (size_t)b - 4) / 4;

		/*  cpu->cd.mips.nullify_next = 1;  */
		/*  01 00 3f 20     lda     t0,1  */
		*a++ = 0x203f0001;
		ofs = (size_t)&dummy_cpu.cd.mips.nullify_next - (size_t)&dummy_cpu;
		*a++ = 0xb0300000 | (ofs & 0xffff);

		/*  fail, so that the next instruction is handled manually:  */
		*addrp = (unsigned char *) a;
		bintrans_write_pc_inc(addrp);
		bintrans_write_chunkreturn_fail(addrp);
		a = (uint32_t *) *addrp;

		if (c != NULL)
			*((unsigned char *)c) = ((size_t)a - (size_t)c - 4) / 4;
	} else {
		/*  Normal (non-likely) exit:  */
		if (b != NULL)
			*((unsigned char *)b) = ((size_t)a - (size_t)b - 4) / 4;
	}

	*addrp = (unsigned char *) a;
	bintrans_write_pc_inc(addrp);
	return 1;
}


/*
 *  bintrans_write_instruction__jr():
 */
static int bintrans_write_instruction__jr(unsigned char **addrp, int rs, int rd, int special)
{
	uint32_t *a;
	int alpha_rd;

	alpha_rd = map_MIPS_to_Alpha[rd];
	if (alpha_rd < 0)
		alpha_rd = ALPHA_T0;

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
		/*  lda alpha_rd,8(t5)  */
		*a++ = 0x20060008 | (alpha_rd << 21);
		*addrp = (unsigned char *) a;
		if (alpha_rd == ALPHA_T0)
			bintrans_move_Alpha_reg_into_MIPS_reg(addrp, ALPHA_T0, rd);
	}

	bintrans_write_pc_inc(addrp);
	return 1;
}


/*
 *  bintrans_write_instruction__jal():
 */
static int bintrans_write_instruction__jal(unsigned char **addrp,
	int imm, int link)
{
	uint32_t *a;

	a = (uint32_t *) *addrp;

	/*  gpr[31] = retaddr   (NOTE: mips register 31 is in alpha reg s3)  */
	if (link) {
		*a++ = 0x21860008;	/*  lda s3,8(t5)  */
	}

	/*  Set the jmpaddr to top 4 bits of pc + lowest 28 bits of imm*4:  */

	/*
	 *  imm = 4*imm;
	 *  t0 = ((pc + 4) & ~0x0fffffff) | imm;
	 *
	 *  04 00 26 20     lda     t0,4(t5)	<-- because the jump is from the delay slot
	 *  23 01 5f 24     ldah    t1,291
	 *  67 45 42 20     lda     t1,17767(t1)
	 *  00 f0 7f 24     ldah    t2,-4096
	 *  04 00 23 44     and     t0,t2,t3
	 *  0a 04 44 44     or      t1,t3,s1
	 */
	imm *= 4;
	*a++ = 0x20260004;
	*a++ = 0x245f0000 | ((imm >> 16) + (imm & 0x8000? 1 : 0));
	*a++ = 0x20420000 | (imm & 0xffff);
	*a++ = 0x247ff000;
	*a++ = 0x44230004;
	*a++ = 0x4444040a;

	/*  02 00 3f 21     lda     s0,TO_BE_DELAYED  */
	*a++ = 0x213f0000 | TO_BE_DELAYED;

	/*  If the machine continues executing here, it will return
	    to the main loop, which is fine.  */

	*addrp = (unsigned char *) a;
	bintrans_write_pc_inc(addrp);
	return 1;
}


/*
 *  bintrans_write_instruction__delayedbranch():
 */
static int bintrans_write_instruction__delayedbranch(
	struct memory *mem, unsigned char **addrp,
	uint32_t *potential_chunk_p, uint32_t *chunks,
	int only_care_about_chunk_p, int p, int forward)
{
	unsigned char *a, *skip=NULL, *generic64bit;
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
		if (mem->bintrans_32bit_only) {
			/*  34 12 70 a7     ldq     t12,4660(a0)  */
			ofs = (size_t)&dummy_cpu.cd.mips.bintrans_jump_to_32bit_pc - (size_t)&dummy_cpu;
			*a++ = ofs; *a++ = ofs >> 8; *a++ = 0x70; *a++ = 0xa7;

			/*  00 00 fb 6b     jmp     (t12)  */
			*a++ = 0; *a++ = 0; *a++ = 0xfb; *a++ = 0x6b;
		} else {
			/*
			 *  If the highest 32 bits of the address are either
			 *  0x00000000 or 0xffffffff, then the tables used for
			 *  32-bit load/stores can be used.
			 *
			 *  81 16 24 4a     srl     a1,0x20,t0
			 *  03 00 20 e4     beq     t0,14 <ok1>
			 *  01 30 20 40     addl    t0,0x1,t0
			 *  01 00 20 e4     beq     t0,14 <ok1>
			 *  01 00 e0 c3     br      18 <nook>
			 */
			*a++ = 0x81; *a++ = 0x16; *a++ = 0x24; *a++ = 0x4a;
			*a++ = 0x03; *a++ = 0x00; *a++ = 0x20; *a++ = 0xe4;
			*a++ = 0x01; *a++ = 0x30; *a++ = 0x20; *a++ = 0x40;
			*a++ = 0x01; *a++ = 0x00; *a++ = 0x20; *a++ = 0xe4;
			generic64bit = a;
			*a++ = 0x01; *a++ = 0x00; *a++ = 0xe0; *a++ = 0xc3;

			/*  34 12 70 a7     ldq     t12,4660(a0)  */
			ofs = (size_t)&dummy_cpu.cd.mips.bintrans_jump_to_32bit_pc - (size_t)&dummy_cpu;
			*a++ = ofs; *a++ = ofs >> 8; *a++ = 0x70; *a++ = 0xa7;

			/*  00 00 fb 6b     jmp     (t12)  */
			*a++ = 0; *a++ = 0; *a++ = 0xfb; *a++ = 0x6b;


			if (generic64bit != NULL)
				*generic64bit = ((size_t)a - (size_t)generic64bit - 4) / 4;

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

			/*  ldq t2,chunk_base_address(a0)  */
			ofs = ((size_t)&dummy_cpu.cd.mips.chunk_base_address) - (size_t)&dummy_cpu;
			*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x70; *a++ = 0xa4;
			/*  addq t0,t2,t0  */
			*a++ = 0x01; *a++ = 0x04; *a++ = 0x23; *a++ = 0x40;

			/*  00 00 e1 6b     jmp     (t0)  */
			*a++ = 0x00; *a++ = 0x00; *a++ = 0xe1; *a++ = 0x6b;	/*  jmp (t0)  */

			/*  Failure, then return to the main loop.  */
			*a++ = 0x01; *a++ = 0x80; *a++ = 0xfa; *a++ = 0x6b;	/*  ret  */
		}
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
		if (!only_care_about_chunk_p && !forward) {
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
		alpha_addr = *potential_chunk_p + (size_t)mem->translation_code_chunk_space;
		ofs = (alpha_addr - ((size_t)a+4)) / 4;
		/*  printf("%016llx %016llx %i\n", (long long)alpha_addr, (long long)a, ofs);  */

		if ((*potential_chunk_p) != 0 && ofs > -0xfffff && ofs < 0xfffff) {
			*a++ = ofs & 255; *a++ = (ofs >> 8) & 255; *a++ = 0xe0 + ((ofs >> 16) & 0x1f); *a++ = 0xc3;	/*  br <chunk>  */
		} else {
			/*  Case 2:  */

			bintrans_register_potential_quick_jump(mem, a, p);

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

			/*  ldq t2,chunk_base_address(a0)  */
			ofs = ((size_t)&dummy_cpu.cd.mips.chunk_base_address) - (size_t)&dummy_cpu;
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
static int bintrans_write_instruction__loadstore(
	struct memory *mem, unsigned char **addrp,
	int rt, int imm, int rs, int instruction_type, int bigendian)
{
	unsigned char *a, *fail, *generic64bit = NULL, *generic64bitA = NULL;
	unsigned char *doloadstore = NULL,
	    *ok_unaligned_load3, *ok_unaligned_load2, *ok_unaligned_load1;
	uint32_t *b;
	int ofs, alignment, load = 0, alpha_rs, alpha_rt, unaligned = 0;

	/*  TODO: Not yet:  */
	if (instruction_type == HI6_LQ_MDMX || instruction_type == HI6_SQ) {
		return 0;
	}

	switch (instruction_type) {
	case HI6_LQ_MDMX:
	case HI6_LD:
	case HI6_LDL:
	case HI6_LDR:
	case HI6_LWU:
	case HI6_LW:
	case HI6_LWL:
	case HI6_LWR:
	case HI6_LHU:
	case HI6_LH:
	case HI6_LBU:
	case HI6_LB:
		load = 1;
		if (rt == 0)
			return 0;
	}

	switch (instruction_type) {
	case HI6_LDL:
	case HI6_LDR:
	case HI6_LWL:
	case HI6_LWR:
	case HI6_SDL:
	case HI6_SDR:
	case HI6_SWL:
	case HI6_SWR:
		unaligned = 1;
	}

	a = *addrp;

	/*
	 *  a1 = gpr[rs] + imm;
	 *
	 *  88 08 30 a4     ldq     t0,2184(a0)
	 *  34 12 21 22     lda     a1,4660(t0)
	 */

	alpha_rs = map_MIPS_to_Alpha[rs];
	if (alpha_rs < 0) {
		bintrans_move_MIPS_reg_into_Alpha_reg(&a, rs, ALPHA_T0);
		alpha_rs = ALPHA_T0;
	}
	*a++ = imm; *a++ = (imm >> 8); *a++ = 0x20 + alpha_rs; *a++ = 0x22;

	alignment = 0;
	switch (instruction_type) {
	case HI6_LQ_MDMX:
	case HI6_SQ:
		alignment = 15;
		break;
	case HI6_LD:
	case HI6_LDL:
	case HI6_LDR:
	case HI6_SD:
	case HI6_SDL:
	case HI6_SDR:
		alignment = 7;
		break;
	case HI6_LW:
	case HI6_LWL:
	case HI6_LWR:
	case HI6_LWU:
	case HI6_SW:
	case HI6_SWL:
	case HI6_SWR:
		alignment = 3;
		break;
	case HI6_LH:
	case HI6_LHU:
	case HI6_SH:
		alignment = 1;
		break;
	}

	if (unaligned) {
		/*
		 *  Unaligned load/store:  Perform the host load/store at
		 *  an aligned address, and then figure out which bytes to
		 *  actually load into the destination register.
		 *
		 *  02 30 20 46     and     a1,alignment,t1
		 *  31 05 22 42     subq    a1,t1,a1
		 */
		*a++ = 0x02; *a++ = 0x10 + alignment * 0x20; *a++ = 0x20 + (alignment >> 3); *a++ = 0x46;
		*a++ = 0x31; *a++ = 0x05; *a++ = 0x22; *a++ = 0x42;
	} else if (alignment > 0) {
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

	alpha_rt = map_MIPS_to_Alpha[rt];

	if (mem->bintrans_32bit_only) {
		/*  Special case for 32-bit addressing:  */

		ofs = ((size_t)&dummy_cpu.cd.mips.bintrans_loadstore_32bit) - (size_t)&dummy_cpu;
		/*  ldq t12,bintrans_loadstore_32bit(a0)  */
		*a++ = ofs; *a++ = ofs >> 8; *a++ = 0x70; *a++ = 0xa7;

		/*  jsr t4,(t12),<after>  */
		*a++ = 0x00; *a++ = 0x40; *a++ = 0xbb; *a++ = 0x68;

		/*
		 *  Now:
		 *	a3 = host page
		 *	t0 = 0 for readonly pages, 1 for read/write pages
		 *	t3 = address of host load/store
		 */

		/*  If this is a store, then the lowest bit must be set:  */
		if (!load) {
			/*  01 00 20 f4     bne     t0,<okzzz>  */
			fail = a;
			*a++ = 0x01; *a++ = 0x00; *a++ = 0x20; *a++ = 0xf4;
			bintrans_write_chunkreturn_fail(&a);
			*fail = ((size_t)a - (size_t)fail - 4) / 4;
		}
	} else {
		/*
		 *  If the highest 33 bits of the address are either all ones
		 *  or all zeroes, then the tables used for 32-bit load/stores
		 *  can be used.
		 */
		*a++ = 0x81; *a++ = 0xf6; *a++ = 0x23; *a++ = 0x4a;	/*  srl a1,0x1f,t0  */
		*a++ = 0x01; *a++ = 0x30; *a++ = 0x20; *a++ = 0x44;	/*  and t0,0x1,t0  */
		*a++ = 0x04; *a++ = 0x00; *a++ = 0x20; *a++ = 0xe4;	/*  beq t0,<noll>  */
		*a++ = 0x81; *a++ = 0x16; *a++ = 0x24; *a++ = 0x4a;	/*  srl a1,0x20,t0  */
		*a++ = 0x01; *a++ = 0x30; *a++ = 0x20; *a++ = 0x40;	/*  addl t0,0x1,t0  */
		*a++ = 0x04; *a++ = 0x00; *a++ = 0x20; *a++ = 0xe4;	/*  beq t0,<ok>  */
		generic64bit = a;
		*a++ = 0x04; *a++ = 0x00; *a++ = 0xe0; *a++ = 0xc3;	/*  br <generic>  */
		/*  <noll>:  */
		*a++ = 0x81; *a++ = 0x16; *a++ = 0x24; *a++ = 0x4a;	/*  srl a1,0x20,t0  */
		*a++ = 0x01; *a++ = 0x00; *a++ = 0x20; *a++ = 0xe4;	/*  beq t0,<ok>  */
		generic64bitA = a;
		*a++ = 0x04; *a++ = 0x00; *a++ = 0xe0; *a++ = 0xc3;	/*  br <generic>  */

		ofs = ((size_t)&dummy_cpu.cd.mips.bintrans_loadstore_32bit) - (size_t)&dummy_cpu;
		/*  ldq t12,bintrans_loadstore_32bit(a0)  */
		*a++ = ofs; *a++ = ofs >> 8; *a++ = 0x70; *a++ = 0xa7;

		/*  jsr t4,(t12),<after>  */
		*a++ = 0x00; *a++ = 0x40; *a++ = 0xbb; *a++ = 0x68;

		/*
		 *  Now:
		 *	a3 = host page  (or NULL if not found)
		 *	t0 = 0 for readonly pages, 1 for read/write pages
		 *	t3 = (potential) address of host load/store
		 */

		/*  If this is a store, then the lowest bit must be set:  */
		if (!load) {
			/*  01 00 20 f4     bne     t0,<okzzz>  */
			fail = a;
			*a++ = 0x01; *a++ = 0x00; *a++ = 0x20; *a++ = 0xf4;
			bintrans_write_chunkreturn_fail(&a);
			*fail = ((size_t)a - (size_t)fail - 4) / 4;
		}

		doloadstore = a;
		*a++ = 0x01; *a++ = 0x00; *a++ = 0xe0; *a++ = 0xc3;


		/*
		 *  Generic (64-bit) load/store:
		 */

		if (generic64bit != NULL)
			*generic64bit = ((size_t)a - (size_t)generic64bit - 4) / 4;
		if (generic64bitA != NULL)
			*generic64bitA = ((size_t)a - (size_t)generic64bitA - 4) / 4;

		*addrp = a;
		b = (uint32_t *) *addrp;

		/*  Save a0 and the old return address on the stack:  */
		*b++ = 0x23deff80;		/*  lda sp,-128(sp)  */

		*b++ = 0xb75e0000;		/*  stq ra,0(sp)  */
		*b++ = 0xb61e0008;		/*  stq a0,8(sp)  */
		*b++ = 0xb4de0010;		/*  stq t5,16(sp)  */
		*b++ = 0xb0fe0018;		/*  stl t6,24(sp)  */
		*b++ = 0xb71e0020;		/*  stq t10,32(sp)  */
		*b++ = 0xb73e0028;		/*  stq t11,40(sp)  */
		*b++ = 0xb51e0030;		/*  stq t7,48(sp)  */
		*b++ = 0xb6de0038;		/*  stq t8,56(sp)  */
		*b++ = 0xb6fe0040;		/*  stq t9,64(sp)  */

		ofs = ((size_t)&dummy_cpu.cd.mips.fast_vaddr_to_hostaddr) - (size_t)&dummy_cpu;

		*b++ = 0xa7700000 | ofs;	/*  ldq t12,0(a0)  */

		/*  a1 is already vaddr. set a2 = writeflag  */
		*b++ = 0x225f0000 | (load? 0 : 1);

		/*  Call fast_vaddr_to_hostaddr:  */
		*b++ = 0x6b5b4000;		/*  jsr ra,(t12),<after>  */

		/*  Restore the old return address and a0 from the stack:  */
		*b++ = 0xa75e0000;		/*  ldq ra,0(sp)  */
		*b++ = 0xa61e0008;		/*  ldq a0,8(sp)  */
		*b++ = 0xa4de0010;		/*  ldq t5,16(sp)  */
		*b++ = 0xa0fe0018;		/*  ldl t6,24(sp)  */
		*b++ = 0xa71e0020;		/*  ldq t10,32(sp)  */
		*b++ = 0xa73e0028;		/*  ldq t11,40(sp)  */
		*b++ = 0xa51e0030;		/*  ldq t7,48(sp)  */
		*b++ = 0xa6de0038;		/*  ldq t8,56(sp)  */
		*b++ = 0xa6fe0040;		/*  ldq t9,64(sp)  */

		*b++ = 0x23de0080;		/*  lda sp,128(sp)  */

		*addrp = (unsigned char *) b;
		a = *addrp;

		/*
		 *  NULL? Then return failure.
		 *  01 00 00 f4     bne     v0,f8 <okzz>
		 */
		fail = a;
		*a++ = 0x01; *a++ = 0x00; *a++ = 0x00; *a++ = 0xf4;
		bintrans_write_chunkreturn_fail(&a);
		*fail = ((size_t)a - (size_t)fail - 4) / 4;

		/*  The rest of this code was written with t3 as the address.  */

		/*  04 14 00 40     addq    v0,0,t3  */
		*a++ = 0x04; *a++ = 0x14; *a++ = 0x00; *a++ = 0x40;

		if (doloadstore != NULL)
			*doloadstore = ((size_t)a - (size_t)doloadstore - 4) / 4;
	}


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
		if (alpha_rt < 0 || bigendian || instruction_type == HI6_LWU)
			alpha_rt = ALPHA_T0;
		/*  ldl rt,0(t3)  */
		*a++ = 0x00; *a++ = 0x00; *a++ = 0x04 | ((alpha_rt & 7) << 5);
		    *a++ = 0xa0 | ((alpha_rt >> 3) & 3);
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
		if (alpha_rt == ALPHA_T0)
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
		if (alpha_rt < 0)
			alpha_rt = ALPHA_T0;
		/*  ldbu rt,0(t3)  */
		*a++ = 0x00; *a++ = 0x00; *a++ = 0x04 | ((alpha_rt & 7) << 5);
		    *a++ = 0x28 | ((alpha_rt >> 3) & 3);
		if (instruction_type == HI6_LB) {
			/*  sextb rt,rt  */
			*a++ = alpha_rt; *a++ = 0x00; *a++ = 0xe0 + alpha_rt; *a++ = 0x73;
		}
		if (alpha_rt == ALPHA_T0)
			bintrans_move_Alpha_reg_into_MIPS_reg(&a, ALPHA_T0, rt);
		break;

	case HI6_LWL:
		/*  a1 = 0..3 (or 0..7 for 64-bit loads):  */
		alpha_rs = map_MIPS_to_Alpha[rs];
		if (alpha_rs < 0) {
			bintrans_move_MIPS_reg_into_Alpha_reg(&a, rs, ALPHA_T0);
			alpha_rs = ALPHA_T0;
		}
		*a++ = imm; *a++ = (imm >> 8); *a++ = 0x20 + alpha_rs; *a++ = 0x22;
		/*  02 30 20 46     and     a1,alignment,t1  */
		*a++ = 0x02; *a++ = 0x10 + alignment * 0x20; *a++ = 0x20 + (alignment >> 3); *a++ = 0x46;

		/*  ldl t0,0(t3)  */
		*a++ = 0x00; *a++ = 0x00; *a++ = 0x24; *a++ = 0xa0;

		if (bigendian) {
			/*  TODO  */
			bintrans_write_chunkreturn_fail(&a);
		}
		/*
		 *  lwl:  memory = 0x12 0x34 0x56 0x78
		 *	offset (a1):	register rt becomes:
		 *	0		0x12......
		 *	1		0x3412....
		 *	2		0x563412..
		 *	3		0x78563412
		 */

		bintrans_move_MIPS_reg_into_Alpha_reg(&a, rt, ALPHA_T2);

/*
  10:   03 00 9f 20     lda     t3,3
  14:   a5 05 82 40     cmpeq   t3,t1,t4
  18:   01 00 a0 e4     beq     t4,20 <skip>
*/
*a++ = 0x03; *a++ = 0x00; *a++ = 0x9f; *a++ = 0x20;
*a++ = 0xa5; *a++ = 0x05; *a++ = 0x82; *a++ = 0x40;
*a++ = 0x02; *a++ = 0x00; *a++ = 0xa0; *a++ = 0xe4;

/* 03 14 20 40     addq    t0,0,t2  */
*a++ = 0x03; *a++ = 0x14; *a++ = 0x20; *a++ = 0x40;

ok_unaligned_load3 = a;
*a++ = 0x01; *a++ = 0x00; *a++ = 0xe0; *a++ = 0xc3;



*a++ = 0x02; *a++ = 0x00; *a++ = 0x9f; *a++ = 0x20;
*a++ = 0xa5; *a++ = 0x05; *a++ = 0x82; *a++ = 0x40;
*a++ = 0x05; *a++ = 0x00; *a++ = 0xa0; *a++ = 0xe4;
/*
		 *	2		0x563412..
  2c:   21 17 21 48     sll     t0,0x8,t0
  30:   01 10 20 40     addl    t0,0,t0
  34:   03 f0 7f 44     and     t2,0xff,t2
  38:   03 04 23 44     or      t0,t2,t2
*/
*a++ = 0x21; *a++ = 0x17; *a++ = 0x21; *a++ = 0x48;
*a++ = 0x01; *a++ = 0x10; *a++ = 0x20; *a++ = 0x40;
*a++ = 0x03; *a++ = 0xf0; *a++ = 0x7f; *a++ = 0x44;
*a++ = 0x03; *a++ = 0x04; *a++ = 0x23; *a++ = 0x44;

ok_unaligned_load2 = a;
*a++ = 0x01; *a++ = 0x00; *a++ = 0xe0; *a++ = 0xc3;



*a++ = 0x01; *a++ = 0x00; *a++ = 0x9f; *a++ = 0x20;
*a++ = 0xa5; *a++ = 0x05; *a++ = 0x82; *a++ = 0x40;
*a++ = 0x05; *a++ = 0x00; *a++ = 0xa0; *a++ = 0xe4;
/*
		 *	1		0x3412....
  2c:   21 17 22 48     sll     t0,0x10,t0
  30:   01 10 20 40     addl    t0,0,t0
  34:   23 76 60 48     zapnot  t2,0x3,t2
  38:   03 04 23 44     or      t0,t2,t2
*/
*a++ = 0x21; *a++ = 0x17; *a++ = 0x22; *a++ = 0x48;
*a++ = 0x01; *a++ = 0x10; *a++ = 0x20; *a++ = 0x40;
*a++ = 0x23; *a++ = 0x76; *a++ = 0x60; *a++ = 0x48;
*a++ = 0x03; *a++ = 0x04; *a++ = 0x23; *a++ = 0x44;

ok_unaligned_load1 = a;
*a++ = 0x01; *a++ = 0x00; *a++ = 0xe0; *a++ = 0xc3;




/*
		 *	0		0x12......
  2c:   21 17 23 48     sll     t0,0x18,t0
  30:   01 10 20 40     addl    t0,0,t0
  34:   23 f6 60 48     zapnot  t2,0x7,t2
  38:   03 04 23 44     or      t0,t2,t2
*/
*a++ = 0x21; *a++ = 0x17; *a++ = 0x23; *a++ = 0x48;
*a++ = 0x01; *a++ = 0x10; *a++ = 0x20; *a++ = 0x40;
*a++ = 0x23; *a++ = 0xf6; *a++ = 0x60; *a++ = 0x48;
*a++ = 0x03; *a++ = 0x04; *a++ = 0x23; *a++ = 0x44;


		*ok_unaligned_load3 = ((size_t)a - (size_t)ok_unaligned_load3 - 4) / 4;
		*ok_unaligned_load2 = ((size_t)a - (size_t)ok_unaligned_load2 - 4) / 4;
		*ok_unaligned_load1 = ((size_t)a - (size_t)ok_unaligned_load1 - 4) / 4;

/*  03 10 60 40     addl    t2,0,t2  */
*a++ = 0x03; *a++ = 0x10; *a++ = 0x60; *a++ = 0x40;

		bintrans_move_Alpha_reg_into_MIPS_reg(&a, ALPHA_T2, rt);
		break;

	case HI6_LWR:
		/*  a1 = 0..3 (or 0..7 for 64-bit loads):  */
		alpha_rs = map_MIPS_to_Alpha[rs];
		if (alpha_rs < 0) {
			bintrans_move_MIPS_reg_into_Alpha_reg(&a, rs, ALPHA_T0);
			alpha_rs = ALPHA_T0;
		}
		*a++ = imm; *a++ = (imm >> 8); *a++ = 0x20 + alpha_rs; *a++ = 0x22;
		/*  02 30 20 46     and     a1,alignment,t1  */
		*a++ = 0x02; *a++ = 0x10 + alignment * 0x20; *a++ = 0x20 + (alignment >> 3); *a++ = 0x46;

		/*  ldl t0,0(t3)  */
		*a++ = 0x00; *a++ = 0x00; *a++ = 0x24; *a++ = 0xa0;

		if (bigendian) {
			/*  TODO  */
			bintrans_write_chunkreturn_fail(&a);
		}
		/*
		 *  lwr:  memory = 0x12 0x34 0x56 0x78
		 *	offset (a1):	register rt becomes:
		 *	0		0x78563412
		 *	1		0x..785634
		 *	2		0x....7856
		 *	3		0x......78
		 */

		bintrans_move_MIPS_reg_into_Alpha_reg(&a, rt, ALPHA_T2);

/*
  10:   03 00 9f 20     lda     t3,3
  14:   a5 05 82 40     cmpeq   t3,t1,t4
  18:   01 00 a0 e4     beq     t4,20 <skip>
*/
*a++ = 0x03; *a++ = 0x00; *a++ = 0x9f; *a++ = 0x20;
*a++ = 0xa5; *a++ = 0x05; *a++ = 0x82; *a++ = 0x40;
*a++ = 0x05; *a++ = 0x00; *a++ = 0xa0; *a++ = 0xe4;

/*
  2c:   81 16 23 48     srl     t0,0x18,t0
  b0:   21 36 20 48     zapnot  t0,0x1,t0
  34:   23 d6 7f 48     zapnot  t2,0xfe,t2
  38:   03 04 23 44     or      t0,t2,t2
*/
*a++ = 0x81; *a++ = 0x16; *a++ = 0x23; *a++ = 0x48;
*a++ = 0x21; *a++ = 0x36; *a++ = 0x20; *a++ = 0x48;
*a++ = 0x23; *a++ = 0xd6; *a++ = 0x7f; *a++ = 0x48;
*a++ = 0x03; *a++ = 0x04; *a++ = 0x23; *a++ = 0x44;

ok_unaligned_load3 = a;
*a++ = 0x01; *a++ = 0x00; *a++ = 0xe0; *a++ = 0xc3;



*a++ = 0x02; *a++ = 0x00; *a++ = 0x9f; *a++ = 0x20;
*a++ = 0xa5; *a++ = 0x05; *a++ = 0x82; *a++ = 0x40;
*a++ = 0x05; *a++ = 0x00; *a++ = 0xa0; *a++ = 0xe4;
/*
  2c:   81 16 22 48     srl     t0,0x10,t0
  b4:   21 76 20 48     zapnot  t0,0x3,t0
  34:   23 96 7f 48     zapnot  t2,0xfc,t2
  38:   03 04 23 44     or      t0,t2,t2
*/
*a++ = 0x81; *a++ = 0x16; *a++ = 0x22; *a++ = 0x48;
*a++ = 0x21; *a++ = 0x76; *a++ = 0x20; *a++ = 0x48;
*a++ = 0x23; *a++ = 0x96; *a++ = 0x7f; *a++ = 0x48;
*a++ = 0x03; *a++ = 0x04; *a++ = 0x23; *a++ = 0x44;

ok_unaligned_load2 = a;
*a++ = 0x01; *a++ = 0x00; *a++ = 0xe0; *a++ = 0xc3;



*a++ = 0x01; *a++ = 0x00; *a++ = 0x9f; *a++ = 0x20;
*a++ = 0xa5; *a++ = 0x05; *a++ = 0x82; *a++ = 0x40;
*a++ = 0x05; *a++ = 0x00; *a++ = 0xa0; *a++ = 0xe4;
/*
  2c:   81 16 21 48     srl     t0,0x8,t0
  b8:   21 f6 20 48     zapnot  t0,0x7,t0
  3c:   23 16 7f 48     zapnot  t2,0xf8,t2
  40:   03 04 23 44     or      t0,t2,t2
*/
*a++ = 0x81; *a++ = 0x16; *a++ = 0x21; *a++ = 0x48;
*a++ = 0x21; *a++ = 0xf6; *a++ = 0x20; *a++ = 0x48;
*a++ = 0x23; *a++ = 0x16; *a++ = 0x7f; *a++ = 0x48;
*a++ = 0x03; *a++ = 0x04; *a++ = 0x23; *a++ = 0x44;

ok_unaligned_load1 = a;
*a++ = 0x01; *a++ = 0x00; *a++ = 0xe0; *a++ = 0xc3;




/*
		 *	0		0x12......
*/
/* 03 14 20 40     addq    t0,0,t2  */
*a++ = 0x03; *a++ = 0x14; *a++ = 0x20; *a++ = 0x40;



		*ok_unaligned_load3 = ((size_t)a - (size_t)ok_unaligned_load3 - 4) / 4;
		*ok_unaligned_load2 = ((size_t)a - (size_t)ok_unaligned_load2 - 4) / 4;
		*ok_unaligned_load1 = ((size_t)a - (size_t)ok_unaligned_load1 - 4) / 4;

/*  03 10 60 40     addl    t2,0,t2  */
*a++ = 0x03; *a++ = 0x10; *a++ = 0x60; *a++ = 0x40;

		bintrans_move_Alpha_reg_into_MIPS_reg(&a, ALPHA_T2, rt);
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
		if (alpha_rt < 0 || bigendian) {
			bintrans_move_MIPS_reg_into_Alpha_reg(&a, rt, ALPHA_T0);
			alpha_rt = ALPHA_T0;
		}
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
		/*  stl to memory:  stl rt,0(t3)  */
		*a++ = 0x00; *a++ = 0x00; *a++ = 0x04 | ((alpha_rt & 7) << 5);
		    *a++ = 0xb0 | ((alpha_rt >> 3) & 3);
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
		if (alpha_rt < 0) {
			bintrans_move_MIPS_reg_into_Alpha_reg(&a, rt, ALPHA_T0);
			alpha_rt = ALPHA_T0;
		}
		/*  stb to memory:  stb rt,0(t3)  */
		*a++ = 0x00; *a++ = 0x00; *a++ = 0x04 | ((alpha_rt & 7) << 5);
		    *a++ = 0x38 | ((alpha_rt >> 3) & 3);
		break;

	case HI6_SWL:
		/*  a1 = 0..3 (or 0..7 for 64-bit stores):  */
		alpha_rs = map_MIPS_to_Alpha[rs];
		if (alpha_rs < 0) {
			bintrans_move_MIPS_reg_into_Alpha_reg(&a, rs, ALPHA_T0);
			alpha_rs = ALPHA_T0;
		}
		*a++ = imm; *a++ = (imm >> 8); *a++ = 0x20 + alpha_rs; *a++ = 0x22;
		/*  02 30 20 46     and     a1,alignment,t1  */
		*a++ = 0x02; *a++ = 0x10 + alignment * 0x20; *a++ = 0x20 + (alignment >> 3); *a++ = 0x46;

		/*  ldl t0,0(t3)  */
		*a++ = 0x00; *a++ = 0x00; *a++ = 0x24; *a++ = 0xa0;

		if (bigendian) {
			/*  TODO  */
			bintrans_write_chunkreturn_fail(&a);
		}

		bintrans_move_MIPS_reg_into_Alpha_reg(&a, rt, ALPHA_T2);

		/*
		 *  swl:	memory = 0x12 0x34 0x56 0x78
		 *		register = 0x89abcdef
		 *	offset (a1):	memory becomes:
		 *	0		0x89 0x.. 0x.. 0x..
		 *	1		0xab 0x89 0x.. 0x..
		 *	2		0xcd 0xab 0x89 0x..
		 *	3		0xef 0xcd 0xab 0x89
		 */

/*
  a5 75 40 40     cmpeq   t1,0x03,t4
  01 00 a0 e4     beq     t4,20 <skip>
*/
*a++ = 0xa5; *a++ = 0x75; *a++ = 0x40; *a++ = 0x40;
*a++ = 0x02; *a++ = 0x00; *a++ = 0xa0; *a++ = 0xe4;

/*  01 10 60 40     addl    t2,0,t0  */
*a++ = 0x01; *a++ = 0x10; *a++ = 0x60; *a++ = 0x40;

ok_unaligned_load3 = a;
*a++ = 0x01; *a++ = 0x00; *a++ = 0xe0; *a++ = 0xc3;




*a++ = 0xa5; *a++ = 0x55; *a++ = 0x40; *a++ = 0x40;
*a++ = 0x05; *a++ = 0x00; *a++ = 0xa0; *a++ = 0xe4;
/*
  2:
  e8:   83 16 61 48     srl     t2,0x8,t2
  ec:   23 f6 60 48     zapnot  t2,0x7,t2
  f0:   21 16 3f 48     zapnot  t0,0xf8,t0
  f4:   01 04 23 44     or      t0,t2,t0
*/
*a++ = 0x83; *a++ = 0x16; *a++ = 0x61; *a++ = 0x48;
*a++ = 0x23; *a++ = 0xf6; *a++ = 0x60; *a++ = 0x48;
*a++ = 0x21; *a++ = 0x16; *a++ = 0x3f; *a++ = 0x48;
*a++ = 0x01; *a++ = 0x04; *a++ = 0x23; *a++ = 0x44;

ok_unaligned_load2 = a;
*a++ = 0x01; *a++ = 0x00; *a++ = 0xe0; *a++ = 0xc3;



*a++ = 0xa5; *a++ = 0x35; *a++ = 0x40; *a++ = 0x40;
*a++ = 0x05; *a++ = 0x00; *a++ = 0xa0; *a++ = 0xe4;
/*
  1:
  f8:   83 16 62 48     srl     t2,0x10,t2
  fc:   23 76 60 48     zapnot  t2,0x3,t2
 100:   21 96 3f 48     zapnot  t0,0xfc,t0
 104:   01 04 23 44     or      t0,t2,t0
*/
*a++ = 0x83; *a++ = 0x16; *a++ = 0x62; *a++ = 0x48;
*a++ = 0x23; *a++ = 0x76; *a++ = 0x60; *a++ = 0x48;
*a++ = 0x21; *a++ = 0x96; *a++ = 0x3f; *a++ = 0x48;
*a++ = 0x01; *a++ = 0x04; *a++ = 0x23; *a++ = 0x44;

ok_unaligned_load1 = a;
*a++ = 0x01; *a++ = 0x00; *a++ = 0xe0; *a++ = 0xc3;





/*
 0:
 108:   83 16 63 48     srl     t2,0x18,t2
 10c:   23 36 60 48     zapnot  t2,0x1,t2
 110:   21 d6 3f 48     zapnot  t0,0xfe,t0
 114:   01 04 23 44     or      t0,t2,t0
*/
*a++ = 0x83; *a++ = 0x16; *a++ = 0x63; *a++ = 0x48;
*a++ = 0x23; *a++ = 0x36; *a++ = 0x60; *a++ = 0x48;
*a++ = 0x21; *a++ = 0xd6; *a++ = 0x3f; *a++ = 0x48;
*a++ = 0x01; *a++ = 0x04; *a++ = 0x23; *a++ = 0x44;


		*ok_unaligned_load3 = ((size_t)a - (size_t)ok_unaligned_load3 - 4) / 4;
		*ok_unaligned_load2 = ((size_t)a - (size_t)ok_unaligned_load2 - 4) / 4;
		*ok_unaligned_load1 = ((size_t)a - (size_t)ok_unaligned_load1 - 4) / 4;

		/*  sdl t0,0(t3)  */
		*a++ = 0x00; *a++ = 0x00; *a++ = 0x24; *a++ = 0xb0;
		break;

	case HI6_SWR:
		/*  a1 = 0..3 (or 0..7 for 64-bit stores):  */
		alpha_rs = map_MIPS_to_Alpha[rs];
		if (alpha_rs < 0) {
			bintrans_move_MIPS_reg_into_Alpha_reg(&a, rs, ALPHA_T0);
			alpha_rs = ALPHA_T0;
		}
		*a++ = imm; *a++ = (imm >> 8); *a++ = 0x20 + alpha_rs; *a++ = 0x22;
		/*  02 30 20 46     and     a1,alignment,t1  */
		*a++ = 0x02; *a++ = 0x10 + alignment * 0x20; *a++ = 0x20 + (alignment >> 3); *a++ = 0x46;

		/*  ldl t0,0(t3)  */
		*a++ = 0x00; *a++ = 0x00; *a++ = 0x24; *a++ = 0xa0;

		if (bigendian) {
			/*  TODO  */
			bintrans_write_chunkreturn_fail(&a);
		}

		bintrans_move_MIPS_reg_into_Alpha_reg(&a, rt, ALPHA_T2);

		/*
		 *  swr:	memory = 0x12 0x34 0x56 0x78
		 *		register = 0x89abcdef
		 *	offset (a1):	memory becomes:
		 *	0		0xef 0xcd 0xab 0x89
		 *	1		0x.. 0xef 0xcd 0xab
		 *	2		0x.. 0x.. 0xef 0xcd
		 *	3		0x.. 0x.. 0x.. 0xef
		 */


/*
  a5 75 40 40     cmpeq   t1,0x03,t4
  01 00 a0 e4     beq     t4,20 <skip>
*/
*a++ = 0xa5; *a++ = 0x75; *a++ = 0x40; *a++ = 0x40;
*a++ = 0x04; *a++ = 0x00; *a++ = 0xa0; *a++ = 0xe4;

/*
 118:   23 17 63 48     sll     t2,0x18,t2
 11c:   21 f6 20 48     zapnot  t0,0x7,t0
 120:   01 04 23 44     or      t0,t2,t0
*/
*a++ = 0x23; *a++ = 0x17; *a++ = 0x63; *a++ = 0x48;
*a++ = 0x21; *a++ = 0xf6; *a++ = 0x20; *a++ = 0x48;
*a++ = 0x01; *a++ = 0x04; *a++ = 0x23; *a++ = 0x44;

ok_unaligned_load3 = a;
*a++ = 0x01; *a++ = 0x00; *a++ = 0xe0; *a++ = 0xc3;





*a++ = 0xa5; *a++ = 0x55; *a++ = 0x40; *a++ = 0x40;
*a++ = 0x04; *a++ = 0x00; *a++ = 0xa0; *a++ = 0xe4;
/*
  2:
 124:   23 17 62 48     sll     t2,0x10,t2
 128:   21 76 20 48     zapnot  t0,0x3,t0
 12c:   01 04 23 44     or      t0,t2,t0
*/
*a++ = 0x23; *a++ = 0x17; *a++ = 0x62; *a++ = 0x48;
*a++ = 0x21; *a++ = 0x76; *a++ = 0x20; *a++ = 0x48;
*a++ = 0x01; *a++ = 0x04; *a++ = 0x23; *a++ = 0x44;

ok_unaligned_load2 = a;
*a++ = 0x01; *a++ = 0x00; *a++ = 0xe0; *a++ = 0xc3;



*a++ = 0xa5; *a++ = 0x35; *a++ = 0x40; *a++ = 0x40;
*a++ = 0x04; *a++ = 0x00; *a++ = 0xa0; *a++ = 0xe4;
/*
  1:
 130:   23 17 61 48     sll     t2,0x8,t2
 134:   21 36 20 48     zapnot  t0,0x1,t0
 138:   01 04 23 44     or      t0,t2,t0
*/
*a++ = 0x23; *a++ = 0x17; *a++ = 0x61; *a++ = 0x48;
*a++ = 0x21; *a++ = 0x36; *a++ = 0x20; *a++ = 0x48;
*a++ = 0x01; *a++ = 0x04; *a++ = 0x23; *a++ = 0x44;

ok_unaligned_load1 = a;
*a++ = 0x01; *a++ = 0x00; *a++ = 0xe0; *a++ = 0xc3;



/*
 0:
 13c:   01 10 60 40     addl    t2,0,t0
*/
*a++ = 0x01; *a++ = 0x10; *a++ = 0x60; *a++ = 0x40;


		*ok_unaligned_load3 = ((size_t)a - (size_t)ok_unaligned_load3 - 4) / 4;
		*ok_unaligned_load2 = ((size_t)a - (size_t)ok_unaligned_load2 - 4) / 4;
		*ok_unaligned_load1 = ((size_t)a - (size_t)ok_unaligned_load1 - 4) / 4;

		/*  sdl t0,0(t3)  */
		*a++ = 0x00; *a++ = 0x00; *a++ = 0x24; *a++ = 0xb0;
		break;

	default:
		;
	}

	*addrp = a;
	bintrans_write_pc_inc(addrp);
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
		int alpha_rt = map_MIPS_to_Alpha[rt];
		if (alpha_rt < 0)
			alpha_rt = ALPHA_T0;

		a = (uint32_t *) *addrp;
		*a++ = 0x241f0000 | (alpha_rt << 21) | ((uint32_t)imm & 0xffff);
		*addrp = (unsigned char *) a;

		if (alpha_rt == ALPHA_T0) {
			*a++ = 0x5fff041f;	/*  fnop  */
			bintrans_move_Alpha_reg_into_MIPS_reg(addrp, ALPHA_T0, rt);
		}
	}

	bintrans_write_pc_inc(addrp);

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
				ofs = ((size_t)&dummy_cpu.cd.mips.hi) - (size_t)&dummy_cpu;
			else
				ofs = ((size_t)&dummy_cpu.cd.mips.lo) - (size_t)&dummy_cpu;
			*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xa4;

			bintrans_move_Alpha_reg_into_MIPS_reg(&a, ALPHA_T0, rd);
		}
	} else {
		/*  mthi or mtlo  */
		bintrans_move_MIPS_reg_into_Alpha_reg(&a, rd, ALPHA_T0);

		if (hi_flag)
			ofs = ((size_t)&dummy_cpu.cd.mips.hi) - (size_t)&dummy_cpu;
		else
			ofs = ((size_t)&dummy_cpu.cd.mips.lo) - (size_t)&dummy_cpu;
		*a++ = (ofs & 255); *a++ = (ofs >> 8); *a++ = 0x30; *a++ = 0xb4;
	}

	*addrp = a;
	bintrans_write_pc_inc(addrp);
	return 1;
}


/*
 *  bintrans_write_instruction__mfc_mtc():
 */
static int bintrans_write_instruction__mfc_mtc(struct memory *mem,
	unsigned char **addrp, int coproc_nr, int flag64bit, int rt,
	int rd, int mtcflag)
{
	uint32_t *a, *jump;
	int ofs;

	/*
	 *  NOTE: Only a few registers are readable without side effects.
	 */
	if (rt == 0 && !mtcflag)
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

	ofs = ((size_t)&dummy_cpu.cd.mips.coproc[0]) - (size_t)&dummy_cpu;
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
			break;

		case COP0_EPC:
			break;

		/*  TODO: Some bits are not writable  */
		case COP0_ENTRYLO0:
		case COP0_ENTRYLO1:
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
			if (mem->bintrans_32bit_only) {
				/*  R3000 etc.  */
				/*  t4 = 0x0fe70000;  */
				*a++ = 0x20bf0000;
				*a++ = 0x24a50fe7;
			} else {
				/*  fe 00 bf 20     lda     t4,0x00fe  */
				/*  ff ff a5 24     ldah    t4,-1(t4)  */
				*a++ = 0x20bf0000;
				*a++ = 0x24a5ffff;
			}

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
			ofs = ((size_t)&dummy_cpu.cd.mips.coproc[0]) - (size_t)&dummy_cpu;
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

		ofs = ((size_t)&dummy_cpu.cd.mips.coproc[0]) - (size_t)&dummy_cpu;
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

	bintrans_write_pc_inc(addrp);
	return 1;
}


/*
 *  bintrans_write_instruction__tlb_rfe_etc():
 */
static int bintrans_write_instruction__tlb_rfe_etc(unsigned char **addrp,
	int itype)
{
	uint32_t *a;
	int ofs = 0;

	switch (itype) {
	case CALL_TLBWI:
	case CALL_TLBWR:
	case CALL_TLBP:
	case CALL_TLBR:
	case CALL_RFE:
	case CALL_ERET:
	case CALL_BREAK:
	case CALL_SYSCALL:
		break;
	default:
		return 0;
	}

	a = (uint32_t *) *addrp;

	/*  a0 = pointer to the cpu struct  */

	switch (itype) {
	case CALL_TLBWI:
	case CALL_TLBWR:
		/*  a1 = 0 for indexed, 1 for random  */
		*a++ = 0x223f0000 | (itype == CALL_TLBWR);
		break;
	case CALL_TLBP:
	case CALL_TLBR:
		/*  a1 = 0 for probe, 1 for read  */
		*a++ = 0x223f0000 | (itype == CALL_TLBR);
		break;
	case CALL_BREAK:
	case CALL_SYSCALL:
		*a++ = 0x223f0000 | (itype == CALL_BREAK? EXCEPTION_BP : EXCEPTION_SYS);
		break;
	}

	/*  Put PC into the cpu struct (both pc and pc_last).  */
	*a++ = 0xb4d00000 | ofs_pc;	/*  stq t5,"pc"(a0)  */
	*a++ = 0xb4d00000 | ofs_pc_last;/*  stq t5,"pc_last"(a0)  */

	/*  Save a0 and the old return address on the stack:  */
	*a++ = 0x23deff80;		/*  lda sp,-128(sp)  */

	*a++ = 0xb75e0000;		/*  stq ra,0(sp)  */
	*a++ = 0xb61e0008;		/*  stq a0,8(sp)  */
	*a++ = 0xb0fe0018;		/*  stl t6,24(sp)  */
	*a++ = 0xb71e0020;		/*  stq t10,32(sp)  */
	*a++ = 0xb73e0028;		/*  stq t11,40(sp)  */
	*a++ = 0xb51e0030;		/*  stq t7,48(sp)  */
	*a++ = 0xb6de0038;		/*  stq t8,56(sp)  */
	*a++ = 0xb6fe0040;		/*  stq t9,64(sp)  */

	switch (itype) {
	case CALL_TLBP:
	case CALL_TLBR:
		ofs = ((size_t)&dummy_cpu.cd.mips.bintrans_fast_tlbpr) - (size_t)&dummy_cpu;
		break;
	case CALL_TLBWR:
	case CALL_TLBWI:
		ofs = ((size_t)&dummy_cpu.cd.mips.bintrans_fast_tlbwri) - (size_t)&dummy_cpu;
		break;
	case CALL_RFE:
		ofs = ((size_t)&dummy_cpu.cd.mips.bintrans_fast_rfe) - (size_t)&dummy_cpu;
		break;
	case CALL_ERET:
		ofs = ((size_t)&dummy_cpu.cd.mips.bintrans_fast_eret) - (size_t)&dummy_cpu;
		break;
	case CALL_BREAK:
	case CALL_SYSCALL:
		ofs = ((size_t)&dummy_cpu.cd.mips.bintrans_simple_exception) - (size_t)&dummy_cpu;
		break;
	}

	*a++ = 0xa7700000 | ofs;	/*  ldq t12,0(a0)  */

	/*  Call bintrans_fast_tlbwr:  */
	*a++ = 0x6b5b4000;		/*  jsr ra,(t12),<after>  */

	/*  Restore the old return address and a0 from the stack:  */
	*a++ = 0xa75e0000;		/*  ldq ra,0(sp)  */
	*a++ = 0xa61e0008;		/*  ldq a0,8(sp)  */
	*a++ = 0xa0fe0018;		/*  ldl t6,24(sp)  */
	*a++ = 0xa71e0020;		/*  ldq t10,32(sp)  */
	*a++ = 0xa73e0028;		/*  ldq t11,40(sp)  */
	*a++ = 0xa51e0030;		/*  ldq t7,48(sp)  */
	*a++ = 0xa6de0038;		/*  ldq t8,56(sp)  */
	*a++ = 0xa6fe0040;		/*  ldq t9,64(sp)  */

	*a++ = 0x23de0080;		/*  lda sp,128(sp)  */

	/*  Load PC from the cpu struct.  */
	*a++ = 0xa4d00000 | ofs_pc;	/*  ldq t5,"pc"(a0)  */

	*addrp = (unsigned char *) a;

	switch (itype) {
	case CALL_ERET:
	case CALL_BREAK:
	case CALL_SYSCALL:
		break;
	default:
		bintrans_write_pc_inc(addrp);
	}

	return 1;
}


/*
 *  bintrans_backend_init():
 *
 *  This is neccessary for broken 2.95.4 compilers on FreeBSD/Alpha 4.9,
 *  and probably a few others. (For Compaq's CC, and for gcc 3.x, this
 *  wouldn't be neccessary, and the old code would have worked.)
 */
static void bintrans_backend_init(void)
{ 
	int size;
	uint32_t *p, *q;


	/*  "runchunk":  */
	size = 256;		/*  NOTE: This MUST be enough, or we fail  */
	p = (uint32_t *)mmap(NULL, size, PROT_READ | PROT_WRITE | PROT_EXEC,
	    MAP_ANON | MAP_PRIVATE, -1, 0);

	/*  If mmap() failed, try malloc():  */
	if (p == NULL) {
		p = malloc(size);
		if (p == NULL) {
			fprintf(stderr, "bintrans_backend_init(): out of memory\n");
			exit(1);
		}
	}

	bintrans_runchunk = (void *)p;

	*p++ = 0x23deffa0;		/*  lda     sp,-0x60(sp)  */
	*p++ = 0xb75e0000;		/*  stq     ra,0(sp)  */
	*p++ = 0xb53e0008;		/*  stq     s0,8(sp)  */
	*p++ = 0xb55e0010;		/*  stq     s1,16(sp)  */
	*p++ = 0xb57e0018;		/*  stq     s2,24(sp)  */
	*p++ = 0xb59e0020;		/*  stq     s3,32(sp)  */
	*p++ = 0xb5be0028;		/*  stq     s4,40(sp)  */
	*p++ = 0xb5de0030;		/*  stq     s5,48(sp)  */
	*p++ = 0xb5fe0038;		/*  stq     s6,56(sp)  */
	*p++ = 0xb7be0058;		/*  stq     gp,0x58(sp)  */

	*p++ = 0xa4d00000 | ofs_pc;	/*  ldq     t5,"pc"(a0)  */
	*p++ = 0xa0f00000 | ofs_n;	/*  ldl     t6,"bintrans_instructions_executed"(a0)  */
	*p++ = 0xa5100000 | ofs_a0;	/*  ldq     t7,"a0"(a0)  */
	*p++ = 0xa6d00000 | ofs_a1;	/*  ldq     t8,"a1"(a0)  */
	*p++ = 0xa6f00000 | ofs_s0;	/*  ldq     t9,"s0"(a0)  */
	*p++ = 0xa1300000 | ofs_ds;	/*  ldl     s0,"delay_slot"(a0)  */
	*p++ = 0xa5500000 | ofs_ja;	/*  ldq     s1,"delay_jmpaddr"(a0)  */
	*p++ = 0xa5700000 | ofs_sp;	/*  ldq     s2,"gpr[sp]"(a0)  */
	*p++ = 0xa5900000 | ofs_ra;	/*  ldq     s3,"gpr[ra]"(a0)  */
	*p++ = 0xa5b00000 | ofs_t0;	/*  ldq     s4,"gpr[t0]"(a0)  */
	*p++ = 0xa5d00000 | ofs_t1;	/*  ldq     s5,"gpr[t1]"(a0)  */
	*p++ = 0xa5f00000 | ofs_t2;	/*  ldq     s6,"gpr[t2]"(a0)  */
	*p++ = 0xa7100000 | ofs_tbl0;	/*  ldq     t10,table0(a0)  */
	*p++ = 0xa7300000 | ofs_v0;	/*  ldq     t11,"gpr[v0]"(a0)  */

	*p++ = 0x6b514000;		/*  jsr     ra,(a1),<back>  */

	*p++ = 0xb4d00000 | ofs_pc;	/*  stq     t5,"pc"(a0)  */
	*p++ = 0xb0f00000 | ofs_n;	/*  stl     t6,"bintrans_instructions_executed"(a0)  */
	*p++ = 0xb5100000 | ofs_a0;	/*  stq     t7,"a0"(a0)  */
	*p++ = 0xb6d00000 | ofs_a1;	/*  stq     t8,"a1"(a0)  */
	*p++ = 0xb6f00000 | ofs_s0;	/*  stq     t9,"s0"(a0)  */
	*p++ = 0xb1300000 | ofs_ds;	/*  stl     s0,"delay_slot"(a0)  */
	*p++ = 0xb5500000 | ofs_ja;	/*  stq     s1,"delay_jmpaddr"(a0)  */
	*p++ = 0xb5700000 | ofs_sp;	/*  stq     s2,"gpr[sp]"(a0)  */
	*p++ = 0xb5900000 | ofs_ra;	/*  stq     s3,"gpr[ra]"(a0)  */
	*p++ = 0xb5b00000 | ofs_t0;	/*  stq     s4,"gpr[t0]"(a0)  */
	*p++ = 0xb5d00000 | ofs_t1;	/*  stq     s5,"gpr[t1]"(a0)  */
	*p++ = 0xb5f00000 | ofs_t2;	/*  stq     s6,"gpr[t2]"(a0)  */
	*p++ = 0xb7300000 | ofs_v0;	/*  stq     t11,"gpr[v0]"(a0)  */

	*p++ = 0xa75e0000;		/*  ldq     ra,0(sp)  */
	*p++ = 0xa53e0008;		/*  ldq     s0,8(sp)  */
	*p++ = 0xa55e0010;		/*  ldq     s1,16(sp)  */
	*p++ = 0xa57e0018;		/*  ldq     s2,24(sp)  */
	*p++ = 0xa59e0020;		/*  ldq     s3,32(sp)  */
	*p++ = 0xa5be0028;		/*  ldq     s4,40(sp)  */
	*p++ = 0xa5de0030;		/*  ldq     s5,48(sp)  */
	*p++ = 0xa5fe0038;		/*  ldq     s6,56(sp)  */
	*p++ = 0xa7be0058;		/*  ldq     gp,0x58(sp)  */
	*p++ = 0x23de0060;		/*  lda     sp,0x60(sp)  */
	*p++ = 0x6bfa8001;		/*  ret   */


	/*  "jump to 32bit pc":  */
	size = 128;	/*  WARNING! Don't make this too small.  */
	p = (uint32_t *)mmap(NULL, size, PROT_READ | PROT_WRITE | PROT_EXEC,
	    MAP_ANON | MAP_PRIVATE, -1, 0);

	/*  If mmap() failed, try malloc():  */
	if (p == NULL) {
		p = malloc(size);
		if (p == NULL) {
			fprintf(stderr, "bintrans_backend_init(): out of memory\n");
			exit(1);
		}
	}

	bintrans_jump_to_32bit_pc = (void *)p;

	/*  Don't execute too many instructions:  */
	*p++ = 0x205f0000 | (N_SAFE_BINTRANS_LIMIT-1);  /*  lda t1,safe-1 */

	*p++ = 0x40e20da1;		/*  cmple t6,t1,t0  */
	q = p;	/*  *q is updated later  */
	*p++ = 0xe4200001;		/*  beq ret (far below)  */

	*p++ = 0x40c01411;		/*  addq t5,0,a1  */

	/*
	 *  Special case for 32-bit addressing:
	 *
	 *  t1 = 1023;
	 *  t2 = ((a1 >> 22) & t1) * sizeof(void *);
	 *  t3 = ((a1 >> 12) & t1) * sizeof(void *);
	 *  t1 = a1 & 4095;
	 */
	*p++ = 0x205f1ff8;	/*  lda     t1,1023 * 8  */
	*p++ = 0x4a227683;	/*  srl     a1,19,t2  */
	*p++ = 0x4a213684;	/*  srl     a1, 9,t3  */
	*p++ = 0x44620003;	/*  and     t2,t1,t2  */

	/*
	 *  t10 is vaddr_to_hostaddr_table0
	 *
	 *  a3 = tbl0[t2]  (load entry from tbl0)
	 */
	*p++ = 0x43030412;	/*  addq    t10,t2,a2  */
	*p++ = 0x44820004;	/*  and     t3,t1,t3  */
	*p++ = 0xa6720000;	/*  ldq     a3,0(a2)  */
	*p++ = 0x205f0ffc;	/*  lda     t1,0xffc  */

	/*
	 *  a3 = tbl1[t3]  (load entry from tbl1 (which is a3))
	 */
	*p++ = 0x42640413;	/*  addq    a3,t3,a3  */
	*p++ = 0x46220002;	/*  and     a1,t1,t1  */

	*p++ = 0xa6730000 | ofs_c0;	/*  ldq a3,chunks[0](a3)  */

	/*
	 *  NULL? Then just return.
	 */
	*p++ = 0xf6600001;	/*  bne     a3,<ok>  */
	*p++ = 0x6bfa8001;	/*  ret  */

	*p++ = 0x40530402;	/*  addq    t1,a3,t1  */
	*p++ = 0xa0220000;	/*  ldl     t0,0(t1)  */

	/*  No translation? Then return.  */
	*p++ = 0xe4200003;	/*  beq t0,<skip>  */

	*p++ = 0xa4700000 | ofs_cb;	/*  ldq t2,chunk_base_address(a0)  */

	*p++ = 0x40230401;	/*  addq t0,t2,t0  */
	*p++ = 0x6be10000;	/*  jmp (t0)  */

	/*  Now, update *q to point here:  */
	*q = 0xe4200000 | (((size_t)p - (size_t)q)/4 - 1);	/*  beq ret  */

	/*  Return to the main translation loop.  */
	*p++ = 0x6bfa8001;		/*  ret  */
} 

