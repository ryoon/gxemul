/*
 *  Copyright (C) 2005  Anders Gavare.  All rights reserved.
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
 *  $Id: bintrans_sparcv9.c,v 1.18 2005-01-14 03:36:19 debug Exp $
 *
 *  UltraSPARC specific code for dynamic binary translation.
 *
 *  See bintrans.c for more information.  Included from bintrans.c.
 */

struct cpu dummy_cpu;
struct coproc dummy_coproc;
struct vth32_table dummy_vth32_table;


/*
 *  bintrans_host_cacheinvalidate()
 *
 *  Invalidate the host's instruction cache.
 */
static void bintrans_host_cacheinvalidate(unsigned char *p, size_t len)
{
	/*  TODO  */
}


static uint32_t bintrans_sparcv9_runchunk[2] = {
	0x81c24000,	/*  jmp %o1  */
	0x01000000
};

static uint32_t bintrans_sparcv9_jump_to_32bit_pc[2] = {
	0x81c3e008,
	0x01000000
};

static uint32_t bintrans_sparcv9_loadstore_32bit[2] = {
	0x81c3e008,
	0x01000000
};

static const void (*bintrans_runchunk)
    (struct cpu *, unsigned char *) = (void *)bintrans_sparcv9_runchunk;

static void (*bintrans_jump_to_32bit_pc)
    (struct cpu *) = (void *)bintrans_sparcv9_jump_to_32bit_pc;

static void (*bintrans_loadstore_32bit)
    (struct cpu *) = (void *)bintrans_sparcv9_loadstore_32bit;


/*
 *  bintrans_write_quickjump():
 */
static void bintrans_write_quickjump(unsigned char *quickjump_code,
	uint32_t chunkoffset)
{
	/*  TODO  */
}


/*
 *  bintrans_write_chunkreturn():
 */
static void bintrans_write_chunkreturn(unsigned char **addrp)
{
	uint32_t *a = (uint32_t *) *addrp;

	*a++ = 0x81c3e008;	/*  retl  */
	*a++ = 0x01000000;	/*  nop  */

	*addrp = (unsigned char *) a;
}


/*
 *  bintrans_write_chunkreturn_fail():
 */
static void bintrans_write_chunkreturn_fail(unsigned char **addrp)
{
	uint32_t *a = (uint32_t *) *addrp;
	int ofs;

	/*  Or BINTRANS_DONT_RUN_NEXT into nr of instrs:  */
	ofs = ((size_t)&dummy_cpu.bintrans_instructions_executed)
	    - ((size_t)&dummy_cpu);
	*a++ = 0xda022000 | ofs;	/*  ld  [ %o0 + ofs ], %o5  */
	*a++ = 0x19000000 | (BINTRANS_DONT_RUN_NEXT >> 10);
					/*  sethi  %hi(0x1000000), %o4  */
	*a++ = 0x9a13000d;		/*  or  %o4, %o5, %o5  */
	*a++ = 0xda222000 | ofs;	/*  st  %o5, [ %o0 + 0x124 ]  */

	*a++ = 0x81c3e008;	/*  retl  */
	*a++ = 0x01000000;	/*  nop  */

	*addrp = (unsigned char *) a;
}


/*
 *  bintrans_write_pc_inc():
 */
static void bintrans_write_pc_inc(unsigned char **addrp)
{
	uint32_t *a = (uint32_t *) *addrp;
	int ofs;

	/*  Add 1 to instruction count:  */
	ofs = ((size_t)&dummy_cpu.bintrans_instructions_executed)
	    - ((size_t)&dummy_cpu);
	*a++ = 0xc4022000 + ofs;	/*  ld [ %o0 + ofs ], %g2  */
	*a++ = 0x8400a001;		/*  add %g2, 1, %g2        */
	*a++ = 0xc4222000 + ofs;	/*  st %g2, [ %o0 + ofs ]  */

	/*  Add 4 to pc:  */
	ofs = ((size_t)&dummy_cpu.pc) - ((size_t)&dummy_cpu);
	*a++ = 0xda5a2000 + ofs;	/*  ldx [ %o0 + ofs ], %o5  */
	*a++ = 0x9a036004;		/*  add %o5, 4, %o5         */
	*a++ = 0xda722000 + ofs;	/*  stx %o5, [ %o0 + ofs ]  */

	*addrp = (unsigned char *) a;
}


/*
 *  bintrans_write_instruction__addiu_etc():
 */
static int bintrans_write_instruction__addiu_etc(unsigned char **addrp,
	int rt, int rs, int imm, int instruction_type)
{
	uint32_t *a = (uint32_t *) *addrp;
	int ofs;

	switch (instruction_type) {
	case HI6_ADDIU:
	case HI6_DADDIU:
	case HI6_ANDI:
	case HI6_ORI:
	case HI6_XORI:
	case HI6_SLTI:
	case HI6_SLTIU:
		break;
	default:
		return 0;
	}

	if (rt == 0)
		goto rt_0;

	ofs = ((size_t)&dummy_cpu.gpr[rs]) - (size_t)&dummy_cpu;
	*a++ = 0xda5a2000 + ofs;		/*  ldx [ %o0 + ofs ], %o5  */

	switch (instruction_type) {
	case HI6_ADDIU:
	case HI6_DADDIU:
		*a++ = 0x19000000 | (imm << 6);	/*  sethi %hi(....), %o4  */
		*a++ = 0x993b2010;		/*  sra %o4, 16, %o4  */
		*a++ = 0x9a03400c;
		if (instruction_type == HI6_ADDIU)
			*a++ = 0x9b3b6000;
		break;
	case HI6_ANDI:
	case HI6_ORI:
	case HI6_XORI:
		*a++ = 0x19000000 | (imm & 0xffff);	/*  sethi %hi(....), %o4  */
		*a++ = 0x993b200a;			/*  sra %o4, 10, %o4  */
		switch (instruction_type) {
		case HI6_ORI:
			*a++ = 0x9a13400c;		/*  or  %o5, %o4, %o5  */
			break;
		case HI6_XORI:
			*a++ = 0x9a1b400c;		/*  xor  %o5, %o4, %o5  */
			break;
		case HI6_ANDI:
			*a++ = 0x9a0b400c;		/*  and  %o5, %o4, %o5  */
			break;
		}
		break;
	case HI6_SLTI:
		*a++ = 0x19000000 | (imm << 6);	/*  sethi %hi(....), %o4  */
		*a++ = 0x993b2010;		/*  sra %o4, 16, %o4  */
		/*  rd = rs < rt     %o5 = %o5 < %o4  */
		*a++ = 0x80a3400c;		/*  cmp  %o5, %o4  */
		*a++ = 0x9a100000;		/*  mov  %g0, %o5  */
		*a++ = 0x9b64f001;		/*  movl  %xcc, 1, %o5  */
		*a++ = 0x9b3b6000;		/*  sra  %o5, 0, %o5  */
		break;
	case HI6_SLTIU:
		*a++ = 0x19000000 | (imm << 6);	/*  sethi %hi(....), %o4  */
		*a++ = 0x993b2010;		/*  sra %o4, 16, %o4  */
		/*  rd = rs < rt  (unsigned)     %o5 = %o5 < %o4  */
		*a++ = 0x80a3400c;		/*  cmp  %o5, %o4  */
		*a++ = 0x9a100000;		/*  mov  %g0, %o5  */
		*a++ = 0x9b657001;		/*  movcs  %xcc, 1, %o5  */
		*a++ = 0x9b3b6000;		/*  sra  %o5, 0, %o5  */
		break;
	}

	ofs = ((size_t)&dummy_cpu.gpr[rt]) - (size_t)&dummy_cpu;
	*a++ = 0xda722000 + ofs;		/*  stx %o5, [ %o0 + ofs ]  */

rt_0:
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
	uint32_t *a = (uint32_t *) *addrp;
	int ofs;

	switch (instruction_type) {
	case SPECIAL_ADDU:
	case SPECIAL_DADDU:
	case SPECIAL_SUBU:
	case SPECIAL_DSUBU:
	case SPECIAL_AND:
	case SPECIAL_OR:
	case SPECIAL_NOR:
	case SPECIAL_XOR:
	case SPECIAL_SLL:
	case SPECIAL_SLLV:
	case SPECIAL_DSLL:
	case SPECIAL_SRL:
	case SPECIAL_SRLV:
	case SPECIAL_SRA:
	case SPECIAL_SRAV:
	case SPECIAL_SLT:
	case SPECIAL_SLTU:
		break;
	default:
		return 0;
	}

	if (rd == 0)
		goto rd_0;

	/*  Most of these are:  rd = rs OP rt  */

	/*  o5 = rs, o4 = rt, then place result in o5  */
	ofs = ((size_t)&dummy_cpu.gpr[rs]) - (size_t)&dummy_cpu;
	*a++ = 0xda5a2000 + ofs;		/*  ldx [ %o0 + ofs ], %o5  */
	ofs = ((size_t)&dummy_cpu.gpr[rt]) - (size_t)&dummy_cpu;
	*a++ = 0xd85a2000 + ofs;		/*  ldx [ %o0 + ofs ], %o4  */

	switch (instruction_type) {
	case SPECIAL_ADDU:
	case SPECIAL_DADDU:
		*a++ = 0x9a03400c;	/*  add  %o5, %o4, %o5  */
		if (instruction_type == SPECIAL_ADDU)
			*a++ = 0x9b3b6000;	/*  sra  %o5, 0, %o5  */
		break;
	case SPECIAL_SUBU:
	case SPECIAL_DSUBU:
		*a++ = 0x9a23400c;	/*  sub  %o5, %o4, %o5  */
		if (instruction_type == SPECIAL_SUBU)
			*a++ = 0x9b3b6000;	/*  sra  %o5, 0, %o5  */
		break;
	case SPECIAL_AND:
		*a++ = 0x9a0b000d;		/*  and  %o4, %o5, %o5  */
		break;
	case SPECIAL_OR:
	case SPECIAL_NOR:
		*a++ = 0x9a13000d;		/*  or   %o4, %o5, %o5  */
		if (instruction_type == SPECIAL_NOR)
			*a++ = 0x9a3b6000;	/*  xnor  %o5, 0, %o5  */
		break;
	case SPECIAL_XOR:
		*a++ = 0x9a1b000d;		/*  xor  %o4, %o5, %o5  */
		break;
	case SPECIAL_SLL:
	case SPECIAL_DSLL:
		*a++ = 0x9b2b2000 | sa;		/*  sll  %o4, sa, %o5  */
		if (instruction_type == SPECIAL_SLL)
			*a++ = 0x9b3b6000;	/*  sra  %o5, 0, %o5  */
		break;
	case SPECIAL_SRA:
		*a++ = 0x9b3b2000 | sa;		/*  sra  %o4, sa, %o5  */
		break;
	case SPECIAL_SRL:
		*a++ = 0x9b332000 | sa;		/*  srl  %o4, sa, %o5  */
		*a++ = 0x9b3b6000;		/*  sra  %o5, 0, %o5  */
		break;
	case SPECIAL_SLLV:
		/*  rd = rt >> (rs&31)  (logical)  */
		*a++ = 0x9b2b000d;		/*  sll  %o4, %o5, %o5  */
		break;
	case SPECIAL_SRLV:
		/*  rd = rt << (rs&31)  (logical)  */
		*a++ = 0x9b33000d;		/*  srl  %o4, %o5, %o5  */
		break;
	case SPECIAL_SRAV:
		/*  rd = rt >> (rs&31)  (logical)  */
		*a++ = 0x9b3b000d;		/*  sra  %o4, %o5, %o5  */
		break;
	case SPECIAL_SLT:
		/*  rd = rs < rt     %o5 = %o5 < %o4  */
		*a++ = 0x80a3400c;		/*  cmp  %o5, %o4  */
		*a++ = 0x9a100000;		/*  mov  %g0, %o5  */
		*a++ = 0x9b64f001;		/*  movl  %xcc, 1, %o5  */
		*a++ = 0x9b3b6000;		/*  sra  %o5, 0, %o5  */
		break;
	case SPECIAL_SLTU:
		/*  rd = rs < rt (unsigned)     %o5 = %o5 < %o4  */
		*a++ = 0x80a3400c;		/*  cmp  %o5, %o4  */
		*a++ = 0x9a100000;		/*  mov  %g0, %o5  */
		*a++ = 0x9b657001;		/*  movcs  %xcc, 1, %o5  */
		*a++ = 0x9b3b6000;		/*  sra  %o5, 0, %o5  */
		break;
	}

	ofs = ((size_t)&dummy_cpu.gpr[rd]) - (size_t)&dummy_cpu;
	*a++ = 0xda722000 + ofs;		/*  stx %o5, [ %o0 + ofs ]  */

rd_0:

	*addrp = (unsigned char *) a;
	bintrans_write_pc_inc(addrp);
	return 1;
}


/*
 *  bintrans_write_instruction__branch():
 */
static int bintrans_write_instruction__branch(unsigned char **addrp,
	int instruction_type, int regimm_type, int rt, int rs, int imm)
{
	return 0;
}


/*
 *  bintrans_write_instruction__jr():
 */
static int bintrans_write_instruction__jr(unsigned char **addrp, int rs, int rd, int special)
{
	uint32_t *a = (uint32_t *) *addrp;
	int ofs;

return 0;

	/*
	 *  Perform the jump by setting cpu->delay_slot = TO_BE_DELAYED
	 *  and cpu->delay_jmpaddr = gpr[rs].
	 */

	ofs = ((size_t)&dummy_cpu.delay_slot) - (size_t)&dummy_cpu;
	*a++ = 0x9a102000 | TO_BE_DELAYED;	/*  mov TO_BE_DELAYED, %o5  */
	*a++ = 0xda222000 | ofs;		/*  st %o5, [ %o0 + ofs ]   */

	ofs = ((size_t)&dummy_cpu.gpr[rs]) - (size_t)&dummy_cpu;
	*a++ = 0xda5a2000 + ofs;		/*  ldx [ %o0 + ofs ], %o5  */
	ofs = ((size_t)&dummy_cpu.delay_jmpaddr) - (size_t)&dummy_cpu;
	*a++ = 0xda722000 + ofs;		/*  stx %o5, [ %o0 + ofs ]  */

	if (special == SPECIAL_JALR && rd != 0) {
                /*  gpr[rd] = retaddr    (pc + 8)  */

		ofs = ((size_t)&dummy_cpu.pc) - (size_t)&dummy_cpu;
		*a++ = 0xda5a2000 + ofs;	/*  ldx [ %o0 + ofs ], %o5  */

		*a++ = 0x9a036008;		/*  add %o5, 8, %o5  */

		ofs = ((size_t)&dummy_cpu.gpr[rd]) - (size_t)&dummy_cpu;
		*a++ = 0xda722000 + ofs;	/*  stx %o5, [ %o0 + ofs ]  */
	}

	*addrp = (unsigned char *) a;
	bintrans_write_pc_inc(addrp);
	return 1;
}


/*
 *  bintrans_write_instruction__jal():
 */
static int bintrans_write_instruction__jal(unsigned char **addrp,
	int imm, int link)
{
	return 0;
}


/*
 *  bintrans_write_instruction__delayedbranch():
 */
static int bintrans_write_instruction__delayedbranch(unsigned char **addrp,
	uint32_t *potential_chunk_p, uint32_t *chunks,
	int only_care_about_chunk_p, int p, int forward)
{
	return 0;
}


/*
 *  bintrans_write_instruction__loadstore():
 */
static int bintrans_write_instruction__loadstore(unsigned char **addrp,
	int rt, int imm, int rs, int instruction_type, int bigendian)
{
	return 0;
}


/*
 *  bintrans_write_instruction__lui():
 */
static int bintrans_write_instruction__lui(unsigned char **addrp,
	int rt, int imm)
{
	uint32_t *a = (uint32_t *) *addrp;
	int ofs;

	if (rt == 0)
		return 0;

	/*
	 *  Trick if imm&0x8000: Load it shifted only
	 *  5 bits to the left instead of 6, and then
	 *  do a sll by 1 to sign-extend it. :-)  (Hm,
	 *  it doesn't seem to work without an sra too.)
	 */
	if (imm & 0x8000) {
		*a++ = 0x1b000000 | ((imm & 0xffff) << 5);	/*  sethi %hi(0xXXXX0000), %o5  */
		*a++ = 0x9b2b6001;				/*  sll  %o5, 1, %o5  */
		*a++ = 0x9b3b6000;				/*  sra  %o5, 0, %o5  */
	} else {
		/*  sethi %hi(0xXXXX0000), %o5  */
		*a++ = 0x1b000000 | ((imm & 0xffff) << 6);
	}

	/*  stx  %o5, [ %o0 + ofs ]  */
	ofs = ((size_t)&dummy_cpu.gpr[rt]) - ((size_t)&dummy_cpu);
	*a++ = 0xda722000 | ofs;

	*addrp = (unsigned char *) a;
	bintrans_write_pc_inc(addrp);
	return 1;
}


/*
 *  bintrans_write_instruction__mfmthilo():
 */
static int bintrans_write_instruction__mfmthilo(unsigned char **addrp,
	int rd, int from_flag, int hi_flag)
{
	uint32_t *a = (uint32_t *) *addrp;
	int ofs;

	if (from_flag) {
		if (rd != 0) {
			/*  mfhi or mflo  */
			if (hi_flag)
				ofs = ((size_t)&dummy_cpu.hi) - (size_t)&dummy_cpu;
			else
				ofs = ((size_t)&dummy_cpu.lo) - (size_t)&dummy_cpu;
			*a++ = 0xda5a2000 + ofs;	/*  ldx [ %o0 + ofs ], %o5  */

			ofs = ((size_t)&dummy_cpu.gpr[rd]) - (size_t)&dummy_cpu;
			*a++ = 0xda722000 + ofs;	/*  stx %o5, [ %o0 + ofs ]  */
		}
	} else {
		/*  mthi or mtlo  */
		ofs = ((size_t)&dummy_cpu.gpr[rd]) - (size_t)&dummy_cpu;
		*a++ = 0xda5a2000 + ofs;	/*  ldx [ %o0 + ofs ], %o5  */

		if (hi_flag)
			ofs = ((size_t)&dummy_cpu.hi) - (size_t)&dummy_cpu;
		else
			ofs = ((size_t)&dummy_cpu.lo) - (size_t)&dummy_cpu;

		*a++ = 0xda722000 + ofs;	/*  stx %o5, [ %o0 + ofs ]  */
	}

	*addrp = (unsigned char *) a;
	bintrans_write_pc_inc(addrp);
	return 1;
}


/*
 *  bintrans_write_instruction__mfc_mtc():
 */
static int bintrans_write_instruction__mfc_mtc(unsigned char **addrp, int coproc_nr, int flag64bit, int rt, int rd, int mtcflag)
{
	return 0;
}


/*
 *  bintrans_write_instruction__tlb_rfe_etc():
 */
static int bintrans_write_instruction__tlb_rfe_etc(unsigned char **addrp,
	int itype)
{
	return 0;
}


/*
 *  bintrans_backend_init():
 */
static void bintrans_backend_init(void)
{ 
} 

