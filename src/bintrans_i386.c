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
 *  $Id: bintrans_i386.c,v 1.21 2004-11-23 20:23:04 debug Exp $
 *
 *  i386 specific code for dynamic binary translation.
 *
 *  See bintrans.c for more information.  Included from bintrans.c.
 */


struct cpu dummy_cpu;
struct coproc dummy_coproc;


/*
 *  bintrans_host_cacheinvalidate()
 *
 *  Invalidate the host's instruction cache. On i386, this isn't neccessary,
 *  so this is an empty function.
 */
void bintrans_host_cacheinvalidate(unsigned char *p, size_t len)
{
	/*  Do nothing.  */
}


unsigned char bintrans_i386_runchunk[13] = {
	0x55,					/*  push   %ebp  */
	0x89, 0xe5,				/*  mov    %esp,%ebp  */
	0x60,					/*  pusha  */

	/*  In all translated code, esi points to the cpu struct.  */

	0x8b, 0x75, 0x08,			/*  mov    0x8(%ebp),%esi  */
	0xff, 0x55, 0x0c,			/*  call   *0xc(%ebp)  */

	0x61,					/*  popa  */
	0xc9,					/*  leave  */
	0xc3					/*  ret  */
};


/*
 *  bintrans_runchunk():
 */
static void bintrans_runchunk(struct cpu *cpu, unsigned char *code)
{
	void (*f)(struct cpu *, unsigned char *);
	f = (void *)&bintrans_i386_runchunk[0];
	f(cpu, code);
}


/*
 *  bintrans_write_quickjump():
 */
static void bintrans_write_quickjump(unsigned char *quickjump_code,
	uint32_t chunkoffset)
{
	uint32_t i386_addr;
	unsigned char *a = quickjump_code;

	i386_addr = chunkoffset + (size_t)translation_code_chunk_space;
	i386_addr = i386_addr - ((size_t)a + 5);

        /*  printf("chunkoffset=%i, %08x %08x %i\n",
            chunkoffset, i386_addr, a, ofs);  */  

	*a++ = 0xe9;
	*a++ = i386_addr;
	*a++ = i386_addr >> 8;
	*a++ = i386_addr >> 16;
	*a++ = i386_addr >> 24;
}


/*
 *  bintrans_write_chunkreturn():
 */
static void bintrans_write_chunkreturn(unsigned char **addrp)
{
	unsigned char *a = *addrp;
	*a++ = 0xc3;		/*  ret  */
	*addrp = a;
}


/*
 *  bintrans_write_chunkreturn_fail():
 */
static void bintrans_write_chunkreturn_fail(unsigned char **addrp)
{
	unsigned char *a = *addrp;
	int ofs;
	ofs = ((size_t)&dummy_cpu.bintrans_instructions_executed) - (size_t)&dummy_cpu;

	/*  81 8e 45 23 01 00 00 00 00 01    orl    $0x1000000,0x12345(%esi)  */
	*a++ = 0x81; *a++ = 0x8e;
	*a++ = ofs & 255;
	*a++ = (ofs >> 8) & 255;
	*a++ = (ofs >> 16) & 255;
	*a++ = (ofs >> 24) & 255;
	*a++ = 0; *a++ = 0; *a++ = 0; *a++ = 0x01;

	*a++ = 0xc3;		/*  ret  */
	*addrp = a;
}


/*
 *  bintrans_write_pc_inc():
 */
static void bintrans_write_pc_inc(unsigned char **addrp, int pc_inc,
	int flag_pc, int flag_ninstr)
{
	unsigned char *a = *addrp;
	int ofs;

	if (pc_inc == 0)
		return;

	if (flag_pc) {
		ofs = ((size_t)&dummy_cpu.pc) - (size_t)&dummy_cpu;

		if (pc_inc < 0x7c) {
			/*  83 86 xx xx xx xx yy    addl   $yy,xx(%esi)  */
			*a++ = 0x83; *a++ = 0x86;
			*a++ = ofs & 255;
			*a++ = (ofs >> 8) & 255;
			*a++ = (ofs >> 16) & 255;
			*a++ = (ofs >> 24) & 255;
			*a++ = pc_inc;
		} else {
			/*  81 86 xx xx xx xx yy yy   addl   $yy,xx(%esi)  */
			*a++ = 0x81; *a++ = 0x86;
			*a++ = ofs & 255;
			*a++ = (ofs >> 8) & 255;
			*a++ = (ofs >> 16) & 255;
			*a++ = (ofs >> 24) & 255;
			*a++ = pc_inc & 255;
			*a++ = (pc_inc >> 8) & 255;
		}

		/*  83 96 zz zz zz zz 00    adcl   $0x0,zz(%esi)  */
		ofs += 4;
		*a++ = 0x83; *a++ = 0x96;
		*a++ = ofs & 255;
		*a++ = (ofs >> 8) & 255;
		*a++ = (ofs >> 16) & 255;
		*a++ = (ofs >> 24) & 255;
		*a++ = 0;
	}

	if (flag_ninstr) {
		pc_inc /= 4;

		ofs = ((size_t)&dummy_cpu.bintrans_instructions_executed) - (size_t)&dummy_cpu;

		if (pc_inc < 0x7c) {
			/*  83 86 xx xx xx xx yy    addl   $yy,xx(%esi)  */
			*a++ = 0x83; *a++ = 0x86;
			*a++ = ofs & 255;
			*a++ = (ofs >> 8) & 255;
			*a++ = (ofs >> 16) & 255;
			*a++ = (ofs >> 24) & 255;
			*a++ = pc_inc;
		} else {
			/*  81 86 xx xx xx xx yy yy   addl   $yy,xx(%esi)  */
			*a++ = 0x81; *a++ = 0x86;
			*a++ = ofs & 255;
			*a++ = (ofs >> 8) & 255;
			*a++ = (ofs >> 16) & 255;
			*a++ = (ofs >> 24) & 255;
			*a++ = pc_inc & 255;
			*a++ = (pc_inc >> 8) & 255;
		}
	}

	*addrp = a;
}


/*
 *  load_into_eax_edx():
 *
 *  Usage:    load_into_eax_edx(&a, &dummy_cpu.gpr[rs]);   etc.
 */
void load_into_eax_edx(unsigned char **addrp, void *p)
{
	unsigned char *a;
	int ofs = (size_t)p - (size_t)&dummy_cpu;
	a = *addrp;

	/*  8b 86 38 30 00 00       mov    0x3038(%esi),%eax  */
	*a++ = 0x8b; *a++ = 0x86;
	*a++ = ofs; *a++ = ofs >> 8; *a++ = ofs >> 16; *a++ = ofs >> 24;

	/*  8b 96 3c 30 00 00       mov    0x303c(%esi),%edx  */
	ofs += 4;
	*a++ = 0x8b; *a++ = 0x96;
	*a++ = ofs; *a++ = ofs >> 8; *a++ = ofs >> 16; *a++ = ofs >> 24;

	*addrp = a;
}


/*
 *  load_into_eax_and_sign_extend_into_edx():
 *
 *  Usage:    load_into_eax_and_sign_extend_into_edx(&a, &dummy_cpu.gpr[rs]);   etc.
 */
void load_into_eax_and_sign_extend_into_edx(unsigned char **addrp, void *p)
{
	unsigned char *a;
	int ofs = (size_t)p - (size_t)&dummy_cpu;
	a = *addrp;

	/*  8b 86 38 30 00 00       mov    0x3038(%esi),%eax  */
	*a++ = 0x8b; *a++ = 0x86;
	*a++ = ofs; *a++ = ofs >> 8; *a++ = ofs >> 16; *a++ = ofs >> 24;

	/*  99                      cltd   */
	*a++ = 0x99;

	*addrp = a;
}


/*
 *  store_eax_edx():
 *
 *  Usage:    store_eax_edx(&a, &dummy_cpu.gpr[rs]);   etc.
 */
void store_eax_edx(unsigned char **addrp, void *p)
{
	unsigned char *a;
	int ofs = (size_t)p - (size_t)&dummy_cpu;
	a = *addrp;

	/*  89 86 38 30 00 00       mov    %eax,0x3038(%esi)  */
	*a++ = 0x89; *a++ = 0x86;
	*a++ = ofs; *a++ = ofs >> 8; *a++ = ofs >> 16; *a++ = ofs >> 24;

	/*  89 96 3c 30 00 00       mov    %edx,0x303c(%esi)  */
	ofs += 4;
	*a++ = 0x89; *a++ = 0x96;
	*a++ = ofs; *a++ = ofs >> 8; *a++ = ofs >> 16; *a++ = ofs >> 24;

	*addrp = a;
}


/*
 *  bintrans_write_instruction__lui():
 */
static int bintrans_write_instruction__lui(unsigned char **addrp, int rt, int imm)
{
	unsigned char *a;

	a = *addrp;
	if (rt == 0)
		goto rt0;

	/*  b8 00 00 dc fe          mov    $0xfedc0000,%eax  */
	*a++ = 0xb8; *a++ = 0; *a++ = 0;
	*a++ = imm & 255; *a++ = imm >> 8;

	/*  99                      cltd   */
	*a++ = 0x99;

	store_eax_edx(&a, &dummy_cpu.gpr[rt]);
	*addrp = a;

rt0:
	bintrans_write_pc_inc(addrp, sizeof(uint32_t), 1, 1);
	return 1;
}


/*
 *  bintrans_write_instruction__jr():
 */
static int bintrans_write_instruction__jr(unsigned char **addrp, int rs, int rd, int special)
{
	unsigned char *a;
	int ofs;

	a = *addrp;

	/*
	 *  Perform the jump by setting cpu->delay_slot = TO_BE_DELAYED
	 *  and cpu->delay_jmpaddr = gpr[rs].
	 */

	/*  c7 86 38 30 00 00 01 00 00 00    movl   $0x1,0x3038(%esi)  */
	ofs = ((size_t)&dummy_cpu.delay_slot) - (size_t)&dummy_cpu;
	*a++ = 0xc7; *a++ = 0x86;
	*a++ = ofs; *a++ = ofs >> 8; *a++ = ofs >> 16; *a++ = ofs >> 24;
	*a++ = TO_BE_DELAYED; *a++ = 0; *a++ = 0; *a++ = 0;

	load_into_eax_edx(&a, &dummy_cpu.gpr[rs]);
	store_eax_edx(&a, &dummy_cpu.delay_jmpaddr);

	if (special == SPECIAL_JALR && rd != 0) {
		/*  gpr[rd] = retaddr    (pc + 8)  */

		load_into_eax_edx(&a, &dummy_cpu.pc);

		/*  83 c0 08                add    $0x8,%eax  */
		/*  83 d2 00                adc    $0x0,%edx  */
		*a++ = 0x83; *a++ = 0xc0; *a++ = 0x08;
		*a++ = 0x83; *a++ = 0xd2; *a++ = 0x00;

		store_eax_edx(&a, &dummy_cpu.gpr[rd]);
	}

	*addrp = a;
	bintrans_write_pc_inc(addrp, sizeof(uint32_t), 1, 1);
	return 1;
}


/*
 *  bintrans_write_instruction__mfmthilo():
 */
static int bintrans_write_instruction__mfmthilo(unsigned char **addrp,
	int rd, int from_flag, int hi_flag)
{
	unsigned char *a;

	a = *addrp;

	if (from_flag) {
		if (rd != 0) {
			/*  mfhi or mflo  */
			if (hi_flag)
				load_into_eax_edx(&a, &dummy_cpu.hi);
			else
				load_into_eax_edx(&a, &dummy_cpu.lo);
			store_eax_edx(&a, &dummy_cpu.gpr[rd]);
		}
	} else {
		/*  mthi or mtlo  */
		load_into_eax_edx(&a, &dummy_cpu.gpr[rd]);
		if (hi_flag)
			store_eax_edx(&a, &dummy_cpu.hi);
		else
			store_eax_edx(&a, &dummy_cpu.lo);
	}

	*addrp = a;
	bintrans_write_pc_inc(addrp, sizeof(uint32_t), 1, 1);
	return 1;
}


/*
 *  bintrans_write_instruction__addiu_etc():
 */
static int bintrans_write_instruction__addiu_etc(unsigned char **addrp,
	int rt, int rs, int imm, int instruction_type)
{
	unsigned char *a;
	unsigned int uimm;
	int load64 = 0, sign3264 = 1;

	switch (instruction_type) {
	case HI6_DADDIU:
	case HI6_ORI:
	case HI6_XORI:
	case HI6_SLTI:
	case HI6_SLTIU:
		load64 = 1;
	}

	switch (instruction_type) {
	case HI6_ANDI:
	case HI6_ORI:
	case HI6_XORI:
	case HI6_DADDIU:
		sign3264 = 0;
	}

	a = *addrp;

	if (rt == 0)
		goto rt0;

	uimm = imm & 0xffff;

	if (uimm == 0 && (instruction_type == HI6_ADDIU ||
	    instruction_type == HI6_DADDIU || instruction_type == HI6_ORI)) {
		load_into_eax_edx(&a, &dummy_cpu.gpr[rs]);
		store_eax_edx(&a, &dummy_cpu.gpr[rt]);
		goto rt0;
	}

	if (load64) {
		load_into_eax_edx(&a, &dummy_cpu.gpr[rs]);
	} else {
		load_into_eax_and_sign_extend_into_edx(&a, &dummy_cpu.gpr[rs]);
	}

	switch (instruction_type) {
	case HI6_ADDIU:
	case HI6_DADDIU:
		if (imm & 0x8000) {
			/*  05 39 fd ff ff          add    $0xfffffd39,%eax  */
			/*  83 d2 ff                adc    $0xffffffff,%edx  */
			*a++ = 0x05; *a++ = uimm; *a++ = uimm >> 8; *a++ = 0xff; *a++ = 0xff;
			*a++ = 0x83; *a++ = 0xd2; *a++ = 0xff;
		} else {
			/*  05 c7 02 00 00          add    $0x2c7,%eax  */
			/*  83 d2 00                adc    $0x0,%edx  */
			*a++ = 0x05; *a++ = uimm; *a++ = uimm >> 8; *a++ = 0; *a++ = 0;
			*a++ = 0x83; *a++ = 0xd2; *a++ = 0;
		}
		break;
	case HI6_ANDI:
		/*  25 34 12 00 00          and    $0x1234,%eax  */
		/*  ba 00 00 00 00          mov    $0x0,%edx  */
		*a++ = 0x25; *a++ = uimm; *a++ = uimm >> 8; *a++ = 0; *a++ = 0;
		*a++ = 0xba; *a++ = 0; *a++ = 0; *a++ = 0; *a++ = 0;
		break;
	case HI6_ORI:
		/*  0d 34 12 00 00          or     $0x1234,%eax  */
		*a++ = 0xd; *a++ = uimm; *a++ = uimm >> 8; *a++ = 0; *a++ = 0;
		break;
	case HI6_XORI:
		/*  35 34 12 00 00          xor    $0x1234,%eax  */
		*a++ = 0x35; *a++ = uimm; *a++ = uimm >> 8; *a++ = 0; *a++ = 0;
		break;
	case HI6_SLTIU:
		/*  set if less than, unsigned. (compare edx:eax to ecx:ebx)  */
		/*  ecx:ebx = the immediate value  */
		/*  bb dc fe ff ff          mov    $0xfffffedc,%ebx  */
		/*  b9 ff ff ff ff          mov    $0xffffffff,%ecx  */
		/*  or  */
		/*  29 c9                   sub    %ecx,%ecx  */
		*a++ = 0xbb; *a++ = uimm; *a++ = uimm >> 8;
		if (uimm & 0x8000) {
			*a++ = 0xff; *a++ = 0xff;
			*a++ = 0xb9; *a++ = 0xff; *a++ = 0xff; *a++ = 0xff; *a++ = 0xff;
		} else {
			*a++ = 0; *a++ = 0;
			*a++ = 0x29; *a++ = 0xc9;
		}

		/*  if edx <= ecx and eax < ebx then 1, else 0.  */
		/*  39 ca                   cmp    %ecx,%edx  */
		/*  77 0b                   ja     <ret0>  */
		/*  39 d8                   cmp    %ebx,%eax  */
		/*  73 07                   jae    58 <ret0>  */
		*a++ = 0x39; *a++ = 0xca;
		*a++ = 0x77; *a++ = 0x0b;
		*a++ = 0x39; *a++ = 0xd8;
		*a++ = 0x73; *a++ = 0x07;

		/*  b8 01 00 00 00          mov    $0x1,%eax  */
		/*  eb 02                   jmp    <common>  */
		*a++ = 0xb8; *a++ = 1; *a++ = 0; *a++ = 0; *a++ = 0;
		*a++ = 0xeb; *a++ = 0x02;

		/*  ret0:  */
		/*  29 c0                   sub    %eax,%eax  */
		*a++ = 0x29; *a++ = 0xc0;

		/*  common:  */
		/*  99                      cltd   */
		*a++ = 0x99;
		break;
	case HI6_SLTI:
		/*  set if less than, signed. (compare edx:eax to ecx:ebx)  */
		/*  ecx:ebx = the immediate value  */
		/*  bb dc fe ff ff          mov    $0xfffffedc,%ebx  */
		/*  b9 ff ff ff ff          mov    $0xffffffff,%ecx  */
		/*  or  */
		/*  29 c9                   sub    %ecx,%ecx  */
		*a++ = 0xbb; *a++ = uimm; *a++ = uimm >> 8;
		if (uimm & 0x8000) {
			*a++ = 0xff; *a++ = 0xff;
			*a++ = 0xb9; *a++ = 0xff; *a++ = 0xff; *a++ = 0xff; *a++ = 0xff;
		} else {
			*a++ = 0; *a++ = 0;
			*a++ = 0x29; *a++ = 0xc9;
		}

		/*  if edx > ecx then 0.  */
		/*  if edx < ecx then 1.  */
		/*  if eax < ebx then 1, else 0.  */
		/*  39 ca                   cmp    %ecx,%edx  */
		/*  7c 0a                   jl     <ret1>  */
		/*  7f 04                   jg     <ret0>  */
		/*  39 d8                   cmp    %ebx,%eax  */
		/*  7c 04                   jl     <ret1>  */
		*a++ = 0x39; *a++ = 0xca;
		*a++ = 0x7c; *a++ = 0x0a;
		*a++ = 0x7f; *a++ = 0x04;
		*a++ = 0x39; *a++ = 0xd8;
		*a++ = 0x7c; *a++ = 0x04;

		/*  ret0:  */
		/*  29 c0                   sub    %eax,%eax  */
		/*  eb 05                   jmp    <common>  */
		*a++ = 0x29; *a++ = 0xc0;
		*a++ = 0xeb; *a++ = 0x05;

		/*  ret1:  */
		/*  b8 01 00 00 00          mov    $0x1,%eax  */
		*a++ = 0xb8; *a++ = 1; *a++ = 0; *a++ = 0; *a++ = 0;

		/*  common:  */
		/*  99                      cltd   */
		*a++ = 0x99;
		break;
	}

	if (sign3264) {
		/*  99                      cltd   */
		*a++ = 0x99;
	}

	store_eax_edx(&a, &dummy_cpu.gpr[rt]);

rt0:
	*addrp = a;
	bintrans_write_pc_inc(addrp, sizeof(uint32_t), 1, 1);
	return 1;
}


/*
 *  bintrans_write_instruction__jal():
 */
static int bintrans_write_instruction__jal(unsigned char **addrp, int imm, int link)
{
	unsigned char *a;
	uint32_t subimm;
	int ofs;

	a = *addrp;

	if (link) {
		/*  gpr[31] = pc + 8  */
		load_into_eax_edx(&a, &dummy_cpu.pc);

		/*  83 c0 08                add    $0x8,%eax  */
		/*  83 d2 00                adc    $0x0,%edx  */
		*a++ = 0x83; *a++ = 0xc0; *a++ = 0x08;
		*a++ = 0x83; *a++ = 0xd2; *a++ = 0x00;

		store_eax_edx(&a, &dummy_cpu.gpr[31]);
	}

	/*  delay_jmpaddr = top 36 bits of pc together with lowest 28 bits of imm*4:  */
	imm *= 4;
	load_into_eax_edx(&a, &dummy_cpu.pc);

	/*  Add 4, because the jump is from the delay slot:  */
	/*  83 c0 04                add    $0x4,%eax  */
	/*  83 d2 00                adc    $0x0,%edx  */
	*a++ = 0x83; *a++ = 0xc0; *a++ = 0x04;
	*a++ = 0x83; *a++ = 0xd2; *a++ = 0x00;

	/*  c1 e8 1c                shr    $0x1c,%eax  */
	/*  c1 e0 1c                shl    $0x1c,%eax  */
	*a++ = 0xc1; *a++ = 0xe8; *a++ = 0x1c;
	*a++ = 0xc1; *a++ = 0xe0; *a++ = 0x1c;

	subimm = imm;
	subimm &= 0x0fffffff;

	/*  0d 78 56 34 12          or     $0x12345678,%eax  */
	*a++ = 0x0d; *a++ = subimm; *a++ = subimm >> 8;
	*a++ = subimm >> 16; *a++ = subimm >> 24;

	store_eax_edx(&a, &dummy_cpu.delay_jmpaddr);

	/*  c7 86 38 30 00 00 01 00 00 00    movl   $0x1,0x3038(%esi)  */
	ofs = ((size_t)&dummy_cpu.delay_slot) - (size_t)&dummy_cpu;
	*a++ = 0xc7; *a++ = 0x86;
	*a++ = ofs; *a++ = ofs >> 8; *a++ = ofs >> 16; *a++ = ofs >> 24;
	*a++ = TO_BE_DELAYED; *a++ = 0; *a++ = 0; *a++ = 0;

	*addrp = a;
	bintrans_write_pc_inc(addrp, sizeof(uint32_t), 1, 1);
	return 1;
}


/*
 *  bintrans_write_instruction__addu_etc():
 */
static int bintrans_write_instruction__addu_etc(unsigned char **addrp,
	int rd, int rs, int rt, int sa, int instruction_type)
{
	unsigned char *a;
	int load64 = 0;

	/*  TODO: Not yet  */
	switch (instruction_type) {
	case SPECIAL_DSLL:
	case SPECIAL_DSLL32:
	case SPECIAL_DSRA:
	case SPECIAL_DSRA32:
	case SPECIAL_DSRL:
	case SPECIAL_DSRL32:
	case SPECIAL_MULT:
	case SPECIAL_MULTU:
		bintrans_write_chunkreturn_fail(addrp);
		return 0;
	}


	switch (instruction_type) {
	case SPECIAL_DADDU:
	case SPECIAL_DSUBU:
	case SPECIAL_OR:
	case SPECIAL_AND:
	case SPECIAL_NOR:
	case SPECIAL_XOR:
	case SPECIAL_DSLL:
	case SPECIAL_DSRL:
	case SPECIAL_DSRA:
	case SPECIAL_DSLL32:
	case SPECIAL_DSRL32:
	case SPECIAL_DSRA32:
	case SPECIAL_SLT:
	case SPECIAL_SLTU:
		load64 = 1;
	}

	if (rd == 0)
		goto rd0;

	a = *addrp;

	if ((instruction_type == SPECIAL_ADDU || instruction_type == SPECIAL_DADDU
	    || instruction_type == SPECIAL_OR) && rt == 0) {
		load_into_eax_edx(&a, &dummy_cpu.gpr[rs]);
		store_eax_edx(&a, &dummy_cpu.gpr[rd]);
		*addrp = a;
		goto rd0;
	}

	/*  edx:eax = rs, ecx:ebx = rt  */
	if (load64) {
		load_into_eax_edx(&a, &dummy_cpu.gpr[rt]);
		/*  89 c3                   mov    %eax,%ebx  */
		/*  89 d1                   mov    %edx,%ecx  */
		*a++ = 0x89; *a++ = 0xc3; *a++ = 0x89; *a++ = 0xd1;
		load_into_eax_edx(&a, &dummy_cpu.gpr[rs]);
	} else {
		load_into_eax_and_sign_extend_into_edx(&a, &dummy_cpu.gpr[rt]);
		/*  89 c3                   mov    %eax,%ebx  */
		/*  89 d1                   mov    %edx,%ecx  */
		*a++ = 0x89; *a++ = 0xc3; *a++ = 0x89; *a++ = 0xd1;
		load_into_eax_and_sign_extend_into_edx(&a, &dummy_cpu.gpr[rs]);
	}

	switch (instruction_type) {
	case SPECIAL_ADDU:
		/*  01 d8                   add    %ebx,%eax  */
		/*  99                      cltd   */
		*a++ = 0x01; *a++ = 0xd8;
		*a++ = 0x99;
		break;
	case SPECIAL_DADDU:
		/*  01 d8                   add    %ebx,%eax  */
		/*  11 ca                   adc    %ecx,%edx  */
		*a++ = 0x01; *a++ = 0xd8;
		*a++ = 0x11; *a++ = 0xca;
		break;
	case SPECIAL_SUBU:
		/*  29 d8                   sub    %ebx,%eax  */
		/*  99                      cltd   */
		*a++ = 0x29; *a++ = 0xd8;
		*a++ = 0x99;
		break;
	case SPECIAL_DSUBU:
		/*  29 d8                   sub    %ebx,%eax  */
		/*  19 ca                   sbb    %ecx,%edx  */
		*a++ = 0x29; *a++ = 0xd8;
		*a++ = 0x19; *a++ = 0xca;
		break;
	case SPECIAL_AND:
		/*  21 d8                   and    %ebx,%eax  */
		/*  21 ca                   and    %ecx,%edx  */
		*a++ = 0x21; *a++ = 0xd8;
		*a++ = 0x21; *a++ = 0xca;
		break;
	case SPECIAL_OR:
		/*  09 d8                   or     %ebx,%eax  */
		/*  09 ca                   or     %ecx,%edx  */
		*a++ = 0x09; *a++ = 0xd8;
		*a++ = 0x09; *a++ = 0xca;
		break;
	case SPECIAL_NOR:
		/*  09 d8                   or     %ebx,%eax  */
		/*  09 ca                   or     %ecx,%edx  */
		/*  f7 d0                   not    %eax  */
		/*  f7 d2                   not    %edx  */
		*a++ = 0x09; *a++ = 0xd8;
		*a++ = 0x09; *a++ = 0xca;
		*a++ = 0xf7; *a++ = 0xd0;
		*a++ = 0xf7; *a++ = 0xd2;
		break;
	case SPECIAL_XOR:
		/*  31 d8                   xor    %ebx,%eax  */
		/*  31 ca                   xor    %ecx,%edx  */
		*a++ = 0x31; *a++ = 0xd8;
		*a++ = 0x31; *a++ = 0xca;
		break;
	case SPECIAL_SLL:
		/*  89 d8                   mov    %ebx,%eax  */
		/*  c1 e0 1f                shl    $0x1f,%eax  */
		/*  99                      cltd   */
		*a++ = 0x89; *a++ = 0xd8;
		if (sa == 1) {
			*a++ = 0xd1; *a++ = 0xe0;
		} else {
			*a++ = 0xc1; *a++ = 0xe0; *a++ = sa;
		}
		*a++ = 0x99;
		break;
	case SPECIAL_SRA:
		/*  89 d8                   mov    %ebx,%eax  */
		/*  c1 f8 1f                sar    $0x1f,%eax  */
		/*  99                      cltd   */
		*a++ = 0x89; *a++ = 0xd8;
		if (sa == 1) {
			*a++ = 0xd1; *a++ = 0xf8;
		} else {
			*a++ = 0xc1; *a++ = 0xf8; *a++ = sa;
		}
		*a++ = 0x99;
		break;
	case SPECIAL_SRL:
		/*  89 d8                   mov    %ebx,%eax  */
		/*  c1 e8 1f                shr    $0x1f,%eax  */
		/*  99                      cltd   */
		*a++ = 0x89; *a++ = 0xd8;
		if (sa == 1) {
			*a++ = 0xd1; *a++ = 0xe8;
		} else {
			*a++ = 0xc1; *a++ = 0xe8; *a++ = sa;
		}
		*a++ = 0x99;
		break;
	case SPECIAL_SLTU:
		/*  set if less than, unsigned. (compare edx:eax to ecx:ebx)  */
		/*  if edx <= ecx and eax < ebx then 1, else 0.  */
		/*  39 ca                   cmp    %ecx,%edx  */
		/*  77 0b                   ja     <ret0>  */
		/*  39 d8                   cmp    %ebx,%eax  */
		/*  73 07                   jae    58 <ret0>  */
		*a++ = 0x39; *a++ = 0xca;
		*a++ = 0x77; *a++ = 0x0b;
		*a++ = 0x39; *a++ = 0xd8;
		*a++ = 0x73; *a++ = 0x07;

		/*  b8 01 00 00 00          mov    $0x1,%eax  */
		/*  eb 02                   jmp    <common>  */
		*a++ = 0xb8; *a++ = 1; *a++ = 0; *a++ = 0; *a++ = 0;
		*a++ = 0xeb; *a++ = 0x02;

		/*  ret0:  */
		/*  29 c0                   sub    %eax,%eax  */
		*a++ = 0x29; *a++ = 0xc0;

		/*  common:  */
		/*  99                      cltd   */
		*a++ = 0x99;
		break;
	case SPECIAL_SLT:
		/*  set if less than, signed. (compare edx:eax to ecx:ebx)  */
		/*  if edx > ecx then 0.  */
		/*  if edx < ecx then 1.  */
		/*  if eax < ebx then 1, else 0.  */
		/*  39 ca                   cmp    %ecx,%edx  */
		/*  7c 0a                   jl     <ret1>  */
		/*  7f 04                   jg     <ret0>  */
		/*  39 d8                   cmp    %ebx,%eax  */
		/*  7c 04                   jl     <ret1>  */
		*a++ = 0x39; *a++ = 0xca;
		*a++ = 0x7c; *a++ = 0x0a;
		*a++ = 0x7f; *a++ = 0x04;
		*a++ = 0x39; *a++ = 0xd8;
		*a++ = 0x7c; *a++ = 0x04;

		/*  ret0:  */
		/*  29 c0                   sub    %eax,%eax  */
		/*  eb 05                   jmp    <common>  */
		*a++ = 0x29; *a++ = 0xc0;
		*a++ = 0xeb; *a++ = 0x05;

		/*  ret1:  */
		/*  b8 01 00 00 00          mov    $0x1,%eax  */
		*a++ = 0xb8; *a++ = 1; *a++ = 0; *a++ = 0; *a++ = 0;

		/*  common:  */
		/*  99                      cltd   */
		*a++ = 0x99;
		break;
	case SPECIAL_SLLV:
		/*  rd = rt << (rs&31)  (logical)     eax = ebx << (eax&31)  */
		/*  xchg ebx,eax, then we can do   eax = eax << (ebx&31)  */
		/*  93                      xchg   %eax,%ebx  */
		/*  89 d9                   mov    %ebx,%ecx  */
		/*  83 e1 1f                and    $0x1f,%ecx  */
		/*  d3 e0                   shl    %cl,%eax  */
		*a++ = 0x93;
		*a++ = 0x89; *a++ = 0xd9;
		*a++ = 0x83; *a++ = 0xe1; *a++ = 0x1f;
		*a++ = 0xd3; *a++ = 0xe0;
		/*  99                      cltd   */
		*a++ = 0x99;
		break;
	case SPECIAL_SRLV:
		/*  rd = rt >> (rs&31)  (logical)     eax = ebx >> (eax&31)  */
		/*  xchg ebx,eax, then we can do   eax = eax >> (ebx&31)  */
		/*  93                      xchg   %eax,%ebx  */
		/*  89 d9                   mov    %ebx,%ecx  */
		/*  83 e1 1f                and    $0x1f,%ecx  */
		/*  d3 e8                   shr    %cl,%eax  */
		*a++ = 0x93;
		*a++ = 0x89; *a++ = 0xd9;
		*a++ = 0x83; *a++ = 0xe1; *a++ = 0x1f;
		*a++ = 0xd3; *a++ = 0xe8;
		/*  99                      cltd   */
		*a++ = 0x99;
		break;
	case SPECIAL_SRAV:
		/*  rd = rt >> (rs&31)  (arithmetic)     eax = ebx >> (eax&31)  */
		/*  xchg ebx,eax, then we can do   eax = eax >> (ebx&31)  */
		/*  93                      xchg   %eax,%ebx  */
		/*  89 d9                   mov    %ebx,%ecx  */
		/*  83 e1 1f                and    $0x1f,%ecx  */
		/*  d3 f8                   sar    %cl,%eax  */
		*a++ = 0x93;
		*a++ = 0x89; *a++ = 0xd9;
		*a++ = 0x83; *a++ = 0xe1; *a++ = 0x1f;
		*a++ = 0xd3; *a++ = 0xf8;
		/*  99                      cltd   */
		*a++ = 0x99;
		break;

#if 0
	/*  TODO:  These are from bintrans_alpha.c. Translate them to i386.  */

	case SPECIAL_DSLL:
		*a++ = 0x21; *a++ = 0x17 + ((sa & 7) << 5); *a++ = 0x40 + (sa >> 3); *a++ = 0x48;	/*  sll t1,sa,t0  */
		break;
	case SPECIAL_DSLL32:
		sa += 32;
		*a++ = 0x21; *a++ = 0x17 + ((sa & 7) << 5); *a++ = 0x40 + (sa >> 3); *a++ = 0x48;	/*  sll t1,sa,t0  */
		break;
	case SPECIAL_DSRA:
		*a++ = 0x81; *a++ = 0x17 + ((sa & 7) << 5); *a++ = 0x40 + (sa >> 3); *a++ = 0x48;	/*  sra t1,sa,t0  */
		break;
	case SPECIAL_DSRA32:
		sa += 32;
		*a++ = 0x81; *a++ = 0x17 + ((sa & 7) << 5); *a++ = 0x40 + (sa >> 3); *a++ = 0x48;	/*  sra t1,sa,t0  */
		break;
	case SPECIAL_DSRL:
		/*  Note: bits of sa are distributed among two different bytes.  */
		*a++ = 0x81; *a++ = 0x16 + ((sa & 7) << 5); *a++ = 0x40 + (sa >> 3); *a++ = 0x48;
		break;
	case SPECIAL_DSRL32:
		/*  Note: bits of sa are distributed among two different bytes.  */
		sa += 32;
		*a++ = 0x81; *a++ = 0x16 + ((sa & 7) << 5); *a++ = 0x40 + (sa >> 3); *a++ = 0x48;
		break;
#endif
	}

	store_eax_edx(&a, &dummy_cpu.gpr[rd]);

	*addrp = a;
rd0:
	bintrans_write_pc_inc(addrp, sizeof(uint32_t), 1, 1);
	return 1;
}


/*
 *  bintrans_write_instruction__rfe():
 */
static int bintrans_write_instruction__rfe(unsigned char **addrp)
{
	unsigned char *a;
	int ofs;

	/*
	 *  cpu->coproc[0]->reg[COP0_STATUS] =
	 *	(cpu->coproc[0]->reg[COP0_STATUS] & ~0x3f) |
	 *      ((cpu->coproc[0]->reg[COP0_STATUS] & 0x3c) >> 2);
	 */

	a = *addrp;

	ofs = ((size_t)&dummy_cpu.coproc[0]) - (size_t)&dummy_cpu;

	/*  8b 96 3c 30 00 00       mov    0x303c(%esi),%edx  */
	*a++ = 0x8b; *a++ = 0x96;
	*a++ = ofs; *a++ = ofs >> 8; *a++ = ofs >> 16; *a++ = ofs >> 24;

	/*  here, edx = cpu->coproc[0]  */

	ofs = ((size_t)&dummy_coproc.reg[COP0_STATUS]) - (size_t)&dummy_coproc;

	/*  8b 82 38 30 00 00       mov    0x3038(%edx),%eax  */
	*a++ = 0x8b; *a++ = 0x82;
	*a++ = ofs; *a++ = ofs >> 8; *a++ = ofs >> 16; *a++ = ofs >> 24;

	/*  here, eax = cpu->coproc[0]->reg[COP0_STATUS]  */

	/*  89 c3                   mov    %eax,%ebx  */
	/*  83 e3 3f                and    $0x3f,%ebx  */
	/*  31 d8                   xor    %ebx,%eax  (clear lowest 6 bits of eax)  */
	/*  c1 eb 02                shr    $0x2,%ebx  */
	/*  09 d8                   or     %ebx,%eax  */
	*a++ = 0x89; *a++ = 0xc3;
	*a++ = 0x83; *a++ = 0xe3; *a++ = 0x3f;
	*a++ = 0x31; *a++ = 0xd8;
	*a++ = 0xc1; *a++ = 0xeb; *a++ = 0x02;
	*a++ = 0x09; *a++ = 0xd8;

	/*  89 82 38 30 00 00       mov    %eax,0x3038(%edx)  */
	*a++ = 0x89; *a++ = 0x82;
	*a++ = ofs; *a++ = ofs >> 8; *a++ = ofs >> 16; *a++ = ofs >> 24;

	/*  NOTE: High 32 bits of the coprocessor register is left unmodified.  */

	*addrp = a;

	bintrans_write_pc_inc(addrp, sizeof(uint32_t), 1, 1);
	return 1;
}


/*
 *  bintrans_write_instruction__mfc_mtc():
 */
static int bintrans_write_instruction__mfc_mtc(unsigned char **addrp, int coproc_nr, int flag64bit, int rt, int rd, int mtcflag)
{
	unsigned char *a;
	int ofs;

	if (mtcflag) {
		/*  mtc: */
		/*  TODO:  see bintrans_alpha.c  */
		bintrans_write_chunkreturn_fail(addrp);
		return 0;
	}

	/*
	 *  NOTE: Only a few registers are readable without side effects.
	 */
	if (rt == 0)
		return 0;

	if (coproc_nr >= 1)
		return 0;

	if (rd == COP0_RANDOM || rd == COP0_COUNT)
		return 0;

	a = *addrp;

	/*************************************************************
	 *
	 *  TODO: Check for kernel mode, or Coproc X usability bit!
	 *
	 *************************************************************/

	ofs = ((size_t)&dummy_cpu.coproc[0]) - (size_t)&dummy_cpu;

	/*  8b 96 3c 30 00 00       mov    0x303c(%esi),%edx  */
	*a++ = 0x8b; *a++ = 0x96;
	*a++ = ofs; *a++ = ofs >> 8; *a++ = ofs >> 16; *a++ = ofs >> 24;

	/*  here, edx = cpu->coproc[0]  */

	ofs = ((size_t)&dummy_coproc.reg[rd]) - (size_t)&dummy_coproc;

	/*  8b 82 38 30 00 00       mov    0x3038(%edx),%eax  */
	*a++ = 0x8b; *a++ = 0x82;
	*a++ = ofs; *a++ = ofs >> 8; *a++ = ofs >> 16; *a++ = ofs >> 24;

	if (flag64bit) {
		/*  Load high 32 bits:  (note: edx gets overwritten)  */
		/*  8b 92 3c 30 00 00       mov    0x303c(%edx),%edx  */
		ofs += 4;
		*a++ = 0x8b; *a++ = 0x92;
		*a++ = ofs; *a++ = ofs >> 8; *a++ = ofs >> 16; *a++ = ofs >> 24;
	} else {
		/*  99                      cltd  */
		*a++ = 0x99;
	}

	store_eax_edx(&a, &dummy_cpu.gpr[rt]);

	*addrp = a;
	bintrans_write_pc_inc(addrp, sizeof(uint32_t), 1, 1);
	return 1;
}


/*
 *  bintrans_write_instruction__branch():
 */
static int bintrans_write_instruction__branch(unsigned char **addrp,
	int instruction_type, int regimm_type, int rt, int rs, int imm)
{
	unsigned char *a;
	unsigned char *skip1 = NULL, *skip2 = NULL;
	int ofs;

	a = *addrp;

	/*
	 *  edx:eax = gpr[rs]; ecx:ebx = gpr[rt];
	 *
	 *  Compare for equality (BEQ).
	 *  If the result was zero, then it means equality; perform the
	 *  delayed jump. Otherwise: skip.
	 */

	switch (instruction_type) {
	case HI6_BEQ:
	case HI6_BNE:
		load_into_eax_edx(&a, &dummy_cpu.gpr[rt]);
		/*  89 c3                   mov    %eax,%ebx  */
		/*  89 d1                   mov    %edx,%ecx  */
		*a++ = 0x89; *a++ = 0xc3; *a++ = 0x89; *a++ = 0xd1;
	}
	load_into_eax_edx(&a, &dummy_cpu.gpr[rs]);

	if (instruction_type == HI6_BEQ && rt != rs) {
		/*  If rt != rs, then skip.  */
		/*  39 c3                   cmp    %eax,%ebx  */
		/*  75 05                   jne    155 <skip>  */
		/*  39 d1                   cmp    %edx,%ecx  */
		/*  75 01                   jne    155 <skip>  */
		*a++ = 0x39; *a++ = 0xc3;
		*a++ = 0x75; skip1 = a; *a++ = 0x00;
		*a++ = 0x39; *a++ = 0xd1;
		*a++ = 0x75; skip2 = a; *a++ = 0x00;
	}

	if (instruction_type == HI6_BNE) {
		/*  If rt != rs, then ok. Otherwise skip.  */
		/*  39 c3                   cmp    %eax,%ebx  */
		/*  75 06                   jne    156 <bra>  */
		/*  39 d1                   cmp    %edx,%ecx  */
		/*  75 02                   jne    156 <bra>  */
		/*  eb 01                   jmp    157 <skip>  */
		*a++ = 0x39; *a++ = 0xc3;
		*a++ = 0x75; *a++ = 0x06;
		*a++ = 0x39; *a++ = 0xd1;
		*a++ = 0x75; *a++ = 0x02;
		*a++ = 0xeb; skip2 = a; *a++ = 0x00;
	}

	if (instruction_type == HI6_BLEZ) {
		/*  If both eax and edx are zero, then do the branch.  */
		/*  83 f8 00                cmp    $0x0,%eax  */
		/*  75 07                   jne    <nott>  */
		/*  83 fa 00                cmp    $0x0,%edx  */
		/*  75 02                   jne    23d <nott>  */
		/*  eb 01                   jmp    <branch>  */
		*a++ = 0x83; *a++ = 0xf8; *a++ = 0x00;
		*a++ = 0x75; *a++ = 0x07;
		*a++ = 0x83; *a++ = 0xfa; *a++ = 0x00;
		*a++ = 0x75; *a++ = 0x02;
		*a++ = 0xeb; skip1 = a; *a++ = 0x00;

		/*  If high bit of edx is set, then rs < 0.  */
		/*  f7 c2 00 00 00 80       test   $0x80000000,%edx  */
		/*  74 00                   jz     skip  */
		*a++ = 0xf7; *a++ = 0xc2; *a++ = 0; *a++ = 0; *a++ = 0; *a++ = 0x80;
		*a++ = 0x74; skip2 = a; *a++ = 0x00;

		if (skip1 != NULL)
			*skip1 = (size_t)a - (size_t)skip1 - 1;
		skip1 = NULL;
	}
	if (instruction_type == HI6_BGTZ) {
		/*  If both eax and edx are zero, then skip the branch.  */
		/*  83 f8 00                cmp    $0x0,%eax  */
		/*  75 07                   jne    <nott>  */
		/*  83 fa 00                cmp    $0x0,%edx  */
		/*  75 02                   jne    23d <nott>  */
		/*  eb 01                   jmp    <skip>  */
		*a++ = 0x83; *a++ = 0xf8; *a++ = 0x00;
		*a++ = 0x75; *a++ = 0x07;
		*a++ = 0x83; *a++ = 0xfa; *a++ = 0x00;
		*a++ = 0x75; *a++ = 0x02;
		*a++ = 0xeb; skip1 = a; *a++ = 0x00;

		/*  If high bit of edx is set, then rs < 0.  */
		/*  f7 c2 00 00 00 80       test   $0x80000000,%edx  */
		/*  75 00                   jnz    skip  */
		*a++ = 0xf7; *a++ = 0xc2; *a++ = 0; *a++ = 0; *a++ = 0; *a++ = 0x80;
		*a++ = 0x75; skip2 = a; *a++ = 0x00;
	}
	if (instruction_type == HI6_REGIMM && regimm_type == REGIMM_BLTZ) {
		/*  If high bit of edx is set, then rs < 0.  */
		/*  f7 c2 00 00 00 80       test   $0x80000000,%edx  */
		/*  74 00                   jz     skip  */
		*a++ = 0xf7; *a++ = 0xc2; *a++ = 0; *a++ = 0; *a++ = 0; *a++ = 0x80;
		*a++ = 0x74; skip2 = a; *a++ = 0x00;
	}
	if (instruction_type == HI6_REGIMM && regimm_type == REGIMM_BGEZ) {
		/*  If high bit of edx is not set, then rs >= 0.  */
		/*  f7 c2 00 00 00 80       test   $0x80000000,%edx  */
		/*  75 00                   jnz    skip  */
		*a++ = 0xf7; *a++ = 0xc2; *a++ = 0; *a++ = 0; *a++ = 0; *a++ = 0x80;
		*a++ = 0x75; skip2 = a; *a++ = 0x00;
	}

	/*
	 *  Perform the jump by setting cpu->delay_slot = TO_BE_DELAYED
	 *  and cpu->delay_jmpaddr = pc + 4 + (imm << 2).
	 */

	/*  c7 86 38 30 00 00 01 00 00 00    movl   $0x1,0x3038(%esi)  */
	ofs = ((size_t)&dummy_cpu.delay_slot) - (size_t)&dummy_cpu;
	*a++ = 0xc7; *a++ = 0x86;
	*a++ = ofs; *a++ = ofs >> 8; *a++ = ofs >> 16; *a++ = ofs >> 24;
	*a++ = TO_BE_DELAYED; *a++ = 0; *a++ = 0; *a++ = 0;

	load_into_eax_edx(&a, &dummy_cpu.pc);

	/*  05 78 56 34 12          add    $0x12345678,%eax  */
	/*  83 d2 00                adc    $0x0,%edx  */
	/*  or  */
	/*  83 d2 ff                adc    $0xffffffff,%edx  */
	imm = (imm << 2) + 4;
	*a++ = 0x05; *a++ = imm; *a++ = imm >> 8; *a++ = imm >> 16; *a++ = imm >> 24;
	if (imm >= 0) {
		*a++ = 0x83; *a++ = 0xd2; *a++ = 0x00;
	} else {
		*a++ = 0x83; *a++ = 0xd2; *a++ = 0xff;
	}
	store_eax_edx(&a, &dummy_cpu.delay_jmpaddr);

	if (skip1 != NULL)
		*skip1 = (size_t)a - (size_t)skip1 - 1;
	if (skip2 != NULL)
		*skip2 = (size_t)a - (size_t)skip2 - 1;

	*addrp = a;
	bintrans_write_pc_inc(addrp, sizeof(uint32_t), 1, 1);
	return 1;
}


/*
 *  bintrans_write_instruction__delayedbranch():
 */
static int bintrans_write_instruction__delayedbranch(unsigned char **addrp,
	uint32_t *potential_chunk_p, uint32_t *chunks,
	int only_care_about_chunk_p, int p)
{
	unsigned char *a, *skip=NULL;
	int ofs;
	uint32_t i386_addr;

	a = *addrp;

	if (only_care_about_chunk_p)
		goto try_chunk_p;

	/*  Skip all of this if there is no branch:  */
	ofs = ((size_t)&dummy_cpu.delay_slot) - (size_t)&dummy_cpu;

	/*  8b 86 38 30 00 00       mov    0x3038(%esi),%eax  */
	*a++ = 0x8b; *a++ = 0x86;
	*a++ = ofs; *a++ = ofs >> 8; *a++ = ofs >> 16; *a++ = ofs >> 24;

	/*  83 f8 00                cmp    $0x0,%eax  */
	/*  74 01                   je     16b <skippa>  */
	*a++ = 0x83; *a++ = 0xf8; *a++ = 0x00;
	*a++ = 0x74; skip = a; *a++ = 0;

	/*
	 *  Perform the jump by setting cpu->delay_slot = 0
	 *  and pc = cpu->delay_jmpaddr.
	 */

	/*  c7 86 38 30 00 00 00 00 00 00    movl   $0x0,0x3038(%esi)  */
	ofs = ((size_t)&dummy_cpu.delay_slot) - (size_t)&dummy_cpu;
	*a++ = 0xc7; *a++ = 0x86;
	*a++ = ofs; *a++ = ofs >> 8; *a++ = ofs >> 16; *a++ = ofs >> 24;
	*a++ = 0; *a++ = 0; *a++ = 0; *a++ = 0;

	/*  REMEMBER old pc:  */
	load_into_eax_edx(&a, &dummy_cpu.pc);
	/*  89 c3                   mov    %eax,%ebx  */
	/*  89 d1                   mov    %edx,%ecx  */
	*a++ = 0x89; *a++ = 0xc3;
	*a++ = 0x89; *a++ = 0xd1;
	load_into_eax_edx(&a, &dummy_cpu.delay_jmpaddr);
	store_eax_edx(&a, &dummy_cpu.pc);

try_chunk_p:

	if (potential_chunk_p == NULL) {
		/*  Not much we can do here if this wasn't to the same physical page...  */

		/*  Don't execute too many instructions.  */
		/*  81 be 38 30 00 00 f0 1f 00 00    cmpl   $0x1ff0,0x3038(%esi)  */
		/*  7c 01                            jl     <okk>  */
		/*  c3                               ret    */
		ofs = ((size_t)&dummy_cpu.bintrans_instructions_executed) - (size_t)&dummy_cpu;
		*a++ = 0x81; *a++ = 0xbe;
		*a++ = ofs; *a++ = ofs >> 8; *a++ = ofs >> 16; *a++ = ofs >> 24;
		*a++ = 0xf0; *a++ = 0x1f; *a++ = 0; *a++ = 0;
		*a++ = 0x7c; *a++ = 0x01;
		*a++ = 0xc3;

		/*
		 *  Compare the old pc (ecx:ebx) and the new pc (edx:eax). If they are on the
		 *  same virtual page (which means that they are on the same physical
		 *  page), then we can check the right chunk pointer, and if it
		 *  is non-NULL, then we can jump there.  Otherwise just return.
		 */

		/*  Subtract 4 from the old pc first. (This is where the jump originated from.)  */
		/*  83 eb 04                sub    $0x4,%ebx  */
		/*  83 d9 00                sbb    $0x0,%ecx  */
		*a++ = 0x83; *a++ = 0xeb; *a++ = 0x04;
		*a++ = 0x83; *a++ = 0xd9; *a++ = 0x00;

		/*  Remember new pc:  */
		/*  89 c7                   mov    %eax,%edi  */
		*a++ = 0x89; *a++ = 0xc7;

		/*  81 e3 00 f0 ff ff       and    $0xfffff000,%ebx  */
		/*  25 00 f0 ff ff          and    $0xfffff000,%eax  */
		*a++ = 0x81; *a++ = 0xe3; *a++ = 0x00; *a++ = 0xf0; *a++ = 0xff; *a++ = 0xff;
		*a++ = 0x25; *a++ = 0x00; *a++ = 0xf0; *a++ = 0xff; *a++ = 0xff;

		/*  39 c3                   cmp    %eax,%ebx  */
		/*  74 01                   je     <ok1>  */
		/*  c3                      ret    */
		/*  39 d1                   cmp    %edx,%ecx  */
		/*  74 01                   je     1b9 <ok2>  */
		/*  c3                      ret    */
		*a++ = 0x39; *a++ = 0xc3;
		*a++ = 0x74; *a++ = 0x01;
		*a++ = 0xc3;
		*a++ = 0x39; *a++ = 0xd1;
		*a++ = 0x74; *a++ = 0x01;
		*a++ = 0xc3;

		/*  81 e7 ff 0f 00 00       and    $0xfff,%edi  */
		*a++ = 0x81; *a++ = 0xe7; *a++ = 0xff; *a++ = 0x0f; *a++ = 0; *a++ = 0;

		/*  8b 87 78 56 34 12       mov    0x12345678(%edi),%eax  */
		ofs = (size_t)chunks;
		*a++ = 0x8b; *a++ = 0x87; *a++ = ofs; *a++ = ofs >> 8; *a++ = ofs >> 16; *a++ = ofs >> 24;

		/*  83 f8 00                cmp    $0x0,%eax  */
		/*  75 01                   jne    1cd <okjump>  */
		/*  c3                      ret    */
		*a++ = 0x83; *a++ = 0xf8; *a++ = 0x00;
		*a++ = 0x75; *a++ = 0x01;
		*a++ = 0xc3;

		/*  03 86 78 56 34 12       add    0x12345678(%esi),%eax  */
		/*  ff e0                   jmp    *%eax  */
		ofs = ((size_t)&dummy_cpu.chunk_base_address) - (size_t)&dummy_cpu;
		*a++ = 0x03; *a++ = 0x86;
		*a++ = ofs; *a++ = ofs >> 8; *a++ = ofs >> 16; *a++ = ofs >> 24;
		*a++ = 0xff; *a++ = 0xe0;
	} else {
		/*
		 *  Just to make sure that we don't become too unreliant
		 *  on the main program loop, we need to return every once
		 *  in a while (interrupts etc).
		 *
		 *  Load the "nr of instructions executed" (which is an int)
		 *  and see if it is below a certain threshold. If so, then
		 *  we go on with the fast path (bintrans), otherwise we
		 *  abort by returning.
		 */
		/*  81 be 38 30 00 00 f0 1f 00 00    cmpl   $0x1ff0,0x3038(%esi)  */
		/*  7c 01                            jl     <okk>  */
		/*  c3                               ret    */
		if (!only_care_about_chunk_p) {
			ofs = ((size_t)&dummy_cpu.bintrans_instructions_executed) - (size_t)&dummy_cpu;
			*a++ = 0x81; *a++ = 0xbe;
			*a++ = ofs; *a++ = ofs >> 8; *a++ = ofs >> 16; *a++ = ofs >> 24;
			*a++ = (N_SAFE_BINTRANS_LIMIT-1) & 255;
			*a++ = ((N_SAFE_BINTRANS_LIMIT-1) >> 8) & 255; *a++ = 0; *a++ = 0;
			*a++ = 0x7c; *a++ = 0x01;
			*a++ = 0xc3;
		}

		/*
		 *  potential_chunk_p points to an "uint32_t".
		 *  If this value is non-NULL, then it is a piece of i386
		 *  machine language code corresponding to the address
		 *  we're jumping to. Otherwise, those instructions haven't
		 *  been translated yet, so we have to return to the main
		 *  loop.  (Actually, we have to add cpu->chunk_base_address.)
		 *
		 *  Case 1:  The value is non-NULL already at translation
		 *           time. Then we can make a direct (fast) native
		 *           i386 jump to the code chunk.
		 *
		 *  Case 2:  The value was NULL at translation time, then we
		 *           have to check during runtime.
		 */

		/*  Case 1:  */
		/*  printf("%08x ", *potential_chunk_p);  */
		i386_addr = *potential_chunk_p + (size_t)translation_code_chunk_space;
		i386_addr = i386_addr - ((size_t)a + 5);
		if ((*potential_chunk_p) != 0) {
			*a++ = 0xe9;
			*a++ = i386_addr;
			*a++ = i386_addr >> 8;
			*a++ = i386_addr >> 16;
			*a++ = i386_addr >> 24;
		} else {
			/*  Case 2:  */

			bintrans_register_potential_quick_jump(a, p);

			i386_addr = (size_t)potential_chunk_p;

			/*
			 *  Load the chunk pointer into eax.
			 *  If it is NULL (zero), then skip the following jump.
			 *  Add chunk_base_address to eax, and jump to eax.
			 */

			/*  a1 78 56 34 12          mov    0x12345678,%eax  */
			/*  83 f8 00                cmp    $0x0,%eax  */
			/*  75 01                   jne    <okaa>  */
			/*  c3                      ret    */
			*a++ = 0xa1;
			*a++ = i386_addr; *a++ = i386_addr >> 8;
			*a++ = i386_addr >> 16; *a++ = i386_addr >> 24;
			*a++ = 0x83; *a++ = 0xf8; *a++ = 0x00;
			*a++ = 0x75; *a++ = 0x01;
			*a++ = 0xc3;

			/*  03 86 78 56 34 12       add    0x12345678(%esi),%eax  */
			/*  ff e0                   jmp    *%eax  */
			ofs = ((size_t)&dummy_cpu.chunk_base_address) - (size_t)&dummy_cpu;
			*a++ = 0x03; *a++ = 0x86;
			*a++ = ofs; *a++ = ofs >> 8; *a++ = ofs >> 16; *a++ = ofs >> 24;
			*a++ = 0xff; *a++ = 0xe0;
		}
	}

	if (skip != NULL)
		*skip = (size_t)a - (size_t)skip - 1;

	*addrp = a;
	return 1;
}


/*
 *  bintrans_write_instruction__loadstore():
 */
static int bintrans_write_instruction__loadstore(unsigned char **addrp,
	int rt, int imm, int rs, int instruction_type, int bigendian)
{
	unsigned char *a, *retfail;
	int ofs, writeflag, alignment, load=0;

	/*  TODO: Not yet:  */
	if (instruction_type == HI6_LQ_MDMX || instruction_type == HI6_SQ)
		return 0;

	/*  TODO: Not yet:  */
	if (bigendian)
		return 0;

	switch (instruction_type) {
	case HI6_LQ_MDMX:
	case HI6_LD:
	case HI6_LWU:
	case HI6_LW:
	case HI6_LHU:
	case HI6_LH:
	case HI6_LBU:
	case HI6_LB:
		load = 1;
		if (rt == 0)
			return 0;
	}


	a = *addrp;

	load_into_eax_edx(&a, &dummy_cpu.gpr[rs]);

	if (imm & 0x8000) {
		/*  05 34 f2 ff ff          add    $0xfffff234,%eax  */
		/*  83 d2 ff                adc    $0xffffffff,%edx  */
		*a++ = 5;
		*a++ = imm; *a++ = imm >> 8; *a++ = 0xff; *a++ = 0xff;
		*a++ = 0x83; *a++ = 0xd2; *a++ = 0xff;
	} else {
		/*  05 34 12 00 00          add    $0x1234,%eax  */
		/*  83 d2 00                adc    $0x0,%edx  */
		*a++ = 5;
		*a++ = imm; *a++ = imm >> 8; *a++ = 0; *a++ = 0;
		*a++ = 0x83; *a++ = 0xd2; *a++ = 0;
	}

	alignment = 0;
	switch (instruction_type) {
	case HI6_LQ_MDMX:
	case HI6_SQ:
		alignment = 15;
		break;
	case HI6_LD:
	case HI6_SD:
		alignment = 7;
		break;
	case HI6_LW:
	case HI6_LWU:
	case HI6_SW:
		alignment = 3;
		break;
	case HI6_LH:
	case HI6_LHU:
	case HI6_SH:
		alignment = 1;
		break;
	}

	if (alignment > 0) {
		unsigned char *alignskip;
		/*
		 *  Check alignment:
		 *
		 *  89 c3                   mov    %eax,%ebx
		 *  83 e3 01                and    $0x1,%ebx
		 *  74 01                   jz     <ok>
		 *  c3                      ret
		 */
		*a++ = 0x89; *a++ = 0xc3;
		*a++ = 0x83; *a++ = 0xe3; *a++ = alignment;
		*a++ = 0x74; alignskip = a; *a++ = 0x00;
		bintrans_write_chunkreturn_fail(&a);
		*alignskip = (size_t)a - (size_t)alignskip - 1;
	}


	/*
	 *  Do manual lookup:
	 *
	 *  hostaddr = fast(cpu, vaddr, writeflag)
	 */

	writeflag = 1 - load;

	/*  push writeflag  */
	*a++ = 0x6a; *a++ = writeflag;

	/*  push vaddr (eax and edx)  */
	*a++ = 0x52;
	*a++ = 0x50;

	/*  push cpu (esi)  */
	*a++ = 0x56;

	ofs = ((size_t)&dummy_cpu.bintrans_fast_vaddr_to_hostaddr) - (size_t)&dummy_cpu;
	*a++ = 0x8b; *a++ = 0x86;
	*a++ = ofs; *a++ = ofs >> 8; *a++ = ofs >> 16; *a++ = ofs >> 24;

	/*  ff d0                   call   *%eax  */
	*a++ = 0xff; *a++ = 0xd0;

	/*  83 c4 10                add    $0x10,%esp  */
	*a++ = 0x83; *a++ = 0xc4; *a++ = 0x10;

	/*  If eax is NULL, then return.  */
	/*  83 f8 00                cmp    $0x0,%eax  */
	/*  75 01                   jne    <okjump>  */
	/*  c3                      ret    */
	*a++ = 0x83; *a++ = 0xf8; *a++ = 0x00;
	*a++ = 0x75; retfail = a; *a++ = 0x00;
	bintrans_write_chunkreturn_fail(&a);		/*  ret (and fail)  */
	*retfail = (size_t)a - (size_t)retfail - 1;

	/*  If eax is -1, then just return (an exception).  */
	/*  83 f8 ff                cmp    $-1,%eax  */
	/*  75 01                   jne    <okjump>  */
	/*  c3                      ret    */
	*a++ = 0x83; *a++ = 0xf8; *a++ = 0xff;
	*a++ = 0x75; retfail = a; *a++ = 0x00;
	bintrans_write_chunkreturn(&a);		/*  ret (but no fail)  */
	*retfail = (size_t)a - (size_t)retfail - 1;

	/*  89 c7                   mov    %eax,%edi  */
	*a++ = 0x89; *a++ = 0xc7;

	if (!load)
		load_into_eax_edx(&a, &dummy_cpu.gpr[rt]);

	switch (instruction_type) {
	case HI6_LD:
		/*  8b 07                   mov    (%edi),%eax  */
		/*  8b 57 04                mov    0x4(%edi),%edx  */
		*a++ = 0x8b; *a++ = 0x07;
		*a++ = 0x8b; *a++ = 0x57; *a++ = 0x04;
		break;
	case HI6_LWU:
		/*  8b 07                   mov    (%edi),%eax  */
		/*  31 d2                   xor    %edx,%edx  */
		*a++ = 0x8b; *a++ = 0x07;
		*a++ = 0x31; *a++ = 0xd2;
		break;
	case HI6_LW:
		/*  8b 07                   mov    (%edi),%eax  */
		/*  99                      cltd   */
		*a++ = 0x8b; *a++ = 0x07;
		*a++ = 0x99;
		break;
	case HI6_LHU:
		/*  31 c0                   xor    %eax,%eax  */
		/*  66 8b 07                mov    (%edi),%ax  */
		/*  99                      cltd   */
		*a++ = 0x31; *a++ = 0xc0;
		*a++ = 0x66; *a++ = 0x8b; *a++ = 0x07;
		*a++ = 0x99;
		break;
	case HI6_LH:
		/*  66 8b 07                mov    (%edi),%ax  */
		/*  98                      cwtl   */
		/*  99                      cltd   */
		*a++ = 0x66; *a++ = 0x8b; *a++ = 0x07;
		*a++ = 0x98;
		*a++ = 0x99;
		break;
	case HI6_LBU:
		/*  31 c0                   xor    %eax,%eax  */
		/*  8a 07                   mov    (%edi),%al  */
		/*  99                      cltd   */
		*a++ = 0x31; *a++ = 0xc0;
		*a++ = 0x8a; *a++ = 0x07;
		*a++ = 0x99;
		break;
	case HI6_LB:
		/*  8a 07                   mov    (%edi),%al  */
		/*  66 98                   cbtw   */
		/*  98                      cwtl   */
		/*  99                      cltd   */
		*a++ = 0x8a; *a++ = 0x07;
		*a++ = 0x66; *a++ = 0x98;
		*a++ = 0x98;
		*a++ = 0x99;
		break;

	case HI6_SD:
		/*  89 07                   mov    %eax,(%edi)  */
		/*  89 57 04                mov    %edx,0x4(%edi)  */
		*a++ = 0x89; *a++ = 0x07;
		*a++ = 0x89; *a++ = 0x57; *a++ = 0x04;
		break;
	case HI6_SW:
		/*  89 07                   mov    %eax,(%edi)  */
		*a++ = 0x89; *a++ = 0x07;
		break;
	case HI6_SH:
		/*  66 89 07                mov    %ax,(%edi)  */
		*a++ = 0x66; *a++ = 0x89; *a++ = 0x07;
		break;
	case HI6_SB:
		/*  88 07                   mov    %al,(%edi)  */
		*a++ = 0x88; *a++ = 0x07;
		break;
	default:
		;
	}

	if (load && rt != 0)
		store_eax_edx(&a, &dummy_cpu.gpr[rt]);

	*addrp = a;
	bintrans_write_pc_inc(addrp, sizeof(uint32_t), 1, 1);
	return 1;
}


/*
 *  bintrans_write_instruction__tlb():
 */
static int bintrans_write_instruction__tlb(unsigned char **addrp, int itype)
{
	unsigned char *a;
	int ofs = 0;	/*  avoid a compiler warning  */

	switch (itype) {
	case TLB_TLBP:
	case TLB_TLBR:
	case TLB_TLBWR:
	case TLB_TLBWI:
		break;
	default:
		return 0;
	}

	a = *addrp;

	switch (itype) {
	case TLB_TLBP:
	case TLB_TLBR:
		/*  push readflag  */
		*a++ = 0x6a; *a++ = (itype == TLB_TLBR);
		ofs = ((size_t)&dummy_cpu.bintrans_fast_tlbpr) - (size_t)&dummy_cpu;
		break;
	case TLB_TLBWR:
	case TLB_TLBWI:
		/*  push randomflag  */
		*a++ = 0x6a; *a++ = (itype == TLB_TLBWR);
		ofs = ((size_t)&dummy_cpu.bintrans_fast_tlbwri) - (size_t)&dummy_cpu;
		break;
	}

	/*  push cpu (esi)  */
	*a++ = 0x56;

	/*  eax = points to the right function  */
	*a++ = 0x8b; *a++ = 0x86;
	*a++ = ofs; *a++ = ofs >> 8; *a++ = ofs >> 16; *a++ = ofs >> 24;

	/*  ff d0                   call   *%eax  */
	*a++ = 0xff; *a++ = 0xd0;

	/*  83 c4 10                add    $8,%esp  */
	*a++ = 0x83; *a++ = 0xc4; *a++ = 8;

	*addrp = a;
	bintrans_write_pc_inc(addrp, sizeof(uint32_t), 1, 1);
	return 1;
}

