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
 *  $Id: bintrans.c,v 1.26 2004-10-09 19:03:29 debug Exp $
 *
 *  Dynamic binary translation.
 *
 *
 *
 *  NOTE:  This file is basically a test are for ideas, and a
 *         place to write down comments.
 *
 *
 *	Keep a cache of a certain number of blocks.
 *
 *	Simple basic-block stuff. Only simple-enough instructions are
 *		translated. (for example, the 'cache' and 'tlbwr' instructions
 *		are NOT simple enough)
 *
 *	Translate code in physical ram, not virtual.
 *		Why?  This will keep things translated over process
 *		switches, and TLB updates.
 *
 *	Do not translate over MIPS page boundaries (4 KB).  (Perhaps translation
 *		over page boundaries can be allowed in kernel space...?)
 *
 *	If memory is overwritten, any translated block for that page must
 *		be invalidated.
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
int bintrans_attempt_translate(struct cpu *cpu, uint64_t pc) { return 0; }
void bintrans_init(void)
{
	fatal("NOT starting bintrans, as mips64emul was compiled without such support!\n");
}

#else


/*  Instructions that can be translated:  */
#define	INSTR_NOP		0


/*  Function declaration, should be the same as in bintrans_*.c:  */

void bintrans_host_cacheinvalidate(void);
size_t bintrans_chunk_header_len(void);
void bintrans_write_chunkhead(unsigned char *p);
void bintrans_write_chunkreturn(unsigned char **addrp);
int bintrans_write_instruction(unsigned char **addrp, int instr,
	int *pc_increment);


/*  Include host architecture specific bintrans code:  */

#ifdef ALPHA
#include "bintrans_alpha.c"
#else
#ifdef I386
#include "bintrans_i386.c"
#else
#ifdef SPARCV9
#include "bintrans_sparcv9.c"
#else
#error Unsupported host architecture for bintrans.
#endif	/*  SPARCV9  */
#endif	/*  I386  */
#endif	/*  ALPHA  */


#define	BINTRANS_CACHE_N_INDEX_BITS	14
#define	CACHE_INDEX_MASK		((1 << BINTRANS_CACHE_N_INDEX_BITS) - 1)
#define	PADDR_TO_INDEX(p)		((p >> 12) & CACHE_INDEX_MASK)

#define	CODE_CHUNK_SPACE_SIZE		(2 * 1048576)
#define	CODE_CHUNK_SPACE_MARGIN		16384

/*
 *  translation_code_chunk_space is a large chunk of (linear) memory where
 *  translated code chunks are stored. When this is filled, we restart from
 *  scratch (by resetting translation_code_chunk_space_head to 0, and
 *  removing all translation entries).
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

	tep = translation_entry_array[entry_index];
	prev = NULL;

	while (tep != NULL) {
		if (paddr >= tep->paddr && paddr < tep->paddr + tep->len) {
			/*  printf("bintrans_invalidate(): invalidating"
			    " %016llx\n", (long long)paddr);  */

			/*  Remove the translation entry from the list,
			    and free whatever memory it was using:  */
			if (prev == NULL)
				translation_entry_array[entry_index] =
				    tep->next;
			else
				prev->next = tep->next;
			free(tep);

			/*  Restart the search from the beginning:  */
			tep = translation_entry_array[entry_index];
			prev = NULL;
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
 *  executed is returned.  Otherwise, 0 is returned.
 */
int bintrans_runchunk(struct cpu *cpu, uint64_t paddr)
{
	int entry_index = PADDR_TO_INDEX(paddr);
	struct translation_entry *tep;
	int (*f)(struct cpu *);

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

	return 0;
}


/*
 *  bintrans_attempt_translate():
 *
 *  Attempt to translate a chunk of code, starting at 'paddr'.
 *
 *  Returns 0 if no code translation occured, otherwise 1 is returned and
 *  the generated code chunk is added to the translation_entry_array.
 */
int bintrans_attempt_translate(struct cpu *cpu, uint64_t paddr)
{
	int try_to_translate = 1;
	int pc_increment = 0;
	int n_translated = 0;
	int res, hi6, special6, rd;
	uint64_t p;
	unsigned char instr[4];
	unsigned char *chunk_addr;
	struct translation_entry *tep;
	int entry_index;

	/*
	 *  If the chunk space is all used up, we need to start over from
	 *  an empty chunk space.
	 */
	if (translation_code_chunk_space_head >= CODE_CHUNK_SPACE_SIZE) {
		struct translation_entry *next;
		int i, n = 1 << BINTRANS_CACHE_N_INDEX_BITS;

		for (i=0; i<n; i++) {
			tep = translation_entry_array[i];
			while (tep != NULL) {
				next = tep->next;
				free(tep);
				tep = next;
			}
			translation_entry_array[i] = NULL;
		}

		translation_code_chunk_space_head = 0;
	}

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

	while (try_to_translate) {
		/*  Read an instruction word from memory:  */
		res = memory_rw(cpu, cpu->mem, p, &instr[0],
		    sizeof(instr), MEM_READ, PHYSICAL | NO_EXCEPTIONS);
		if (!res)
			break;

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
				    &chunk_addr, INSTR_NOP, &pc_increment);
				if (!res)
					try_to_translate = 0;
				else
					n_translated += res;
			} else
				try_to_translate = 0;
		} else
			try_to_translate = 0;

		/*  If the translation of this instruction failed,
		    then don't count this as an increment of pc.  */
		if (try_to_translate == 0)
			pc_increment -= sizeof(instr);

		p += sizeof(instr);

		/*  Did we reach a different page? Then stop here.  */
		if ((p & 0xfff) == 0)
			try_to_translate = 0;
	}

	if (n_translated == 0)
		return 0;

	/*  Flush the pc, to let it have a correct (emulated) value:  */
	bintrans_write_pcflush(&chunk_addr, &pc_increment);

	/*  Add code chunk header...  */
	bintrans_write_chunkhead(translation_code_chunk_space
	    + translation_code_chunk_space_head);

	/*  ...and return code:  */
	bintrans_write_chunkreturn(&chunk_addr);

	/*  Invalidate the host's instruction cache, if neccessary:  */
	bintrans_host_cacheinvalidate();

	/*  Add the translation to the translation entry array:  */
	tep = malloc(sizeof(struct translation_entry));
	if (tep == NULL) {
		fprintf(stderr, "out of memory in bintrans_attempt_translate()\n");
		exit(1);
	}
	memset(tep, 0, sizeof(struct translation_entry));
	tep->paddr = paddr;
	tep->len = n_translated * sizeof(instr);
	tep->chunk = translation_code_chunk_space +
	    translation_code_chunk_space_head;
	tep->chunk_len = (size_t)chunk_addr - (size_t)tep->chunk;

	/*  printf("TEP paddr=%08x len=%i chunk=%p chunk_len=%i\n",
	    (int)tep->paddr, tep->len, tep->chunk, (int)tep->chunk_len);  */

	translation_code_chunk_space_head += tep->chunk_len;

	entry_index = PADDR_TO_INDEX(paddr);
	tep->next = translation_entry_array[entry_index];
	translation_entry_array[entry_index] = tep;

	return 1;
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

	debug("starting bintrans: EXPERIMENTAL!\n");

	s = 1 << BINTRANS_CACHE_N_INDEX_BITS;
	s *= sizeof(struct translation_entry *);
	translation_entry_array = malloc(s);
	if (translation_entry_array == NULL) {
		fprintf(stderr, "bintrans_init(): out of memory (1)\n");
		exit(1);
	}

	/*  The entry array must be NULLed, as these are pointers to
	    translation entries.  */
	memset(translation_entry_array, 0, s);

	translation_code_chunk_space = malloc(CODE_CHUNK_SPACE_SIZE
	    + CODE_CHUNK_SPACE_MARGIN);
	if (translation_code_chunk_space == NULL) {
		fprintf(stderr, "bintrans_init(): out of memory (2)\n");
		exit(1);
	}

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
	    CODE_CHUNK_SPACE_SIZE + CODE_CHUNK_SPACE_MARGIN,
	    PROT_READ | PROT_WRITE | PROT_EXEC);
	if (res)
		debug("warning: mprotect() failed with errno %i."
		    " this usually doesn't really matter...\n", res);
}


#endif	/*  BINTRANS  */
