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
 *  $Id: bintrans.c,v 1.44 2004-11-08 16:09:02 debug Exp $
 *
 *  Dynamic binary translation.
 *
 *  NOTE:  This file is basically a place for me to write down my ideas,
 *         the dynamic binary translation system isn't really working
 *         again yet.
 *
 *
 *	Keep a cache of a certain number of blocks.
 *
 *	Only translate simple instructions. (For example, the 'cache' and
 *	'tlbwr' instructions are NOT simple enough.)
 *
 *	Translate code in physical ram, not virtual. (This will keep things
 *	translated over process switches, and TLB updates.)
 *
 *	Do not translate over MIPS page boundaries (4 KB), unless we're
 *	running in kernel-space (where the "next physical page" means
 *	"the next virtual page" as well).
 *
 *	If memory is overwritten, any translated block for that page must
 *	be invalidated. (It is removed from the cache so that it cannot be
 *	found on lookups, and the code chunk is overwritten with a simple
 *	return instruction which does nothing. The later is needed because
 *	other translated code chunks may still try to jump to this one.)
 *		TODO: instead of just returning, maybe a hint should be
 *		given that the block has been removed, so that other blocks
 *		jumping to this block will also be invalidated?
 *
 *	Check before running a basic block that no external
 *		exceptions will occur for the duration of the
 *		block, and count down so that we can run for as
 *		long as possible in bintrans mode.
 *		(External = from a hardware device.)
 *
 *	Check for exceptions inside the block, for those instructions
 *		that require that.  Update the instruction counter by
 *		the number of successfully executed instructions only.
 *
 *	Don't do dynamic register allocation:
 *		For alpha, manipulate the cpu struct for "uncommon"
 *			registers, store common registers in alpha
 *			registers and write back at end. (TODO:
 *			experiments must reveal which registers are
 *			common and which aren't.)
 *		For i386, manipulate the cpu struct directly.
 *
 *	Multiple target archs (alpha, i386, sparc, mips :-), ...)
 *		Try to use arch specific optimizations, such as prefetch
 *		on alphas that support that.
 *		Not all instructions will be easily translated to all
 *		backends.
 *
 *	How to do loads/stores?
 *		Caches of some kind, or hard-coded at translation time?
 *		Hard-coded would mean that a translation is valid as
 *		long as the TLB entries that that translation entry
 *		depends on aren't modified.
 *
 *	Testing:  Running regression tests with and without the binary
 *		translator enabled should obviously result in the exact
 *		same results, or something is wrong.
 *
 *	JUMPS:  Blocks need to be glued together efficiently.
 *
 *
 *  The general idea would be something like this:
 *  A block of code in the host's memory would have to conform with
 *  the C function calling ABI on that platform, so that the code block
 *  can be called easily from C. That is, entry and exit code needs to
 *  be added, in addition to the translated instructions.
 *
 *	o)  Check for the current PC (actually: its physical form) in the
 *		translation cache.
 *
 *	o)  If the current PC is not found, then make a decision
 *		regarding making a translation attempt, or not.
 *		Fallback to normal instruction execution in cpu.c.
 *
 *	o)  If there is a code block for the current PC in the
 *		translation cache, do a couple of checks and then
 *		run the block. Update registers and other values
 *		as neccessary.
 *
 *  The checks would include:
 *	don't start running inside a delay slot or "likely" slot.
 *	nr of cycles until the next hardware interrupt occurs
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
	int run_flag, int translate_depth) { return 0; }
void bintrans_init(void)
{
	fatal("NOT starting bintrans, as mips64emul was compiled without such support!\n");
}

#else


/*  Function declaration, should be the same as in bintrans_*.c:  */

void bintrans_host_cacheinvalidate(unsigned char *p, size_t len);
size_t bintrans_chunk_header_len(void);
void bintrans_write_chunkhead(unsigned char *p);
void bintrans_write_chunkreturn(unsigned char **addrp);
void bintrans_write_pcflush(unsigned char **addrp, int *pc_increment,
	int flag_pc, int flag_ninstr);

int bintrans_write_instruction__addiu(unsigned char **addrp, int *pc_increment, int rt, int rs, int imm);
int bintrans_write_instruction__andi(unsigned char **addrp, int *pc_increment, int rt, int rs, int imm);
int bintrans_write_instruction__ori(unsigned char **addrp, int *pc_increment, int rt, int rs, int imm);
int bintrans_write_instruction__xori(unsigned char **addrp, int *pc_increment, int rt, int rs, int imm);
int bintrans_write_instruction__addu(unsigned char **addrp, int *pc_increment, int rd, int rs, int rt);
int bintrans_write_instruction__subu(unsigned char **addrp, int *pc_increment, int rd, int rs, int rt);
int bintrans_write_instruction__and(unsigned char **addrp, int *pc_increment, int rd, int rs, int rt);
int bintrans_write_instruction__or(unsigned char **addrp, int *pc_increment, int rd, int rs, int rt);
int bintrans_write_instruction__nor(unsigned char **addrp, int *pc_increment, int rd, int rs, int rt);
int bintrans_write_instruction__xor(unsigned char **addrp, int *pc_increment, int rd, int rs, int rt);
int bintrans_write_instruction__slt(unsigned char **addrp, int *pc_increment, int rd, int rs, int rt);
int bintrans_write_instruction__sltu(unsigned char **addrp, int *pc_increment, int rd, int rs, int rt);
int bintrans_write_instruction__sll(unsigned char **addrp, int *pc_increment, int rd, int rt, int sa);
int bintrans_write_instruction__sra(unsigned char **addrp, int *pc_increment, int rd, int rt, int sa);
int bintrans_write_instruction__srl(unsigned char **addrp, int *pc_increment, int rd, int rt, int sa);
int bintrans_write_instruction__lui(unsigned char **addrp, int *pc_increment, int rt, int imm);
int bintrans_write_instruction__lw(unsigned char **addrp, int *pc_increment, int first_load, int first_store, int rt, int imm, int rs, int load_type);
int bintrans_write_instruction__jr(unsigned char **addrp, int *pc_increment, int rs);
int bintrans_write_instruction__jalr(unsigned char **addrp, int *pc_increment, int rd, int rs);
int bintrans_write_instruction__mfmthilo(unsigned char **addrp, int *pc_increment, int rd, int from_flag, int hi_flag);

#define	LOAD_TYPE_LW	0
#define	LOAD_TYPE_LHU	1
#define	LOAD_TYPE_LH	2
#define	LOAD_TYPE_LBU	3
#define	LOAD_TYPE_LB	4
#define	LOAD_TYPE_SW	5
#define	LOAD_TYPE_SH	6
#define	LOAD_TYPE_SB	7


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


#define	BINTRANS_CACHE_N_INDEX_BITS	14
#define	CACHE_INDEX_MASK		((1 << BINTRANS_CACHE_N_INDEX_BITS) - 1)
#define	PADDR_TO_INDEX(p)		((p >> 12) & CACHE_INDEX_MASK)

#define	CODE_CHUNK_SPACE_SIZE		(16 * 1048576)
#define	CODE_CHUNK_SPACE_MARGIN		131072

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

	unsigned char			*chunk[1024];
	int				length_and_flags[1024];
};
#define	LENMASK			0x0ffff
#define	START_OF_CHUNK		0x10000
#define	UNTRANSLATABLE		0x20000
#define	TO_BE_INVALIDATED	0x40000

struct translation_page_entry **translation_page_entry_array;


/*
 *  bintrans_invalidate():
 *
 *  Invalidate translations containing a certain physical address.
 */
void bintrans_invalidate(struct cpu *cpu, uint64_t paddr)
{
	int entry_index = PADDR_TO_INDEX(paddr);
	struct translation_page_entry *tep;
	unsigned char *p;
	uint64_t paddr_page = paddr & ~0xfff;
	int offset_within_page = (paddr & 0xfff) / 4;
	int chunklen, i,j;

	tep = translation_page_entry_array[entry_index];
	while (tep != NULL) {
		if (tep->paddr == paddr_page) {
			/*  Nothing translated? Then simply return.  */
			if (tep->chunk[offset_within_page] == NULL &&
			    !(tep->length_and_flags[offset_within_page] & UNTRANSLATABLE))
				return;

			/*  Just untranslatable? Then clear the flags and return:  */
			if (tep->length_and_flags[offset_within_page] & UNTRANSLATABLE) {
				tep->length_and_flags[offset_within_page] = 0;
				return;
			}

			/*  printf("bintrans_invalidate(): invalidating"
			    " %016llx\n", (long long)paddr);  */

			/*  Overwrite the translated chunk so that it is
			    just a header and a return instruction:  */
			p = tep->chunk[offset_within_page] +
			    bintrans_chunk_header_len();
			bintrans_write_chunkreturn(&p);

			bintrans_host_cacheinvalidate(p, 80);		/*  TODO: len of return?  */

			/*  Remove any entry which reaches this address:  */
			i = 0;
			while (i < 1024) {
				chunklen = tep->length_and_flags[i] & LENMASK;
				/*  from i to i+chunklen-1  */
				if (i <= offset_within_page && offset_within_page < i+chunklen) {
					for (j=i; j<i+chunklen; j++)
						tep->length_and_flags[j] |= TO_BE_INVALIDATED;
				}
				i++;
			}

			for (i=0; i<1024; i++)
				if (tep->length_and_flags[i] & TO_BE_INVALIDATED) {
					tep->chunk[i] = NULL;
					tep->length_and_flags[i] = 0;
				}

			return;
		}

		tep = tep->next;
	}
}


/*
 *  bintrans_runchunk():
 *
 *  Checks the translation cache for a physical address. If the address
 *  is found, then that code is run, and the number of MIPS instructions
 *  executed is returned.  Otherwise, -1 is returned.
 */
int bintrans_runchunk(struct cpu *cpu, uint64_t paddr)
{
	int entry_index = PADDR_TO_INDEX(paddr);
	struct translation_page_entry *tep;
	uint64_t paddr_page = paddr & ~0xfff;
	int offset_within_page = (paddr & 0xfff) / 4;
	int (*f)(struct cpu *);

	if (cpu->delay_slot || cpu->nullify_next)
		return -1;

	tep = translation_page_entry_array[entry_index];

	while (tep != NULL) {
		if (tep->paddr == paddr_page) {
			if (tep->chunk[offset_within_page] == NULL)
				return -1;
			if (!(tep->length_and_flags[offset_within_page] & START_OF_CHUNK))
				return -1;

			/*  printf("bintrans_runchunk(): chunk = %p\n",
			    tep->chunk[offset_within_page]);  */
			cpu->bintrans_instructions_executed = 0;

			f = (void *)tep->chunk[offset_within_page];

			f(cpu);

			/*  printf("after the chunk has run.\n");  */
			return cpu->bintrans_instructions_executed;
		}

		tep = tep->next;
	}

	return -1;
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
 *
 *  If run_flag is zero, then translation occurs, potentially using recursion
 *  (at most translate_depth levels), and a return value of -1 (for failure)
 *  or 0 (success) is returned, but no translated code is executed.
 */
int bintrans_attempt_translate(struct cpu *cpu, uint64_t paddr,
	int run_flag, int translate_depth)
{
	uint64_t paddr_page;
	int offset_within_page;
	int entry_index;
	unsigned char *host_mips_page;
	unsigned char *chunk_addr, *chunk_addr2;
	int res, hi6, special6, hi6_2;
	unsigned char instr[4];
	unsigned char instr2[4];
	uint32_t instrX;
	size_t p;
	int try_to_translate;
	int n_translated, i;
	int pc_increment;
	int (*f)(struct cpu *);
	struct translation_page_entry *tep;
	size_t chunk_len;
	int rs,rt,rd,sa,imm;
	int first_load, first_store;
	int byte_order_cached;


	/*  Abort if the current "environment" isn't safe enough:  */
	if (cpu->delay_slot || cpu->nullify_next)
		return -1;

	if (translate_depth == 0)
		return -1;

	/*  Not on a page directly readable by the host? Then abort.  */
	host_mips_page = cpu->pc_bintrans_host_4kpage;
	if (host_mips_page == NULL || (paddr & 3)!=0)
		return -1;

	/*  Is this a part of something that is already translated?  */
	paddr_page = paddr & ~0xfff;
	offset_within_page = (paddr & 0xfff) / 4;
	entry_index = PADDR_TO_INDEX(paddr);
	tep = translation_page_entry_array[entry_index];
	while (tep != NULL) {
		if (tep->paddr == paddr_page) {
			if (tep->length_and_flags[offset_within_page] & UNTRANSLATABLE)
				return -1;
			if (tep->length_and_flags[offset_within_page] & START_OF_CHUNK)
				return -1;
#if 0
			if (tep->chunk[offset_within_page] != NULL)
				return -1;
#endif
			break;
		}
		tep = tep->next;
	}

	if (tep == NULL) {
		/*  Allocate a new translation page entry:  */
		tep = (void *)(size_t) (translation_code_chunk_space +
		    translation_code_chunk_space_head);
		translation_code_chunk_space_head += sizeof(struct translation_page_entry);

		/*  ... and align again:  */
		translation_code_chunk_space_head =
		    ((translation_code_chunk_space_head - 1) |
		    (sizeof(uint64_t)-1)) + 1;

		/*  Add the entry to the array:  */
		memset(tep, 0, sizeof(struct translation_page_entry));
		tep->next = translation_page_entry_array[entry_index];
		translation_page_entry_array[entry_index] = tep;
		tep->paddr = paddr_page;
	}

	/*  printf("translation_page_entry_array[%i] = %p, ofs = %i\n",
	    entry_index, translation_page_entry_array[entry_index], offset_within_page);  */


	/*
	 *  If the chunk space is all used up, we need to start over from
	 *  an empty chunk space.
	 */
	if (translation_code_chunk_space_head >= CODE_CHUNK_SPACE_SIZE) {
		int i, n = 1 << BINTRANS_CACHE_N_INDEX_BITS;
		for (i=0; i<n; i++)
			translation_page_entry_array[i] = NULL;
		translation_code_chunk_space_head = 0;
		fatal("bintrans: Starting over!\n");
	}


	/*  chunk_addr = where to start generating the new chunk:  */
	chunk_addr = translation_code_chunk_space
	    + translation_code_chunk_space_head;

	/*
	 *  Some backends need a code chunk header, but assuming that
	 *  this header is of a fixed known size, it does not have to
	 *  be written until we're sure that a block of code was
	 *  actually translated.
	 */
	chunk_addr += bintrans_chunk_header_len();


	/*
	 *  Try to translate a chunk of code:
	 */

	byte_order_cached = cpu->byte_order;
	p = paddr & 0xfff;
	try_to_translate = 1;
	n_translated = 0;
	pc_increment = 0;
	first_load=1, first_store=1;
	res = 0;

	while (try_to_translate) {
		/*  Read an instruction word from host memory:  */
		*((uint32_t *)&instr[0]) = *((uint32_t *)(host_mips_page + p));

		if (byte_order_cached == EMUL_BIG_ENDIAN) {
			int tmp;
			tmp = instr[0]; instr[0] = instr[3]; instr[3] = tmp;
			tmp = instr[1]; instr[1] = instr[2]; instr[2] = tmp;
		}

		/*  Assuming that the translation succeeds, let's increment the pc.  */
		pc_increment += sizeof(instr);

		hi6 = instr[3] >> 2;
		special6 = instr[0] & 0x3f;

		/*  Check for instructions that can be translated:  */
		switch (hi6) {
		case HI6_SPECIAL:
			switch (special6) {
			case SPECIAL_ADDU:
			case SPECIAL_SUBU:
			case SPECIAL_AND:
			case SPECIAL_OR:
			case SPECIAL_NOR:
			case SPECIAL_XOR:
			case SPECIAL_SLT:
			case SPECIAL_SLTU:
			case SPECIAL_SLL:
			case SPECIAL_SRA:
			case SPECIAL_SRL:
				rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
				rt = instr[2] & 31;
				rd = (instr[1] >> 3) & 31;
				sa = ((instr[1] & 7) << 2) + ((instr[0] >> 6) & 3);
				if (rd == 0)
					res = 1;
				else {
					switch (special6) {
					case SPECIAL_ADDU:
						res = bintrans_write_instruction__addu(&chunk_addr, &pc_increment, rd, rs, rt);
						break;
					case SPECIAL_SUBU:
						res = bintrans_write_instruction__subu(&chunk_addr, &pc_increment, rd, rs, rt);
						break;
					case SPECIAL_AND:
						res = bintrans_write_instruction__and(&chunk_addr, &pc_increment, rd, rs, rt);
						break;
					case SPECIAL_OR:
						res = bintrans_write_instruction__or(&chunk_addr, &pc_increment, rd, rs, rt);
						break;
					case SPECIAL_NOR:
						res = bintrans_write_instruction__nor(&chunk_addr, &pc_increment, rd, rs, rt);
						break;
					case SPECIAL_XOR:
						res = bintrans_write_instruction__xor(&chunk_addr, &pc_increment, rd, rs, rt);
						break;
					case SPECIAL_SLT:
						res = bintrans_write_instruction__slt(&chunk_addr, &pc_increment, rd, rs, rt);
						break;
					case SPECIAL_SLTU:
						res = bintrans_write_instruction__sltu(&chunk_addr, &pc_increment, rd, rs, rt);
						break;
					case SPECIAL_SLL:
						res = bintrans_write_instruction__sll(&chunk_addr, &pc_increment, rd, rt, sa);
						break;
					case SPECIAL_SRA:
						res = bintrans_write_instruction__sra(&chunk_addr, &pc_increment, rd, rt, sa);
						break;
					case SPECIAL_SRL:
						res = bintrans_write_instruction__srl(&chunk_addr, &pc_increment, rd, rt, sa);
						break;
					}
				}
				if (!res)
					try_to_translate = 0;
				else
					n_translated += res;
				break;
			case SPECIAL_JR:
				rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
				if (p == 0xffc) {
					try_to_translate = 0;
					break;
				}
				/*  Read the next instruction:  */
				*((uint32_t *)&instr2[0]) = *((uint32_t *)(host_mips_page + p + 4));
				hi6_2 = instr2[3] >> 2;
				if (hi6_2 == HI6_ADDIU) {
					rs = ((instr2[3] & 3) << 3) + ((instr2[2] >> 5) & 7);
					rt = instr2[2] & 31;
					imm = (instr2[1] << 8) + instr2[0];
					if (imm >= 32768)
						imm -= 65536;
					if (rt == 0)
						res = 1;
					else
						res = bintrans_write_instruction__addiu(&chunk_addr, &pc_increment, rt, rs, imm);
					if (!res)
						try_to_translate = 0;
					else
						n_translated += res;
					if (!try_to_translate)
						break;

					/*  "jr rs; addiu" combination  */
					rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
					bintrans_write_instruction__jr(&chunk_addr, &pc_increment, rs);
					n_translated += 1;
					p += sizeof(instr);
					pc_increment = sizeof(instr);	/*  will be decreased later  */
				} else if (instr2[0] == 0 && instr2[1] == 0 && instr2[2] == 0 && instr2[3] == 0) {
					/*  "jr rs; nop" combination  */
					bintrans_write_instruction__jr(&chunk_addr, &pc_increment, rs);
					n_translated += 2;
					p += sizeof(instr);
					pc_increment = sizeof(instr);	/*  will be decreased later  */
				}
				try_to_translate = 0;
				break;
			case SPECIAL_JALR:
				rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
				rd = (instr[1] >> 3) & 31;
				if (p == 0xffc) {
					try_to_translate = 0;
					break;
				}
				instrX = *((uint32_t *)(host_mips_page + p + 4));	/*  next instruction  */
				if (instrX == 0) {
					/*  "jalr rd,rs; nop" combination  */
					pc_increment += sizeof(instr);
					bintrans_write_instruction__jalr(&chunk_addr, &pc_increment, rd, rs);
					n_translated += 2;
					p += sizeof(instr);
					pc_increment = sizeof(instr);	/*  will be decreased later  */
				}
				try_to_translate = 0;
				break;
			case SPECIAL_MFHI:
			case SPECIAL_MFLO:
			case SPECIAL_MTHI:
			case SPECIAL_MTLO:
				rd = (instr[1] >> 3) & 31;
				rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
				res = bintrans_write_instruction__mfmthilo(&chunk_addr, &pc_increment,
				    (special6 == SPECIAL_MFHI || special6 == SPECIAL_MFLO)? rd : rs,
				    special6 == SPECIAL_MFHI || special6 == SPECIAL_MFLO,
				    special6 == SPECIAL_MFHI || special6 == SPECIAL_MTHI);
				if (!res)
					try_to_translate = 0;
				else
					n_translated += res;
				break;
			default:
				try_to_translate = 0;
			}
			break;
		case HI6_ADDIU:
			rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
			rt = instr[2] & 31;
			imm = (instr[1] << 8) + instr[0];
			if (imm >= 32768)
				imm -= 65536;
			if (rt == 0)
				res = 1;
			else
				res = bintrans_write_instruction__addiu(&chunk_addr, &pc_increment, rt, rs, imm);
			if (!res)
				try_to_translate = 0;
			else
				n_translated += res;
			break;
		case HI6_ANDI:
		case HI6_ORI:
		case HI6_XORI:
			rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
			rt = instr[2] & 31;
			imm = (instr[1] << 8) + instr[0];
			if (rt == 0)
				res = 1;
			else {
				switch (hi6) {
				case HI6_ANDI:
					res = bintrans_write_instruction__andi(&chunk_addr, &pc_increment, rt, rs, imm);
					break;
				case HI6_ORI:
					res = bintrans_write_instruction__ori(&chunk_addr, &pc_increment, rt, rs, imm);
					break;
				case HI6_XORI:
					res = bintrans_write_instruction__xori(&chunk_addr, &pc_increment, rt, rs, imm);
					break;
				}
			}
			if (!res)
				try_to_translate = 0;
			else
				n_translated += res;
			break;
		case HI6_LUI:
			rt = instr[2] & 31;
			imm = (instr[1] << 8) + instr[0];
			if (rt == 0)
				res = 1;
			else
				res = bintrans_write_instruction__lui(&chunk_addr, &pc_increment, rt, imm);
			if (!res)
				try_to_translate = 0;
			else
				n_translated += res;
			break;
		case HI6_LW:
		case HI6_LHU:
		case HI6_LH:
		case HI6_LBU:
		case HI6_LB:
		case HI6_SW:
		case HI6_SH:
		case HI6_SB:
			rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
			rt = instr[2] & 31;
			imm = (instr[1] << 8) + instr[0];
			if (imm >= 32768)
				imm -= 65536;

			switch (hi6) {
			case HI6_LW:
				if (rt == 0) {
					res = 0;
				} else {
					res = bintrans_write_instruction__lw(&chunk_addr, &pc_increment, first_load, first_store, rt, imm, rs, LOAD_TYPE_LW);
					first_load = 0;
				}
				break;
			case HI6_LHU:
				if (rt == 0) {
					res = 0;
				} else {
					res = bintrans_write_instruction__lw(&chunk_addr, &pc_increment, first_load, first_store, rt, imm, rs, LOAD_TYPE_LHU);
					first_load = 0;
				}
				break;
			case HI6_LH:
				if (rt == 0) {
					res = 0;
				} else {
					res = bintrans_write_instruction__lw(&chunk_addr, &pc_increment, first_load, first_store, rt, imm, rs, LOAD_TYPE_LH);
					first_load = 0;
				}
				break;
			case HI6_LBU:
				if (rt == 0) {
					res = 0;
				} else {
					res = bintrans_write_instruction__lw(&chunk_addr, &pc_increment, first_load, first_store, rt, imm, rs, LOAD_TYPE_LBU);
					first_load = 0;
				}
				break;
			case HI6_LB:
				if (rt == 0) {
					res = 0;
				} else {
					res = bintrans_write_instruction__lw(&chunk_addr, &pc_increment, first_load, first_store, rt, imm, rs, LOAD_TYPE_LB);
					first_load = 0;
				}
				break;
			case HI6_SW:
				res = bintrans_write_instruction__lw(&chunk_addr, &pc_increment, first_load, first_store, rt, imm, rs, LOAD_TYPE_SW);
				first_store = 0;
				break;
			case HI6_SH:
				res = bintrans_write_instruction__lw(&chunk_addr, &pc_increment, first_load, first_store, rt, imm, rs, LOAD_TYPE_SH);
				first_store = 0;
				break;
			case HI6_SB:
				res = bintrans_write_instruction__lw(&chunk_addr, &pc_increment, first_load, first_store, rt, imm, rs, LOAD_TYPE_SB);
				first_store = 0;
				break;
			}
			if (!res)
				try_to_translate = 0;
			else
				n_translated += res;
			break;
		default:
			try_to_translate = 0;
		}

		/*  If the translation of this instruction failed,
		    then don't count this as an increment of pc.  */
		if (try_to_translate == 0) {
			pc_increment -= sizeof(instr);
			break;
		}

		p += sizeof(instr);

		/*  If we have reached a different (MIPS) page, then stop translating.  */
		if (p == 0x1000)
			try_to_translate = 0;
	}

	/*  Not enough translated? Then abort.  */
	if (n_translated < 1) {
		tep->length_and_flags[offset_within_page] |= UNTRANSLATABLE;
		return -1;
	}

	/*  Flush the pc, to let it have a correct (emulated) value:  */
	bintrans_write_pcflush(&chunk_addr, &pc_increment, 1, 1);

	/*  chunk_addr2 = ptr to the head of the new code chunk  */
	chunk_addr2 = translation_code_chunk_space +
	    translation_code_chunk_space_head;

	/*  Add code chunk header...  */
	bintrans_write_chunkhead(chunk_addr2);

	/*  ...and return code:  */
	bintrans_write_chunkreturn(&chunk_addr);

	/*  chunk_len = nr of bytes occupied by the new code chunk  */
	chunk_len = (size_t)chunk_addr - (size_t)chunk_addr2;

	/*  Invalidate the host's instruction cache, if neccessary:  */
	bintrans_host_cacheinvalidate(chunk_addr2, chunk_len);

	translation_code_chunk_space_head += chunk_len;

	/*  Align the code chunk space:  */
	translation_code_chunk_space_head =
	    ((translation_code_chunk_space_head - 1) |
	    (sizeof(uint64_t)-1)) + 1;

	/*  Insert the translated chunk into the page entry:  */
	tep->length_and_flags[offset_within_page] = START_OF_CHUNK | n_translated;
	tep->chunk[offset_within_page] = chunk_addr2;

	/*  ... and make sure all instructions are accounted for:  */
	if (n_translated > 1) {
		for (i=1; i<n_translated; i++) {
			tep->length_and_flags[offset_within_page + i] = 0;
			tep->chunk[offset_within_page + i] = chunk_addr2;
		}
	}


	if (!run_flag)
		return 0;


	/*  RUN the code chunk:  */
	cpu->bintrans_instructions_executed = 0;
	f = (void *)chunk_addr2;

	/*  printf("BEFORE: pc=%016llx r31=%016llx\n",
	    (long long)cpu->pc, (long long)cpu->gpr[31]); */

	f(cpu);

	/*  printf("AFTER:  pc=%016llx r31=%016llx\n",
	    (long long)cpu->pc, (long long)cpu->gpr[31]); */

	return cpu->bintrans_instructions_executed;
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
	translation_page_entry_array = malloc(s);
	if (translation_page_entry_array == NULL) {
		fprintf(stderr, "bintrans_init(): out of memory (1)\n");
		exit(1);
	}

	/*  The entry array must be NULLed, as these are pointers to
	    translation page entries.  */
	debug("bintrans: translation_page_entry_array = %i KB at %p\n",
	    (int)(s/1024), translation_page_entry_array);
	for (i=0; i<n; i++)
		translation_page_entry_array[i] = NULL;

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
