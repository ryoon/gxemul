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
 *  $Id: bintrans.c,v 1.6 2004-01-30 03:10:22 debug Exp $
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


/*
 *  bintrans_check_cache():
 *
 *  This function searches the translation cache for a specific address. If
 *  it is found, 1 is returned.  If the address was not found, 0 is returned.
 */
int bintrans_check_cache(struct memory *mem, uint64_t paddr, int *chunk_nr)
{
	int i;

	/*  debug("bintrans_check_cache(): paddr=0x%016llx\n", (long long)paddr);  */

	for (i=0; i<BINTRANS_CACHEENTRIES; i++) {
		if (paddr == mem->bintrans_paddr_start[i] &&
		    mem->bintrans_codechunk[i] != NULL) {
			if (chunk_nr != NULL)
				*chunk_nr = i;
			return 1;
		}
	}

	return 0;
}


/*
 *  bintrans_invalidate():
 *
 *  This function should be called whenever a part of RAM is being modified,
 *  so that any translation of code at those addresses is removed from the
 *  translation cache.
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
#define	BT_ADDIU		1
#define	BT_DADDIU		2
#define	BT_ADDU			3
#define	BT_DADDU		4
#define	BT_NOP			5

#define	CODECHUNK_SIZE		512

/*
 *  bintrans__codechunk_addinstr():
 *
 *  Used internally by bintrans_try_to_add().
 *  This function tries to translate an instruction into native
 *  code.
 *  Returns 1 on success, 0 if no code was translated.
 */
int bintrans__codechunk_addinstr(void **codechunkp, size_t *curlengthp, struct cpu *cpu, struct memory *mem,
	int bt_instruction, int rt, int rs, int rd, int imm)
{
	void *codechunk;
	size_t curlength;
	int success = 0;
	int ofs_rt, ofs_rs, ofs_rd;
	unsigned char *p;

	if (codechunkp == NULL) {
		fatal("bintrans__codechunk_addinstr(): codechunkp == NULL\n");
		exit(1);
	}

	codechunk = *codechunkp;
	curlength = *curlengthp;

	/*  Create codechunk header, if neccessary:  */
	if (codechunk == NULL) {
		/*  debug("creating codechunk header...\n");  */
		codechunk = malloc(CODECHUNK_SIZE);
		curlength = 0;
	}

	p = (unsigned char *)codechunk + curlength;


	/*
	 *  ADDIU / DADDIU:
	 *
	 *  f(unsigned char *p) {
	 *	(*((unsigned long long *)(p + 0x1234))) =
	 *	    (*((unsigned long long *)(p + 0x2348))) + 0x5678;
	 *  }
	 *
	 *  If p is a pointer to a cpu struct, 0x1234 is the offset to the cpu
	 *  register rt, 0x2348 is the offset of register rs (the source), and
	 *  0x5678 is the amount to add, then we get this Alpha code:
	 *
	 *  (a0 is the cpu struct pointer)
	 *
	 *   0:   48 23 30 a4     ldq     t0,9032(a0)		<-- load
	 *   4:   01 14 20 40     addq    t0,0,t0		<-- inc
	 *   8:   34 12 30 b4     stq     t0,4660(a0)		<-- store
	 *
	 *  NOTE: the code above is for DADDIU. The following two instructions
	 *  sign-extend as should be done with ADDIU:
	 *
	 *   c:   f0 7f 50 a0     ldl     t1,32752(a0)		<-- load 32-bit signed
	 *  10:   f0 7f 50 b4     stq     t1,32752(a0)		<-- store as 64-bit again
	 */
	if ((bt_instruction == BT_ADDIU || bt_instruction == BT_DADDIU) && imm >= 0 && imm < 0xff) {
		ofs_rt = (size_t)&(cpu->gpr[rt]) - (size_t)cpu;
		ofs_rs = (size_t)&(cpu->gpr[rs]) - (size_t)cpu;
		/*  debug("offsets ofs_rt=%i ofs_rs=%i\n", ofs_rt, ofs_rs);  */

		if (imm != 0 && rt != 0) {
			p[0] = ofs_rs & 255; p[1] = ofs_rs >> 8; p[2] = 0x30; p[3] = 0xa4;  curlength += 4; p += 4;
			p[0] = 0x01; p[1] = 0x14 + ((imm & 7) << 5); p[2] = 0x20 + ((imm & 0xf8) >> 3); p[3] = 0x40;  curlength += 4; p += 4;
			p[0] = ofs_rt & 255; p[1] = ofs_rt >> 8; p[2] = 0x30; p[3] = 0xb4;  curlength += 4; p += 4;

			/*  Sign-extend, for 32-bit addiu:  */
			if (bt_instruction == BT_ADDIU) {
				p[0] = ofs_rt & 255; p[1] = ofs_rt >> 8; p[2] = 0x50; p[3] = 0xa0;  curlength += 4; p += 4;
				p[0] = ofs_rt & 255; p[1] = ofs_rt >> 8; p[2] = 0x50; p[3] = 0xb4;  curlength += 4; p += 4;
			}
		}

		/*  Add 4 to cpu->pc  */
		ofs_rs = (size_t)&(cpu->pc) - (size_t)cpu;
		p[0] = ofs_rs & 255; p[1] = ofs_rs >> 8; p[2] = 0x30; p[3] = 0xa4;	curlength += 4; p += 4;
		p[0] = 1;            p[1] = 0x94;        p[2] = 0x20; p[3] = 0x40;	curlength += 4; p += 4;
		p[0] = ofs_rs & 255; p[1] = ofs_rs >> 8; p[2] = 0x30; p[3] = 0xb4;	curlength += 4; p += 4;

		/*  Actually, PC should be sign extended, but this would be really uncommon in practice. (TODO)  */

		success = 1;	/*  TODO: rename to "continue" or something?  */
	}

	/*
	 *  ADDU:
	 *
	 *  (a0 is the cpu struct pointer)
	 *
	 *   0:   08 10 30 a4     ldq     t0,4104(a0)		<-- load
	 *   4:   0c 10 50 a4     ldq     t1,4108(a0)		<-- load
	 *   8:   01 04 22 40     addq    t0,t1,t0		<-- add
	 *   c:   1f 04 ff 5f     fnop
	 *  10:   04 10 30 b4     stq     t0,4100(a0)		<-- store
	 *
	 *  NOTE: the code above is for DADDU. Sign extension like in addiu (above)
	 *  is neccessary for addu.
	 */
	if (bt_instruction == BT_ADDU || bt_instruction == BT_DADDU) {
		ofs_rt = (size_t)&(cpu->gpr[rt]) - (size_t)cpu;
		ofs_rs = (size_t)&(cpu->gpr[rs]) - (size_t)cpu;
		ofs_rd = (size_t)&(cpu->gpr[rd]) - (size_t)cpu;
		/*  debug("offsets ofs_rt=%i ofs_rs=%i, ofs_rd=%i\n", ofs_rt, ofs_rs, ofs_rd);  */

		if (rd != 0) {
			p[0] = ofs_rs & 255; p[1] = ofs_rs >> 8; p[2] = 0x30; p[3] = 0xa4;  curlength += 4; p += 4;
			p[0] = ofs_rt & 255; p[1] = ofs_rt >> 8; p[2] = 0x50; p[3] = 0xa4;  curlength += 4; p += 4;
			p[0] = 0x01;         p[1] = 0x04;        p[2] = 0x22; p[3] = 0x40;  curlength += 4; p += 4;
			p[0] = 0x1f;         p[1] = 0x04;        p[2] = 0xff; p[3] = 0x5f;  curlength += 4; p += 4;
			p[0] = ofs_rd & 255; p[1] = ofs_rd >> 8; p[2] = 0x30; p[3] = 0xb4;  curlength += 4; p += 4;

			/*  Sign-extend rd, for 32-bit addu:  */
			if (bt_instruction == BT_ADDU) {
				p[0] = ofs_rd & 255; p[1] = ofs_rd >> 8; p[2] = 0x50; p[3] = 0xa0;  curlength += 4; p += 4;
				p[0] = ofs_rd & 255; p[1] = ofs_rd >> 8; p[2] = 0x50; p[3] = 0xb4;  curlength += 4; p += 4;
			}
		}

		/*  Add 4 to cpu->pc  */
		ofs_rs = (size_t)&(cpu->pc) - (size_t)cpu;
		p[0] = ofs_rs & 255; p[1] = ofs_rs >> 8; p[2] = 0x30; p[3] = 0xa4;	curlength += 4; p += 4;
		p[0] = 1;            p[1] = 0x94;        p[2] = 0x20; p[3] = 0x40;	curlength += 4; p += 4;
		p[0] = ofs_rs & 255; p[1] = ofs_rs >> 8; p[2] = 0x30; p[3] = 0xb4;	curlength += 4; p += 4;

		/*  Actually, PC should be sign extended, but this would be really uncommon in practice. (TODO)  */

		success = 1;	/*  TODO: rename to "continue" or something?  */
	}

	/*
	 *  NOP:
	 *
	 *  Add 4 to cpu->pc.
	 */
	if (bt_instruction == BT_NOP) {
		/*  Add 4 to cpu->pc  */
		ofs_rs = (size_t)&(cpu->pc) - (size_t)cpu;
		p[0] = ofs_rs & 255; p[1] = ofs_rs >> 8; p[2] = 0x30; p[3] = 0xa4;	curlength += 4; p += 4;
		p[0] = 1;            p[1] = 0x94;        p[2] = 0x20; p[3] = 0x40;	curlength += 4; p += 4;
		p[0] = ofs_rs & 255; p[1] = ofs_rs >> 8; p[2] = 0x30; p[3] = 0xb4;	curlength += 4; p += 4;

		/*  Actually, PC should be sign extended, but this would be really uncommon in practice. (TODO)  */
		success = 1;	/*  TODO: rename to "continue" or something?  */
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

	p[0] = 0x01;  p[1] = 0x80;  p[2] = 0xfa;  p[3] = 0x6b;  /*  ret     zero,(ra),0x1  */	p += 4; curlength += 4;

	if ((curlength & 0x7) == 0) {
		p[0] = 0x1f;  p[1] = 0x04;  p[2] = 0xff;  p[3] = 0x47;	/*  nop  */			p += 4; curlength += 4;
	}
	p[0] = 0x00;  p[1] = 0x00;  p[2] = 0xe0;  p[3] = 0x2f;  /*  unop  */			p += 4; curlength += 4;

	if ((curlength & 0x7) == 0) {
		p[0] = 0x1f;  p[1] = 0x04;  p[2] = 0xff;  p[3] = 0x47;	/*  nop  */			p += 4; curlength += 4;
	}
	p[0] = 0x00;  p[1] = 0x00;  p[2] = 0xe0;  p[3] = 0x2f;  /*  unop  */			p += 4; curlength += 4;

	if ((curlength & 0x7) == 0) {
		p[0] = 0x1f;  p[1] = 0x04;  p[2] = 0xff;  p[3] = 0x47;	/*  nop  */			p += 4; curlength += 4;
	}
	p[0] = 0x00;  p[1] = 0x00;  p[2] = 0xe0;  p[3] = 0x2f;  /*  unop  */			p += 4; curlength += 4;

	if ((curlength & 0x7) == 0) {
		p[0] = 0x1f;  p[1] = 0x04;  p[2] = 0xff;  p[3] = 0x47;	/*  nop  */			p += 4; curlength += 4;
	}
	p[0] = 0x00;  p[1] = 0x00;  p[2] = 0xe0;  p[3] = 0x2f;  /*  unop  */			p += 4; curlength += 4;

	(*curlengthp) = curlength;
}


/*
 *  bintrans_try_to_add():
 *
 *  Try to add code at paddr to the translation cache.
 *  Returns 1 on success (if some code was added), 0 if no such code was
 *  added or any other problem occured.
 */
int bintrans_try_to_add(struct cpu *cpu, struct memory *mem, uint64_t paddr)
{
	int oldest, oldest_i, i;
	int ok;
	uint8_t instr[4];
	uint32_t instrword;
	int rs, rt, rd, imm;
	int do_translate;
	size_t curlength = 0;
	uint64_t paddr_start = paddr;

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

	while (do_translate) {
		ok = memory_rw(cpu, mem, paddr, &instr[0], sizeof(instr), MEM_READ, CACHE_NONE | PHYSICAL);
		if (!ok) {
			fatal("bintrans_try_to_add(): could not read from 0x%llx (?)\n", (long long)paddr);
			return 0;
		}

		if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
			instrword = (instr[3] << 24) + (instr[2] << 16) + (instr[1] << 8) + instr[0];
		else
			instrword = (instr[0] << 24) + (instr[1] << 16) + (instr[2] << 8) + instr[3];

/*		debug("  bintrans_try_to_add(): instr @ 0x%016llx = %08x\n", (long long)paddr, instrword);
*/

		/*  If the instruction we are translating is unknown or too hard to translate,
			then we stop.  Individual cases below should set do_translate to 1
			to continue processing...  */
		do_translate = 0;

		/*  addiu:  */
		if ((instrword >> 26) == 0x09 || (instrword >> 26) == 0x19) {
			rs = (instrword >> 21) & 0x1f;
			rt = (instrword >> 16) & 0x1f;
			imm = instrword & 0xffff;
			if (imm >= 32768)
				imm -= 65536;

/*			if ((instrword >> 26) == 0x19)
				debug("    daddiu r%i,r%i,%i\n", rt,rs,imm);
			else
				debug("    addiu r%i,r%i,%i\n", rt,rs,imm);
*/
			/*  Add this instruction to the code chunk:  */
			if (bintrans__codechunk_addinstr(&mem->bintrans_codechunk[oldest_i], &curlength, cpu, mem,
			    ((instrword >> 26) == 0x19)? BT_DADDIU : BT_ADDIU, rt, rs, 0, imm))
				do_translate = 1;
		}

		/*  addu:  */
		if ( ((instrword >> 26) == 0x00 && (instrword & 0x3f) == 0x21) ||
		     ((instrword >> 26) == 0x00 && (instrword & 0x3f) == 0x2d) ) {
			rs = (instrword >> 21) & 0x1f;
			rt = (instrword >> 16) & 0x1f;
			rd = (instrword >> 11) & 0x1f;

/*			if ((instrword & 0x3f) == 0x2d)
				debug("    daddu r%i,r%i,r%i\n", rd,rs,rt);
			else
				debug("    addu r%i,r%i,r%i\n", rd,rs,rt);
*/
			/*  Add this instruction to the code chunk:  */
			if (bintrans__codechunk_addinstr(&mem->bintrans_codechunk[oldest_i], &curlength, cpu, mem,
			    ((instrword & 0x3f) == 0x2d)? BT_DADDU : BT_ADDU, rt, rs, rd, 0))
				do_translate = 1;
		}

		/*  nop:  */
		if (instrword == 0) {
/*			debug("    nop\n");
*/
			/*  Add this instruction to the code chunk:  */
			if (bintrans__codechunk_addinstr(&mem->bintrans_codechunk[oldest_i], &curlength, cpu, mem,
			    BT_NOP, 0,0,0,0))
				do_translate = 1;
		}

		paddr += sizeof(instr);

		/*  Abort if we are crossing a physical page boundary:  */
		if ((paddr & 4095) == 0)
			do_translate = 0;
	}

	if (curlength == 0)
		return 0;

	/*  Add tail (exit instructions) to the codechunk.  */
	bintrans__codechunk_addtail(mem->bintrans_codechunk[oldest_i], &curlength);

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

	mem->bintrans_paddr_start[oldest_i] = paddr_start;
	mem->bintrans_paddr_end[oldest_i] = paddr - 1;

	return 1;
}


/*
 *  bintrans_try_to_run():
 *
 *  Try to run code at paddr from the translation cache, if circumstances
 *  are good.
 *
 *  Returns 1 on success (if some code was run), 0 if no such code was
 *  run or any other problem occured.
 */
int bintrans_try_to_run(struct cpu *cpu, struct memory *mem, uint64_t paddr)
{
	int res, chunk_nr;
	volatile void (*f)(struct cpu *);

	/*  debug("bintrans_try_to_run(): paddr=0x%016llx\n", (long long)paddr);  */

	if (cpu->delay_slot || cpu->nullify_next)
		return 0;

	/*  If any external interrupts will occur in the time it will take
		to run the codechunk, then we must abort now!  */
	/*  TODO  */

	res = bintrans_check_cache(mem, paddr, &chunk_nr);
	if (!res) {
		fatal("bintrans_try_to_run() called, but chunk not in cache?\n");
		exit(1);
	}

	/*  Run the chunk:  */
	f = mem->bintrans_codechunk[chunk_nr];
        mem->bintrans_tickcount ++;
	mem->bintrans_codechunk_time[chunk_nr] = mem->bintrans_tickcount;

	debug("DEBUG A: running codechunk %i @ %p (paddr 0x%08llx)\n", chunk_nr, (void *)f, (long long)paddr);
{
uint64_t regs[33];	/*  nr 32 is pc  */
int i;
for (i=0; i<32; i++)
	regs[i] = cpu->gpr[i];
regs[32] = cpu->pc;

	f(cpu);

for (i=0; i<32; i++)
	if (regs[i] != cpu->gpr[i])
		printf("REGISTER r%i changed from 0x%llx to 0x%llx\n",
		    i, (long long)regs[i], (long long)cpu->gpr[i]);
if (regs[32] != cpu->pc)
	printf("REGISTER pc  changed from 0x%llx to 0x%llx\n",
	    (long long)regs[i], (long long)cpu->pc);
}
	debug("DEBUG B: returning from codechunk %i\n", chunk_nr);

	return 1;
}

