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
 *  $Id: bintrans_alpha.c,v 1.2 2004-06-17 22:52:03 debug Exp $
 *
 *  Alpha specific code for binary translation.
 *
 *  See bintrans.c for more information.  This file is included from bintrans.c.
 */


/*
 *  bintrans__codechunk_flushpc():
 *
 *  Output machine code which flushes the PC.  The value of PC does not
 *  need to be updated after every instruction, only if an instruction
 *  may abort, and at the end of a code chunk.
 */
void bintrans__codechunk_flushpc(struct cpu *cpu, void *codechunk,
	size_t *curlengthp, int *n_instructions, int *n_instr_delta)
{
	size_t curlength;
	unsigned char *p;
	int ofs_pc, imm;
	int n = *n_instr_delta;

	curlength = *curlengthp;

	p = (unsigned char *)codechunk + curlength;

	/*  Add 4 * n to cpu->pc:  */
	if (n > 0) {
		ofs_pc = (size_t)&(cpu->pc) - (size_t)cpu;
		imm = n * 4;

		p[0] = ofs_pc & 255; p[1] = ofs_pc >> 8; p[2] = 0x30; p[3] = 0xa4;	curlength += 4; p += 4;
		p[0] = imm & 255; p[1] = (imm >> 8) & 255; p[2] = 0x21; p[3] = 0x20;    curlength += 4; p += 4;
		p[0] = ofs_pc & 255; p[1] = ofs_pc >> 8; p[2] = 0x30; p[3] = 0xb4;	curlength += 4; p += 4;

		/*
		 *  TODO:  Actually, PC should be sign extended here if we are running in 32-bit
		 *  mode, but it would be really uncommon in practice. (pc needs to wrap around
		 *  from 0x7ffffffc to 0x80000000 for that to happen.)
		 */
	}

	(*curlengthp) = curlength;
	/*  printf("n_instructions increased from %i to", *n_instructions);  */
	(*n_instructions) += *n_instr_delta;
	/*  printf("%i\n", *n_instructions);  */
	(*n_instr_delta) = 0;
}


/*
 *  bintrans__codechunk_addinstr():
 *
 *  Used internally by bintrans_try_to_add().
 *  This function tries to translate an instruction into native
 *  code.
 *
 *  NOTE: Any codechunk creation clause which needs to be able to abort an
 *  instruction should flush the PC before adding its code.  Also,
 *  curlength and p need to be recalculated, as flushing the PC can 
 *  change those values.  This is really ugly. TODO.
 *
 *  Returns 1 on success, 0 if no code was translated.
 */
int bintrans__codechunk_addinstr(void **codechunkp, size_t *curlengthp,
	struct cpu *cpu, struct memory *mem, int bt_instruction,
	int rt, int rs, int rd, int imm,
	int *n_instructions, int *n_instr_delta)
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
	 *  ANDI:
	 *
	 *  rt = rs & 0x1234:
	 *	  60:   34 12 5f 20     lda     t1,4660
	 *	  64:   28 03 30 a4     ldq     t0,808(a0)
	 *	  68:   01 00 22 44     and     t0,t1,t0
	 *	  70:   20 03 30 b4     stq     t0,800(a0)
	 *
	 *  rt = rs & 0xf234:
	 *	  80:   34 f2 3f 20     lda     t0,-3532
	 *	  88:   28 03 50 a4     ldq     t1,808(a0)
	 *	  8c:   01 00 21 24     ldah    t0,1(t0)
	 *	  90:   02 00 41 44     and     t1,t0,t1
	 *	  98:   20 03 50 b4     stq     t1,800(a0)
	 *
	 *  The above is for andi, these are for ori:
	 *	  68:   01 04 22 44     or      t0,t1,t0
	 *	  90:   02 04 41 44     or      t1,t0,t1
	 */
	if (bt_instruction == BT_ANDI || bt_instruction == BT_ORI) {
		ofs_rt = (size_t)&(cpu->gpr[rt]) - (size_t)cpu;
		ofs_rs = (size_t)&(cpu->gpr[rs]) - (size_t)cpu;
		/*  debug("offsets ofs_rt=%i ofs_rs=%i\n", ofs_rt, ofs_rs);  */

		if (rt != 0) {
			if (imm >= 0) {
				p[0] = imm & 255; p[1] = (imm >> 8) & 255; p[2] = 0x5f; p[3] = 0x20;  curlength += 4; p += 4;
				p[0] = ofs_rs & 255; p[1] = ofs_rs >> 8; p[2] = 0x30; p[3] = 0xa4;  curlength += 4; p += 4;
				if (bt_instruction == BT_ANDI) {
					p[0] = 0x01; p[1] = 0x00; p[2] = 0x22; p[3] = 0x44;  curlength += 4; p += 4;
				} else {
					p[0] = 0x01; p[1] = 0x04; p[2] = 0x22; p[3] = 0x44;  curlength += 4; p += 4;
				}
				p[0] = ofs_rt & 255; p[1] = ofs_rt >> 8; p[2] = 0x30; p[3] = 0xb4;  curlength += 4; p += 4;
			} else {
				imm += 0x10000;
				p[0] = imm & 255; p[1] = (imm >> 8) & 255; p[2] = 0x3f; p[3] = 0x20;  curlength += 4; p += 4;
				p[0] = ofs_rs & 255; p[1] = ofs_rs >> 8; p[2] = 0x50; p[3] = 0xa4;  curlength += 4; p += 4;
				p[0] = 0x01; p[1] = 0x00; p[2] = 0x21; p[3] = 0x24;  curlength += 4; p += 4;
				if (bt_instruction == BT_ANDI) {
					p[0] = 0x02; p[1] = 0x00; p[2] = 0x41; p[3] = 0x44;  curlength += 4; p += 4;
				} else {
					p[0] = 0x02; p[1] = 0x04; p[2] = 0x41; p[3] = 0x44;  curlength += 4; p += 4;
				}
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

		/*  Flush the PC, before this instruction:  */
		bintrans__codechunk_flushpc(cpu, *codechunkp, curlengthp,
		    n_instructions, n_instr_delta);
		curlength = *curlengthp;
		p = (unsigned char *)codechunk + curlength;

		if (bt_instruction == BT_JAL) {
			t0 = (cpu->pc + 8 + (*n_instructions) * 4);
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

		/*  Flush the PC, before this instruction:  */
		bintrans__codechunk_flushpc(cpu, *codechunkp, curlengthp,
		    n_instructions, n_instr_delta);
		curlength = *curlengthp;
		p = (unsigned char *)codechunk + curlength;

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

		t0 = cpu->pc + 4 + 4*(*n_instructions) + (imm << 2);
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
	 *  LW, SW:
	 *
	 *  Pseudo-code of what do to:    lw rX,ofs(rY)
	 *
	 *  int f(unsigned long *cpu, unsigned long x, unsigned long y) {
	 *	unsigned long t, t2;
	 *
	 *	t = cpu->gpr[rY];
	 *	t += ofs;
	 *	t2 = t & ~0xfff;
	 *	if (t2 != x)
	 *		return; (abort)
	 *	t &= 0xfff;
	 *	(TODO: check alignment, t&3)
	 *	t += y;
	 *	t2 = *((unsigned long *)t);
	 *	cpu->gpr[rX] = t2;
	 *  }
	 */
	if (bt_instruction == BT_LW || bt_instruction == BT_SW) {
		/*  Flush the PC, before this instruction:  */
		bintrans__codechunk_flushpc(cpu, *codechunkp, curlengthp,
		    n_instructions, n_instr_delta);
		curlength = *curlengthp;
		p = (unsigned char *)codechunk + curlength;

		ofs_rt = (size_t)&(cpu->gpr[rt]) - (size_t)cpu;
		ofs_rs = (size_t)&(cpu->gpr[rs]) - (size_t)cpu;

		/*   0:   00 f0 3f 20     lda     t0,-4096  */
		p[0] = 0x00;  p[1] = 0xf0;  p[2] = 0x3f; p[3] = 0x20;  curlength += 4; p += 4;

		/*   4:   18 09 50 a4     ldq     t1,2328(a0)  */
		p[0] = ofs_rs & 255;  p[1] = ofs_rs >> 8;  p[2] = 0x50; p[3] = 0xa4;  curlength += 4; p += 4;

		/*   8:   25 01 42 20     lda     t1,293(t1)  */
		p[0] = imm & 255; p[1] = (imm >> 8) & 255; p[2] = 0x42; p[3] = 0x20;  curlength += 4; p += 4;

		/*   c:   01 00 41 44     and     t1,t0,t0  */
		p[0] = 0x01;  p[1] = 0x00;  p[2] = 0x41; p[3] = 0x44;  curlength += 4; p += 4;

		/*  10:   a1 05 31 40     cmpeq   t0,a1,t0  */
		p[0] = 0xa1;  p[1] = 0x05;  p[2] = 0x31; p[3] = 0x40;  curlength += 4; p += 4;

		/*  14:   0a 00 20 f4     bne     t0,40 <f+0x40>  */
		/*  jump past the ret  */
		p[0] = 0x01; p[1] = 0x00; p[2] = 0x20; p[3] = 0xf4;  curlength += 4; p += 4;

		/*  44:   01 80 fa 6b     ret  */
		p[0] = 0x01; p[1] = 0x80; p[2] = 0xfa; p[3] = 0x6b;  curlength += 4; p += 4;

		/*  18:   ff 0f 3f 20     lda     t0,4095  */
		p[0] = 0xff;  p[1] = 0x0f;  p[2] = 0x3f; p[3] = 0x20;  curlength += 4; p += 4;

		/*  1c:   02 00 41 44     and     t1,t0,t1  */
		p[0] = 0x02;  p[1] = 0x00;  p[2] = 0x41; p[3] = 0x44;  curlength += 4; p += 4;

		/*  20:   02 04 52 40     addq    t1,a2,t1  */
		p[0] = 0x02;  p[1] = 0x04;  p[2] = 0x52; p[3] = 0x40;  curlength += 4; p += 4;

		if (bt_instruction == BT_LW) {
			/*  28:   00 00 22 a0     ldl     t0,0(t1)  */
			p[0] = 0x00;  p[1] = 0x00;  p[2] = 0x22; p[3] = 0xa0;  curlength += 4; p += 4;

			/*  30:   48 09 30 b4     stq     t0,2376(a0)  */
			p[0] = ofs_rt & 255;  p[1] = ofs_rt >> 8; p[2] = 0x30; p[3] = 0xb4;  curlength += 4; p += 4;
		} else {
			/*  1c:   48 09 50 89     lds     $f10,2376(a0)  */
			p[0] = ofs_rt & 255;  p[1] = ofs_rt >> 8; p[2] = 0x50; p[3] = 0x89;  curlength += 4; p += 4;

			/*  30:   00 00 42 99     sts     $f10,0(t1)  */
			p[0] = 0x00;  p[1] = 0x00;  p[2] = 0x42; p[3] = 0x99;  curlength += 4; p += 4;
		}

		success = 1;
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
void bintrans__codechunk_addtail(struct cpu *cpu, void *codechunk,
	size_t *curlengthp, int *n_instructions, int *n_instr_delta)
{
	size_t curlength;
	unsigned char *p;
	int ofs_pc, imm;

	bintrans__codechunk_flushpc(cpu, codechunk, curlengthp,
	    n_instructions, n_instr_delta);

	curlength = *curlengthp;

	p = (unsigned char *)codechunk + curlength;

	p[0] = 0x01;  p[1] = 0x80;  p[2] = 0xfa;  p[3] = 0x6b;  /*  ret     zero,(ra),0x1  */	p += 4; curlength += 4;

	(*curlengthp) = curlength;


	/*
	 *  Flush the instruction cache:
	 */
	asm("imb");
}

