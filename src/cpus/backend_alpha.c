/*
 *  Copyright (C) 2005  Anders Gavare.  All rights reserved.
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
 *  $Id: backend_alpha.c,v 1.2 2005-12-16 21:44:42 debug Exp $
 *
 *  Dyntrans backend for Alpha hosts.
 *
 *
 *  Registers are currently allocated as follows:
 *
 *	s0 = saved copy of original a0 (struct cpu *)
 *	s1 = saved copy of original t12 (ptr to the function itself)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "misc.h"

#define ALPHA_T0                1   
#define ALPHA_T1                2
#define ALPHA_T2                3
#define ALPHA_T3                4
#define ALPHA_T4                5
#define ALPHA_T5                6
#define ALPHA_T6                7
#define ALPHA_T7                8
#define ALPHA_S0                9
#define ALPHA_S1                10
#define ALPHA_S2                11
#define ALPHA_S3                12
#define ALPHA_S4                13
#define ALPHA_S5                14
#define ALPHA_S6                15   
#define ALPHA_A0                16
#define ALPHA_A1                17
#define ALPHA_A2                18
#define ALPHA_A3                19
#define ALPHA_A4                20
#define ALPHA_A5                21
#define ALPHA_T8                22
#define ALPHA_T9                23
#define ALPHA_T10               24
#define ALPHA_T11               25
#define	ALPHA_RA		26
#define	ALPHA_T12		27
#define ALPHA_ZERO              31


uint32_t alpha_imb[8] = {
	0x00000086,			/*  imb   */
	0x6bfa8001,			/*  ret   */
	0x47ff041f,			/*  nop   */
	0x2ffe0000,			/*  unop  */
	0x47ff041f,			/*  nop   */
	0x2ffe0000,			/*  unop  */
	0x47ff041f,			/*  nop   */
	0x2ffe0000			/*  unop  */
};


/*
 *  dtb_host_cacheinvalidate():
 *
 *  Invalidate the host's instruction cache. On Alpha, this is done by
 *  executing an imb instruction. (This invalidates everything, there is no way
 *  to specify which parts of the cache to affect.)
 *
 *  NOTE:  A simple  asm("imb");  would be enough here, but not all compilers
 *  have such simple constructs, so an entire function has to be written as
 *  alpha_imb[] above.
 */
void dtb_host_cacheinvalidate(void *p, size_t len)
{ 
        /*  Long form of ``asm("imb");''  */
        void (*f)(void) = (void *)&alpha_imb[0];
        f();
}


/*
 *  dtb_function_prologue():
 *
 *  Incoming register values:
 *	a0 = struct cpu *cpu;
 *	a1 = struct xxx_instr_call *ic;
 *	t12 = pointer to the function start itself
 *
 *  The prologue code does the following:
 *	1) Save ra, s0, and s1 onto the stack.
 *	1) s0 = a0, s1 = t12  (save the incoming args for later use)
 */
uint32_t alpha_prologue[6] = {
	0x23deffe0,		/*  lda sp,-32(sp)  */
	0xb75e0000,		/*  stq ra,0(sp)  */
	0xb53e0008,		/*  stq s0,8(sp)  */
	0xb55e0010,		/*  stq s1,16(sp)  */
	0x40001400 | (ALPHA_A0  << 21) | ALPHA_S0,
	0x40001400 | (ALPHA_T12 << 21) | ALPHA_S1
};
int dtb_function_prologue(struct translation_context *ctx, size_t *sizep)
{
	memcpy((uint32_t *)ctx->p, alpha_prologue, sizeof(alpha_prologue));
	*sizep = sizeof(alpha_prologue);
	return 1;
}


/*
 *  dtb_function_epilogue():
 *
 *  The epilogue code does the following:
 *	1) Load ra, s0, and s1 from the stack.
 *	2) Return.
 */
int dtb_function_epilogue(struct translation_context *ctx, size_t *sizep)
{
	uint32_t *q = (uint32_t *) ctx->p;

	*q++ = 0xa75e0000;	/*  ldq ra,0(sp)  */
	*q++ = 0xa53e0008;	/*  lda s0,8(sp)  */
	*q++ = 0xa55e0010;	/*  lda s1,16(sp)  */
	*q++ = 0x23de0020;	/*  lda sp,32(sp)  */

	*q++ = 0x6bfa8001;				/*  ret  */
	if ((((size_t)q) & 0x7) == 4)
		*q++ = 0x2ffe0000;			/*  unop  */
	if ((((size_t)q) & 0xf) == 8) {
		*q++ = 0x47ff041f;			/*  nop  */
		if ((((size_t)q) & 0x7) == 4)
			*q++ = 0x2ffe0000;		/*  unop  */
	}

	*sizep = ((size_t)q - (size_t)ctx->p);
	return 1;
}


/*
 *  dtb_generate_fcall():
 *
 *  Generates a function call (to a C function).
 *
 *	(a0 already contains the cpu pointer)
 *	ldq  t12,ofs_a(s1)		Get the function address and
 *	ldq  a1,ofs_b(s1)		the xxx_instr_call pointer.
 *	jsr  ra,(t12),<nextinstr>	Call the function!
 *	mov  s0,a0			Restore a0.
 */
int dtb_generate_fcall(struct cpu *cpu, struct translation_context *ctx,
	size_t *sizep, size_t f, size_t instr_call_ptr)
{
	uint32_t *q = (uint32_t *) ctx->p;

	cpu_dtb_add_fixup(cpu, 0, q, f);
	*q++ = 0xa76a0000;	/*  ldq t12,ofs(s1)  */

	cpu_dtb_add_fixup(cpu, 0, q, instr_call_ptr);
	*q++ = 0xa62a0000;	/*  ldq a1,ofs(s1)  */

	*q++ = 0x6b5b4000;	/*  jsr  ra,(t12),nextinstr  */
	*q++ = 0x47e90410;	/*  mov  s0,a0  */

	*sizep = ((size_t)q - (size_t)ctx->p);
	return 1;
}


/*
 *  dtb_generate_ptr_inc():
 *
 *  Generates an increment of a pointer (for example cpu->cd.XXX.next_ic).
 *
 *  NOTE: The syntax for calling this function is something like:
 *
 *	dtb_generate_ptr_inc(cpu, &cpu->translation_context,
 *	    &cpu->cd.arm.next_ic);
 */
int dtb_generate_ptr_inc(struct cpu *cpu, struct translation_context *ctx,
	size_t *sizep, void *ptr, int amount)
{
	uint32_t *q = (uint32_t *) ctx->p;
	ssize_t ofs = (size_t)ptr - (size_t)(void *)cpu;

	if (ofs < 0 || ofs > 0x7fff) {
		fatal("dtb_generate_ptr_inc(): Huh? ofs=%p\n", (void *)ofs);
		exit(1);
	}

	*q++ = 0xa4500000 | ofs;	/*  ldq  t1, ofs(a0)     */
	*q++ = 0x20420000 | amount;	/*  lda  t1, amount(t1)  */
	*q++ = 0xb4500000 | ofs;	/*  stq  t1, ofs(a0)     */

	*sizep = ((size_t)q - (size_t)ctx->p);
	return 1;
}


