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
 *  $Id: cpu_arm_instr_sub_self.c,v 1.1 2005-06-30 20:41:28 debug Exp $
 */

#ifndef CPU_ARM_INSTR_SUB_SELF
#define	CPU_ARM_INSTR_SUB_SELF

#define	A__NAME		arm_instr_sub_self_r0_1
#define	A__Rn		0
#include "cpu_arm_instr_sub_self.c"
#undef	A__Rn
#undef	A__NAME

#define	A__NAME		arm_instr_sub_self_r1_1
#define	A__Rn		1
#include "cpu_arm_instr_sub_self.c"
#undef	A__Rn
#undef	A__NAME

#define	A__NAME		arm_instr_sub_self_r2_1
#define	A__Rn		2
#include "cpu_arm_instr_sub_self.c"
#undef	A__Rn
#undef	A__NAME

#define	A__NAME		arm_instr_sub_self_r3_1
#define	A__Rn		3
#include "cpu_arm_instr_sub_self.c"
#undef	A__Rn
#undef	A__NAME

#define	A__NAME		arm_instr_sub_self_r4_1
#define	A__Rn		4
#include "cpu_arm_instr_sub_self.c"
#undef	A__Rn
#undef	A__NAME

#define	A__NAME		arm_instr_sub_self_r5_1
#define	A__Rn		5
#include "cpu_arm_instr_sub_self.c"
#undef	A__Rn
#undef	A__NAME

#define	A__NAME		arm_instr_sub_self_r6_1
#define	A__Rn		6
#include "cpu_arm_instr_sub_self.c"
#undef	A__Rn
#undef	A__NAME

#define	A__NAME		arm_instr_sub_self_r7_1
#define	A__Rn		7
#include "cpu_arm_instr_sub_self.c"
#undef	A__Rn
#undef	A__NAME

#define	A__NAME		arm_instr_sub_self_r8_1
#define	A__Rn		8
#include "cpu_arm_instr_sub_self.c"
#undef	A__Rn
#undef	A__NAME

#define	A__NAME		arm_instr_sub_self_r9_1
#define	A__Rn		9
#include "cpu_arm_instr_sub_self.c"
#undef	A__Rn
#undef	A__NAME

#define	A__NAME		arm_instr_sub_self_r10_1
#define	A__Rn		10
#include "cpu_arm_instr_sub_self.c"
#undef	A__Rn
#undef	A__NAME

#define	A__NAME		arm_instr_sub_self_r11_1
#define	A__Rn		11
#include "cpu_arm_instr_sub_self.c"
#undef	A__Rn
#undef	A__NAME

#define	A__NAME		arm_instr_sub_self_r12_1
#define	A__Rn		12
#include "cpu_arm_instr_sub_self.c"
#undef	A__Rn
#undef	A__NAME

#define	A__NAME		arm_instr_sub_self_r13_1
#define	A__Rn		13
#include "cpu_arm_instr_sub_self.c"
#undef	A__Rn
#undef	A__NAME

#define	A__NAME		arm_instr_sub_self_r14_1
#define	A__Rn		14
#include "cpu_arm_instr_sub_self.c"
#undef	A__Rn
#undef	A__NAME


void (*arm_sub_self_1[15])(struct cpu *, struct arm_instr_call *) = {
	arm_instr_sub_self_r0_1,  arm_instr_sub_self_r1_1,
	arm_instr_sub_self_r2_1,  arm_instr_sub_self_r3_1,
	arm_instr_sub_self_r4_1,  arm_instr_sub_self_r5_1,
	arm_instr_sub_self_r6_1,  arm_instr_sub_self_r7_1,
	arm_instr_sub_self_r8_1,  arm_instr_sub_self_r9_1,
	arm_instr_sub_self_r10_1,  arm_instr_sub_self_r11_1,
	arm_instr_sub_self_r12_1,  arm_instr_sub_self_r13_1,
	arm_instr_sub_self_r14_1  };

#else

void A__NAME(struct cpu *cpu, struct arm_instr_call *ic)
{
	cpu->cd.arm.r[A__Rn] --;
}

#endif
