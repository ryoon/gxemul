/*
 *  Copyright (C) 2004 by Anders Gavare.  All rights reserved.
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
 *  $Id: bintrans.c,v 1.35 2004-10-17 13:36:05 debug Exp $
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
	uint64_t vaddr, int run_flag, int translate_depth) { return 0; }
void bintrans_init(void)
{
	fatal("NOT starting bintrans, as mips64emul was compiled without such support!\n");
}

#else


/*  Instructions that can be translated:  */
#define	INSTR_NOP		0
#define	INSTR_JAL		1


/*  Function declaration, should be the same as in bintrans_*.c:  */

void bintrans_host_cacheinvalidate(unsigned char *p, size_t len);
size_t bintrans_chunk_header_len(void);
void bintrans_write_chunkhead(unsigned char *p);
void bintrans_write_chunkreturn(unsigned char **addrp);
void bintrans_write_pcflush(unsigned char **addrp, int *pc_increment,
	int flag_pc, int flag_ninstr);
int bintrans_write_instruction(unsigned char **addrp, int instr,
	int *pc_increment, uint64_t addr_a, uint64_t addr_b);


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

#define	CODE_CHUNK_SPACE_SIZE		(4 * 1048576)
#define	CODE_CHUNK_SPACE_MARGIN		16384

/*
 *  translation_code_chunk_space is a large chunk of (linear) memory where
 *  translated code chunks and translation_entrys are stored. When this is
 *  filled, we restart from scratch (by resetting
 *  translation_code_chunk_space_head to 0, and removing all translation
 *  entries).
 *
 *  (This is somewhat inspired by the QEMU web pages,
 *  http://fabrice.bellard.free.fr/qemu/qemu-tech.html#SEC13)
 */
unsigned char *translation_code_chunk_space;
size_t translation_code_chunk_space_head;

/*  TODO: Something better than a linked list would be nice.  */
struct translation_entry {
	uint64_t			paddr;
	int				len;

	struct translation_entry	*next;

	unsigned char			*chunk;
	size_t				chunk_len;
};

struct translation_entry **translation_entry_array;


/*
 *  bintrans_invalidate():
 *
 *  Invalidate translations containing a certain physical address.
 */
void bintrans_invalidate(struct cpu *cpu, uint64_t paddr)
{
	int entry_index = PADDR_TO_INDEX(paddr);
	struct translation_entry *tep, *prev;
	unsigned char *p;

	tep = translation_entry_array[entry_index];
	prev = NULL;

	while (tep != NULL) {
		if (paddr >= tep->paddr && paddr < tep->paddr + tep->len) {
			/*  printf("bintrans_invalidate(): invalidating"
			    " %016llx\n", (long long)paddr);  */

			/*  Overwrite the translated chunk so that it is
			    just a header and a return instruction:  */
			p = tep->chunk + bintrans_chunk_header_len();
			bintrans_write_chunkreturn(&p);

			/*  Remove the translation entry from the list:  */
			if (prev == NULL)
				translation_entry_array[entry_index] =
				    tep->next;
			else
				prev->next = tep->next;

			/*  Continue search at the next entry, without
			    changing prev:  */
			tep = tep->next;
			continue;
		}

		prev = tep;
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
	struct translation_entry *tep;
	int (*f)(struct cpu *);

	if (cpu->delay_slot || cpu->nullify_next)
		return -1;

	tep = translation_entry_array[entry_index];

	while (tep != NULL) {
		if (tep->paddr == paddr) {
			/*  printf("bintrans_runchunk(): chunk = %p\n",
			    tep->chunk);  */
			cpu->bintrans_instructions_executed = 0;

			f = (void *)tep->chunk;
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
	uint64_t vaddr, int run_flag, int translate_depth)
{
	int (*f)(struct cpu *);
	int try_to_translate;
	int pc_increment = 0;
	int n_translated = 0;
	int res, hi6, special6, rd;
	uint64_t addr, addr2;
	uint64_t p;
	unsigned char instr[4];
	unsigned char instr2[4];
	unsigned char *host_mips_page;
	unsigned char *chunk_addr, *chunk_addr2;
	struct translation_entry *tep;
	int entry_index;
	size_t chunk_len;

	/*
	 *  Abort if the current "environment" isn't safe enough:
	 */
	if (cpu->delay_slot || cpu->nullify_next)
		return -1;

	if (translate_depth == 0)
		return -1;

	host_mips_page = cpu->pc_bintrans_host_4kpage;
	if (host_mips_page == NULL || (paddr & 3)!=0)
		return -1;


	/*
	 *  If the chunk space is all used up, we need to start over from
	 *  an empty chunk space.
	 */
	if (translation_code_chunk_space_head >= CODE_CHUNK_SPACE_SIZE) {
		int i, n = 1 << BINTRANS_CACHE_N_INDEX_BITS;

		for (i=0; i<n; i++)
			translation_entry_array[i] = NULL;

		translation_code_chunk_space_head = 0;

		fatal("NOTE: bintrans starting over\n");
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
	p = paddr;
	try_to_translate = 1;

	while (try_to_translate) {
		/*  Read an instruction word from host memory:  */
		*((uint32_t *)&instr[0]) =
		    *((uint32_t *)(host_mips_page + (p & 0xfff)));

		if (cpu->byte_order == EMUL_BIG_ENDIAN) {
			int tmp;
			tmp = instr[0]; instr[0] = instr[3]; instr[3] = tmp;
			tmp = instr[1]; instr[1] = instr[2]; instr[2] = tmp;
		}

		/*  Assuming that the translation succeeds, let's
		    increment the pc.  */
		pc_increment += sizeof(instr);

		hi6 = instr[3] >> 2;
		special6 = instr[0] & 0x3f;

		/*  Check for instructions that can be translated:  */
		if (hi6 == HI6_SPECIAL && special6 == SPECIAL_SLL) {
			rd = (instr[1] >> 3) & 31;
			if (rd == 0) {
				/*  NOP  */
				res = bintrans_write_instruction(
				    &chunk_addr, INSTR_NOP, &pc_increment,
				    0, 0);
				if (!res)
					try_to_translate = 0;
				else
					n_translated += res;
			} else
				try_to_translate = 0;
		} else if (hi6 == HI6_JAL && (p & 0xfff) != 0xffc) {
			/*  JAL  */

			/*  Read the next instruction:  */
			*((uint32_t *)&instr2[0]) =
			    *((uint32_t *)(host_mips_page + ((p+4) & 0xfff)));

			if (instr2[0] == instr2[1] &&
			    instr2[0] == instr2[2] &&
			    instr2[0] == instr2[3] &&
			    instr2[0] == 0x00) {
				/*  NOP in delay slot  */
				addr = instr[0] + (instr[1] << 8) +
				    (instr[2] << 16) + (instr[3] & 0x03);
				addr <<= 2;
				addr2 = (vaddr & ~0xfff) | (p & 0xfff);
				addr2 += 8;
				addr |= (addr2 & ~0x0fffffffULL);
				res = bintrans_write_instruction(
				    &chunk_addr, INSTR_JAL, &pc_increment,
				    addr, addr2);
				/*  Success: JAL+NOP were translated.  */
				if (res)
					n_translated += 2;
				/*  End of basic block.  */
				try_to_translate = 0;
			} else
				try_to_translate = 0;
		} else
			try_to_translate = 0;

		/*  If the translation of this instruction failed,
		    then don't count this as an increment of pc.  */
		if (try_to_translate == 0)
			pc_increment -= sizeof(instr);

		p += sizeof(instr);

		/*  If we have reached a different (MIPS) page, then
		    stop translating.  */
		if ((p & 0xfff) == 0)
			try_to_translate = 0;
	}

	if (n_translated == 0)
		return -1;

	/*  Flush the pc, to let it have a correct (emulated) value:  */
	bintrans_write_pcflush(&chunk_addr, &pc_increment, 1, 1);

	/*  Add code chunk header...  */
	bintrans_write_chunkhead(translation_code_chunk_space
	    + translation_code_chunk_space_head);

	/*  ...and return code:  */
	bintrans_write_chunkreturn(&chunk_addr);

	/*
	 *  Add the translation to the translation entry array:
	 *
	 *  If malloc()/free() were used, then tep would just be malloced
	 *  like this:
	 *
	 *	tep = malloc(sizeof(struct translation_entry));
	 *
	 *  However, the space used for code chunks could just as well be
	 *  used for translation entries as well, thus removing the
	 *  overhead of calling malloc/free.
	 */

	/*  chunk_addr2 = ptr to the head of the new code chunk  */
	chunk_addr2 = translation_code_chunk_space +
	    translation_code_chunk_space_head;

	/*  chunk_len = nr of bytes occupied by the new code chunk  */
	chunk_len = (size_t)chunk_addr - (size_t)chunk_addr2;

	/*  Invalidate the host's instruction cache, if neccessary:  */
	bintrans_host_cacheinvalidate(chunk_addr2, chunk_len);

	translation_code_chunk_space_head += chunk_len;

	/*  Now, we can set tep to point to the new .._space_head, but it
	    might be wise to uint64_t align it first:  */
	translation_code_chunk_space_head =
	    ((translation_code_chunk_space_head - 1) |
	    (sizeof(uint64_t)-1)) + 1;

	tep = (void *)(size_t) (translation_code_chunk_space +
	    translation_code_chunk_space_head);

	/*  MIPS physical address and length:  */
	tep->paddr = paddr;
	tep->len = n_translated * sizeof(instr);

	tep->chunk = chunk_addr2;
	tep->chunk_len = chunk_len;

	/*  printf("TEP paddr=%08x len=%i chunk=%p chunk_len=%i\n",
	    (int)tep->paddr, tep->len, tep->chunk, (int)tep->chunk_len);  */

	entry_index = PADDR_TO_INDEX(paddr);
	tep->next = translation_entry_array[entry_index];
	translation_entry_array[entry_index] = tep;

	/*  Increase .._space_head again (the allocation of the
	    translation_entry struct), and align it just in case:  */
	translation_code_chunk_space_head += sizeof(struct translation_entry);
	translation_code_chunk_space_head =
	    ((translation_code_chunk_space_head - 1) |
	    (sizeof(uint64_t)-1)) + 1;


	/*  RUN the code chunk:  */
	cpu->bintrans_instructions_executed = 0;
	f = (void *)tep->chunk;
	f(cpu);
	return cpu->bintrans_instructions_executed;
}


/*
 *  bintrans_init():
 *
 *  Should be called before any other bintrans_*() function is used.
 */
void bintrans_init(void)
{
	size_t s;
	int res;

	debug("bintrans: EXPERIMENTAL!\n");

	s = 1 << BINTRANS_CACHE_N_INDEX_BITS;
	s *= sizeof(struct translation_entry *);
	translation_entry_array = malloc(s);
	if (translation_entry_array == NULL) {
		fprintf(stderr, "bintrans_init(): out of memory (1)\n");
		exit(1);
	}

	/*  The entry array must be NULLed, as these are pointers to
	    translation entries.  */
	debug("bintrans: translation_entry_array = %i KB at %p\n",
	    (int)(s/1024), translation_entry_array);
	memset(translation_entry_array, 0, s);

	/*  Allocate the large code chunk space:  */
	s = CODE_CHUNK_SPACE_SIZE + CODE_CHUNK_SPACE_MARGIN;
	translation_code_chunk_space = mmap(NULL, s,
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
	res = mprotect(translation_code_chunk_space,
	    s, PROT_READ | PROT_WRITE | PROT_EXEC);
	if (res)
		debug("warning: mprotect() failed with errno %i."
		    " this usually doesn't really matter...\n", errno);
}

#endif	/*  BINTRANS  */
