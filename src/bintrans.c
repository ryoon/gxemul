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
 *  $Id: bintrans.c,v 1.13 2004-06-08 10:50:12 debug Exp $
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
 *	Do not translate over MIPS page boundaries (4 KB).  (Perhaps translation
 *		over page boundaries can be allowed in kernel space...?)
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
 *
 *
 *  The general idea would be something like this:
 *  A block of code in the host's memory would have to conform with
 *  the C function calling ABI on that platform, so that the code block
 *  can be called easily from C. That is, entry and exit code needs to
 *  be added, in addition to the translated instructions.
 *
 *	o)  Check for the current PC in the translation cache.
 *	o)  If the current PC is not found, then make a decision
 *		regarding making a translation attempt, or not.
 *		Fallback to normal instruction execution in cpu.c.
 *	o)  If there is a code block for the current PC in the
 *		translation cache, do a couple of checks and then
 *		run the block. Update registers and other values
 *		as neccessary.
 *
 *  The checks would include:
 *	boundaries for load/store memory accesses  (these must be
 *		to pages that are accessible without modifying the TLB,
 *		that is, they either have to be to addresses which
 *		are in the TLB or to kseg0/1 addresses)
 *	no external interrupts should occur for enough number
 *		of cycles
 */


#include <stdio.h>
#include <stdlib.h>

#include "bintrans.h"
#include "memory.h"

/* #define BINTRANS_REGISTER_DEBUG */


/*
 *  bintrans_check_cache():
 *
 *  This function searches the translation cache for a specific address. If
 *  it is found, 1 is returned.  If the address was not found, 0 is returned.
 */
int bintrans_check_cache(struct memory *mem, uint64_t paddr, int *chunk_nr)
{
	int i;
	int stepsize = BINTRANS_CACHEENTRIES / 2;

	/*  debug("bintrans_check_cache(): paddr=0x%016llx\n", (long long)paddr);  */

#if 1
	/*
	 *  Binary tree scan, O(log n):
	 *  This requires BINTRANS_CACHEENTRIES to be a power of
	 *  two, 4 at minimum (?). It also requires all entries to
	 *  always be sorted.
	 */
	i = BINTRANS_CACHEENTRIES / 2;
	while (stepsize > 0) {
		stepsize >>= 1;

		if (i<0 || i>=BINTRANS_CACHEENTRIES) {
			fatal("bintrans_check_cache(): internal error! i=%i\n", i);
			exit(1);
		}

		if (paddr == mem->bintrans_paddr_start[i]) {
			if (mem->bintrans_codechunk_len[i] != 0) {
				if (chunk_nr != NULL)
					*chunk_nr = i;
				return 1;
			} else
				return 0;
		}

		/*  Go right or left:  */
		if (paddr > mem->bintrans_paddr_start[i])
			i += stepsize;
		else
			i -= stepsize;
	}
#else
	/*  Old, linear scan, O(n):  */
	for (i=0; i<BINTRANS_CACHEENTRIES; i++) {
		if (paddr == mem->bintrans_paddr_start[i] &&
		    mem->bintrans_codechunk_len[i] != 0) {
			if (chunk_nr != NULL)
				*chunk_nr = i;
			return 1;
		}
	}
#endif

	return 0;
}


/*
 *  bintrans_invalidate():
 *
 *  This function should be called whenever a part of RAM is being modified,
 *  so that any translation of code at those addresses is removed from the
 *  translation cache.
 *
 *  TODO:  This could be done in O(log n) time, similar to
 *  bintrans_check_cache() above.
 *
 *  TODO 2:  Perhaps we should check that memory is actually modified.
 *  Compare this to the situation of modifying framebuffer (video) memory,
 *  if the new values put in the framebuffer equal the old values. Then
 *  no redraw of the screen is neccessary.  Similarly, if ram is updated
 *  with new code, but the new code is the same as the old code, then no
 *  invalidation needs to be done!
 */
void bintrans_invalidate(struct memory *mem, uint64_t paddr, uint64_t len)
{
	int i;

	/*  debug("bintrans_invalidate(): paddr=0x%016llx len=0x%llx\n",
	    (long long)paddr, (long long)len);  */

	for (i=0; i<BINTRANS_CACHEENTRIES; i++) {
		/*
		 *  Invalidate codechunk[i] if any of the following is true:
		 *
		 *	paddr is within [start, end]
		 *	paddr+len-1 is within [start, end]
		 *	paddr is less than start, and paddr+len-1 is more than end
		 */
		if (   (paddr >= mem->bintrans_paddr_start[i] && paddr <= mem->bintrans_paddr_end[i])
		    || (paddr+len-1 >= mem->bintrans_paddr_start[i] && paddr+len-1 <= mem->bintrans_paddr_end[i])
		    || (paddr < mem->bintrans_paddr_start[i] && paddr+len-1 > mem->bintrans_paddr_end[i])
		    ) {
			mem->bintrans_codechunk_len[i] = 0;
		}
	}
}


#define	BT_UNKNOWN		0
#define	BT_NOP			1
#define	BT_ADDIU		2
#define	BT_DADDIU		3
#define	BT_ADDU			4
#define	BT_DADDU		5
#define	BT_SUBU			6
#define	BT_DSUBU		7
#define	BT_LUI			8
#define	BT_OR			9
#define	BT_XOR			10
#define	BT_AND			11
#define	BT_SLT			12
#define	BT_SLTU			13
#define	BT_LW			14
#define	BT_JAL			15
#define	BT_J			16
#define	BT_BNE			17
#define	BT_BEQ			18
#define	BT_SLL			19
#define	BT_DSLL			20
#define	BT_SRL			21
#define	BT_SRA			22
#define	BT_DSRL			23
#define	BT_SW			24
#define	BT_LB			25
#define	BT_LBU			26
#define	BT_SB			27
#define	BT_ANDI			28
#define	BT_ORI			29

#define	CODECHUNK_SIZE		(4*192)
#define	CODECHUNK_SIZE_MARGIN	(4*64)


#ifndef BINTRANS
/*
 *  No bintrans, then let's supply dummy functions:
 */
void bintrans__codechunk_flushpc(struct cpu *cpu, void *codechunk,
	size_t *curlengthp, int *n_instructions, int *n_instr_delta) { }
int bintrans__codechunk_addinstr(void **codechunkp, size_t *curlengthp,
	struct cpu *cpu, struct memory *mem, int bt_instruction,
	int rt, int rs, int rd, int imm,
	int *n_instructions, int *n_instr_delta) { return 0; }
void bintrans__codechunk_addtail(struct cpu *cpu, void *codechunk,
	size_t *curlengthp, int *n_instructions, int *n_instr_delta) { }
#else
#ifdef ALPHA
#include "bintrans_alpha.c"
#endif
#endif


/*
 *  bintrans_try_to_add():
 *
 *  Try to add code at paddr to the translation cache.
 *
 *  Returns 1 on success (if some code was added), 0 if no such code was
 *  added or any other problem occured.
 *
 *  If we're returning successfully, and chunk_nr != NULL, then *chunk_nr
 *  is set to the chunk number that we just filled with code.
 */
int bintrans_try_to_add(struct cpu *cpu, struct memory *mem, uint64_t paddr, int *chunk_nr)
{
	int oldest, oldest_i, i;
	int ok;
	uint32_t instrword;
	int rs, rt, rd, imm;
	int do_translate;
	int n_instructions = 0, n_instr_delta = 0;
	size_t curlength = 0;
	uint64_t paddr_start = paddr;
	unsigned char *hostaddr;
	int registerhint = 0, registerhint_write = 0;

	static int blah_counter = 0;

	if ((++blah_counter) & 15)
		return 0;

        mem->bintrans_tickcount ++;


	/*  Find a suitable slot in the cache:  */
	oldest = -1;
	oldest_i = -1;
	for (i=0; i<BINTRANS_CACHEENTRIES; i++) {
		if (oldest_i == -1 || mem->bintrans_codechunk_time[i] < oldest
		    || mem->bintrans_codechunk[i] == NULL || mem->bintrans_codechunk_len == 0) {
			oldest_i = i;
			oldest = mem->bintrans_codechunk_time[i];
		}
	}

	if (oldest_i == -1) {
		fatal("no free codechunk slot?\n");
		exit(1);
	}

	/*  debug("bintrans_try_to_add(): paddr=0x%016llx\n", (long long)paddr);  */

	do_translate = 1;

	/*  hostaddr is the address on the host where we read the instruction from:  */
	hostaddr = (unsigned char *) (size_t) (((size_t)mem->bintrans_last_host4kpage) | (paddr & 0xfff));

	while (do_translate) {
		if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
			instrword = (hostaddr[3] << 24) + (hostaddr[2] << 16) + (hostaddr[1] << 8) + hostaddr[0];
		else
			instrword = (hostaddr[0] << 24) + (hostaddr[1] << 16) + (hostaddr[2] << 8) + hostaddr[3];

		/*  debug("  bintrans_try_to_add(): instr @ 0x%016llx = %08x\n", (long long)paddr, instrword);  */

		/*  If the instruction we are translating is unknown or too hard to translate,
			then we stop.  Individual cases below should set do_translate to 1
			to continue processing...  */
		do_translate = 0;

		/*  addiu:  */
		if ((instrword >> 26) == HI6_ADDIU || (instrword >> 26) == HI6_DADDIU) {
			rs = (instrword >> 21) & 0x1f;
			rt = (instrword >> 16) & 0x1f;
			imm = instrword & 0xffff;
			if (imm >= 32768)
				imm -= 65536;

			do_translate = bintrans__codechunk_addinstr(&mem->bintrans_codechunk[oldest_i], &curlength, cpu, mem,
			    ((instrword >> 26) == HI6_DADDIU)? BT_DADDIU : BT_ADDIU, rt, rs, 0, imm, &n_instructions, &n_instr_delta);
		}
		else
		/*  andi:  */
		if ((instrword >> 26) == HI6_ANDI) {
			rs = (instrword >> 21) & 0x1f;
			rt = (instrword >> 16) & 0x1f;
			imm = instrword & 0xffff;
			if (imm >= 32768)
				imm -= 65536;

			do_translate = bintrans__codechunk_addinstr(&mem->bintrans_codechunk[oldest_i], &curlength, cpu, mem,
			    BT_ANDI, rt, rs, 0, imm, &n_instructions, &n_instr_delta);
		}
		else
		/*  ori:  */
		if ((instrword >> 26) == HI6_ORI) {
			rs = (instrword >> 21) & 0x1f;
			rt = (instrword >> 16) & 0x1f;
			imm = instrword & 0xffff;
			if (imm >= 32768)
				imm -= 65536;

			do_translate = bintrans__codechunk_addinstr(&mem->bintrans_codechunk[oldest_i], &curlength, cpu, mem,
			    BT_ORI, rt, rs, 0, imm, &n_instructions, &n_instr_delta);
		}
		else
		/*  sll:  */
		if (  (instrword >> 26) == HI6_SPECIAL && ( (instrword & 0x1f) == SPECIAL_SLL
		    || (instrword & 0x1f) == SPECIAL_DSRL
		    || (instrword & 0x1f) == SPECIAL_SRL
		    || (instrword & 0x1f) == SPECIAL_SRA
		    || (instrword & 0x1f) == SPECIAL_DSLL ) ) {
			int b;

			switch (instrword & 0x1f) {
			case SPECIAL_SLL:	b = BT_SLL; break;
			case SPECIAL_SRA:	b = BT_SRA; break;
			case SPECIAL_SRL:	b = BT_SRL; break;
			case SPECIAL_DSLL:	b = BT_DSLL; break;
			case SPECIAL_DSRL:	b = BT_DSRL; break;
			}

			rt = (instrword >> 16) & 0x1f;
			rd = (instrword >> 11) & 0x1f;
			rs = (instrword >>  6) & 0x1f;	/*  rs used as sa  */

			do_translate = bintrans__codechunk_addinstr(&mem->bintrans_codechunk[oldest_i], &curlength, cpu, mem,
			    b, rt, rs, rd, 0, &n_instructions, &n_instr_delta);
		}
		else
		/*  many r1,r2,r3 instructions:  */
		if ( ((instrword >> 26) == 0x00 && (instrword & 0x3f) == SPECIAL_OR) ||
		     ((instrword >> 26) == 0x00 && (instrword & 0x3f) == SPECIAL_ADDU) ||
		     ((instrword >> 26) == 0x00 && (instrword & 0x3f) == SPECIAL_DADDU) ||
		     ((instrword >> 26) == 0x00 && (instrword & 0x3f) == SPECIAL_SUBU) ||
		     ((instrword >> 26) == 0x00 && (instrword & 0x3f) == SPECIAL_DSUBU) ||
		     ((instrword >> 26) == 0x00 && (instrword & 0x3f) == SPECIAL_XOR) ||
		     ((instrword >> 26) == 0x00 && (instrword & 0x3f) == SPECIAL_SLT) ||
		     ((instrword >> 26) == 0x00 && (instrword & 0x3f) == SPECIAL_SLTU) ||
		     ((instrword >> 26) == 0x00 && (instrword & 0x3f) == SPECIAL_AND) ) {
			int b;

			rs = (instrword >> 21) & 0x1f;
			rt = (instrword >> 16) & 0x1f;
			rd = (instrword >> 11) & 0x1f;

			switch (instrword & 0x3f) {
			case SPECIAL_ADDU:	b = BT_ADDU; break;
			case SPECIAL_DADDU:	b = BT_DADDU; break;
			case SPECIAL_SUBU:	b = BT_SUBU; break;
			case SPECIAL_DSUBU:	b = BT_DSUBU; break;
			case SPECIAL_OR:	b = BT_OR; break;
			case SPECIAL_XOR:	b = BT_XOR; break;
			case SPECIAL_AND:	b = BT_AND; break;
			case SPECIAL_SLT:	b = BT_SLT; break;
			case SPECIAL_SLTU:	b = BT_SLTU; break;
			}

			do_translate = bintrans__codechunk_addinstr(&mem->bintrans_codechunk[oldest_i], &curlength, cpu, mem, b, rt, rs, rd, 0, &n_instructions, &n_instr_delta);
		}
		else
		/*  lui:  */
		if ((instrword >> 26) == HI6_LUI) {
			rt = (instrword >> 16) & 0x1f;
			imm = instrword & 0xffff;
			do_translate = bintrans__codechunk_addinstr(&mem->bintrans_codechunk[oldest_i], &curlength, cpu, mem, BT_LUI, rt, 0, 0, imm, &n_instructions, &n_instr_delta);
		}
		else
		/*  jal or j:  */
		if ((instrword >> 26) == HI6_JAL || (instrword >> 26) == HI6_J) {
			int b;
			switch (instrword >> 26) {
			case HI6_J:	b = BT_J; break;
			case HI6_JAL:	b = BT_JAL; break;
			}

			imm = instrword & ((1<<26)-1);
			do_translate = bintrans__codechunk_addinstr(&mem->bintrans_codechunk[oldest_i], &curlength, cpu, mem, b, 0, 0, 0, imm, &n_instructions, &n_instr_delta);

			/*  XXX  */
			if (do_translate)
				n_instr_delta ++;
			do_translate = 0;
		}
		else
		/*  bne, beq:  */
		if ((instrword >> 26) == HI6_BNE || (instrword >> 26) == HI6_BEQ) {
			int b;
			switch (instrword >> 26) {
			case HI6_BNE:	b = BT_BNE; break;
			case HI6_BEQ:	b = BT_BEQ; break;
			}

			rs = (instrword >> 21) & 0x1f;
			rt = (instrword >> 16) & 0x1f;
			imm = instrword & 0xffff;
			if (imm >= 32768)
				imm -= 65536;

			do_translate = bintrans__codechunk_addinstr(&mem->bintrans_codechunk[oldest_i], &curlength, cpu, mem, b, rt, rs, 0, imm, &n_instructions, &n_instr_delta);

			/*  XXX  */
			if (do_translate)
				n_instr_delta ++;
			do_translate = 0;
		}
		else
		/*  lb:  */
		if ((instrword >> 26) == HI6_LB) {
			rt = (instrword >> 16) & 0x1f;
			rs = (instrword >> 21) & 0x1f;		/*  use rs as base!  */
			imm = instrword & 0xffff;		/*  offset  */
			if (imm >= 32768)
				imm -= 65536;

			registerhint = rs;
			do_translate = bintrans__codechunk_addinstr(&mem->bintrans_codechunk[oldest_i], &curlength, cpu, mem, BT_LB, rt, rs, 0, imm, &n_instructions, &n_instr_delta);
		}
		else
		/*  lbu:  */
		if ((instrword >> 26) == HI6_LBU) {
			rt = (instrword >> 16) & 0x1f;
			rs = (instrword >> 21) & 0x1f;		/*  use rs as base!  */
			imm = instrword & 0xffff;		/*  offset  */
			if (imm >= 32768)
				imm -= 65536;

			registerhint = rs;
			do_translate = bintrans__codechunk_addinstr(&mem->bintrans_codechunk[oldest_i], &curlength, cpu, mem, BT_LBU, rt, rs, 0, imm, &n_instructions, &n_instr_delta);
		}
		else
		/*  lw:  */
		if ((instrword >> 26) == HI6_LW) {
			rt = (instrword >> 16) & 0x1f;
			rs = (instrword >> 21) & 0x1f;		/*  use rs as base!  */
			imm = instrword & 0xffff;		/*  offset  */
			if (imm >= 32768)
				imm -= 65536;

			registerhint = rs;
			do_translate = bintrans__codechunk_addinstr(&mem->bintrans_codechunk[oldest_i], &curlength, cpu, mem, BT_LW, rt, rs, 0, imm, &n_instructions, &n_instr_delta);
		}
		else
#if 1
		/*  sb:  */
		if ((instrword >> 26) == HI6_SB) {
			rt = (instrword >> 16) & 0x1f;
			rs = (instrword >> 21) & 0x1f;		/*  use rs as base!  */
			imm = instrword & 0xffff;		/*  offset  */
			if (imm >= 32768)
				imm -= 65536;

			registerhint = rs;
			registerhint_write = 1;
			do_translate = bintrans__codechunk_addinstr(&mem->bintrans_codechunk[oldest_i], &curlength, cpu, mem, BT_SB, rt, rs, 0, imm, &n_instructions, &n_instr_delta);
		}
		else
#endif
		/*  sw:  */
		if ((instrword >> 26) == HI6_SW) {
			rt = (instrword >> 16) & 0x1f;
			rs = (instrword >> 21) & 0x1f;		/*  use rs as base!  */
			imm = instrword & 0xffff;		/*  offset  */
			if (imm >= 32768)
				imm -= 65536;

			registerhint = rs;
			registerhint_write = 1;
			do_translate = bintrans__codechunk_addinstr(&mem->bintrans_codechunk[oldest_i], &curlength, cpu, mem, BT_SW, rt, rs, 0, imm, &n_instructions, &n_instr_delta);
		}
		else
		/*  nop:  */
		if (instrword == 0) {
			do_translate = bintrans__codechunk_addinstr(&mem->bintrans_codechunk[oldest_i], &curlength, cpu, mem, BT_NOP, 0,0,0,0, &n_instructions, &n_instr_delta);
		}
#if 0
		else {
			char *names[] = HI6_NAMES;
			printf("instrword = %s\n", names[instrword >> 26]);
		}
#endif

		paddr += sizeof(instrword);
		hostaddr += sizeof(instrword);

		if (do_translate)
			n_instr_delta ++;

		/*  Abort if we are crossing a physical MIPS page boundary:  */
		if ((paddr & 0xfff) == 0)
			do_translate = 0;
	}

#if 1
	/*  If too few instructions were translated, then forget it:  */
	if (n_instructions + n_instr_delta <= 1)
		curlength = 0;
#endif

	/*  In case of length zero, we have to destroy this entry:  */
	mem->bintrans_codechunk_len[oldest_i] = curlength;

	if (curlength == 0)
		return 0;

	/*  Add tail (exit instructions) to the codechunk.  */
	bintrans__codechunk_addtail(cpu, mem->bintrans_codechunk[oldest_i],
	    &curlength, &n_instructions, &n_instr_delta);


#if 0
{
	int i;
	unsigned char *p;
	debug("  codechunk == %p, dump:\n", (void *)mem->bintrans_codechunk[oldest_i]);
	p = (unsigned char *)mem->bintrans_codechunk[oldest_i];
	for (i=0; i<curlength; i+=4)
		debug("    %02x %02x %02x %02x\n", p[i], p[i+1], p[i+2], p[i+3]);
}
#endif

	/*
	 *  Add the codechunk to the cache:
	 */
	mem->bintrans_codechunk_len[oldest_i] = curlength;
	mem->bintrans_codechunk_time[oldest_i] = mem->bintrans_tickcount;
	mem->bintrans_codechunk_memregisterhint[oldest_i] = registerhint + (registerhint_write? MEMREGISTERHINT_WRITE : 0);
	mem->bintrans_codechunk_ninstr[oldest_i] = n_instructions;

	mem->bintrans_paddr_start[oldest_i] = paddr_start;
	mem->bintrans_paddr_end[oldest_i] = paddr - 1;


#if 1
	/*
	 *  Sort the bintrans codechunks:
	 */
{
	int i = oldest_i, j;
	while (i>=0 && i<BINTRANS_CACHEENTRIES) {
		j = -1;
		if (i>0 && mem->bintrans_paddr_start[i] < mem->bintrans_paddr_start[i-1])
			j = i-1;
		if (i<BINTRANS_CACHEENTRIES-1 && mem->bintrans_paddr_start[i] > mem->bintrans_paddr_start[i+1])
			j = i+1;

		if (j > -1) {
			uint64_t tmp; int64_t tmps; void *tmpp;
			tmp = mem->bintrans_paddr_start[i];
			mem->bintrans_paddr_start[i] = mem->bintrans_paddr_start[j];
			mem->bintrans_paddr_start[j] = tmp;

			tmp = mem->bintrans_paddr_end[i];
			mem->bintrans_paddr_end[i] = mem->bintrans_paddr_end[j];
			mem->bintrans_paddr_end[j] = tmp;

			tmpp = mem->bintrans_codechunk[i];
			mem->bintrans_codechunk[i] = mem->bintrans_codechunk[j];
			mem->bintrans_codechunk[j] = tmpp;

			tmps = mem->bintrans_codechunk_len[i];
			mem->bintrans_codechunk_len[i] = mem->bintrans_codechunk_len[j];
			mem->bintrans_codechunk_len[j] = tmps;

			tmps = mem->bintrans_codechunk_time[i];
			mem->bintrans_codechunk_time[i] = mem->bintrans_codechunk_time[j];
			mem->bintrans_codechunk_time[j] = tmps;

			tmps = mem->bintrans_codechunk_ninstr[i];
			mem->bintrans_codechunk_ninstr[i] = mem->bintrans_codechunk_ninstr[j];
			mem->bintrans_codechunk_ninstr[j] = tmps;

			tmps = mem->bintrans_codechunk_memregisterhint[i];
			mem->bintrans_codechunk_memregisterhint[i] = mem->bintrans_codechunk_memregisterhint[j];
			mem->bintrans_codechunk_memregisterhint[j] = tmps;

			i = j;
		} else {
			oldest_i = i;
			break;
		}
	}
}
#endif

	if (chunk_nr != NULL)
		*chunk_nr = oldest_i;

	return 1;
}


/*
 *  bintrans_try_to_run():
 *
 *  Try to run code at paddr from the translation cache, if circumstances
 *  are good.
 *
 *  Returns:
 *    On success:  the number of instructions executed in bintrans mode,
 *    on failure:  -1.
 */
int bintrans_try_to_run(struct cpu *cpu, struct memory *mem, uint64_t paddr, int chunk_nr)
{
	void (*f)(struct cpu *, long, long);
	long vaddrbase, hostaddrbase;

	/*  debug("bintrans_try_to_run(): paddr=0x%016llx\n", (long long)paddr);  */

/*	if (cpu->delay_slot || cpu->nullify_next)
		return -1;
*/

	/*  If any external interrupts will occur in the time it will take
		to run the codechunk, then we must abort now!  */
	/*  TODO  */

/*	if (chunk_nr < 0 || mem->bintrans_codechunk_len[chunk_nr] == 0) {
		fatal("bintrans_try_to_run() called, paddr=0x%016llx, but chunk nr %i not in cache?\n",
		    (long long)paddr, chunk_nr);
		exit(1);
	}
*/

#if 0
{
	/*  Reorder to gain a little speed:  */
	int other_chunk_nr = chunk_nr - 1;
	uint64_t tmpv;  void *tmpp;  size_t tmpl;

	if (other_chunk_nr >= 0) {
		tmpv = mem->bintrans_paddr_start[other_chunk_nr];
		    mem->bintrans_paddr_start[other_chunk_nr] = mem->bintrans_paddr_start[chunk_nr];
		    mem->bintrans_paddr_start[chunk_nr] = tmpv;

		tmpv = mem->bintrans_paddr_end[other_chunk_nr];
		    mem->bintrans_paddr_end[other_chunk_nr] = mem->bintrans_paddr_end[chunk_nr];
		    mem->bintrans_paddr_end[chunk_nr] = tmpv;

		tmpp = mem->bintrans_codechunk[other_chunk_nr];
		    mem->bintrans_codechunk[other_chunk_nr] = mem->bintrans_codechunk[chunk_nr];
		    mem->bintrans_codechunk[chunk_nr] = tmpp;

		tmpl = mem->bintrans_codechunk_len[other_chunk_nr];
		    mem->bintrans_codechunk_len[other_chunk_nr] = mem->bintrans_codechunk_len[chunk_nr];
		    mem->bintrans_codechunk_len[chunk_nr] = tmpl;

		tmpv = mem->bintrans_codechunk_time[other_chunk_nr];
		    mem->bintrans_codechunk_time[other_chunk_nr] = mem->bintrans_codechunk_time[chunk_nr];
		    mem->bintrans_codechunk_time[chunk_nr] = tmpv;

		tmpl = other_chunk_nr;
		    other_chunk_nr = chunk_nr;
		    chunk_nr = tmpl;
	}
}
#endif

	/*  Run the chunk:  */
	f = mem->bintrans_codechunk[chunk_nr];
        mem->bintrans_tickcount ++;
	mem->bintrans_codechunk_time[chunk_nr] = mem->bintrans_tickcount;

#ifdef BINTRANS_REGISTER_DEBUG
	debug("DEBUG A: running codechunk %i @ %p (paddr 0x%08llx)\n", chunk_nr, (void *)f, (long long)paddr);

{
	uint64_t regs[33];	/*  nr 32 is pc  */
	int i;
	for (i=0; i<32; i++)
		regs[i] = cpu->gpr[i];
	regs[32] = cpu->pc;
#endif

	/*
	 *  Single-page address translation (for translated load and store):
	 *
	 *  TODO:  What are good initial values for vaddrbase, hostaddrbase?
	 */
	vaddrbase = 0;
	hostaddrbase = 0;	/*  NULL ptr!!! TODO  */

	if (mem->bintrans_codechunk_memregisterhint[chunk_nr] != 0) {
		uint64_t paddr;
		int reg = mem->bintrans_codechunk_memregisterhint[chunk_nr] & 31;
		uint64_t vaddr = cpu->gpr[reg];
		unsigned char *hostpage;

		if (vaddr < 0x80000000 || cpu->pc >= 0x80000000) {
			vaddr &= ~0xfff;
			hostpage = translate_vaddrpage_to_hostpage(cpu, vaddr, &paddr);

			/*  No hostpage? Then don't run the translated
			    code chunk.  */
			if (hostpage == NULL)
				return -1;

			if (mem->bintrans_codechunk_memregisterhint[chunk_nr] & MEMREGISTERHINT_WRITE) {
				bintrans_invalidate(cpu->mem, paddr, 4096);

				/*  Writing to the page we're about to run code from?  Then abort.  */
				if ((mem->bintrans_paddr_start[chunk_nr] & ~0xfff) == paddr)
					return -1;
			}

			hostaddrbase = (size_t)hostpage;
		}
	}

#ifdef ALPHA
	f(cpu, vaddrbase, hostaddrbase);
#endif

#ifdef BINTRANS_REGISTER_DEBUG
	for (i=0; i<32; i++)
		if (regs[i] != cpu->gpr[i])
			printf("REGISTER r%i changed from 0x%llx to 0x%llx\n",
			    i, (long long)regs[i], (long long)cpu->gpr[i]);
	if (regs[32] != cpu->pc)
		printf("REGISTER pc  changed from 0x%llx to 0x%llx\n",
		    (long long)regs[32], (long long)cpu->pc);
	}

	debug("DEBUG B: returning from codechunk %i\n\n", chunk_nr);
#endif

	return mem->bintrans_codechunk_ninstr[chunk_nr];
}

