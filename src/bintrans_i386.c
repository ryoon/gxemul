/*
 *  Copyright (C) 2004-2005  Anders Gavare.  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright  
 *     notice, this list of conditions and the following disclaimer in the 
 *     documentation and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
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
 *  $Id: bintrans_i386.c,v 1.63 2005-01-10 03:06:32 debug Exp $
 *
 *  i386 specific code for dynamic binary translation.
 *  See bintrans.c for more information.  Included from bintrans.c.
 *
 *  Translated code uses the following conventions at all time:
 *
 *	esi		points to the cpu struct
 *	edi		lowest 32 bits of cpu->pc
 *	ebp		contains cpu->bintrans_instructions_executed
 */


struct cpu dummy_cpu;
struct coproc dummy_coproc;
struct vth32_table dummy_vth32_table;


/*
 *  bintrans_host_cacheinvalidate()
 *
 *  Invalidate the host's instruction cache. On i386, this isn't necessary,
 *  so this is an empty function.
 */
static void bintrans_host_cacheinvalidate(unsigned char *p, size_t len)
{
	/*  Do nothing.  */
}


#define ofs_i		(((size_t)&dummy_cpu.bintrans_instructions_executed) - ((size_t)&dummy_cpu))
#define ofs_pc		(((size_t)&dummy_cpu.pc) - ((size_t)&dummy_cpu))
#define ofs_pc_last	(((size_t)&dummy_cpu.pc_last) - ((size_t)&dummy_cpu))


unsigned char bintrans_i386_runchunk[41] = {
	0x57,					/*  push   %edi  */
	0x56,					/*  push   %esi  */
	0x55,					/*  push   %ebp  */
	0x53,					/*  push   %ebx  */

	/*
	 *  In all translated code, esi points to the cpu struct, and
	 *  ebp is the nr of executed (translated) instructions.
	 */

	/*  0=ebx, 4=ebp, 8=esi, 0xc=edi, 0x10=retaddr, 0x14=arg0, 0x18=arg1  */

	0x8b, 0x74, 0x24, 0x14,			/*  mov    0x8(%esp,1),%esi  */

	0x8b, 0xae, ofs_i&255, (ofs_i>>8)&255, (ofs_i>>16)&255, (ofs_i>>24)&255,
						/*  mov    nr_instr(%esi),%ebp  */
	0x8b, 0xbe, ofs_pc&255, (ofs_pc>>8)&255, (ofs_pc>>16)&255, (ofs_pc>>24)&255,
						/*  mov    pc(%esi),%edi  */

	0xff, 0x54, 0x24, 0x18,			/*  call   *0x18(%esp,1)  */

	0x89, 0xae, ofs_i&255, (ofs_i>>8)&255, (ofs_i>>16)&255, (ofs_i>>24)&255,
						/*  mov    %ebp,0x1234(%esi)  */
	0x89, 0xbe, ofs_pc&255, (ofs_pc>>8)&255, (ofs_pc>>16)&255, (ofs_pc>>24)&255,
						/*  mov    %edi,pc(%esi)  */

	0x5b,					/*  pop    %ebx  */
	0x5d,					/*  pop    %ebp  */
	0x5e,					/*  pop    %esi  */
	0x5f,					/*  pop    %edi  */
	0xc3					/*  ret  */
};

static unsigned char bintrans_i386_jump_to_32bit_pc[76] = {
	/*  Don't execute too many instructions.  */
	/*  81 fd f0 1f 00 00    cmpl   $0x1ff0,%ebp  */
	/*  7c 01                jl     <okk>  */
	/*  c3                   ret    */
	0x81, 0xfd,
	(N_SAFE_BINTRANS_LIMIT-1) & 255, ((N_SAFE_BINTRANS_LIMIT-1) >> 8) & 255, 0, 0,
	0x7c, 0x01,
	0xc3,

	/*
	 *  ebx = ((vaddr >> 22) & 1023) * sizeof(void *)
	 *
	 *  89 c3                   mov    %eax,%ebx
	 *  c1 eb 14                shr    $20,%ebx
	 *  81 e3 fc 0f 00 00       and    $0xffc,%ebx
	 */
	0x89, 0xc3,
	0xc1, 0xeb, 0x14,
	0x81, 0xe3, 0xfc, 0x0f, 0, 0,

	/*
	 *  ecx = vaddr_to_hostaddr_table0
	 *
	 *  8b 8e 34 12 00 00       mov    0x1234(%esi),%ecx
	 */
#define ofs_tabl0	(((size_t)&dummy_cpu.vaddr_to_hostaddr_table0) - ((size_t)&dummy_cpu))
	0x8b, 0x8e,
	ofs_tabl0 & 255, (ofs_tabl0 >> 8) & 255, (ofs_tabl0 >> 16) & 255, (ofs_tabl0 >> 24) & 255,

	/*
	 *  ecx = vaddr_to_hostaddr_table0[a]
	 *
	 *  8b 0c 19                mov    (%ecx,%ebx),%ecx
	 */
	0x8b, 0x0c, 0x19,

	/*
	 *  ebx = ((vaddr >> 12) & 1023) * sizeof(void *)
	 *
	 *  89 c3                   mov    %eax,%ebx
	 *  c1 eb 0a                shr    $10,%ebx
	 *  81 e3 fc 0f 00 00       and    $0xffc,%ebx
	 */
	0x89, 0xc3,
	0xc1, 0xeb, 0x0a,
	0x81, 0xe3, 0xfc, 0x0f, 0, 0,

	/*
	 *  ecx = vaddr_to_hostaddr_table0[a][b].chunks
	 *
	 *  8b 8c 19 56 34 12 00    mov    0x123456(%ecx,%ebx,1),%ecx
	 */
#define ofs_chunks	((size_t)&dummy_vth32_table.bintrans_chunks[0] - (size_t)&dummy_vth32_table)
	0x8b, 0x8c, 0x19,
	    ofs_chunks & 255, (ofs_chunks >> 8) & 255, (ofs_chunks >> 16) & 255, (ofs_chunks >> 24) & 255,

	/*
	 *  ecx = NULL? Then return with failure.
	 *
	 *  83 f9 00                cmp    $0x0,%ecx
	 *  75 01                   jne    <okzzz>
	 */
	0x83, 0xf9, 0x00,
	0x75, 0x01,
	0xc3,		/*  TODO: failure?  */

	/*
	 *  25 fc 0f 00 00          and    $0xffc,%eax
	 *  01 c1                   add    %eax,%ecx
	 *
	 *  8b 01                   mov    (%ecx),%eax
	 *
	 *  83 f8 00                cmp    $0x0,%eax
	 *  75 01                   jne    <ok>
	 *  c3                      ret
	 */
	0x25, 0xfc, 0x0f, 0, 0,
	0x01, 0xc1,

	0x8b, 0x01,

	0x83, 0xf8, 0x00,
	0x75, 0x01,
	0xc3,		/*  TODO: failure?  */

	/*  03 86 78 56 34 12       add    0x12345678(%esi),%eax  */
	/*  ff e0                   jmp    *%eax  */
#define ofs_chunkbase	((size_t)&dummy_cpu.chunk_base_address - (size_t)&dummy_cpu)
	0x03, 0x86,
	    ofs_chunkbase & 255, (ofs_chunkbase >> 8) & 255, (ofs_chunkbase >> 16) & 255, (ofs_chunkbase >> 24) & 255,
	0xff, 0xe0
};

static unsigned char bintrans_i386_loadstore_32bit[35] = {
	/*
	 *  ebx = ((vaddr >> 22) & 1023) * sizeof(void *)
	 *
	 *  89 c3                   mov    %eax,%ebx
	 *  c1 eb 14                shr    $20,%ebx
	 *  81 e3 fc 0f 00 00       and    $0xffc,%ebx
	 */
	0x89, 0xc3,
	0xc1, 0xeb, 0x14,
	0x81, 0xe3, 0xfc, 0x0f, 0x00, 0x00,

	/*
	 *  ecx = vaddr_to_hostaddr_table0
	 *
	 *  8b 8e 34 12 00 00       mov    0x1234(%esi),%ecx
	 */
	0x8b, 0x8e,
	ofs_tabl0 & 255, (ofs_tabl0 >> 8) & 255, (ofs_tabl0 >> 16) & 255, (ofs_tabl0 >> 24) & 255,

	/*
	 *  ecx = vaddr_to_hostaddr_table0[a]
	 *
	 *  8b 0c 19                mov    (%ecx,%ebx),%ecx
	 */
	0x8b, 0x0c, 0x19,

	/*
	 *  ebx = ((vaddr >> 12) & 1023) * sizeof(void *)
	 *
	 *  89 c3                   mov    %eax,%ebx
	 *  c1 eb 0a                shr    $10,%ebx
	 *  81 e3 fc 0f 00 00       and    $0xffc,%ebx
	 */
	0x89, 0xc3,
	0xc1, 0xeb, 0x0a,
	0x81, 0xe3, 0xfc, 0x0f, 0x00, 0x00,

	/*
	 *  ecx = vaddr_to_hostaddr_table0[a][b]
	 *
	 *  8b 0c 19                mov    (%ecx,%ebx,1),%ecx
	 */
	0x8b, 0x0c, 0x19,

	/*  ret  */
	0xc3
};

static const void (*bintrans_runchunk)
    (struct cpu *, unsigned char *) = (void *)bintrans_i386_runchunk;

static void (*bintrans_jump_to_32bit_pc)
    (struct cpu *) = (void *)bintrans_i386_jump_to_32bit_pc;

static void (*bintrans_loadstore_32bit)
    (struct cpu *) = (void *)bintrans_i386_loadstore_32bit;


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

	/*  81 cd 00 00 00 01    orl    $0x1000000,%ebp  */
	*a++ = 0x81; *a++ = 0xcd;
	*a++ = 0; *a++ = 0; *a++ = 0; *a++ = 0x01;	/*  TODO: not hardcoded  */

	*a++ = 0xc3;		/*  ret  */
	*addrp = a;
}


/*
 *  bintrans_write_pc_inc():
 */
static void bintrans_write_pc_inc(unsigned char **addrp)
{
	unsigned char *a = *addrp;
	int ofs;

	/*  83 c7 04                add    $0x4,%edi  */
	*a++ = 0x83; *a++ = 0xc7; *a++ = 4;

	if (!bintrans_32bit_only) {
		/*  83 96 zz zz zz zz 00    adcl   $0x0,zz(%esi)  */
		ofs = ((size_t)&dummy_cpu.pc) - (size_t)&dummy_cpu;
		ofs += 4;
		*a++ = 0x83; *a++ = 0x96;
		*a++ = ofs & 255;
		*a++ = (ofs >> 8) & 255;
		*a++ = (ofs >> 16) & 255;
		*a++ = (ofs >> 24) & 255;
		*a++ = 0;
	}

	/*  45   inc %ebp  */
	*a++ = 0x45;

	*addrp = a;
}


/*
 *  load_pc_into_eax_edx():
 */
static void load_pc_into_eax_edx(unsigned char **addrp)
{
	unsigned char *a;
	a = *addrp;

	/*  89 f8                   mov    %edi,%eax  */
	*a++ = 0x89; *a++ = 0xf8;

	if (bintrans_32bit_only) {
		/*  99                      cltd   */
		*a++ = 0x99;
	} else {
		int ofs = ((size_t)&dummy_cpu.pc) - (size_t)&dummy_cpu;
		/*  8b 96 3c 30 00 00       mov    0x303c(%esi),%edx  */
		ofs += 4;
		*a++ = 0x8b; *a++ = 0x96;
		*a++ = ofs; *a++ = ofs >> 8; *a++ = ofs >> 16; *a++ = ofs >> 24;
	}

	*addrp = a;
}


/*
 *  store_eax_edx_into_pc():
 */
static void store_eax_edx_into_pc(unsigned char **addrp)
{
	unsigned char *a;
	int ofs = ((size_t)&dummy_cpu.pc) - (size_t)&dummy_cpu;
	a = *addrp;

	/*  89 c7                   mov    %eax,%edi  */
	*a++ = 0x89; *a++ = 0xc7;

	/*  89 96 3c 30 00 00       mov    %edx,0x303c(%esi)  */
	ofs += 4;
	*a++ = 0x89; *a++ = 0x96;
	*a++ = ofs; *a++ = ofs >> 8; *a++ = ofs >> 16; *a++ = ofs >> 24;

	*addrp = a;
}


/*
 *  load_into_eax_edx():
 *
 *  Usage:    load_into_eax_edx(&a, &dummy_cpu.gpr[rs]);   etc.
 */
static void load_into_eax_edx(unsigned char **addrp, void *p)
{
	unsigned char *a;
	int ofs = (size_t)p - (size_t)&dummy_cpu;
	a = *addrp;

	/*  8b 86 38 30 00 00       mov    0x3038(%esi),%eax  */
	*a++ = 0x8b; *a++ = 0x86;
	*a++ = ofs; *a++ = ofs >> 8; *a++ = ofs >> 16; *a++ = ofs >> 24;

	if (bintrans_32bit_only) {
		/*  99                      cltd   */
		*a++ = 0x99;
	} else {
		/*  8b 96 3c 30 00 00       mov    0x303c(%esi),%edx  */
		ofs += 4;
		*a++ = 0x8b; *a++ = 0x96;
		*a++ = ofs; *a++ = ofs >> 8; *a++ = ofs >> 16; *a++ = ofs >> 24;
	}

	*addrp = a;
}


/*
 *  load_into_eax_and_sign_extend_into_edx():
 *
 *  Usage:    load_into_eax_and_sign_extend_into_edx(&a, &dummy_cpu.gpr[rs]);   etc.
 */
static void load_into_eax_and_sign_extend_into_edx(unsigned char **addrp, void *p)
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
 *  load_into_eax_dont_care_about_edx():
 *
 *  Usage:    load_into_eax_dont_care_about_edx(&a, &dummy_cpu.gpr[rs]);   etc.
 */
static void load_into_eax_dont_care_about_edx(unsigned char **addrp, void *p)
{
	unsigned char *a;
	int ofs = (size_t)p - (size_t)&dummy_cpu;
	a = *addrp;

	/*  8b 86 38 30 00 00       mov    0x3038(%esi),%eax  */
	*a++ = 0x8b; *a++ = 0x86;
	*a++ = ofs; *a++ = ofs >> 8; *a++ = ofs >> 16; *a++ = ofs >> 24;

	*addrp = a;
}


/*
 *  store_eax_edx():
 *
 *  Usage:    store_eax_edx(&a, &dummy_cpu.gpr[rs]);   etc.
 */
static void store_eax_edx(unsigned char **addrp, void *p)
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
	bintrans_write_pc_inc(addrp);
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

	if (bintrans_32bit_only)
		load_into_eax_and_sign_extend_into_edx(&a, &dummy_cpu.gpr[rs]);
	else
		load_into_eax_edx(&a, &dummy_cpu.gpr[rs]);

	store_eax_edx(&a, &dummy_cpu.delay_jmpaddr);

	if (special == SPECIAL_JALR && rd != 0) {
		/*  gpr[rd] = retaddr    (pc + 8)  */

		if (bintrans_32bit_only) {
			load_pc_into_eax_edx(&a);
			/*  83 c0 08                add    $0x8,%eax  */
			*a++ = 0x83; *a++ = 0xc0; *a++ = 0x08;
		} else {
			load_pc_into_eax_edx(&a);
			/*  83 c0 08                add    $0x8,%eax  */
			/*  83 d2 00                adc    $0x0,%edx  */
			*a++ = 0x83; *a++ = 0xc0; *a++ = 0x08;
			*a++ = 0x83; *a++ = 0xd2; *a++ = 0x00;
		}

		store_eax_edx(&a, &dummy_cpu.gpr[rd]);
	}

	*addrp = a;
	bintrans_write_pc_inc(addrp);
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
	bintrans_write_pc_inc(addrp);
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

	/*  TODO: overflow detection for ADDI and DADDI  */
	switch (instruction_type) {
	case HI6_ADDI:
	case HI6_DADDI:
		return 0;
	}

	a = *addrp;

	if (rt == 0)
		goto rt0;

	uimm = imm & 0xffff;

	if (uimm == 0 && (instruction_type == HI6_ADDIU ||
	    instruction_type == HI6_ADDI)) {
		load_into_eax_and_sign_extend_into_edx(&a, &dummy_cpu.gpr[rs]);
		store_eax_edx(&a, &dummy_cpu.gpr[rt]);
		goto rt0;
	}

	if (uimm == 0 && (instruction_type == HI6_DADDIU ||
	    instruction_type == HI6_DADDI || instruction_type == HI6_ORI)) {
		load_into_eax_edx(&a, &dummy_cpu.gpr[rs]);
		store_eax_edx(&a, &dummy_cpu.gpr[rt]);
		goto rt0;
	}

	if (bintrans_32bit_only)
		load_into_eax_and_sign_extend_into_edx(&a, &dummy_cpu.gpr[rs]);
	else
		load_into_eax_edx(&a, &dummy_cpu.gpr[rs]);

	switch (instruction_type) {
	case HI6_ADDIU:
	case HI6_DADDIU:
	case HI6_ADDI:
	case HI6_DADDI:
		if (imm & 0x8000) {
			/*  05 39 fd ff ff          add    $0xfffffd39,%eax  */
			/*  83 d2 ff                adc    $0xffffffff,%edx  */
			*a++ = 0x05; *a++ = uimm; *a++ = uimm >> 8; *a++ = 0xff; *a++ = 0xff;
			if (instruction_type == HI6_DADDIU) {
				*a++ = 0x83; *a++ = 0xd2; *a++ = 0xff;
			}
		} else {
			/*  05 c7 02 00 00          add    $0x2c7,%eax  */
			/*  83 d2 00                adc    $0x0,%edx  */
			*a++ = 0x05; *a++ = uimm; *a++ = uimm >> 8; *a++ = 0; *a++ = 0;
			if (instruction_type == HI6_DADDIU) {
				*a++ = 0x83; *a++ = 0xd2; *a++ = 0;
			}
		}
		if (instruction_type == HI6_ADDIU) {
			/*  99                      cltd   */
			*a++ = 0x99;
		}
		break;
	case HI6_ANDI:
		/*  25 34 12 00 00          and    $0x1234,%eax  */
		/*  31 d2                   xor    %edx,%edx  */
		*a++ = 0x25; *a++ = uimm; *a++ = uimm >> 8; *a++ = 0; *a++ = 0;
		*a++ = 0x31; *a++ = 0xd2;
		break;
	case HI6_ORI:
		/*  0d 34 12 00 00          or     $0x1234,%eax  */
		*a++ = 0xd; *a++ = uimm; *a++ = uimm >> 8; *a++ = 0; *a++ = 0;
		if (bintrans_32bit_only) {
			/*  99                      cltd   */
			*a++ = 0x99;
		}
		break;
	case HI6_XORI:
		/*  35 34 12 00 00          xor    $0x1234,%eax  */
		*a++ = 0x35; *a++ = uimm; *a++ = uimm >> 8; *a++ = 0; *a++ = 0;
		if (bintrans_32bit_only) {
			/*  99                      cltd   */
			*a++ = 0x99;
		}
		break;
	case HI6_SLTIU:
		/*  set if less than, unsigned. (compare edx:eax to ecx:ebx)  */
		/*  ecx:ebx = the immediate value  */
		/*  bb dc fe ff ff          mov    $0xfffffedc,%ebx  */
		/*  b9 ff ff ff ff          mov    $0xffffffff,%ecx  */
		/*  or  */
		/*  29 c9                   sub    %ecx,%ecx  */
		if (bintrans_32bit_only) {
			/*  99                      cltd   */
			*a++ = 0x99;
		}
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
		if (bintrans_32bit_only) {
			/*  99                      cltd   */
			*a++ = 0x99;
		}
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

	store_eax_edx(&a, &dummy_cpu.gpr[rt]);

rt0:
	*addrp = a;
	bintrans_write_pc_inc(addrp);
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

	load_pc_into_eax_edx(&a);

	if (link) {
		/*  gpr[31] = pc + 8  */
		if (bintrans_32bit_only) {
			/*  50             push  %eax */
			/*  83 c0 08       add   $0x8,%eax  */
			*a++ = 0x50;
			*a++ = 0x83; *a++ = 0xc0; *a++ = 0x08;
		} else {
			/*  50             push  %eax */
			/*  52             push  %edx */
			/*  83 c0 08                add    $0x8,%eax  */
			/*  83 d2 00                adc    $0x0,%edx  */
			*a++ = 0x50;
			*a++ = 0x52;
			*a++ = 0x83; *a++ = 0xc0; *a++ = 0x08;
			*a++ = 0x83; *a++ = 0xd2; *a++ = 0x00;
		}
		store_eax_edx(&a, &dummy_cpu.gpr[31]);
		if (bintrans_32bit_only) {
			/*  58     pop %eax  */
			*a++ = 0x58;
		} else {
			/*  5a     pop %edx  */
			/*  58     pop %eax  */
			*a++ = 0x5a;
			*a++ = 0x58;
		}
	}

	/*  delay_jmpaddr = top 36 bits of pc together with lowest 28 bits of imm*4:  */
	imm *= 4;

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
	bintrans_write_pc_inc(addrp);
	return 1;
}


/*
 *  bintrans_write_instruction__addu_etc():
 */
static int bintrans_write_instruction__addu_etc(unsigned char **addrp,
	int rd, int rs, int rt, int sa, int instruction_type)
{
	unsigned char *a;
	int load64 = 0, do_store = 1;

	/*  TODO: Not yet  */
	switch (instruction_type) {
	case SPECIAL_MULT:
	case SPECIAL_MULTU:
	case SPECIAL_DIV:
	case SPECIAL_DIVU:
		if (rd != 0)
			return 0;
		break;
	case SPECIAL_DSLL:
	case SPECIAL_DSLL32:
	case SPECIAL_DSRA:
	case SPECIAL_DSRA32:
	case SPECIAL_DSRL:
	case SPECIAL_DSRL32:
	case SPECIAL_MOVZ:
	case SPECIAL_MOVN:
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

	switch (instruction_type) {
	case SPECIAL_MULT:
	case SPECIAL_MULTU:
	case SPECIAL_DIV:
	case SPECIAL_DIVU:
		break;
	default:
		if (rd == 0)
			goto rd0;
	}

	a = *addrp;

	if ((instruction_type == SPECIAL_ADDU || instruction_type == SPECIAL_DADDU
	    || instruction_type == SPECIAL_OR) && rt == 0) {
		if (load64)
			load_into_eax_edx(&a, &dummy_cpu.gpr[rs]);
		else
			load_into_eax_and_sign_extend_into_edx(&a, &dummy_cpu.gpr[rs]);
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
	case SPECIAL_MULT:
	case SPECIAL_MULTU:
		/*  57    push %edi  */
		*a++ = 0x57;
		if (instruction_type == SPECIAL_MULT) {
			/*  f7 eb                   imul   %ebx  */
			*a++ = 0xf7; *a++ = 0xeb;
		} else {
			/*  f7 e3                   mul   %ebx  */
			*a++ = 0xf7; *a++ = 0xe3;
		}
		/*  here: edx:eax = hi:lo  */
		/*  89 d7                   mov    %edx,%edi  */
		/*  99                      cltd   */
		*a++ = 0x89; *a++ = 0xd7;
		*a++ = 0x99;
		/*  here: edi=hi, edx:eax = sign-extended lo  */
		store_eax_edx(&a, &dummy_cpu.lo);
		/*  89 f8                   mov    %edi,%eax  */
		/*  99                      cltd   */
		*a++ = 0x89; *a++ = 0xf8;
		*a++ = 0x99;
		/*  here: edx:eax = sign-extended hi  */
		store_eax_edx(&a, &dummy_cpu.hi);
		/*  5f    pop %edi  */
		*a++ = 0x5f;
		do_store = 0;
		break;
	case SPECIAL_DIV:
	case SPECIAL_DIVU:
		/*
		 *  In:   edx:eax = rs, ecx:ebx = rt
		 *  Out:  LO = rs / rt, HI = rs % rt
		 */
		/*  Division by zero on MIPS is undefined, but on
		    i386 it causes an exception, so we'll try to
		    avoid that.  */
		*a++ = 0x83; *a++ = 0xfb; *a++ = 0x00;	/*  cmp $0x0,%ebx  */
		*a++ = 0x75; *a++ = 0x01;		/*  jne skip_inc  */
		*a++ = 0x43;				/*  inc %ebx  */

		/*  57    push %edi  */
		*a++ = 0x57;
		if (instruction_type == SPECIAL_DIV) {
			*a++ = 0x99;			/*  cltd */
			*a++ = 0xf7; *a++ = 0xfb;	/*  idiv %ebx  */
		} else {
			*a++ = 0x29; *a++ = 0xd2;	/*  sub %edx,%edx  */
			*a++ = 0xf7; *a++ = 0xf3;	/*  div %ebx  */
		}
		/*  here: edx:eax = hi:lo  */
		/*  89 d7                   mov    %edx,%edi  */
		/*  99                      cltd   */
		*a++ = 0x89; *a++ = 0xd7;
		*a++ = 0x99;
		/*  here: edi=hi, edx:eax = sign-extended lo  */
		store_eax_edx(&a, &dummy_cpu.lo);
		/*  89 f8                   mov    %edi,%eax  */
		/*  99                      cltd   */
		*a++ = 0x89; *a++ = 0xf8;
		*a++ = 0x99;
		/*  here: edx:eax = sign-extended hi  */
		store_eax_edx(&a, &dummy_cpu.hi);
		/*  5f    pop %edi  */
		*a++ = 0x5f;
		do_store = 0;
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

	if (do_store)
		store_eax_edx(&a, &dummy_cpu.gpr[rd]);

	*addrp = a;
rd0:
	bintrans_write_pc_inc(addrp);
	return 1;
}


/*
 *  bintrans_write_instruction__mfc_mtc():
 */
static int bintrans_write_instruction__mfc_mtc(unsigned char **addrp, int coproc_nr, int flag64bit, int rt, int rd, int mtcflag)
{
	unsigned char *a, *failskip;
	int ofs;

	if (mtcflag && flag64bit) {
		/*  mtc: */
		return 0;
	}

	/*
	 *  NOTE: Only a few registers are readable without side effects.
	 */
	if (rt == 0 && !mtcflag)
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

	/*  8b 96 3c 30 00 00       mov    0x303c(%esi),%edx  */
	ofs = ((size_t)&dummy_cpu.coproc[0]) - (size_t)&dummy_cpu;
	*a++ = 0x8b; *a++ = 0x96;
	*a++ = ofs; *a++ = ofs >> 8; *a++ = ofs >> 16; *a++ = ofs >> 24;

	/*  here, edx = cpu->coproc[0]  */

	if (mtcflag) {
		/*  mtc  */

		/*  TODO: This code only works for mtc0, not dmtc0  */

		/*  8b 9a 38 30 00 00       mov    0x3038(%edx),%ebx  */
		ofs = ((size_t)&dummy_coproc.reg[rd]) - (size_t)&dummy_coproc;
		*a++ = 0x8b; *a++ = 0x9a;
		*a++ = ofs; *a++ = ofs >> 8; *a++ = ofs >> 16; *a++ = ofs >> 24;

		load_into_eax_edx(&a, &dummy_cpu.gpr[rt]);

		/*
		 *  Here:  eax contains the value in register rt,
		 *         ebx contains the coproc register rd value.
		 *
		 *  In the general case, only allow mtc if it does not
		 *  change the coprocessor register!
		 */

		switch (rd) {
		case COP0_INDEX:
			break;

		case COP0_ENTRYLO0:
		case COP0_ENTRYLO1:
			/*  TODO: Not all bits are writable!  */
			break;

		case COP0_EPC:
			break;

		case COP0_STATUS:
			/*  Only allow updates to the status register if
			    the interrupt enable bits were changed, but no
			    other bits!  */
			/*  89 c1                   mov    %eax,%ecx  */
			/*  89 da                   mov    %ebx,%edx  */
			/*  81 e1 00 00 e7 0f       and    $0x0fe70000,%ecx  */
			/*  81 e2 00 00 e7 0f       and    $0x0fe70000,%edx  */
			/*  39 ca                   cmp    %ecx,%edx  */
			/*  74 01                   je     <ok>  */
			*a++ = 0x89; *a++ = 0xc1;
			*a++ = 0x89; *a++ = 0xda;
			*a++ = 0x81; *a++ = 0xe1; *a++ = 0x00; *a++ = 0x00;
			if (bintrans_32bit_only) {
				*a++ = 0xe7; *a++ = 0x0f;
			} else {
				*a++ = 0xff; *a++ = 0xff;
			}
			*a++ = 0x81; *a++ = 0xe2; *a++ = 0x00; *a++ = 0x00;
			if (bintrans_32bit_only) {
				*a++ = 0xe7; *a++ = 0x0f;
			} else {
				*a++ = 0xff; *a++ = 0xff;
			}
			*a++ = 0x39; *a++ = 0xca;
			*a++ = 0x74; failskip = a; *a++ = 0x00;
			bintrans_write_chunkreturn_fail(&a);
			*failskip = (size_t)a - (size_t)failskip - 1;

			/*  Only allow the update if it would NOT cause
			    an interrupt exception:  */

			/*  8b 96 3c 30 00 00       mov    0x303c(%esi),%edx  */
			ofs = ((size_t)&dummy_cpu.coproc[0]) - (size_t)&dummy_cpu;
			*a++ = 0x8b; *a++ = 0x96;
			*a++ = ofs; *a++ = ofs >> 8; *a++ = ofs >> 16; *a++ = ofs >> 24;

			/*  8b 9a 38 30 00 00       mov    0x3038(%edx),%ebx  */
			ofs = ((size_t)&dummy_coproc.reg[COP0_CAUSE]) - (size_t)&dummy_coproc;
			*a++ = 0x8b; *a++ = 0x9a;
			*a++ = ofs; *a++ = ofs >> 8; *a++ = ofs >> 16; *a++ = ofs >> 24;

			/*  21 c3                   and    %eax,%ebx  */
			/*  81 e3 00 ff 00 00       and    $0xff00,%ebx  */
			/*  83 fb 00                cmp    $0x0,%ebx  */
			/*  74 01                   je     <ok>  */
			*a++ = 0x21; *a++ = 0xc3;
			*a++ = 0x81; *a++ = 0xe3; *a++ = 0x00;
			    *a++ = 0xff; *a++ = 0x00; *a++ = 0x00;
			*a++ = 0x83; *a++ = 0xfb; *a++ = 0x00;
			*a++ = 0x74; failskip = a; *a++ = 0x00;
			bintrans_write_chunkreturn_fail(&a);
			*failskip = (size_t)a - (size_t)failskip - 1;

			break;

		default:
			/*  39 d8                   cmp    %ebx,%eax  */
			/*  74 01                   je     <ok>  */
			*a++ = 0x39; *a++ = 0xd8;
			*a++ = 0x74; failskip = a; *a++ = 0x00;
			bintrans_write_chunkreturn_fail(&a);
			*failskip = (size_t)a - (size_t)failskip - 1;
		}

		/*  8b 96 3c 30 00 00       mov    0x303c(%esi),%edx  */
		ofs = ((size_t)&dummy_cpu.coproc[0]) - (size_t)&dummy_cpu;
		*a++ = 0x8b; *a++ = 0x96;
		*a++ = ofs; *a++ = ofs >> 8; *a++ = ofs >> 16; *a++ = ofs >> 24;

		/*  8d 9a 38 30 00 00       lea    0x3038(%edx),%ebx  */
		ofs = ((size_t)&dummy_coproc.reg[rd]) - (size_t)&dummy_coproc;
		*a++ = 0x8d; *a++ = 0x9a;
		*a++ = ofs; *a++ = ofs >> 8; *a++ = ofs >> 16; *a++ = ofs >> 24;

		/*  Sign-extend eax into edx:eax, and store it in
		    coprocessor register rd:  */
		/*  99		cltd  */
		*a++ = 0x99;

		/*  89 03                   mov    %eax,(%ebx)  */
		/*  89 53 04                mov    %edx,0x4(%ebx)  */
		*a++ = 0x89; *a++ = 0x03;
		*a++ = 0x89; *a++ = 0x53; *a++ = 0x04;
	} else {
		/*  mfc  */

		/*  8b 82 38 30 00 00       mov    0x3038(%edx),%eax  */
		ofs = ((size_t)&dummy_coproc.reg[rd]) - (size_t)&dummy_coproc;
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
	}

	*addrp = a;
	bintrans_write_pc_inc(addrp);
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
	int ofs, likely = 0;

	switch (instruction_type) {
	case HI6_BEQL:
	case HI6_BNEL:
	case HI6_BLEZL:
	case HI6_BGTZL:
		likely = 1;
	}

	/*  TODO: See the Alpha backend on how these could be implemented:  */
	if (likely)
		return 0;

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
		if (!bintrans_32bit_only) {
			*a++ = 0x39; *a++ = 0xd1;
			*a++ = 0x75; skip2 = a; *a++ = 0x00;
		}
	}

	if (instruction_type == HI6_BNE) {
		/*  If rt != rs, then ok. Otherwise skip.  */
		if (bintrans_32bit_only) {
			/*  39 c3                   cmp    %eax,%ebx  */
			/*  74 xx                   je     <skip>  */
			*a++ = 0x39; *a++ = 0xc3;
			*a++ = 0x74; skip2 = a; *a++ = 0x00;
		} else {
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

	load_pc_into_eax_edx(&a);

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
	bintrans_write_pc_inc(addrp);
	return 1;
}


/*
 *  bintrans_write_instruction__delayedbranch():
 */
static int bintrans_write_instruction__delayedbranch(unsigned char **addrp,
	uint32_t *potential_chunk_p, uint32_t *chunks,
	int only_care_about_chunk_p, int p, int forward)
{
	unsigned char *a, *skip=NULL, *failskip;
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
	load_pc_into_eax_edx(&a);
	/*  89 c3                   mov    %eax,%ebx  */
	/*  89 d1                   mov    %edx,%ecx  */
	*a++ = 0x89; *a++ = 0xc3;
	*a++ = 0x89; *a++ = 0xd1;
	load_into_eax_edx(&a, &dummy_cpu.delay_jmpaddr);
	store_eax_edx_into_pc(&a);

try_chunk_p:

	if (potential_chunk_p == NULL) {
		if (bintrans_32bit_only) {
#if 1
			/*  8b 86 78 56 34 12       mov    0x12345678(%esi),%eax  */
			/*  ff e0                   jmp    *%eax  */
			ofs = ((size_t)&dummy_cpu.bintrans_jump_to_32bit_pc) - (size_t)&dummy_cpu;
			*a++ = 0x8b; *a++ = 0x86;
			*a++ = ofs; *a++ = ofs >> 8; *a++ = ofs >> 16; *a++ = ofs >> 24;
			*a++ = 0xff; *a++ = 0xe0;

#else
			/*  Don't execute too many instructions.  */
			/*  81 fd f0 1f 00 00    cmpl   $0x1ff0,%ebp  */
			/*  7c 01                jl     <okk>  */
			/*  c3                   ret    */
			*a++ = 0x81; *a++ = 0xfd;
			*a++ = (N_SAFE_BINTRANS_LIMIT-1) & 255;
			*a++ = ((N_SAFE_BINTRANS_LIMIT-1) >> 8) & 255; *a++ = 0; *a++ = 0;
			*a++ = 0x7c; failskip = a; *a++ = 0x01;
			bintrans_write_chunkreturn_fail(&a);
			*failskip = (size_t)a - (size_t)failskip - 1;

			/*
			 *  ebx = ((vaddr >> 22) & 1023) * sizeof(void *)
			 *
			 *  89 c3                   mov    %eax,%ebx
			 *  c1 eb 14                shr    $20,%ebx
			 *  81 e3 fc 0f 00 00       and    $0xffc,%ebx
			 */
			*a++ = 0x89; *a++ = 0xc3;
			*a++ = 0xc1; *a++ = 0xeb; *a++ = 0x14;
			*a++ = 0x81; *a++ = 0xe3; *a++ = 0xfc; *a++ = 0x0f; *a++ = 0; *a++ = 0;

			/*
			 *  ecx = vaddr_to_hostaddr_table0
			 *
			 *  8b 8e 34 12 00 00       mov    0x1234(%esi),%ecx
			 */
			ofs = ((size_t)&dummy_cpu.vaddr_to_hostaddr_table0) - (size_t)&dummy_cpu;
			*a++ = 0x8b; *a++ = 0x8e;
			*a++ = ofs; *a++ = ofs >> 8; *a++ = ofs >> 16; *a++ = ofs >> 24;

			/*
			 *  ecx = vaddr_to_hostaddr_table0[a]
			 *
			 *  8b 0c 19                mov    (%ecx,%ebx),%ecx
			 */
			*a++ = 0x8b; *a++ = 0x0c; *a++ = 0x19;

			/*
			 *  ebx = ((vaddr >> 12) & 1023) * sizeof(void *)
			 *
			 *  89 c3                   mov    %eax,%ebx
			 *  c1 eb 0a                shr    $10,%ebx
			 *  81 e3 fc 0f 00 00       and    $0xffc,%ebx
			 */
			*a++ = 0x89; *a++ = 0xc3;
			*a++ = 0xc1; *a++ = 0xeb; *a++ = 0x0a;
			*a++ = 0x81; *a++ = 0xe3; *a++ = 0xfc; *a++ = 0x0f; *a++ = 0; *a++ = 0;

			/*
			 *  ecx = vaddr_to_hostaddr_table0[a][b].chunks
			 *
			 *  8b 8c 19 56 34 12 00    mov    0x123456(%ecx,%ebx,1),%ecx
			 */
			ofs = (size_t)&dummy_vth32_table.bintrans_chunks[0]
			    - (size_t)&dummy_vth32_table;

			*a++ = 0x8b; *a++ = 0x8c;  *a++ = 0x19;
			    *a++ = ofs; *a++ = ofs >> 8; *a++ = ofs >> 16; *a++ = ofs >> 24;

			/*
			 *  ecx = NULL? Then return with failure.
			 *
			 *  83 f9 00                cmp    $0x0,%ecx
			 *  75 01                   jne    <okzzz>
			 */
			*a++ = 0x83; *a++ = 0xf9; *a++ = 0x00;
			*a++ = 0x75; fail = a; *a++ = 0x00;
			bintrans_write_chunkreturn(&a);
			*fail = (size_t)a - (size_t)fail - 1;

			/*
			 *  25 fc 0f 00 00          and    $0xffc,%eax
			 *  01 c1                   add    %eax,%ecx
			 *
			 *  8b 01                   mov    (%ecx),%eax
			 *
			 *  83 f8 00                cmp    $0x0,%eax
			 *  75 01                   jne    <ok>
			 *  c3                      ret
			 */
			*a++ = 0x25; *a++ = 0xfc; *a++ = 0x0f; *a++ = 0; *a++ = 0;
			*a++ = 0x01; *a++ = 0xc1;

			*a++ = 0x8b; *a++ = 0x01;

			*a++ = 0x83; *a++ = 0xf8; *a++ = 0x00;
			*a++ = 0x75; fail = a; *a++ = 0x01;
			bintrans_write_chunkreturn(&a);
			*fail = (size_t)a - (size_t)fail - 1;

			/*  03 86 78 56 34 12       add    0x12345678(%esi),%eax  */
			/*  ff e0                   jmp    *%eax  */
			ofs = ((size_t)&dummy_cpu.chunk_base_address) - (size_t)&dummy_cpu;
			*a++ = 0x03; *a++ = 0x86;
			*a++ = ofs; *a++ = ofs >> 8; *a++ = ofs >> 16; *a++ = ofs >> 24;
			*a++ = 0xff; *a++ = 0xe0;
#endif
		} else {
			/*  Not much we can do here if this wasn't to the same physical page...  */

			/*  Don't execute too many instructions.  */
			/*  81 fd f0 1f 00 00    cmpl   $0x1ff0,%ebp  */
			/*  7c 01                jl     <okk>  */
			/*  c3                   ret    */
			*a++ = 0x81; *a++ = 0xfd;
			*a++ = (N_SAFE_BINTRANS_LIMIT-1) & 255;
			*a++ = ((N_SAFE_BINTRANS_LIMIT-1) >> 8) & 255; *a++ = 0; *a++ = 0;
			*a++ = 0x7c; failskip = a; *a++ = 0x01;
			bintrans_write_chunkreturn_fail(&a);
			*failskip = (size_t)a - (size_t)failskip - 1;

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

			/*  39 d1                   cmp    %edx,%ecx  */
			/*  74 01                   je     1b9 <ok2>  */
			/*  c3                      ret    */
			*a++ = 0x39; *a++ = 0xd1;
			*a++ = 0x74; *a++ = 0x01;
			*a++ = 0xc3;

			/*  Remember new pc:  */
			/*  89 c1                   mov    %eax,%ecx  */
			*a++ = 0x89; *a++ = 0xc1;

			/*  81 e3 00 f0 ff ff       and    $0xfffff000,%ebx  */
			/*  25 00 f0 ff ff          and    $0xfffff000,%eax  */
			*a++ = 0x81; *a++ = 0xe3; *a++ = 0x00; *a++ = 0xf0; *a++ = 0xff; *a++ = 0xff;
			*a++ = 0x25; *a++ = 0x00; *a++ = 0xf0; *a++ = 0xff; *a++ = 0xff;

			/*  39 c3                   cmp    %eax,%ebx  */
			/*  74 01                   je     <ok1>  */
			/*  c3                      ret    */
			*a++ = 0x39; *a++ = 0xc3;
			*a++ = 0x74; *a++ = 0x01;
			*a++ = 0xc3;

			/*  81 e1 ff 0f 00 00       and    $0xfff,%ecx  */
			*a++ = 0x81; *a++ = 0xe1; *a++ = 0xff; *a++ = 0x0f; *a++ = 0; *a++ = 0;

			/*  8b 81 78 56 34 12       mov    0x12345678(%ecx),%eax  */
			ofs = (size_t)chunks;
			*a++ = 0x8b; *a++ = 0x81; *a++ = ofs; *a++ = ofs >> 8; *a++ = ofs >> 16; *a++ = ofs >> 24;

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
		}
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
		/*  81 fd f0 1f 00 00    cmpl   $0x1ff0,%ebp  */
		/*  7c 01                jl     <okk>  */
		/*  c3                   ret    */
		if (!only_care_about_chunk_p && !forward) {
			*a++ = 0x81; *a++ = 0xfd;
			*a++ = (N_SAFE_BINTRANS_LIMIT-1) & 255;
			*a++ = ((N_SAFE_BINTRANS_LIMIT-1) >> 8) & 255; *a++ = 0; *a++ = 0;
			*a++ = 0x7c; failskip = a; *a++ = 0x01;
			bintrans_write_chunkreturn_fail(&a);
			*failskip = (size_t)a - (size_t)failskip - 1;
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
	unsigned char *a, *retfail, *generic64bit, *doloadstore,
	    *okret0, *okret1, *okret2, *skip;
	int ofs, alignment, load=0, unaligned=0;

	/*  TODO: Not yet:  */
	if (instruction_type == HI6_LQ_MDMX || instruction_type == HI6_SQ)
		return 0;

	/*  TODO: Not yet:  */
	if (bigendian)
		return 0;

	switch (instruction_type) {
	case HI6_LQ_MDMX:
	case HI6_LDL:
	case HI6_LDR:
	case HI6_LD:
	case HI6_LWU:
	case HI6_LWL:
	case HI6_LWR:
	case HI6_LW:
	case HI6_LHU:
	case HI6_LH:
	case HI6_LBU:
	case HI6_LB:
		load = 1;
		if (rt == 0)
			return 0;
	}

	switch (instruction_type) {
	case HI6_LWL:
	case HI6_LWR:
	case HI6_LDL:
	case HI6_LDR:
	case HI6_SWL:
	case HI6_SWR:
	case HI6_SDL:
	case HI6_SDR:
		unaligned = 1;
	}

	a = *addrp;

	if (bintrans_32bit_only)
		load_into_eax_dont_care_about_edx(&a, &dummy_cpu.gpr[rs]);
	else
		load_into_eax_edx(&a, &dummy_cpu.gpr[rs]);

	if (imm & 0x8000) {
		/*  05 34 f2 ff ff          add    $0xfffff234,%eax  */
		/*  83 d2 ff                adc    $0xffffffff,%edx  */
		*a++ = 5;
		*a++ = imm; *a++ = imm >> 8; *a++ = 0xff; *a++ = 0xff;
		if (!bintrans_32bit_only) {
			*a++ = 0x83; *a++ = 0xd2; *a++ = 0xff;
		}
	} else {
		/*  05 34 12 00 00          add    $0x1234,%eax  */
		/*  83 d2 00                adc    $0x0,%edx  */
		*a++ = 5;
		*a++ = imm; *a++ = imm >> 8; *a++ = 0; *a++ = 0;
		if (!bintrans_32bit_only) {
			*a++ = 0x83; *a++ = 0xd2; *a++ = 0;
		}
	}

	alignment = 0;
	switch (instruction_type) {
	case HI6_LQ_MDMX:
	case HI6_SQ:
		alignment = 15;
		break;
	case HI6_LD:
	case HI6_LDL:
	case HI6_LDR:
	case HI6_SD:
	case HI6_SDL:
	case HI6_SDR:
		alignment = 7;
		break;
	case HI6_LW:
	case HI6_LWL:
	case HI6_LWR:
	case HI6_LWU:
	case HI6_SW:
	case HI6_SWL:
	case HI6_SWR:
		alignment = 3;
		break;
	case HI6_LH:
	case HI6_LHU:
	case HI6_SH:
		alignment = 1;
		break;
	}

	if (unaligned) {
		/*
		 *  Perform the actual load/store from an
		 *  aligned address.
		 *
		 *  83 e0 fc       and    $0xfffffffc,%eax
		 */
		*a++ = 0x83; *a++ = 0xe0; *a++ = 0xff - alignment;
	} else if (alignment > 0) {
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


	/*  Here, edx:eax = vaddr  */

	if (bintrans_32bit_only) {
		/*  Call the quick lookup routine:  */
		ofs = (size_t)bintrans_i386_loadstore_32bit;
		ofs = ofs - ((size_t)a + 5);
		*a++ = 0xe8; *a++ = ofs; *a++ = ofs >> 8;
		    *a++ = ofs >> 16; *a++ = ofs >> 24;

		/*
		 *  ecx = NULL? Then return with failure.
		 *
		 *  83 f9 00                cmp    $0x0,%ecx
		 *  75 01                   jne    <okzzz>
		 */
		*a++ = 0x83; *a++ = 0xf9; *a++ = 0x00;
		*a++ = 0x75; retfail = a; *a++ = 0x00;
		bintrans_write_chunkreturn_fail(&a);		/*  ret (and fail)  */
		*retfail = (size_t)a - (size_t)retfail - 1;

		/*
		 *  If the lowest bit is zero, and we're storing, then fail.
		 */
		if (!load) {
			/*
			 *  f7 c1 01 00 00 00       test   $0x1,%ecx
			 *  75 01                   jne    <ok>
			 */
			*a++ = 0xf7; *a++ = 0xc1; *a++ = 1; *a++ = 0; *a++ = 0; *a++ = 0;
			*a++ = 0x75; retfail = a; *a++ = 0x00;
			bintrans_write_chunkreturn_fail(&a);		/*  ret (and fail)  */
			*retfail = (size_t)a - (size_t)retfail - 1;
		}

		/*
		 *  eax = offset within page = vaddr & 0xfff
		 *
		 *  25 ff 0f 00 00       and    $0xfff,%eax
		 */
		*a++ = 0x25; *a++ = 0xff; *a++ = 0x0f; *a++ = 0; *a++ = 0;

		/*
		 *  ecx = host address   ( = host page + offset)
		 *
		 *  83 e1 fe                and    $0xfffffffe,%ecx	clear the lowest bit
		 *  01 c1                   add    %eax,%ecx
		 */
		*a++ = 0x83; *a++ = 0xe1; *a++ = 0xfe;
		*a++ = 0x01; *a++ = 0xc1;
	} else {
		/*
		 *  If the load/store address has the top 32 bits set to
		 *  0x00000000 or 0xffffffff, then we can use the 32-bit
		 *  lookup tables:
		 *

TODO: top 33 bits!!!!!!!

		 *  83 fa 00                cmp    $0x0,%edx
		 *  74 05                   je     <ok32>
		 *  83 fa ff                cmp    $0xffffffff,%edx
		 *  75 01                   jne    <not32>
		 */
		*a++ = 0x83; *a++ = 0xfa; *a++ = 0x00;
		*a++ = 0x74; *a++ = 0x05;
		*a++ = 0x83; *a++ = 0xfa; *a++ = 0xff;
		*a++ = 0x75; generic64bit = a; *a++ = 0x01;

		/*  Call the quick lookup routine:  */
		ofs = (size_t)bintrans_i386_loadstore_32bit;
		ofs = ofs - ((size_t)a + 5);
		*a++ = 0xe8; *a++ = ofs; *a++ = ofs >> 8;
		    *a++ = ofs >> 16; *a++ = ofs >> 24;

		/*
		 *  ecx = NULL? Then return with failure.
		 *
		 *  83 f9 00                cmp    $0x0,%ecx
		 *  75 01                   jne    <okzzz>
		 */
		*a++ = 0x83; *a++ = 0xf9; *a++ = 0x00;
		*a++ = 0x75; retfail = a; *a++ = 0x00;
		bintrans_write_chunkreturn_fail(&a);		/*  ret (and fail)  */
		*retfail = (size_t)a - (size_t)retfail - 1;

		/*
		 *  If the lowest bit is zero, and we're storing, then fail.
		 */
		if (!load) {
			/*
			 *  f7 c1 01 00 00 00       test   $0x1,%ecx
			 *  75 01                   jne    <ok>
			 */
			*a++ = 0xf7; *a++ = 0xc1; *a++ = 1; *a++ = 0; *a++ = 0; *a++ = 0;
			*a++ = 0x75; retfail = a; *a++ = 0x00;
			bintrans_write_chunkreturn_fail(&a);		/*  ret (and fail)  */
			*retfail = (size_t)a - (size_t)retfail - 1;
		}

		/*
		 *  eax = offset within page = vaddr & 0xfff
		 *
		 *  25 ff 0f 00 00       and    $0xfff,%eax
		 */
		*a++ = 0x25; *a++ = 0xff; *a++ = 0x0f; *a++ = 0; *a++ = 0;

		/*
		 *  ecx = host address   ( = host page + offset)
		 *
		 *  83 e1 fe                and    $0xfffffffe,%ecx	clear the lowest bit
		 *  01 c1                   add    %eax,%ecx
		 */
		*a++ = 0x83; *a++ = 0xe1; *a++ = 0xfe;
		*a++ = 0x01; *a++ = 0xc1;

		*a++ = 0xeb; doloadstore = a; *a++ = 0x01;


		/*  TODO: The stuff above is so similar to the pure 32-bit
		    case that it should be factored out.  */


		*generic64bit = (size_t)a - (size_t)generic64bit - 1;

		/*
		 *  64-bit generic case:
		 */

		/*  push writeflag  */
		*a++ = 0x6a; *a++ = load? 0 : 1;

		/*  push vaddr (edx:eax)  */
		*a++ = 0x52; *a++ = 0x50;

		/*  push cpu (esi)  */
		*a++ = 0x56;

		/*  eax = points to the right function  */
		ofs = ((size_t)&dummy_cpu.fast_vaddr_to_hostaddr) - (size_t)&dummy_cpu;
		*a++ = 0x8b; *a++ = 0x86;
		*a++ = ofs; *a++ = ofs >> 8; *a++ = ofs >> 16; *a++ = ofs >> 24;

		/*  ff d0                   call   *%eax  */
		*a++ = 0xff; *a++ = 0xd0;

		/*  83 c4 08                add    $0x10,%esp  */
		*a++ = 0x83; *a++ = 0xc4; *a++ = 0x10;

		/*  If eax is NULL, then return.  */
		/*  83 f8 00                cmp    $0x0,%eax  */
		/*  75 01                   jne    1cd <okjump>  */
		/*  c3                      ret    */
		*a++ = 0x83; *a++ = 0xf8; *a++ = 0x00;
		*a++ = 0x75; retfail = a; *a++ = 0x00;
		bintrans_write_chunkreturn_fail(&a);            /*  ret (and fail)  */
		*retfail = (size_t)a - (size_t)retfail - 1;  

		/*  89 c1                   mov    %eax,%ecx  */
		*a++ = 0x89; *a++ = 0xc1;

		*doloadstore = (size_t)a - (size_t)doloadstore - 1;
	}


	if (!load) {
		if (alignment >= 7)
			load_into_eax_edx(&a, &dummy_cpu.gpr[rt]);
		else
			load_into_eax_dont_care_about_edx(&a, &dummy_cpu.gpr[rt]);
	}

	switch (instruction_type) {
	case HI6_LD:
		/*  8b 01                   mov    (%ecx),%eax  */
		/*  8b 51 04                mov    0x4(%ecx),%edx  */
		*a++ = 0x8b; *a++ = 0x01;
		*a++ = 0x8b; *a++ = 0x51; *a++ = 0x04;
		break;
	case HI6_LWU:
		/*  8b 01                   mov    (%ecx),%eax  */
		/*  31 d2                   xor    %edx,%edx  */
		*a++ = 0x8b; *a++ = 0x01;
		*a++ = 0x31; *a++ = 0xd2;
		break;
	case HI6_LW:
		/*  8b 01                   mov    (%ecx),%eax  */
		/*  99                      cltd   */
		*a++ = 0x8b; *a++ = 0x01;
		*a++ = 0x99;
		break;
	case HI6_LHU:
		/*  31 c0                   xor    %eax,%eax  */
		/*  66 8b 01                mov    (%ecx),%ax  */
		/*  99                      cltd   */
		*a++ = 0x31; *a++ = 0xc0;
		*a++ = 0x66; *a++ = 0x8b; *a++ = 0x01;
		*a++ = 0x99;
		break;
	case HI6_LH:
		/*  66 8b 01                mov    (%ecx),%ax  */
		/*  98                      cwtl   */
		/*  99                      cltd   */
		*a++ = 0x66; *a++ = 0x8b; *a++ = 0x01;
		*a++ = 0x98;
		*a++ = 0x99;
		break;
	case HI6_LBU:
		/*  31 c0                   xor    %eax,%eax  */
		/*  8a 01                   mov    (%ecx),%al  */
		/*  99                      cltd   */
		*a++ = 0x31; *a++ = 0xc0;
		*a++ = 0x8a; *a++ = 0x01;
		*a++ = 0x99;
		break;
	case HI6_LB:
		/*  8a 01                   mov    (%ecx),%al  */
		/*  66 98                   cbtw   */
		/*  98                      cwtl   */
		/*  99                      cltd   */
		*a++ = 0x8a; *a++ = 0x01;
		*a++ = 0x66; *a++ = 0x98;
		*a++ = 0x98;
		*a++ = 0x99;
		break;

	case HI6_LWL:
		load_into_eax_dont_care_about_edx(&a, &dummy_cpu.gpr[rs]);
		/*  05 34 f2 ff ff          add    $0xfffff234,%eax  */
		*a++ = 5;
		*a++ = imm; *a++ = imm >> 8; *a++ = 0xff; *a++ = 0xff;
		/*  83 e0 03                and    $0x03,%eax  */
		*a++ = 0x83; *a++ = 0xe0; *a++ = alignment;
		/*  89 c3                   mov    %eax,%ebx  */
		*a++ = 0x89; *a++ = 0xc3;

		load_into_eax_dont_care_about_edx(&a, &dummy_cpu.gpr[rt]);

		/*  ALIGNED LOAD:  */
		/*  8b 11                   mov    (%ecx),%edx  */
		*a++ = 0x8b; *a++ = 0x11;

		/*
		 *  CASE 0:
		 *	memory = 0x12 0x34 0x56 0x78
		 *	register after lwl: 0x12 0x.. 0x.. 0x..
		 */
		/*  83 fb 00                cmp    $0x0,%ebx  */
		/*  75 01                   jne    <skip>  */
		*a++ = 0x83; *a++ = 0xfb; *a++ = 0x00;
		*a++ = 0x75; skip = a; *a++ = 0x01;

		/*  c1 e2 18                shl    $0x18,%edx  */
		/*  25 ff ff ff 00          and    $0xffffff,%eax  */
		/*  09 d0                   or     %edx,%eax  */
		*a++ = 0xc1; *a++ = 0xe2; *a++ = 0x18;
		*a++ = 0x25; *a++ = 0xff; *a++ = 0xff; *a++ = 0xff; *a++ = 0x00;
		*a++ = 0x09; *a++ = 0xd0;

		/*  eb 00                   jmp    <okret>  */
		*a++ = 0xeb; okret0 = a; *a++ = 0;

		*skip = (size_t)a - (size_t)skip - 1;

		/*
		 *  CASE 1:
		 *	memory = 0x12 0x34 0x56 0x78
		 *	register after lwl: 0x34 0x12 0x.. 0x..
		 */
		/*  83 fb 01                cmp    $0x1,%ebx  */
		/*  75 01                   jne    <skip>  */
		*a++ = 0x83; *a++ = 0xfb; *a++ = 0x01;
		*a++ = 0x75; skip = a; *a++ = 0x01;

		/*  c1 e2 10                shl    $0x10,%edx  */
		/*  25 ff ff 00 00          and    $0xffff,%eax  */
		/*  09 d0                   or     %edx,%eax  */
		*a++ = 0xc1; *a++ = 0xe2; *a++ = 0x10;
		*a++ = 0x25; *a++ = 0xff; *a++ = 0xff; *a++ = 0x00; *a++ = 0x00;
		*a++ = 0x09; *a++ = 0xd0;

		/*  eb 00                   jmp    <okret>  */
		*a++ = 0xeb; okret1 = a; *a++ = 0;

		*skip = (size_t)a - (size_t)skip - 1;

		/*
		 *  CASE 2:
		 *	memory = 0x12 0x34 0x56 0x78
		 *	register after lwl: 0x56 0x34 0x12 0x..
		 */
		/*  83 fb 02                cmp    $0x2,%ebx  */
		/*  75 01                   jne    <skip>  */
		*a++ = 0x83; *a++ = 0xfb; *a++ = 0x02;
		*a++ = 0x75; skip = a; *a++ = 0x01;

		/*  c1 e2 08                shl    $0x08,%edx  */
		/*  25 ff 00 00 00          and    $0xff,%eax  */
		/*  09 d0                   or     %edx,%eax  */
		*a++ = 0xc1; *a++ = 0xe2; *a++ = 0x08;
		*a++ = 0x25; *a++ = 0xff; *a++ = 0x00; *a++ = 0x00; *a++ = 0x00;
		*a++ = 0x09; *a++ = 0xd0;

		/*  eb 00                   jmp    <okret>  */
		*a++ = 0xeb; okret2 = a; *a++ = 0;

		*skip = (size_t)a - (size_t)skip - 1;

		/*
		 *  CASE 3:
		 *	memory = 0x12 0x34 0x56 0x78
		 *	register after lwl: 0x78 0x56 0x34 0x12
		 */
		/*  89 d0                   mov    %edx,%eax  */
		*a++ = 0x89; *a++ = 0xd0;

		/*  okret:  */
		*okret0 = (size_t)a - (size_t)okret0 - 1;
		*okret1 = (size_t)a - (size_t)okret1 - 1;
		*okret2 = (size_t)a - (size_t)okret2 - 1;

		/*  99                      cltd   */
		*a++ = 0x99;
		break;

	case HI6_LWR:
		load_into_eax_dont_care_about_edx(&a, &dummy_cpu.gpr[rs]);
		/*  05 34 f2 ff ff          add    $0xfffff234,%eax  */
		*a++ = 5;
		*a++ = imm; *a++ = imm >> 8; *a++ = 0xff; *a++ = 0xff;
		/*  83 e0 03                and    $0x03,%eax  */
		*a++ = 0x83; *a++ = 0xe0; *a++ = alignment;
		/*  89 c3                   mov    %eax,%ebx  */
		*a++ = 0x89; *a++ = 0xc3;

		load_into_eax_dont_care_about_edx(&a, &dummy_cpu.gpr[rt]);

		/*  ALIGNED LOAD:  */
		/*  8b 11                   mov    (%ecx),%edx  */
		*a++ = 0x8b; *a++ = 0x11;

		/*
		 *  CASE 0:
		 *	memory = 0x12 0x34 0x56 0x78
		 *	register after lwr: 0x78 0x56 0x34 0x12
		 */
		/*  83 fb 00                cmp    $0x0,%ebx  */
		/*  75 01                   jne    <skip>  */
		*a++ = 0x83; *a++ = 0xfb; *a++ = 0x00;
		*a++ = 0x75; skip = a; *a++ = 0x01;

		/*  89 d0                   mov    %edx,%eax  */
		*a++ = 0x89; *a++ = 0xd0;

		/*  eb 00                   jmp    <okret>  */
		*a++ = 0xeb; okret0 = a; *a++ = 0;

		*skip = (size_t)a - (size_t)skip - 1;

		/*
		 *  CASE 1:
		 *	memory = 0x12 0x34 0x56 0x78
		 *	register after lwr: 0x.. 0x78 0x56 0x34
		 */
		/*  83 fb 01                cmp    $0x1,%ebx  */
		/*  75 01                   jne    <skip>  */
		*a++ = 0x83; *a++ = 0xfb; *a++ = 0x01;
		*a++ = 0x75; skip = a; *a++ = 0x01;

		/*  c1 ea 08                shr    $0x8,%edx  */
		/*  25 00 00 00 ff          and    $0xff000000,%eax  */
		/*  09 d0                   or     %edx,%eax  */
		*a++ = 0xc1; *a++ = 0xea; *a++ = 0x08;
		*a++ = 0x25; *a++ = 0x00; *a++ = 0x00; *a++ = 0x00; *a++ = 0xff;
		*a++ = 0x09; *a++ = 0xd0;

		/*  eb 00                   jmp    <okret>  */
		*a++ = 0xeb; okret1 = a; *a++ = 0;

		*skip = (size_t)a - (size_t)skip - 1;

		/*
		 *  CASE 2:
		 *	memory = 0x12 0x34 0x56 0x78
		 *	register after lwr: 0x.. 0x.. 0x78 0x56
		 */
		/*  83 fb 02                cmp    $0x2,%ebx  */
		/*  75 01                   jne    <skip>  */
		*a++ = 0x83; *a++ = 0xfb; *a++ = 0x02;
		*a++ = 0x75; skip = a; *a++ = 0x01;

		/*  c1 ea 10                shr    $0x10,%edx  */
		/*  25 00 00 ff ff          and    $0xffff0000,%eax  */
		/*  09 d0                   or     %edx,%eax  */
		*a++ = 0xc1; *a++ = 0xea; *a++ = 0x10;
		*a++ = 0x25; *a++ = 0x00; *a++ = 0x00; *a++ = 0xff; *a++ = 0xff;
		*a++ = 0x09; *a++ = 0xd0;

		/*  eb 00                   jmp    <okret>  */
		*a++ = 0xeb; okret2 = a; *a++ = 0;

		*skip = (size_t)a - (size_t)skip - 1;

		/*
		 *  CASE 3:
		 *	memory = 0x12 0x34 0x56 0x78
		 *	register after lwr: 0x.. 0x.. 0x.. 0x78
		 */
		/*  c1 ea 18                shr    $0x18,%edx  */
		/*  25 00 ff ff ff          and    $0xffffff00,%eax  */
		/*  09 d0                   or     %edx,%eax  */
		*a++ = 0xc1; *a++ = 0xea; *a++ = 0x18;
		*a++ = 0x25; *a++ = 0x00; *a++ = 0xff; *a++ = 0xff; *a++ = 0xff;
		*a++ = 0x09; *a++ = 0xd0;

		/*  okret:  */
		*okret0 = (size_t)a - (size_t)okret0 - 1;
		*okret1 = (size_t)a - (size_t)okret1 - 1;
		*okret2 = (size_t)a - (size_t)okret2 - 1;

		/*  99                      cltd   */
		*a++ = 0x99;
		break;

	case HI6_SD:
		/*  89 01                   mov    %eax,(%ecx)  */
		/*  89 51 04                mov    %edx,0x4(%ecx)  */
		*a++ = 0x89; *a++ = 0x01;
		*a++ = 0x89; *a++ = 0x51; *a++ = 0x04;
		break;
	case HI6_SW:
		/*  89 01                   mov    %eax,(%ecx)  */
		*a++ = 0x89; *a++ = 0x01;
		break;
	case HI6_SH:
		/*  66 89 01                mov    %ax,(%ecx)  */
		*a++ = 0x66; *a++ = 0x89; *a++ = 0x01;
		break;
	case HI6_SB:
		/*  88 01                   mov    %al,(%ecx)  */
		*a++ = 0x88; *a++ = 0x01;
		break;

	case HI6_SWL:
		load_into_eax_dont_care_about_edx(&a, &dummy_cpu.gpr[rs]);
		/*  05 34 f2 ff ff          add    $0xfffff234,%eax  */
		*a++ = 5;
		*a++ = imm; *a++ = imm >> 8; *a++ = 0xff; *a++ = 0xff;
		/*  83 e0 03                and    $0x03,%eax  */
		*a++ = 0x83; *a++ = 0xe0; *a++ = alignment;
		/*  89 c3                   mov    %eax,%ebx  */
		*a++ = 0x89; *a++ = 0xc3;

		load_into_eax_dont_care_about_edx(&a, &dummy_cpu.gpr[rt]);

		/*  ALIGNED LOAD:  */
		/*  8b 11                   mov    (%ecx),%edx  */
		*a++ = 0x8b; *a++ = 0x11;

		/*
		 *  CASE 0:
		 *	memory (edx):	0x12 0x34 0x56 0x78
		 *	register (eax):	0x89abcdef
		 *	mem after swl:	0x89 0x.. 0x.. 0x..
		 */
		/*  83 fb 00                cmp    $0x0,%ebx  */
		/*  75 01                   jne    <skip>  */
		*a++ = 0x83; *a++ = 0xfb; *a++ = 0x00;
		*a++ = 0x75; skip = a; *a++ = 0x01;

		/*  81 e2 00 ff ff ff       and    $0xffffff00,%edx  */
		/*  c1 e8 18                shr    $0x18,%eax  */
		/*  09 d0                   or     %edx,%eax  */
		*a++ = 0x81; *a++ = 0xe2; *a++ = 0x00; *a++ = 0xff; *a++ = 0xff; *a++ = 0xff;
		*a++ = 0xc1; *a++ = 0xe8; *a++ = 0x18;
		*a++ = 0x09; *a++ = 0xd0;

		/*  eb 00                   jmp    <okret>  */
		*a++ = 0xeb; okret0 = a; *a++ = 0;

		*skip = (size_t)a - (size_t)skip - 1;

		/*
		 *  CASE 1:
		 *	memory (edx):	0x12 0x34 0x56 0x78
		 *	register (eax):	0x89abcdef
		 *	mem after swl:	0xab 0x89 0x.. 0x..
		 */
		/*  83 fb 01                cmp    $0x1,%ebx  */
		/*  75 01                   jne    <skip>  */
		*a++ = 0x83; *a++ = 0xfb; *a++ = 0x01;
		*a++ = 0x75; skip = a; *a++ = 0x01;

		/*  81 e2 00 00 ff ff       and    $0xffff0000,%edx  */
		/*  c1 e8 10                shr    $0x10,%eax  */
		/*  09 d0                   or     %edx,%eax  */
		*a++ = 0x81; *a++ = 0xe2; *a++ = 0x00; *a++ = 0x00; *a++ = 0xff; *a++ = 0xff;
		*a++ = 0xc1; *a++ = 0xe8; *a++ = 0x10;
		*a++ = 0x09; *a++ = 0xd0;

		/*  eb 00                   jmp    <okret>  */
		*a++ = 0xeb; okret1 = a; *a++ = 0;

		*skip = (size_t)a - (size_t)skip - 1;

		/*
		 *  CASE 2:
		 *	memory (edx):	0x12 0x34 0x56 0x78
		 *	register (eax):	0x89abcdef
		 *	mem after swl:	0xcd 0xab 0x89 0x..
		 */
		/*  83 fb 02                cmp    $0x2,%ebx  */
		/*  75 01                   jne    <skip>  */
		*a++ = 0x83; *a++ = 0xfb; *a++ = 0x02;
		*a++ = 0x75; skip = a; *a++ = 0x01;

		/*  81 e2 00 00 00 ff       and    $0xff000000,%edx  */
		/*  c1 e8 08                shr    $0x08,%eax  */
		/*  09 d0                   or     %edx,%eax  */
		*a++ = 0x81; *a++ = 0xe2; *a++ = 0x00; *a++ = 0x00; *a++ = 0x00; *a++ = 0xff;
		*a++ = 0xc1; *a++ = 0xe8; *a++ = 0x08;
		*a++ = 0x09; *a++ = 0xd0;

		/*  eb 00                   jmp    <okret>  */
		*a++ = 0xeb; okret2 = a; *a++ = 0;

		*skip = (size_t)a - (size_t)skip - 1;

		/*
		 *  CASE 3:
		 *	memory (edx):	0x12 0x34 0x56 0x78
		 *	register (eax):	0x89abcdef
		 *	mem after swl:	0xef 0xcd 0xab 0x89
		 */
		/*  eax = eax :-)  */

		/*  okret:  */
		*okret0 = (size_t)a - (size_t)okret0 - 1;
		*okret1 = (size_t)a - (size_t)okret1 - 1;
		*okret2 = (size_t)a - (size_t)okret2 - 1;

		/*  Store back to memory:  */
		/*  89 01                   mov    %eax,(%ecx)  */
		*a++ = 0x89; *a++ = 0x01;
		break;

	case HI6_SWR:
		load_into_eax_dont_care_about_edx(&a, &dummy_cpu.gpr[rs]);
		/*  05 34 f2 ff ff          add    $0xfffff234,%eax  */
		*a++ = 5;
		*a++ = imm; *a++ = imm >> 8; *a++ = 0xff; *a++ = 0xff;
		/*  83 e0 03                and    $0x03,%eax  */
		*a++ = 0x83; *a++ = 0xe0; *a++ = alignment;
		/*  89 c3                   mov    %eax,%ebx  */
		*a++ = 0x89; *a++ = 0xc3;

		load_into_eax_dont_care_about_edx(&a, &dummy_cpu.gpr[rt]);

		/*  ALIGNED LOAD:  */
		/*  8b 11                   mov    (%ecx),%edx  */
		*a++ = 0x8b; *a++ = 0x11;

		/*
		 *  CASE 0:
		 *	memory (edx):	0x12 0x34 0x56 0x78
		 *	register (eax):	0x89abcdef
		 *	mem after swr:	0xef 0xcd 0xab 0x89
		 */
		/*  83 fb 00                cmp    $0x0,%ebx  */
		/*  75 01                   jne    <skip>  */
		*a++ = 0x83; *a++ = 0xfb; *a++ = 0x00;
		*a++ = 0x75; skip = a; *a++ = 0x01;

		/*  eax = eax, so do nothing  */

		/*  eb 00                   jmp    <okret>  */
		*a++ = 0xeb; okret0 = a; *a++ = 0;

		*skip = (size_t)a - (size_t)skip - 1;

		/*
		 *  CASE 1:
		 *	memory (edx):	0x12 0x34 0x56 0x78
		 *	register (eax):	0x89abcdef
		 *	mem after swr:	0x12 0xef 0xcd 0xab
		 */
		/*  83 fb 01                cmp    $0x1,%ebx  */
		/*  75 01                   jne    <skip>  */
		*a++ = 0x83; *a++ = 0xfb; *a++ = 0x01;
		*a++ = 0x75; skip = a; *a++ = 0x01;

		/*  81 e2 ff 00 00 00       and    $0x000000ff,%edx  */
		/*  c1 e0 08                shl    $0x08,%eax  */
		/*  09 d0                   or     %edx,%eax  */
		*a++ = 0x81; *a++ = 0xe2; *a++ = 0xff; *a++ = 0x00; *a++ = 0x00; *a++ = 0x00;
		*a++ = 0xc1; *a++ = 0xe0; *a++ = 0x08;
		*a++ = 0x09; *a++ = 0xd0;

		/*  eb 00                   jmp    <okret>  */
		*a++ = 0xeb; okret1 = a; *a++ = 0;

		*skip = (size_t)a - (size_t)skip - 1;

		/*
		 *  CASE 2:
		 *	memory (edx):	0x12 0x34 0x56 0x78
		 *	register (eax):	0x89abcdef
		 *	mem after swr:	0x12 0x34 0xef 0xcd
		 */
		/*  83 fb 02                cmp    $0x2,%ebx  */
		/*  75 01                   jne    <skip>  */
		*a++ = 0x83; *a++ = 0xfb; *a++ = 0x02;
		*a++ = 0x75; skip = a; *a++ = 0x01;

		/*  81 e2 ff ff 00 00       and    $0x0000ffff,%edx  */
		/*  c1 e0 10                shl    $0x10,%eax  */
		/*  09 d0                   or     %edx,%eax  */
		*a++ = 0x81; *a++ = 0xe2; *a++ = 0xff; *a++ = 0xff; *a++ = 0x00; *a++ = 0x00;
		*a++ = 0xc1; *a++ = 0xe0; *a++ = 0x10;
		*a++ = 0x09; *a++ = 0xd0;

		/*  eb 00                   jmp    <okret>  */
		*a++ = 0xeb; okret2 = a; *a++ = 0;

		*skip = (size_t)a - (size_t)skip - 1;

		/*
		 *  CASE 3:
		 *	memory (edx):	0x12 0x34 0x56 0x78
		 *	register (eax):	0x89abcdef
		 *	mem after swr:	0x12 0x34 0x56 0xef
		 */
		/*  81 e2 ff ff ff 00       and    $0x00ffffff,%edx  */
		/*  c1 e0 18                shl    $0x18,%eax  */
		/*  09 d0                   or     %edx,%eax  */
		*a++ = 0x81; *a++ = 0xe2; *a++ = 0xff; *a++ = 0xff; *a++ = 0xff; *a++ = 0x00;
		*a++ = 0xc1; *a++ = 0xe0; *a++ = 0x18;
		*a++ = 0x09; *a++ = 0xd0;


		/*  okret:  */
		*okret0 = (size_t)a - (size_t)okret0 - 1;
		*okret1 = (size_t)a - (size_t)okret1 - 1;
		*okret2 = (size_t)a - (size_t)okret2 - 1;

		/*  Store back to memory:  */
		/*  89 01                   mov    %eax,(%ecx)  */
		*a++ = 0x89; *a++ = 0x01;
		break;

	default:
		bintrans_write_chunkreturn_fail(&a);		/*  ret (and fail)  */
	}

	if (load && rt != 0)
		store_eax_edx(&a, &dummy_cpu.gpr[rt]);

	*addrp = a;
	bintrans_write_pc_inc(addrp);
	return 1;
}


/*
 *  bintrans_write_instruction__tlb_rfe_etc():
 */
static int bintrans_write_instruction__tlb_rfe_etc(unsigned char **addrp,
	int itype)
{
	unsigned char *a;
	int ofs = 0;	/*  avoid a compiler warning  */

	switch (itype) {
	case TLB_TLBP:
	case TLB_TLBR:
	case TLB_TLBWR:
	case TLB_TLBWI:
	case TLB_RFE:
	case TLB_ERET:
	case TLB_SYSCALL:
	case TLB_BREAK:
		break;
	default:
		return 0;
	}

	a = *addrp;

	/*  Put back PC into the cpu struct, both as pc and pc_last  */
	*a++ = 0x89; *a++ = 0xbe; *a++ = ofs_pc&255;
	*a++ = (ofs_pc>>8)&255; *a++ = (ofs_pc>>16)&255;
	*a++ = (ofs_pc>>24)&255;	/*  mov    %edi,pc(%esi)  */
	*a++ = 0x89; *a++ = 0xbe; *a++ = ofs_pc_last&255;
	*a++ = (ofs_pc_last>>8)&255; *a++ = (ofs_pc_last>>16)&255;
	*a++ = (ofs_pc_last>>24)&255;	/*  mov    %edi,pc_last(%esi)  */

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
	case TLB_SYSCALL:
	case TLB_BREAK:
		/*  push randomflag  */
		*a++ = 0x6a; *a++ = (itype == TLB_BREAK? EXCEPTION_BP : EXCEPTION_SYS);
		ofs = ((size_t)&dummy_cpu.bintrans_simple_exception) - (size_t)&dummy_cpu;
		break;
	case TLB_RFE:
		ofs = ((size_t)&dummy_cpu.bintrans_fast_rfe) - (size_t)&dummy_cpu;
		break;
	case TLB_ERET:
		ofs = ((size_t)&dummy_cpu.bintrans_fast_eret) - (size_t)&dummy_cpu;
		break;
	}

	/*  push cpu (esi)  */
	*a++ = 0x56;

	/*  eax = points to the right function  */
	*a++ = 0x8b; *a++ = 0x86;
	*a++ = ofs; *a++ = ofs >> 8; *a++ = ofs >> 16; *a++ = ofs >> 24;

	/*  ff d0                   call   *%eax  */
	*a++ = 0xff; *a++ = 0xd0;

	switch (itype) {
	case TLB_RFE:
	case TLB_ERET:
		/*  83 c4 04                add    $4,%esp  */
		*a++ = 0x83; *a++ = 0xc4; *a++ = 4;
		break;
	default:
		/*  83 c4 08                add    $8,%esp  */
		*a++ = 0x83; *a++ = 0xc4; *a++ = 8;
		break;
	}

	/*  Load PC from the cpu struct.  */
	*a++ = 0x8b; *a++ = 0xbe; *a++ = ofs_pc&255;
	*a++ = (ofs_pc>>8)&255; *a++ = (ofs_pc>>16)&255;
	*a++ = (ofs_pc>>24)&255;	/*  mov    pc(%esi),%edi  */

	*addrp = a;

	switch (itype) {
	case TLB_ERET:
	case TLB_SYSCALL:
	case TLB_BREAK:
		break;
	default:
		bintrans_write_pc_inc(addrp);
	}

	return 1;
}

