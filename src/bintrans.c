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
 *  $Id: bintrans.c,v 1.5 2004-01-29 20:48:04 debug Exp $
 *
 *  Binary translation.
 *
 *  TODO:  This is just a brainstorming scratch area so far.
 *
 *
 *	Keep a cache of a certain number of blocks. Least-recently-used
 *		blocks are replaced.
 *
 *	Don't translate blindly. (?)  Try to wait until we are sure that
 *		a block is actualy used more than once before translating
 *		it. (We can either keep an absolute count of lots of
 *		memory addresses, or utilize some kind of random function.
 *		In the later case, if a block is run many times, it will
 *		have a higher probability of being translated.)
 *
 *	Simple basic-block stuff. Only simple-enough instructions are
 *		translated. (for example, the 'cache' and 'tlbwr' instructions
 *		are NOT simple enough)
 *
 *	Invalidate a block if it is overwritten.
 *
 *	Translate code in physical ram, not virtual.
 *		Why?  This will keep things translated over process
 *		switches, and TLB updates.
 *
 *	Do not translate over hardware (4KB) page boundaries.
 *
 *	Check before running a basic block that no external
 *		exceptions will occur for the duration of the
 *		block.  (External = from a clock device or other
 *		hardware device.)
 *
 *	Check for exceptions inside the block, for those instructions
 *		that require that.  Update the instruction counter by
 *		the number of successfully executed instructions only.
 *
 *	Register allocation, set registers before running the block
 *		and read them back afterwards (for example on Alpha)
 *	OR:
 *		manually manipulate the emulated cpu's registers in the
 *		host's "struct cpu". (For example on i386.)
 *
 *	Multiple target archs (alpha, i386, sparc, mips :-), ...)
 *		Try to use arch specific optimizations, such as prefetch
 *		on alphas that support that.
 *		Not all instructions will be easily translated to all
 *		backends.
 *
 *	Allow load/store if all such load/stores are confined
 *		to a specific page of virtual memory (or somewhere in kernel
 *		memory, in which case access are allowed to cross page
 *		boundaries), so that any code not requiring TLB updates
 *		will still run without intervention.
 *		The loads/stores will go to physical RAM, so they have
 *		to be translated (once) via the TLB.
 *
 *	Testing:  Running regression tests with and without the binary
 *		translator enabled should obviously result in the exact
 *		same results, or something is wrong.
 *
 *	How about loops?
 */

/*
The general idea would be something like this:
A block of code in the host's memory would have to conform with
the C function calling ABI on that platform, so that the code block
can be called easily from C. That is, entry and exit code needs to
be added, in addition to the translated instructions.

	o)  Check for the current PC in the translation cache.
	o)  If the current PC is not found, then make a decision
		regarding making a translation attempt, or not.
		Fallback to normal instruction execution in cpu.c.
	o)  If there is a code block for the current PC in the
		translation cache, do a couple of checks and then
		run the block. Update registers and other values
		as neccessary.

The checks would include:
	boundaries for load/store memory accesses  (these must be
		to pages that are accessible without modifying the TLB,
		that is, they either have to be to addresses which
		are in the TLB or to kseg0/1 addresses)
	no external interrupts should occur for enough number
		of cycles

 */


#include <stdio.h>
#include <stdlib.h>

#include "bintrans.h"
#include "memory.h"


/*
 *  bintrans_check_cache():
 *
 *  This function searches the translation cache for a specific address. If
 *  it is found, 1 is returned.  If the address was not found, 0 is returned.
 *
 *  TODO
 */
int bintrans_check_cache(struct memory *mem, uint64_t paddr)
{
	debug("bintrans_check_cache(): paddr=0x%016llx\n", (long long)paddr);

	return 0;
}


/*
 *  bintrans_invalidate():
 *
 *  This function should be called whenever a part of RAM is being modified,
 *  so that any translation of code at those addresses is removed from the
 *  translation cache.
 *
 *  TODO
 */
void bintrans_invalidate(struct memory *mem, uint64_t paddr, uint64_t len)
{
	debug("bintrans_invalidate(): paddr=0x%016llx len=0x%llx\n",
	    (long long)paddr, (long long)len);

}


#define	BT_UNKNOWN		0
#define	BT_ADDIU		1

#define	CODECHUNK_SIZE		1024

/*
 *  bintrans__codechunk_addinstr():
 *
 *  Used internally by bintrans_try_to_add().
 *  This function tries to translate an instruction into native
 *  code.
 *  Returns 1 on success, 0 if no code was translated.
 */
int bintrans__codechunk_addinstr(void **codechunkp, size_t *curlengthp, struct cpu *cpu, struct memory *mem, int bt_instruction, int rt, int rs, int imm)
{
	void *codechunk;
	size_t curlength;
	int success = 0;
	unsigned char *p;

	if (codechunkp == NULL) {
		fatal("bintrans__codechunk_addinstr(): codechunkp == NULL\n");
		exit(1);
	}

	codechunk = *codechunkp;
	curlength = *curlengthp;

	/*  Create codechunk header, if neccessary:  */
	if (codechunk == NULL) {
		fatal("creating codechunk header...\n");

		codechunk = malloc(CODECHUNK_SIZE);
		curlength = 0;
	}

	/*
	 *  General idea:   example: ADDIU to same register (inc register)
	 *
	 *  f(unsigned char *p) {		<-- input argument is pointer to cpu struct
	 *        p += 0x1234;			<-- p increased by offset to the register to increase
	 *        (*((unsigned long long *)p)) += 0x5678;	<-- inc the register
	 *  }
	 *
	 *  becomes
	 *
	 *   0:   34 12 10 22     lda     a0,4660(a0)		<-- offset
	 *   4:   00 00 30 a4     ldq     t0,0(a0)		<-- load
	 *   8:   78 56 21 20     lda     t0,22136(t0)		<-- inc
	 *   c:   1f 04 ff 5f     fnop
	 *  10:   00 00 30 b4     stq     t0,0(a0)		<-- store back
	 *
	 *  And the tail:
	 *  14:   01 80 fa 6b     ret     zero,(ra),0x1		<-- return
	 */
	if (bt_instruction == BT_ADDIU && rt == rs && imm >= 0) {
		p = (unsigned char *)codechunk + curlength;

		p[0] = 0x34; p[1] = 0x12; p[2] = 0x10; p[3] = 0x22;
		curlength += 4; p += 4;

		p[0] = 0x00; p[1] = 0x00; p[2] = 0x30; p[3] = 0xa4;
		curlength += 4; p += 4;

		p[0] = 0x78; p[1] = 0x56; p[2] = 0x21; p[3] = 0x20;
		curlength += 4; p += 4;

		p[0] = 0x1f; p[1] = 0x04; p[2] = 0xff; p[3] = 0x5f;
		curlength += 4; p += 4;

		p[0] = 0x00; p[1] = 0x00; p[2] = 0x30; p[3] = 0xb4;
		curlength += 4; p += 4;

		success = 0;	/*  TODO: rename to "continue" or something?  */
	}

	*codechunkp = codechunk;
	*curlengthp = curlength;

	return success;
}


/*
 *  bintrans__codechunk_addtail():
 */
void bintrans__codechunk_addtail(void *codechunk, size_t *curlengthp)
{
	size_t curlength;
	unsigned char *p;

	curlength = *curlengthp;

	p = (unsigned char *)codechunk + curlength;

	/*  Alpha   01 80 fa 6b     ret     zero,(ra),0x1  */
	p[0] = 0x01;
	p[1] = 0x80;
	p[2] = 0xfa;
	p[3] = 0x6b;
	(*curlengthp) += 4;
}


/*
 *  bintrans_try_to_add():
 *
 *  Try to add code at paddr to the translation cache.
 *  Returns 1 on success (if some code was added), 0 if no such code was
 *  added or any other problem occured.
 *
 *  TODO
 */
int bintrans_try_to_add(struct cpu *cpu, struct memory *mem, uint64_t paddr)
{
	int ok;
	uint8_t instr[4];
	uint32_t instrword;
	int rs, rt, imm;
	int do_translate;
	void *codechunk = NULL;
	size_t curlength = 0;

	debug("bintrans_try_to_add(): paddr=0x%016llx\n", (long long)paddr);

	do_translate = 1;

	while (do_translate) {
		ok = memory_rw(cpu, mem, paddr, &instr[0], sizeof(instr), MEM_READ, CACHE_NONE | PHYSICAL);
		if (!ok) {
			fatal("bintrans_try_to_add(): could not read from 0x%llx (?)\n", (long long)paddr);
			if (codechunk != NULL)
				free(codechunk);
			return 0;
		}

		if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
			instrword = (instr[3] << 24) + (instr[2] << 16) + (instr[1] << 8) + instr[0];
		else
			instrword = (instr[0] << 24) + (instr[1] << 16) + (instr[2] << 8) + instr[3];

		fatal("  bintrans_try_to_add(): instr @ 0x%016llx = %08x\n", (long long)paddr, instrword);

		/*  If the instruction we are translating is unknown or too hard to translate,
			then we stop.  Individual cases below should set do_translate to 1
			to continue processing...  */
		do_translate = 0;

		/*  addiu:  */
		if ((instrword >> 26) == 0x09) {
			rs = (instrword >> 21) & 0x1f;
			rt = (instrword >> 16) & 0x1f;
			imm = instrword & 0xffff;
			if (imm >= 32768)
				imm -= 65536;
			fatal("    addiu r%i,r%i,%i\n", rt,rs,imm);
			/*  Add this instruction to the code chunk:  */
			if (bintrans__codechunk_addinstr(&codechunk, &curlength, cpu, mem, BT_ADDIU, rt, rs, imm))
				do_translate = 1;
		}

		paddr += sizeof(instr);

		/*  Abort if we are crossing a physical page boundary:  */
		if ((paddr & 4095) == 0)
			do_translate = 0;
	}

	if (codechunk == NULL)
		return 0;

	if (curlength == 0) {
		free(codechunk);
		return 0;
	}

	/*  Add tail (exit instructions) to the codechunk.  */
	bintrans__codechunk_addtail(codechunk, &curlength);

	fatal("codechunk == %p\n", (void *)codechunk);
{
	int i;
	unsigned char *p;
	printf("codechunk dump:\n");
	p = (unsigned char *)codechunk;
	for (i=0; i<curlength; i+=4)
		printf("  %02x %02x %02x %02x\n", p[i], p[i+1], p[i+2], p[i+3]);
}

	exit(1);

	/*  TODO: add the codechunk to the cache  */

	return 1;
}


/*
 *  bintrans_try_to_run():
 *
 *  Try to run code at paddr from the translation cache.
 *  Returns 1 on success (if some code was run), 0 if no such code was
 *  run or any other problem occured.
 *
 *  TODO
 */
int bintrans_try_to_run(struct cpu *cpu, struct memory *mem, uint64_t paddr)
{
	debug("bintrans_try_to_run(): paddr=0x%016llx\n", (long long)paddr);

	/*  TODO  */

	return 0;
}

