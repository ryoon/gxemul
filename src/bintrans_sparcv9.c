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
 *  $Id: bintrans_sparcv9.c,v 1.11 2005-01-09 03:52:24 debug Exp $
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
	0x81c3e008,
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

	/*  TODO:  Or BINTRANS_DONT_RUN_NEXT into nr of instrs  */

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
	return 0;
}


/*
 *  bintrans_write_instruction__addu_etc():
 */
static int bintrans_write_instruction__addu_etc(unsigned char **addrp,
	int rd, int rs, int rt, int sa, int instruction_type)
{
	if (instruction_type == SPECIAL_SLL && rd == 0) {
		bintrans_write_pc_inc(addrp);
		return 1;
	}

	return 0;
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
	return 0;
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
	return 0;
}


/*
 *  bintrans_write_instruction__mfmthilo():
 */
static int bintrans_write_instruction__mfmthilo(unsigned char **addrp,
	int rd, int from_flag, int hi_flag)
{
	return 0;
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

