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
 *  $Id: bintrans.c,v 1.22 2004-10-08 17:26:35 debug Exp $
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


/*  Function declaration, should be the same as in bintrans_*.c:  */

void bintrans_host_cacheinvalidate(void);


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


#define	BINTRANS_CACHE_N_INDEX_BITS	12
#define	CACHE_INDEX_MASK		((1 << BINTRANS_CACHE_N_INDEX_BITS) - 1)
#define	PADDR_TO_INDEX(p)		((p >> 12) & CACHE_INDEX_MASK)

struct translation_entry {
	uint64_t			paddr;
	int				len;

	struct translation_entry	*next;

	/*  TODO  */
};

struct translation_entry **translation_entry_array;


/*
 *  bintrans_paddr_is_in_cache():
 *
 *  Checks the translation cache to see if a certain address is translated.
 *  Return 1 if the address is known, 0 otherwise.
 *
 *  Some bits of the physical page number are used as an index into an array
 *  to speed up the search.
 */
int bintrans_paddr_is_in_cache(struct cpu *cpu, uint64_t paddr)
{
	int entry_index = PADDR_TO_INDEX(paddr);
	struct translation_entry *tep;

	tep = translation_entry_array[entry_index];

	/*  TODO: Something better than a linked list would be nice.  */

	while (tep != NULL) {
		if (tep->paddr == paddr)
			return 1;

		tep = tep->next;
	}

	return 0;
}


/*
 *  bintrans_invalidate():
 *
 *  Invalidate translations containing a certain physical address.
 */
void bintrans_invalidate(struct cpu *cpu, uint64_t paddr)
{
	int entry_index = PADDR_TO_INDEX(paddr);
	struct translation_entry *tep;

	tep = translation_entry_array[entry_index];

	/*  TODO: Something better than a linked list would be nice.  */

	while (tep != NULL) {
		if (paddr >= tep->paddr && paddr < tep->paddr + tep->len) {

			/*  TODO  */
			fprintf(stderr, "bintrans_invalidate(): invalidating"
			    " %016llx: TODO\n", (long long)paddr);
			exit(1);
			/*  TODO: remove the translation from the list,
			    and free whatever memory it was using  */

			/*  Restart the search from the beginning:  */
			tep = translation_entry_array[entry_index];
			continue;
		}

		tep = tep->next;
	}
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
	/*  printf("tr: paddr=%08x\n", (int)paddr);  */

	return 0;
}


/*
 *  bintrans_init():
 *
 *  Should be called before any other bintrans_*() function is used.
 */
void bintrans_init(void)
{
	size_t s;

	debug("starting bintrans: EXPERIMENTAL!\n");

	s = 1 << BINTRANS_CACHE_N_INDEX_BITS;
	s *= sizeof(struct translation_entry *);
	translation_entry_array = malloc(s);
	if (translation_entry_array == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}

	memset(translation_entry_array, 0, s);
}


#endif	/*  BINTRANS  */
