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
 *  $Id: bintrans.c,v 1.83 2004-11-24 08:53:26 debug Exp $
 *
 *  Dynamic binary translation.
 *
 *
 *  This should be documented/commented better. Some of the main concepts are:
 *
 *	o)  Keep a translation cache of a certain number of blocks.
 *
 *	o)  Only translate simple instructions. (For example, the 'tlbwr'
 *	    instruction is not translated.)
 *
 *	o)  Translate code in physical ram, not virtual. This will keep
 *	    things translated over process switches, and TLB updates.
 *
 *	o)  When the translation cache is "full", then throw away everything
 *	    translated so far and restart from scratch. The cache is of a
 *	    fixed size, say 20 MB. (This is inspired by a comment in the Qemu
 *	    technical documentation.)
 *
 *	o)  Do not translate over MIPS page boundaries (4 KB).
 *	    (TODO: Perhaps it would be possible if we're running in kernel
 *	    space? But it this would then require special checks at the
 *	    end of each page.)
 *
 *	o)  If memory is overwritten, any translated block for that page
 *	    must be invalidated. (It is removed from the cache so that it
 *	    cannot be found on lookups.)
 *
 *	o)  Only run a certain number of instructions, before returning to
 *	    the main loop. (This is needed in order to allow devices to
 *	    cause interrupts, and so on.)
 *
 *	o)  Check for exceptions inside the block, for those instructions
 *	    that require that.  Update the program counter by the number
 *	    of successfully executed instructions only.
 *
 *	o)  There is no "intermediate representation"; everything is translated
 *	    directly from MIPS machine code to target machine code.
 *
 *	o)  Theoretical support for multiple target architectures (Alpha,
 *	    i386, sparc, mips :-), ...), but only Alpha implemented so far.
 *
 *	o)  Load/stores: TODO: Comment.
 *
 *  Testing:  Running regression tests with and without the binary translator
 *  enabled should obviously result in the exact same results, or something is
 *  wrong.
 *
 *  The general idea is something like this:
 *
 *	Check for the current PC (actually: its physical form) in the
 *	translation cache. If it is found, then run the translated code chunk,
 *	otherwise try to translate and then run it.
 *
 *  A few checks are made though, to make sure that the environment is "safe"
 *  enough; starting inside a delay slot or "nullified" slot is considered
 *  non-safe.
 */


#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/mman.h>

#include "misc.h"

#include "bintrans.h"
#include "memory.h"


#ifndef BINTRANS
/*
 *  No bintrans, then let's supply dummy functions:
 */

int bintrans_pc_is_in_cache(struct cpu *cpu, uint64_t pc) { return 0; }
void bintrans_invalidate(struct cpu *cpu, uint64_t paddr) { }
int bintrans_attempt_translate(struct cpu *cpu, uint64_t paddr,
	int run_flag) { return 0; }
void bintrans_init_cpu(struct cpu *cpu) { }
void bintrans_init(void)
{
	fatal("NOT starting bintrans, as mips64emul was compiled without such support!\n");
}

#else


/*  Function declaration, should be the same as in bintrans_*.c:  */

static void bintrans_host_cacheinvalidate(unsigned char *p, size_t len);
static void bintrans_write_chunkreturn(unsigned char **addrp);
static void bintrans_write_chunkreturn_fail(unsigned char **addrp);
static void bintrans_write_pc_inc(unsigned char **addrp, int pc_inc,
	int flag_pc, int flag_ninstr);
static void bintrans_runchunk(struct cpu *cpu, unsigned char *code);
static void bintrans_write_quickjump(unsigned char *quickjump_code, uint32_t chunkoffset);
static int bintrans_write_instruction__addiu_etc(unsigned char **addrp, int rt, int rs, int imm, int instruction_type);
static int bintrans_write_instruction__addu_etc(unsigned char **addrp, int rd, int rs, int rt, int sa, int instruction_type);
static int bintrans_write_instruction__branch(unsigned char **addrp, int instruction_type, int regimm_type, int rt, int rs, int imm);
static int bintrans_write_instruction__jr(unsigned char **addrp, int rs, int rd, int special);
static int bintrans_write_instruction__jal(unsigned char **addrp, int imm, int link);
static int bintrans_write_instruction__delayedbranch(unsigned char **addrp, uint32_t *potential_chunk_p, uint32_t *chunks, int only_care_about_chunk_p, int p);
static int bintrans_write_instruction__loadstore(unsigned char **addrp, int rt, int imm, int rs, int instruction_type, int bigendian);
static int bintrans_write_instruction__lui(unsigned char **addrp, int rt, int imm);
static int bintrans_write_instruction__mfmthilo(unsigned char **addrp, int rd, int from_flag, int hi_flag);
static int bintrans_write_instruction__mfc_mtc(unsigned char **addrp, int coproc_nr, int flag64bit, int rt, int rd, int mtcflag);
static int bintrans_write_instruction__tlb_rfe_etc(unsigned char **addrp, int itype);

#define	TLB_TLBWI	0
#define	TLB_TLBWR	1
#define	TLB_TLBP	2
#define	TLB_TLBR	3
#define	TLB_RFE		4
#define	TLB_ERET	5


#define	BINTRANS_CACHE_N_INDEX_BITS	14
#define	CACHE_INDEX_MASK		((1 << BINTRANS_CACHE_N_INDEX_BITS) - 1)
#define	PADDR_TO_INDEX(p)		((p >> 12) & CACHE_INDEX_MASK)

#ifndef BINTRANS_SIZE_IN_MB
#define BINTRANS_SIZE_IN_MB	20
#endif

#define	CODE_CHUNK_SPACE_SIZE		(BINTRANS_SIZE_IN_MB * 1048576)
#define	CODE_CHUNK_SPACE_MARGIN		262144

/*
 *  translation_code_chunk_space is a large chunk of (linear) memory where
 *  translated code chunks and translation_entrys are stored. When this is
 *  filled, we restart from scratch (by resetting
 *  translation_code_chunk_space_head to 0, and removing all translation
 *  entries).
 *
 *  (Using a static memory region like this is somewhat inspired by the QEMU
 *  web pages, http://fabrice.bellard.free.fr/qemu/qemu-tech.html#SEC13)
 */
unsigned char *translation_code_chunk_space;
size_t translation_code_chunk_space_head;

struct translation_page_entry {
	struct translation_page_entry	*next;

	uint64_t			paddr;

	int				page_is_potentially_in_use;

	uint32_t			chunk[1024];
	char				flags[1024];
};
#define	UNTRANSLATABLE		0x01

static struct translation_page_entry **translation_page_entry_array;


#define	MAX_QUICK_JUMPS		10
static unsigned char *quick_jump_host_address[MAX_QUICK_JUMPS];
static int quick_jump_page_offset[MAX_QUICK_JUMPS];
static int n_quick_jumps;
static int quick_jumps_index;		/*  where to write the next quick jump  */

static void bintrans_register_potential_quick_jump(unsigned char *a, int p)
{
	/*  printf("%02i: a=%016llx p=%i\n", quick_jumps_index, a, p);  */
	quick_jump_host_address[quick_jumps_index] = a;
	quick_jump_page_offset[quick_jumps_index] = p;
	quick_jumps_index ++;
	if (quick_jumps_index > n_quick_jumps)
		n_quick_jumps = quick_jumps_index;
	if (quick_jumps_index >= MAX_QUICK_JUMPS)
		quick_jumps_index = 0;
}


/*  Include host architecture specific bintrans code:  */

#ifdef ALPHA
#include "bintrans_alpha.c"
#else
#ifdef I386
#include "bintrans_i386.c"
#else
#ifdef MIPS
#include "bintrans_mips.c"
#else
#ifdef SPARCV9
#include "bintrans_sparcv9.c"
#else
#error Unsupported host architecture for bintrans.
#endif	/*  SPARCV9  */
#endif	/*  MIPS  */
#endif	/*  I386  */
#endif	/*  ALPHA  */


/*
 *  bintrans_invalidate():
 *
 *  Invalidate translations containing a certain physical address.
 */
void bintrans_invalidate(struct cpu *cpu, uint64_t paddr)
{
	int entry_index = PADDR_TO_INDEX(paddr);
	struct translation_page_entry *tep;
struct translation_page_entry *prev = NULL;
	uint64_t paddr_page = paddr & ~0xfff;

	tep = translation_page_entry_array[entry_index];
	while (tep != NULL) {
		if (tep->paddr == paddr_page)
#if 0
			break;
#else
{
			if (prev == NULL)
				translation_page_entry_array[entry_index] = tep->next;
			else
				prev->next = tep->next;
			return;
}
#endif
prev = tep;
		tep = tep->next;
	}
	if (tep == NULL)
		return;

	if (!tep->page_is_potentially_in_use)
		return;

	memset(&tep->chunk[0], 0, sizeof(tep->chunk));
	memset(&tep->flags[0], 0, sizeof(tep->flags));

	tep->page_is_potentially_in_use = 0;
	return;
}


/*
 *  bintrans_attempt_translate():
 *
 *  Attempt to translate a chunk of code, starting at 'paddr'. If successful,
 *  and "run_flag" is non-zero, then the code chunk is run.
 *
 *  Returns -1 if no code translation occured, otherwise the generated code
 *  chunk is added to the translation_entry_array. The return value is then
 *  the number of instructions executed.
 */
int bintrans_attempt_translate(struct cpu *cpu, uint64_t paddr, int run_flag)
{
	uint64_t paddr_page;
	int offset_within_page;
	int entry_index;
	unsigned char *host_mips_page;
	unsigned char *ca, *ca_justdid, *ca2;
	int res, hi6, special6, regimm5;
	unsigned char instr[4];
	int old_n_executed;
	size_t p;
	int try_to_translate;
	int n_translated, translated;
	unsigned char *f;
	struct translation_page_entry *tep;
	size_t chunk_len;
	int rs,rt=0,rd,sa,imm;
	uint32_t *potential_chunk_p;	/*  for branches  */
	int byte_order_cached_bigendian;
	int delayed_branch, stop_after_delayed_branch;
	uint64_t delayed_branch_new_p=0;
	int prev_p;


	/*
	 *  If the chunk space is all used up, we need to start over from
	 *  an empty chunk space.
	 */
	if (translation_code_chunk_space_head >= CODE_CHUNK_SPACE_SIZE) {
		int i, n = 1 << BINTRANS_CACHE_N_INDEX_BITS;
		for (i=0; i<n; i++)
			translation_page_entry_array[i] = NULL;
		translation_code_chunk_space_head = 0;
		debug("bintrans: Starting over!\n");
	}


	/*  Abort if the current "environment" isn't safe enough:  */
	if (cpu->delay_slot || cpu->nullify_next || (paddr & 3) != 0)
		return -1;

	/*  Is this a part of something that is already translated?  */
	paddr_page = paddr & ~0xfff;
	offset_within_page = (paddr & 0xfff) / 4;
	entry_index = PADDR_TO_INDEX(paddr);
	tep = translation_page_entry_array[entry_index];
	while (tep != NULL) {
		if (tep->paddr == paddr_page) {
			if (tep->flags[offset_within_page] & UNTRANSLATABLE)
				return -1;
			if (tep->chunk[offset_within_page] != 0) {
				if (!run_flag)
					return -1;

				f = (size_t)tep->chunk[offset_within_page] +
				    translation_code_chunk_space;
				goto run_it;	/*  see further down  */
			}
			break;
		}
		tep = tep->next;
	}

	host_mips_page = cpu->pc_bintrans_host_4kpage;
	if (host_mips_page == NULL)
		return -1;


	if (tep == NULL) {
		/*  Allocate a new translation page entry:  */
		tep = (void *)(size_t) (translation_code_chunk_space +
		    translation_code_chunk_space_head);
		translation_code_chunk_space_head += sizeof(struct translation_page_entry);

		/*  ... and align again:  */
		translation_code_chunk_space_head =
		    ((translation_code_chunk_space_head - 1) | 31) + 1;

		/*  Add the entry to the array:  */
		memset(tep, 0, sizeof(struct translation_page_entry));
		tep->next = translation_page_entry_array[entry_index];
		translation_page_entry_array[entry_index] = tep;
		tep->paddr = paddr_page;
	}

	/*  printf("translation_page_entry_array[%i] = %p, ofs = %i\n",
	    entry_index, translation_page_entry_array[entry_index], offset_within_page);  */

	/*  ca is the "chunk address"; where to start generating a chunk:  */
	ca = translation_code_chunk_space
	    + translation_code_chunk_space_head;

	/*
	 *  Try to translate a chunk of code:
	 */

	byte_order_cached_bigendian = (cpu->byte_order == EMUL_BIG_ENDIAN);
	p = paddr & 0xfff;
	try_to_translate = 1;
	n_translated = 0;
	res = 0;
	delayed_branch = 0;
	stop_after_delayed_branch = 0;

	n_quick_jumps = quick_jumps_index = 0;

	while (try_to_translate) {
		ca_justdid = ca;
		prev_p = p/4;
		translated = 0;

		/*  Read an instruction word from host memory:  */
		*((uint32_t *)&instr[0]) = *((uint32_t *)(host_mips_page + p));

		if (byte_order_cached_bigendian) {
			int tmp;
			tmp = instr[0]; instr[0] = instr[3]; instr[3] = tmp;
			tmp = instr[1]; instr[1] = instr[2]; instr[2] = tmp;
		}

		hi6 = instr[3] >> 2;

		/*  Check for instructions that can be translated:  */
		switch (hi6) {

		case HI6_REGIMM:
			regimm5 = instr[2] & 0x1f;
			switch (regimm5) {
			case REGIMM_BLTZ:
			case REGIMM_BGEZ:
				rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
				imm = (instr[1] << 8) + instr[0];
				if (imm >= 32768)
					imm -= 65536;  
				translated = try_to_translate = bintrans_write_instruction__branch(&ca, hi6, regimm5, rt, rs, imm);
				n_translated += translated;
				delayed_branch = 2;
				delayed_branch_new_p = p + 4 + 4*imm;
				break;
			default:
				try_to_translate = 0;
				/*  Untranslatable:  */
				/*  TODO: this code should only be in one place  */
				bintrans_write_chunkreturn_fail(&ca);
				tep->flags[prev_p] |= UNTRANSLATABLE;
				try_to_translate = 0;
			}
			break;

		case HI6_SPECIAL:
			special6 = instr[0] & 0x3f;
			switch (special6) {
			case SPECIAL_JR:
			case SPECIAL_JALR:
				rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
				rd = (instr[1] >> 3) & 31;
				translated = try_to_translate = bintrans_write_instruction__jr(&ca, rs, rd, special6);
				n_translated += translated;
				delayed_branch = 2;
				delayed_branch_new_p = -1;	/*  anything, not within this physical page  */
				if (special6 == SPECIAL_JR)
					stop_after_delayed_branch = 1;
				break;
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
			case SPECIAL_DSLL32:
			case SPECIAL_SRA:
			case SPECIAL_SRAV:
			case SPECIAL_SRLV:
			case SPECIAL_SRL:
			case SPECIAL_DSRA:
			case SPECIAL_DSRA32:
			case SPECIAL_DSRL:
			case SPECIAL_DSRL32:
			case SPECIAL_SLT:
			case SPECIAL_SLTU:
			case SPECIAL_MULT:
			case SPECIAL_MULTU:
			case SPECIAL_SYNC:
				rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
				rt = instr[2] & 31;
				rd = (instr[1] >> 3) & 31;
				sa = ((instr[1] & 7) << 2) + ((instr[0] >> 6) & 3);

				/*  treat SYNC as a nop :-)  */
				if (special6 == SPECIAL_SYNC) {
					rd = rt = rs = sa = 0;
					special6 = SPECIAL_SLL;
				}
				translated = try_to_translate = bintrans_write_instruction__addu_etc(&ca, rd, rs, rt, sa, special6);
				n_translated += translated;
				break;
			case SPECIAL_MFHI:
			case SPECIAL_MFLO:
			case SPECIAL_MTHI:
			case SPECIAL_MTLO:
				rd = (instr[1] >> 3) & 31;
				rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
				translated = try_to_translate = bintrans_write_instruction__mfmthilo(&ca,
				    (special6 == SPECIAL_MFHI || special6 == SPECIAL_MFLO)? rd : rs,
				    special6 == SPECIAL_MFHI || special6 == SPECIAL_MFLO,
				    special6 == SPECIAL_MFHI || special6 == SPECIAL_MTHI);
				n_translated += translated;
				break;
			default:
				/*  Untranslatable:  */
				/*  TODO: this code should only be in one place  */
				bintrans_write_chunkreturn_fail(&ca);
				tep->flags[prev_p] |= UNTRANSLATABLE;
				try_to_translate = 0;
			}
			break;

		case HI6_BEQ:
		case HI6_BNE:
		case HI6_BGTZ:
		case HI6_BLEZ:
			rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
			rt = instr[2] & 31;
			imm = (instr[1] << 8) + instr[0];
			if (imm >= 32768)
				imm -= 65536;
			translated = try_to_translate = bintrans_write_instruction__branch(&ca, hi6, 0, rt, rs, imm);
			n_translated += translated;
			delayed_branch = 2;
			delayed_branch_new_p = p + 4 + 4*imm;
			break;

		case HI6_CACHE:
			translated = try_to_translate = bintrans_write_instruction__addu_etc(&ca, 0, 0, 0, 0, SPECIAL_SLL);
			n_translated += translated;
			break;

		case HI6_LUI:
			rt = instr[2] & 31;
			imm = (instr[1] << 8) + instr[0];
			translated = try_to_translate = bintrans_write_instruction__lui(&ca, rt, imm);
			n_translated += translated;
			break;

		case HI6_LQ_MDMX:
		case HI6_LD:
		case HI6_LWU:
		case HI6_LW:
		case HI6_LHU:
		case HI6_LH:
		case HI6_LBU:
		case HI6_LB:
		case HI6_SQ:
		case HI6_SD:
		case HI6_SW:
		case HI6_SH:
		case HI6_SB:
			rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
			rt = instr[2] & 31;
			imm = (instr[1] << 8) + instr[0];
			if (imm >= 32768)
				imm -= 65536;
switch (cpu->cpu_type.mmu_model) {
case MMU3K:
			translated = try_to_translate = bintrans_write_instruction__loadstore(&ca, rt, imm, rs, hi6, byte_order_cached_bigendian);
	break;
default:
	translated = try_to_translate = 0;
}
			n_translated += translated;
			break;

		case HI6_J:
		case HI6_JAL:
			imm = (((instr[3] & 3) << 24) + (instr[2] << 16) +
			    (instr[1] << 8) + instr[0]) & 0x03ffffff;
			translated = try_to_translate = bintrans_write_instruction__jal(&ca, imm, hi6 == HI6_JAL);
			n_translated += translated;
			delayed_branch = 2;
			delayed_branch_new_p = -1;
			if (hi6 == HI6_J)
				stop_after_delayed_branch = 1;
			break;

		case HI6_ADDIU:
		case HI6_DADDIU:
		case HI6_ANDI:
		case HI6_ORI:
		case HI6_XORI:
		case HI6_SLTI:
		case HI6_SLTIU:
			rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
			rt = instr[2] & 31;
			imm = (instr[1] << 8) + instr[0];
			translated = try_to_translate = bintrans_write_instruction__addiu_etc(&ca, rt, rs, imm, hi6);
			n_translated += translated;
			break;

		case HI6_COP0:
			if (instr[3] == 0x42 && instr[2] == 0x00 && instr[1] == 0x00 && instr[0] == 0x10) {
				/*  rfe:  */
				translated = bintrans_write_instruction__tlb_rfe_etc(&ca, TLB_RFE);
				n_translated += translated;
 				try_to_translate = 0;
			} else if (instr[3] == 0x42 && instr[2] == 0x00 && instr[1] == 0x00 && instr[0] == 0x18) {
				/*  eret:  */
				translated = bintrans_write_instruction__tlb_rfe_etc(&ca, TLB_ERET);
				n_translated += translated;
 				try_to_translate = 0;
			} else if (instr[3] == 0x40 && (instr[2] & 0xe0)==0 && (instr[1]&7)==0 && instr[0]==0) {
				/*  mfc0:  */
				rt = instr[2] & 31;
				rd = (instr[1] >> 3) & 31;
				translated = try_to_translate = bintrans_write_instruction__mfc_mtc(&ca, 0, 0, rt, rd, 0);
				n_translated += translated;
			} else if (instr[3] == 0x40 && (instr[2] & 0xe0)==0x20 && (instr[1]&7)==0 && instr[0]==0) {
				/*  dmfc0:  */
				rt = instr[2] & 31;
				rd = (instr[1] >> 3) & 31;
				translated = try_to_translate = bintrans_write_instruction__mfc_mtc(&ca, 0, 1, rt, rd, 0);
				n_translated += translated;
			} else if (instr[3] == 0x40 && (instr[2] & 0xe0)==0x80 && (instr[1]&7)==0 && instr[0]==0) {
				/*  mtc0:  */
				rt = instr[2] & 31;
				rd = (instr[1] >> 3) & 31;
				translated = try_to_translate = bintrans_write_instruction__mfc_mtc(&ca, 0, 0, rt, rd, 1);
				n_translated += translated;
			} else if (instr[3] == 0x40 && (instr[2] & 0xe0)==0xa0 && (instr[1]&7)==0 && instr[0]==0) {
				/*  dmtc0:  */
				rt = instr[2] & 31;
				rd = (instr[1] >> 3) & 31;
				translated = try_to_translate = bintrans_write_instruction__mfc_mtc(&ca, 0, 1, rt, rd, 1);
				n_translated += translated;
			} else if (instr[3] == 0x42 && instr[2] == 0 && instr[1] == 0 && instr[0] == 2) {
				/*  tlbwi:  */
				translated = try_to_translate = bintrans_write_instruction__tlb_rfe_etc(&ca, TLB_TLBWI);
				n_translated += translated;
			} else if (instr[3] == 0x42 && instr[2] == 0 && instr[1] == 0 && instr[0] == 6) {
				/*  tlbwr:  */
				translated = try_to_translate = bintrans_write_instruction__tlb_rfe_etc(&ca, TLB_TLBWR);
				n_translated += translated;
			} else if (instr[3] == 0x42 && instr[2] == 0 && instr[1] == 0 && instr[0] == 8) {
				/*  tlbp:  */
				translated = try_to_translate = bintrans_write_instruction__tlb_rfe_etc(&ca, TLB_TLBP);
				n_translated += translated;
			} else if (instr[3] == 0x42 && instr[2] == 0 && instr[1] == 0 && instr[0] == 1) {
				/*  tlbr:  */
				translated = try_to_translate = bintrans_write_instruction__tlb_rfe_etc(&ca, TLB_TLBR);
				n_translated += translated;
			} else
				try_to_translate = 0;
			break;

		default:
			/*  Untranslatable:  */
			/*  TODO: this code should only be in one place  */
			bintrans_write_chunkreturn_fail(&ca);
			tep->flags[prev_p] |= UNTRANSLATABLE;
			try_to_translate = 0;
		}

		if (translated && delayed_branch) {
			delayed_branch --;
			if (delayed_branch == 0) {
				/*
				 *  p is 0x000 .. 0xffc. If the jump is to
				 *  within the same page, then we can use
				 *  the same translation page to check if
				 *  there already is a translation.
				 */
				if ((delayed_branch_new_p & ~0xfff) == 0)
					potential_chunk_p =
					    &tep->chunk[delayed_branch_new_p/4];
				else
					potential_chunk_p = NULL;
				bintrans_write_instruction__delayedbranch(&ca,
				    potential_chunk_p, &tep->chunk[0], 0,
				    delayed_branch_new_p & 0xfff);

				if (stop_after_delayed_branch)
					try_to_translate = 0;
			}
		}

		if (translated) {
			int i;

			if (tep->chunk[prev_p] == 0)
				tep->chunk[prev_p] = (uint32_t)
				    ((size_t)ca_justdid -
				    (size_t)translation_code_chunk_space);

			/*  Quickjump to the translated instruction from some
			    previous instruction?  */
			for (i=0; i<n_quick_jumps; i++)
				if (quick_jump_page_offset[i] == p)
					bintrans_write_quickjump(
					    quick_jump_host_address[i],
					    tep->chunk[prev_p]);
		}

		/*  Glue together with previously translated code, if any:  */
		if (translated && try_to_translate && n_translated > 30 &&
		    prev_p < 1020 && tep->chunk[prev_p+1] != 0 &&
		    !delayed_branch) {
			bintrans_write_instruction__delayedbranch(
			    &ca, &tep->chunk[prev_p+1], NULL, 1, prev_p+1);
			try_to_translate = 0;
		}

		if (n_translated > 200)
			try_to_translate = 0;

		p += sizeof(instr);

		/*  If we have reached a different (MIPS) page, then stop translating.  */
		if (p == 0x1000)
			try_to_translate = 0;
	}

	tep->page_is_potentially_in_use = 1;

	/*  Not enough translated? Then abort.  */
	if (n_translated < 1) {
		tep->flags[offset_within_page] |= UNTRANSLATABLE;
		return -1;
	}

	/*  ca2 = ptr to the head of the new code chunk  */
	ca2 = translation_code_chunk_space +
	    translation_code_chunk_space_head;

	/*  Add return code:  */
	bintrans_write_chunkreturn(&ca);

	/*  chunk_len = nr of bytes occupied by the new code chunk  */
	chunk_len = (size_t)ca - (size_t)ca2;

	/*  Invalidate the host's instruction cache, if neccessary:  */
	bintrans_host_cacheinvalidate(ca2, chunk_len);

	translation_code_chunk_space_head += chunk_len;

	/*  Align the code chunk space:  */
	translation_code_chunk_space_head =
	    ((translation_code_chunk_space_head - 1) | 31) + 1;

	if (!run_flag)
		return 0;


	/*  RUN the code chunk:  */
	f = ca2;
run_it:

	/*  printf("BEFORE: pc=%016llx r31=%016llx\n",
	    (long long)cpu->pc, (long long)cpu->gpr[31]); */

	old_n_executed = cpu->bintrans_instructions_executed;

	bintrans_runchunk(cpu, f);

	/*  printf("AFTER:  pc=%016llx r31=%016llx\n",
	    (long long)cpu->pc, (long long)cpu->gpr[31]);  */

	if (!cpu->delay_slot && !cpu->nullify_next &&
	    cpu->bintrans_instructions_executed < N_SAFE_BINTRANS_LIMIT
	    && (cpu->pc & 3) == 0
	    && cpu->bintrans_instructions_executed != old_n_executed) {
		uint64_t paddr;
		uint64_t old_pc;
		int ok;

		old_pc = cpu->pc_last = cpu->pc;

		ok = translate_address(cpu, cpu->pc, &paddr,
		    FLAG_INSTR);

		if (!ok && old_pc != cpu->pc) {
			ok = translate_address(cpu, cpu->pc, &paddr,
			    FLAG_INSTR + FLAG_NOEXCEPTIONS);
		}

		if (ok) {
			/*  TODO: how about code between devices?  */
			if (paddr >= cpu->mem->mmap_dev_minaddr && paddr < cpu->mem->mmap_dev_maxaddr)
				ok = 0;
		}

		if (ok) {
			paddr_page = paddr & ~0xfff;
			offset_within_page = (paddr & 0xfff) / 4;
			entry_index = PADDR_TO_INDEX(paddr);
			tep = translation_page_entry_array[entry_index];
			while (tep != NULL) {
				if (tep->paddr == paddr_page) {
					if (tep->flags[offset_within_page] & UNTRANSLATABLE)
						break;
					if (tep->chunk[offset_within_page] != 0) {
						f = (size_t)tep->chunk[offset_within_page] +
						    translation_code_chunk_space;
						goto run_it;	/*  see further down  */
					}
					break;
				}
				tep = tep->next;
			}
		}
	}

	return cpu->bintrans_instructions_executed;
}


/*
 *  bintrans_init_cpu():
 *
 *  This must be called for each cpu wishing to use bintrans. This should
 *  be called after bintrans_init(), but before any other function in this
 *  module.
 */
void bintrans_init_cpu(struct cpu *cpu)
{
	int i;

	cpu->chunk_base_address   = translation_code_chunk_space;
	cpu->bintrans_fast_tlbwri = coproc_tlbwri;
	cpu->bintrans_fast_tlbpr  = coproc_tlbpr;
	cpu->bintrans_fast_rfe    = coproc_rfe;
	cpu->bintrans_fast_eret   = coproc_eret;

	/*  Initialize vaddr->hostaddr translation tables:  */
	switch (cpu->cpu_type.mmu_model) {
	case MMU3K:
		cpu->vaddr_to_hostaddr_nulltable =
		    zeroed_alloc(sizeof(struct vth32_table));

		cpu->vaddr_to_hostaddr_table0_kernel =
		    zeroed_alloc(1024 * sizeof(struct vth32_table *));
		cpu->vaddr_to_hostaddr_table0_user =
		    zeroed_alloc(1024 * sizeof(struct vth32_table *));

		for (i=0; i<1024; i++) {
			cpu->vaddr_to_hostaddr_table0_kernel[i] = cpu->vaddr_to_hostaddr_nulltable;
			cpu->vaddr_to_hostaddr_table0_user[i] = cpu->vaddr_to_hostaddr_nulltable;
		}

		cpu->vaddr_to_hostaddr_table0 = cpu->vaddr_to_hostaddr_table0_kernel;

		break;
	}
}


/*
 *  bintrans_init():
 *
 *  Should be called before any other bintrans_*() function is used.
 */
void bintrans_init(void)
{
	int res, i, n = 1 << BINTRANS_CACHE_N_INDEX_BITS;
	size_t s;

	debug("bintrans: EXPERIMENTAL!\n");

	s = 1 << BINTRANS_CACHE_N_INDEX_BITS;
	s *= sizeof(struct translation_page_entry *);
	translation_page_entry_array = (void *) mmap(NULL, s,
	    PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
	if (translation_page_entry_array == NULL) {
		translation_page_entry_array = malloc(s);
		if (translation_page_entry_array == NULL) {
			fprintf(stderr, "bintrans_init(): out of memory (1)\n");
			exit(1);
		}

		/*
		 *  The entry array must be NULLed, as these are pointers to
		 *  translation page entries. If the mmap() succeeded, then
		 *  the array is zero-filled by default anyway...
		 */
		for (i=0; i<n; i++)
			translation_page_entry_array[i] = NULL;
	}

	debug("bintrans: translation_page_entry_array = %i KB at %p\n",
	    (int)(s/1024), translation_page_entry_array);

	/*  Allocate the large code chunk space:  */
	s = CODE_CHUNK_SPACE_SIZE + CODE_CHUNK_SPACE_MARGIN;
	translation_code_chunk_space = (unsigned char *) mmap(NULL, s,
	    PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANON | MAP_PRIVATE, -1, 0);

	/*  If mmap() failed, try malloc():  */
	if (translation_code_chunk_space == NULL) {
		translation_code_chunk_space = malloc(s);
		if (translation_code_chunk_space == NULL) {
			fprintf(stderr, "bintrans_init(): out of memory (2)\n");
			exit(1);
		}
	}

	debug("bintrans: translation_code_chunk_space = %i MB at %p\n",
	    (int)(s/1048576), translation_code_chunk_space);

	/*
	 *  The translation_code_chunk_space does not need to be zeroed,
	 *  but the pointers to where in the chunk space we are about to
	 *  add new chunks must be initialized to the beginning of the
	 *  chunk space.
	 */
	translation_code_chunk_space_head = 0;

	/*
	 *  Some operating systems (for example OpenBSD using the default
	 *  stack protection settings in GCC) don't allow code to be 
	 *  dynamically created in memory and executed. This will attempt
	 *  to enable execution of the code chunk space.
	 *
	 *  NOTE/TODO: A Linux man page for mprotect from 1997 says that
	 *  "POSIX.1b says that mprotect can be used only on regions
	 *  of memory obtained from mmap(2).".  If malloc() isn't implemented
	 *  using mmap(), then this could be a problem.
	 */
	res = mprotect((void *)translation_code_chunk_space,
	    s, PROT_READ | PROT_WRITE | PROT_EXEC);
	if (res)
		debug("warning: mprotect() failed with errno %i."
		    " this usually doesn't really matter...\n", errno);
}

#endif	/*  BINTRANS  */
