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
 *  $Id: bintrans.c,v 1.9 2004-05-06 03:51:54 debug Exp $
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

	/*  debug("bintrans_check_cache(): paddr=0x%016llx\n", (long long)paddr);  */

	for (i=0; i<BINTRANS_CACHEENTRIES; i++) {
		if (paddr == mem->bintrans_paddr_start[i] &&
		    mem->bintrans_codechunk_len[i] != 0) {
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

#define	CODECHUNK_SIZE		256
#define	CODECHUNK_SIZE_MARGIN	64

/*
 *  bintrans__codechunk_addinstr():
 *
 *  Used internally by bintrans_try_to_add().
 *  This function tries to translate an instruction into native
 *  code.
 *  Returns 1 on success, 0 if no code was translated.
 */
int bintrans__codechunk_addinstr(void **codechunkp, size_t *curlengthp, struct cpu *cpu, struct memory *mem,
	int bt_instruction, int rt, int rs, int rd, int imm, int n_instructions)
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
		if (codechunk == NULL) {
			fprintf(stderr, "out of memory in bintrans__codechunk_addinstr()\n");
			exit(1);
		}
		curlength = 0;
	}

	if (curlength >= CODECHUNK_SIZE - CODECHUNK_SIZE_MARGIN)
		return 0;

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
	 *   4:   9a 78 21 20     lda     t0,30874(t0)		<-- inc by 0x789a
	 *   8:   34 12 30 b4     stq     t0,4660(a0)		<-- store
	 *
	 *  NOTE: the code above is for DADDIU. The following two instructions
	 *  sign-extend as should be done with ADDIU:
	 *
	 *   c:   f0 7f 50 a0     ldl     t1,32752(a0)		<-- load 32-bit signed
	 *  10:   f0 7f 50 b4     stq     t1,32752(a0)		<-- store as 64-bit again
	 */
	if (bt_instruction == BT_ADDIU || bt_instruction == BT_DADDIU) {
		ofs_rt = (size_t)&(cpu->gpr[rt]) - (size_t)cpu;
		ofs_rs = (size_t)&(cpu->gpr[rs]) - (size_t)cpu;
		/*  debug("offsets ofs_rt=%i ofs_rs=%i\n", ofs_rt, ofs_rs);  */

		if (imm < 0)
			imm += 0x10000;

		if ((imm != 0 || rt != rs) && rt != 0) {
			p[0] = ofs_rs & 255; p[1] = ofs_rs >> 8; p[2] = 0x30; p[3] = 0xa4;  curlength += 4; p += 4;
			if (imm != 0) {
				p[0] = imm & 255; p[1] = (imm >> 8) & 255; p[2] = 0x21; p[3] = 0x20;  curlength += 4; p += 4;
			}
			p[0] = ofs_rt & 255; p[1] = ofs_rt >> 8; p[2] = 0x30; p[3] = 0xb4;  curlength += 4; p += 4;

			/*  Sign-extend, for 32-bit addiu:  */
			if (bt_instruction == BT_ADDIU) {
				p[0] = ofs_rt & 255; p[1] = ofs_rt >> 8; p[2] = 0x50; p[3] = 0xa0;  curlength += 4; p += 4;
				p[0] = ofs_rt & 255; p[1] = ofs_rt >> 8; p[2] = 0x50; p[3] = 0xb4;  curlength += 4; p += 4;
			}
		}

		success = 1;
	}

	/*
	 *  SLL rd,rt,sa: (and DSLL, SRL, SRA, ..)
	 *
	 *  0c 10 30 a4     ldq     t0,4108(a0)
	 *  21 f7 23 48     sll     t0,0x1f,t0
	 *  04 10 30 b4     stq     t0,4100(a0)
	 *
	 *  21 17 20 48     sll     t0,0,t0
	 *  81 16 20 48     srl     t0,0,t0
	 *  81 17 20 48     sra     t0,0,t0
	 *
	 *  Sign-extend like in addiu.
	 */
	if (bt_instruction == BT_SLL || bt_instruction == BT_DSLL ||
	    bt_instruction == BT_SRL || bt_instruction == BT_DSRL) {
/* ||
	    bt_instruction == BT_SRA) {  */
		int op0, op1, op2;
		int signextend = 1;

		ofs_rt = (size_t)&(cpu->gpr[rt]) - (size_t)cpu;
		ofs_rd = (size_t)&(cpu->gpr[rd]) - (size_t)cpu;
		/*  debug("offsets ofs_rt=%i ofs_rd=%i\n", ofs_rt, ofs_rd);  */

		switch (bt_instruction) {
		case BT_DSLL:
			signextend = 0;
		case BT_SLL:
			op0 = 0x21; op1 = 0x17; op2 = 0x20;
			break;
		case BT_DSRL:
			signextend = 0;
		case BT_SRL:
			op0 = 0x81; op1 = 0x16; op2 = 0x20;
			break;
		case BT_SRA:
			op0 = 0x81; op1 = 0x17; op2 = 0x20;
			break;
		}

		rs &= 0x1f;	/*  rs = shift amount  */

		if (rd != 0) {
			p[0] = ofs_rt & 255; p[1] = ofs_rt >> 8; p[2] = 0x30; p[3] = 0xa4;  curlength += 4; p += 4;
			if (rs != 0) {
				p[0] = op0; p[1] = op1 + ((rs & 0x7) << 5); p[2] = op2 + ((rs >> 3) & 0x3); p[3] = 0x48;  curlength += 4; p += 4;
			}
			p[0] = ofs_rd & 255; p[1] = ofs_rd >> 8; p[2] = 0x30; p[3] = 0xb4;  curlength += 4; p += 4;

			/*  Sign-extend, for 32-bit addiu:  */
			if (signextend) {
				p[0] = ofs_rd & 255; p[1] = ofs_rd >> 8; p[2] = 0x50; p[3] = 0xa0;  curlength += 4; p += 4;
				p[0] = ofs_rd & 255; p[1] = ofs_rd >> 8; p[2] = 0x50; p[3] = 0xb4;  curlength += 4; p += 4;
			}
		}

		success = 1;
	}

	/*
	 *  AND, OR, XOR, ...:
	 *
	 *   0:   08 10 30 a4     ldq     t0,4104(a0)		<-- load
	 *   4:   0c 10 50 a4     ldq     t1,4108(a0)		<-- load
	 *   8:   01 04 22 44     or      t0,t1,t0		<-- or
	 *   c:   1f 04 ff 5f     fnop
	 *  10:   04 10 30 b4     stq     t0,4100(a0)		<-- store
	 *
	 *  XOR, AND, SLT, SLTU respectively:
	 *   8:   01 08 22 44     xor     t0,t1,t0
	 *   8:   01 00 22 44     and     t0,t1,t0
	 *   8:   a1 09 22 40     cmplt   t0,t1,t0
	 *   8:   a1 03 22 40     cmpult  t0,t1,t0
	 */
	if (bt_instruction == BT_OR || bt_instruction == BT_AND || bt_instruction == BT_XOR ||
	    bt_instruction == BT_SLT || bt_instruction == BT_SLTU ||
	    bt_instruction == BT_SUBU || bt_instruction == BT_DSUBU ||
	    bt_instruction == BT_ADDU || bt_instruction == BT_DADDU) {
		ofs_rt = (size_t)&(cpu->gpr[rt]) - (size_t)cpu;
		ofs_rs = (size_t)&(cpu->gpr[rs]) - (size_t)cpu;
		ofs_rd = (size_t)&(cpu->gpr[rd]) - (size_t)cpu;
		/*  debug("offsets ofs_rt=%i ofs_rs=%i, ofs_rd=%i\n", ofs_rt, ofs_rs, ofs_rd);  */

		if (rd != 0) {
			p[0] = ofs_rs & 255; p[1] = ofs_rs >> 8; p[2] = 0x30; p[3] = 0xa4;  curlength += 4; p += 4;
			p[0] = ofs_rt & 255; p[1] = ofs_rt >> 8; p[2] = 0x50; p[3] = 0xa4;  curlength += 4; p += 4;

			switch (bt_instruction) {
			case BT_ADDU:
			case BT_DADDU:	p[0] = 0x01; p[1] = 0x04; p[2] = 0x22; p[3] = 0x40;  curlength += 4; p += 4; break;
			case BT_SUBU:
			case BT_DSUBU:	p[0] = 0x21; p[1] = 0x05; p[2] = 0x22; p[3] = 0x40;  curlength += 4; p += 4; break;
			case BT_OR:	p[0] = 0x01; p[1] = 0x04; p[2] = 0x22; p[3] = 0x44;  curlength += 4; p += 4; break;
			case BT_XOR:	p[0] = 0x01; p[1] = 0x08; p[2] = 0x22; p[3] = 0x44;  curlength += 4; p += 4; break;
			case BT_AND:	p[0] = 0x01; p[1] = 0x00; p[2] = 0x22; p[3] = 0x44;  curlength += 4; p += 4; break;
			case BT_SLT:	p[0] = 0xa1; p[1] = 0x09; p[2] = 0x22; p[3] = 0x40;  curlength += 4; p += 4; break;
			case BT_SLTU:	p[0] = 0xa1; p[1] = 0x03; p[2] = 0x22; p[3] = 0x40;  curlength += 4; p += 4; break;
			}

			p[0] = 0x1f;         p[1] = 0x04;        p[2] = 0xff; p[3] = 0x5f;  curlength += 4; p += 4;
			p[0] = ofs_rd & 255; p[1] = ofs_rd >> 8; p[2] = 0x30; p[3] = 0xb4;  curlength += 4; p += 4;

			/*  Sign-extend rd, for 32-bit addu or subu:  */
			if (bt_instruction == BT_ADDU || bt_instruction == BT_SUBU) {
				p[0] = ofs_rd & 255; p[1] = ofs_rd >> 8; p[2] = 0x50; p[3] = 0xa0;  curlength += 4; p += 4;
				p[0] = ofs_rd & 255; p[1] = ofs_rd >> 8; p[2] = 0x50; p[3] = 0xb4;  curlength += 4; p += 4;
			}
		}

		success = 1;
	}

	/*
	 *  LUI:
	 *
	 *	0:   67 45 3f 24     ldah    t0,17767(zero)	<-- lui
	 *	4:   30 12 30 b4     stq     t0,4656(a0)	<-- store
	 *
	 *  and then a sign extend
	 */
	if (bt_instruction == BT_LUI) {
		ofs_rt = (size_t)&(cpu->gpr[rt]) - (size_t)cpu;
		/*  debug("offsets ofs_rt=%i\n", ofs_rt);  */

		if (rt != 0) {
			p[0] = imm & 255;    p[1] = imm >> 8;    p[2] = 0x3f; p[3] = 0x24;  curlength += 4; p += 4;
			p[0] = ofs_rt & 255; p[1] = ofs_rt >> 8; p[2] = 0x30; p[3] = 0xb4;  curlength += 4; p += 4;

			/*  Sign extend:  */
			p[0] = ofs_rt & 255; p[1] = ofs_rt >> 8; p[2] = 0x50; p[3] = 0xa0;  curlength += 4; p += 4;
			p[0] = ofs_rt & 255; p[1] = ofs_rt >> 8; p[2] = 0x50; p[3] = 0xb4;  curlength += 4; p += 4;
		}

		success = 1;
	}

	/*
	 *  JAL and J:
	 *
	 *  The following needs to be done:
	 *	if (JAL)
	 *	    cpu->grp[31] = cpu->pc + 8 + n_instructions * 4;	the instruction after the branch delay instr.
	 *	addr = cpu->pc & ~((1 << 28) - 1);
	 *	addr |= (imm << 2);
	 *	cpu->delay_jmpaddr = addr;
	 *	cpu->delay_slot = TO_BE_DELAYED;
	 *
	 *  In pseudo-asm, it will be something like:
	 *
	 *	if (JAL) {
	 *		set t0 = (cpu->pc + 8 + n_instructions * 4)
	 *		store t0 into cpu->grp[31]
	 *	}
	 *	set t0 = (cpu->pc & ~((1<<28) - 1)) | (imm << 2)
	 *	store t0 into cpu->delay_jmpaddr;
	 *	set t0 = TO_BE_DELAYED
	 *	store t0 into cpu->delay_slot;
	 *
	 *  Setting t0 to 0x12345678, and storing into cpu->0x9abc is done like this:
	 *
	 *	78 56 3f 20     lda     t0,22136(zero)
	 *	34 12 21 24     ldah    t0,4660(t0)
	 *	bc 9a 30 b4     stq     t0,0x9abc(a0)
	 *
	 *  TODO:  Sign-extend in 32-bit mode?
	 */
	if (bt_instruction == BT_JAL || bt_instruction == BT_J) {
		uint32_t t0;
		int ofs_t0;

		if (bt_instruction == BT_JAL) {
			t0 = (cpu->pc + 8 + n_instructions * 4);
			/*  printf("t0 jal = %08x\n", t0);  */
			if (t0 & 0x8000)		/*  alpha hi/lo  */
				t0 += 0x10000;
			ofs_t0 = (size_t)&(cpu->gpr[31]) - (size_t)cpu;
			p[0] = t0 & 255;         p[1] = (t0 >> 8) & 255;   p[2] = 0x3f; p[3] = 0x20;  curlength += 4; p += 4;
			p[0] = (t0 >> 16) & 255; p[1] = (t0 >> 24) & 255;  p[2] = 0x21; p[3] = 0x24;  curlength += 4; p += 4;
			p[0] = ofs_t0 & 255;     p[1] = ofs_t0 >> 8;       p[2] = 0x30; p[3] = 0xb4;  curlength += 4; p += 4;
		}

		t0 = (cpu->pc & ~((1<<28)-1)) | (imm << 2);
		/*  printf("t0 jmp = %08x\n", t0);  */
		if (t0 & 0x8000)		/*  alpha hi/lo  */
			t0 += 0x10000;
		ofs_t0 = (size_t)&(cpu->delay_jmpaddr) - (size_t)cpu;
		p[0] = t0 & 255;         p[1] = (t0 >> 8) & 255;   p[2] = 0x3f; p[3] = 0x20;  curlength += 4; p += 4;
		p[0] = (t0 >> 16) & 255; p[1] = (t0 >> 24) & 255;  p[2] = 0x21; p[3] = 0x24;  curlength += 4; p += 4;
		p[0] = ofs_t0 & 255;     p[1] = ofs_t0 >> 8;       p[2] = 0x30; p[3] = 0xb4;  curlength += 4; p += 4;

		t0 = TO_BE_DELAYED;
		ofs_t0 = (size_t)&(cpu->delay_slot) - (size_t)cpu;
		p[0] = t0 & 255;      p[1] = (t0 >> 8) & 255;   p[2] = 0x3f; p[3] = 0x20;  curlength += 4; p += 4;
		p[0] = ofs_t0 & 255;  p[1] = ofs_t0 >> 8;       p[2] = 0x30; p[3] = 0xb0;  curlength += 4; p += 4;

		/*  note, 0x30 0xb0 for stl, 0x30 0xb4 for stq  */

		success = 1;
	}

	/*
	 *  BNE rs,rt,offset: (and BEQ)
	 *
	 *  Similar to J/JAL in that delay_slot and delay_jmpaddr are set, but
	 *  only if a condition is met.
	 *
	 *   0:   04 10 30 a4     ldq     t0,4100(a0)
	 *   4:   08 10 50 a4     ldq     t1,4104(a0)
	 *   8:   a1 05 22 40     cmpeq   t0,t1,t0
	 *   c:   03 00 20 e4     beq     t0,1c <f+0x1c>
	 *  10:   78 f6 3f 20     lda     t0,-2440(zero)
	 *  14:   35 12 21 24     ldah    t0,4661(t0)
	 *  18:   0c 10 30 b4     stq     t0,4108(a0)
	 *
	 *   c:   04 00 20 f4     bne     t0,20 <f+0x20>
	 *
	 *  In pseudo-asm:
	 *
	 *	compute condition
	 *	if (condition) {
	 *		set t0 = (cpu->pc + 4*n_instructions + (imm << 2))
	 *		store t0 into cpu->delay_jmpaddr;
	 *		set t0 = TO_BE_DELAYED
	 *		store t0 into cpu->delay_slot;
	 *	}
	 */
	if (bt_instruction == BT_BNE || bt_instruction == BT_BEQ) {
		uint32_t t0;
		int ofs_t0, ofs_rs, ofs_rt;

		ofs_rs = (size_t)&(cpu->gpr[rs]) - (size_t)cpu;
		ofs_rt = (size_t)&(cpu->gpr[rt]) - (size_t)cpu;

		/*  load t0=rs, t1=rt  */
		p[0] = ofs_rs & 255;     p[1] = ofs_rs >> 8;       p[2] = 0x30; p[3] = 0xa4;  curlength += 4; p += 4;
		p[0] = ofs_rt & 255;     p[1] = ofs_rt >> 8;       p[2] = 0x50; p[3] = 0xa4;  curlength += 4; p += 4;

		/*  a1 05 22 40     cmpeq   t0,t1,t0  */
		p[0] = 0xa1; p[1] = 0x05; p[2] = 0x22; p[3] = 0x40;  curlength += 4; p += 4;

		/*  p[0] = nr of instructions to skip  */
		if (bt_instruction == BT_BNE) {
			/*  04 00 20 f4     bne     t0,20 <f+0x20>  */
			p[0] = 0x05; p[1] = 0x00; p[2] = 0x20; p[3] = 0xf4;  curlength += 4; p += 4;
		} else {
			/*  04 00 20 e4     beq     t0,20 <f+0x20>  */
			p[0] = 0x05; p[1] = 0x00; p[2] = 0x20; p[3] = 0xe4;  curlength += 4; p += 4;
		}

		t0 = cpu->pc + 4 + 4*n_instructions + (imm << 2);
		/*  printf("t0 bne = %08x\n", t0);  */
		if (t0 & 0x8000)		/*  alpha hi/lo  */
			t0 += 0x10000;
		ofs_t0 = (size_t)&(cpu->delay_jmpaddr) - (size_t)cpu;
		p[0] = t0 & 255;         p[1] = (t0 >> 8) & 255;   p[2] = 0x3f; p[3] = 0x20;  curlength += 4; p += 4;
		p[0] = (t0 >> 16) & 255; p[1] = (t0 >> 24) & 255;  p[2] = 0x21; p[3] = 0x24;  curlength += 4; p += 4;
		p[0] = ofs_t0 & 255;     p[1] = ofs_t0 >> 8;       p[2] = 0x30; p[3] = 0xb4;  curlength += 4; p += 4;

		t0 = TO_BE_DELAYED;
		ofs_t0 = (size_t)&(cpu->delay_slot) - (size_t)cpu;
		p[0] = t0 & 255;      p[1] = (t0 >> 8) & 255;   p[2] = 0x3f; p[3] = 0x20;  curlength += 4; p += 4;
		p[0] = ofs_t0 & 255;  p[1] = ofs_t0 >> 8;       p[2] = 0x30; p[3] = 0xb0;  curlength += 4; p += 4;

		/*  note, 0x30 0xb0 for stl, 0x30 0xbf for stq  */

		success = 1;
	}

	/*
	 *  LW:
	 *
	 *  TODO:  Perhaps cpu->mem->...->datablock in a1 ?
	 */
	if (bt_instruction == BT_LW) {

		success = 0;
	}

	/*
	 *  SW:
	 *
	 *  TODO:  Perhaps cpu->mem->...->datablock in a1 ?
	 */
	if (bt_instruction == BT_SW) {

		success = 0;
	}

	/*
	 *  NOP:
	 *
	 *  Add 4 to cpu->pc.
	 */
	if (bt_instruction == BT_NOP) {
		success = 1;
	}


	*codechunkp = codechunk;
	*curlengthp = curlength;

	return success;
}


/*
 *  bintrans__codechunk_addtail():
 */
void bintrans__codechunk_addtail(struct cpu *cpu, void *codechunk, size_t *curlengthp, int n_instructions)
{
	size_t curlength;
	unsigned char *p;
	int ofs_pc, imm;

	curlength = *curlengthp;

	p = (unsigned char *)codechunk + curlength;

	/*  Add 4 * n_instructions to cpu->pc:  */
	if (n_instructions > 0) {
		ofs_pc = (size_t)&(cpu->pc) - (size_t)cpu;
		imm = n_instructions * 4;

		p[0] = ofs_pc & 255; p[1] = ofs_pc >> 8; p[2] = 0x30; p[3] = 0xa4;	curlength += 4; p += 4;
/*		p[0] = 0x01; p[1] = 0x14 + ((imm & 7) << 5); p[2] = 0x20 + ((imm & 0xf8) >> 3); p[3] = 0x40;  curlength += 4; p += 4;  */
		p[0] = imm & 255; p[1] = (imm >> 8) & 255; p[2] = 0x21; p[3] = 0x20;    curlength += 4; p += 4;
		p[0] = ofs_pc & 255; p[1] = ofs_pc >> 8; p[2] = 0x30; p[3] = 0xb4;	curlength += 4; p += 4;

		/*
		 *  TODO:  Actually, PC should be sign extended here if we are running in 32-bit
		 *  mode, but it would be really uncommon in practice. (pc needs to wrap around
		 *  from 0x7ffffffc to 0x80000000 for that to happen.)
		 */
	}

	p[0] = 0x01;  p[1] = 0x80;  p[2] = 0xfa;  p[3] = 0x6b;  /*  ret     zero,(ra),0x1  */	p += 4; curlength += 4;

#if 0
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
#endif

	(*curlengthp) = curlength;

	/*  On alpha, flush the instruction cache:  */
#ifdef ALPHA
	/*  Haha, actually this whole file should be ifdef Alpha  */
	asm("imb");
#endif
}


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
	int n_instructions = 0;
	size_t curlength = 0;
	uint64_t paddr_start = paddr;
	unsigned char *hostaddr;

	static int blah_counter = 0;

/*	if ((++blah_counter) & 7)
		return 0;
*/
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
			    ((instrword >> 26) == HI6_DADDIU)? BT_DADDIU : BT_ADDIU, rt, rs, 0, imm, n_instructions);
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
			    b, rt, rs, rd, 0, n_instructions);
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

			do_translate = bintrans__codechunk_addinstr(&mem->bintrans_codechunk[oldest_i], &curlength, cpu, mem, b, rt, rs, rd, 0, n_instructions);
		}
		else
		/*  lui:  */
		if ((instrword >> 26) == HI6_LUI) {
			rt = (instrword >> 16) & 0x1f;
			imm = instrword & 0xffff;
			do_translate = bintrans__codechunk_addinstr(&mem->bintrans_codechunk[oldest_i], &curlength, cpu, mem, BT_LUI, rt, 0, 0, imm, n_instructions);
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
			do_translate = bintrans__codechunk_addinstr(&mem->bintrans_codechunk[oldest_i], &curlength, cpu, mem, b, 0, 0, 0, imm, n_instructions);

			/*  XXX  */
			if (do_translate)
				n_instructions ++;
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

			do_translate = bintrans__codechunk_addinstr(&mem->bintrans_codechunk[oldest_i], &curlength, cpu, mem, b, rt, rs, 0, imm, n_instructions);

			/*  XXX  */
			if (do_translate)
				n_instructions ++;
			do_translate = 0;
		}
		else
		/*  lw:  */
		if ((instrword >> 26) == HI6_LW) {
			rt = (instrword >> 16) & 0x1f;
			rs = (instrword >> 21) & 0x1f;		/*  use rs as base!  */
			imm = instrword & 0xffff;		/*  offset  */
			if (imm >= 32768)
				imm -= 65536;

			do_translate = bintrans__codechunk_addinstr(&mem->bintrans_codechunk[oldest_i], &curlength, cpu, mem, BT_LW, rt, rs, 0, imm, n_instructions);
		}
		else
		/*  sw:  */
		if ((instrword >> 26) == HI6_SW) {
			rt = (instrword >> 16) & 0x1f;
			rs = (instrword >> 21) & 0x1f;		/*  use rs as base!  */
			imm = instrword & 0xffff;		/*  offset  */
			if (imm >= 32768)
				imm -= 65536;

			do_translate = bintrans__codechunk_addinstr(&mem->bintrans_codechunk[oldest_i], &curlength, cpu, mem, BT_SW, rt, rs, 0, imm, n_instructions);
		}
		else
		/*  nop:  */
		if (instrword == 0) {
			do_translate = bintrans__codechunk_addinstr(&mem->bintrans_codechunk[oldest_i], &curlength, cpu, mem, BT_NOP, 0,0,0,0, n_instructions);
		}

		paddr += sizeof(instrword);
		hostaddr += sizeof(instrword);

		if (do_translate)
			n_instructions ++;

		/*  Abort if we are crossing a physical MIPS page boundary:  */
		if ((paddr & 0xfff) == 0)
			do_translate = 0;
	}

#if 1
	/*  If too few instructions were translated, then forget it:  */
	if (curlength <= 4)
		curlength = 0;
#endif

	/*  In case of length zero, we have to destroy this entry:  */
	mem->bintrans_codechunk_len[oldest_i] = curlength;

	if (curlength == 0)
		return 0;

	/*  Add tail (exit instructions) to the codechunk.  */
	bintrans__codechunk_addtail(cpu, mem->bintrans_codechunk[oldest_i], &curlength, n_instructions);

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
 *  Returns 1 on success (if some code was run), 0 if no such code was
 *  run or any other problem occured.
 */
int bintrans_try_to_run(struct cpu *cpu, struct memory *mem, uint64_t paddr, int chunk_nr)
{
	void (*f)(struct cpu *);

	/*  debug("bintrans_try_to_run(): paddr=0x%016llx\n", (long long)paddr);  */

/*	if (cpu->delay_slot || cpu->nullify_next)
		return 0;
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

	f(cpu);

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

	return 1;
}

