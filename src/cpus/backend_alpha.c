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
 *  $Id: backend_alpha.c,v 1.1 2005-12-11 12:46:24 debug Exp $
 *
 *  Dyntrans backend for Alpha hosts.
 *
 *
 *  Registers are currently allocated as follows:
 *
 *	s0 = saved copy of original a0 (struct cpu *)
 *	s1 = saved copy of original a1 (xxx_instr_call *)
 *	s2 = saved copy of original t12 (ptr to the function itself)
 */

#include <stdio.h>
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
	0x2efe0000,			/*  unop  */
	0x47ff041f,			/*  nop   */
	0x2efe0000,			/*  unop  */
	0x47ff041f,			/*  nop   */
	0x2efe0000			/*  unop  */
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
 *	1) s0 = a0, s1 = a1, s2 = t12  (save the incoming args for later use)
 *	2) Save ra onto the stack.
 */
uint32_t alpha_prologue[5] = {
	0x40001400 | (ALPHA_A0  << 21) | ALPHA_S0,
	0x40001400 | (ALPHA_A1  << 21) | ALPHA_S1,
	0x40001400 | (ALPHA_T12 << 21) | ALPHA_S2,
	0x23deffe0,		/*  lda sp,-32(sp)  */
	0xb75e0000		/*  stq ra,0(sp)  */
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
 *	1) Load ra from the stack.
 *	2) Return.
 */
int dtb_function_epilogue(struct translation_context *ctx, size_t *sizep)
{
	uint32_t *q = (uint32_t *) ctx->p;

	*q++ = 0xa75e0000;	/*  ldq ra,0(sp)  */
	*q++ = 0x23de0020;	/*  lda sp,32(sp)  */

	*q++ = 0x6bfa8001;				/*  ret  */
	if ((((size_t)q) & 0x7) == 4)
		*q++ = 0x2efe0000;			/*  unop  */
	if ((((size_t)q) & 0xf) == 8) {
		*q++ = 0x47ff041f;			/*  nop  */
		if ((((size_t)q) & 0x7) == 4)
			*q++ = 0x2efe0000;		/*  unop  */
	}

	*sizep = ((size_t)q - (size_t)ctx->p);
	return 1;
}

