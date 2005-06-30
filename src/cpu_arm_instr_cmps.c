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
 *  $Id: cpu_arm_instr_cmps.c,v 1.1 2005-06-30 20:41:28 debug Exp $
 */

#ifndef CPU_ARM_INSTR_CMPS
#define	CPU_ARM_INSTR_CMPS

#define	A__NAME		arm_instr_cmps_r0_0
#define	A__Rn		0
#include "cpu_arm_instr_cmps.c"
#undef	A__Rn
#undef	A__NAME

#define	A__NAME		arm_instr_cmps_r1_0
#define	A__Rn		1
#include "cpu_arm_instr_cmps.c"
#undef	A__Rn
#undef	A__NAME

#define	A__NAME		arm_instr_cmps_r2_0
#define	A__Rn		2
#include "cpu_arm_instr_cmps.c"
#undef	A__Rn
#undef	A__NAME

#define	A__NAME		arm_instr_cmps_r3_0
#define	A__Rn		3
#include "cpu_arm_instr_cmps.c"
#undef	A__Rn
#undef	A__NAME

#define	A__NAME		arm_instr_cmps_r4_0
#define	A__Rn		4
#include "cpu_arm_instr_cmps.c"
#undef	A__Rn
#undef	A__NAME

#define	A__NAME		arm_instr_cmps_r5_0
#define	A__Rn		5
#include "cpu_arm_instr_cmps.c"
#undef	A__Rn
#undef	A__NAME

#define	A__NAME		arm_instr_cmps_r6_0
#define	A__Rn		6
#include "cpu_arm_instr_cmps.c"
#undef	A__Rn
#undef	A__NAME

#define	A__NAME		arm_instr_cmps_r7_0
#define	A__Rn		7
#include "cpu_arm_instr_cmps.c"
#undef	A__Rn
#undef	A__NAME

#define	A__NAME		arm_instr_cmps_r8_0
#define	A__Rn		8
#include "cpu_arm_instr_cmps.c"
#undef	A__Rn
#undef	A__NAME

#define	A__NAME		arm_instr_cmps_r9_0
#define	A__Rn		9
#include "cpu_arm_instr_cmps.c"
#undef	A__Rn
#undef	A__NAME

#define	A__NAME		arm_instr_cmps_r10_0
#define	A__Rn		10
#include "cpu_arm_instr_cmps.c"
#undef	A__Rn
#undef	A__NAME

#define	A__NAME		arm_instr_cmps_r11_0
#define	A__Rn		11
#include "cpu_arm_instr_cmps.c"
#undef	A__Rn
#undef	A__NAME

#define	A__NAME		arm_instr_cmps_r12_0
#define	A__Rn		12
#include "cpu_arm_instr_cmps.c"
#undef	A__Rn
#undef	A__NAME

#define	A__NAME		arm_instr_cmps_r13_0
#define	A__Rn		13
#include "cpu_arm_instr_cmps.c"
#undef	A__Rn
#undef	A__NAME

#define	A__NAME		arm_instr_cmps_r14_0
#define	A__Rn		14
#include "cpu_arm_instr_cmps.c"
#undef	A__Rn
#undef	A__NAME

void (*arm_cmps_0[15])(struct cpu *, struct arm_instr_call *) = {
	arm_instr_cmps_r0_0,	arm_instr_cmps_r1_0,
	arm_instr_cmps_r2_0,	arm_instr_cmps_r3_0,
	arm_instr_cmps_r4_0,	arm_instr_cmps_r5_0,
	arm_instr_cmps_r6_0,	arm_instr_cmps_r7_0,
	arm_instr_cmps_r8_0,	arm_instr_cmps_r9_0,
	arm_instr_cmps_r10_0,	arm_instr_cmps_r11_0,
	arm_instr_cmps_r12_0,	arm_instr_cmps_r13_0,
	arm_instr_cmps_r14_0  };

#else

void A__NAME(struct cpu *cpu, struct arm_instr_call *ic)
{
	uint32_t a = cpu->cd.arm.r[A__Rn];
	cpu->cd.arm.flags &=
	    ~(ARM_FLAG_Z | ARM_FLAG_N | ARM_FLAG_V | ARM_FLAG_C);
	if (a != 0)
		cpu->cd.arm.flags |= ARM_FLAG_C;
	else
		cpu->cd.arm.flags |= ARM_FLAG_Z;
	if ((int32_t)a < 0)
		cpu->cd.arm.flags |= ARM_FLAG_N;
}

#endif
