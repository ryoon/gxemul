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
 *  $Id: cpu_arm_instr_misc.c,v 1.2 2005-11-01 22:07:00 debug Exp $
 *
 *  Misc ARM instructions. Included from cpu_arm_instr.c.
 */


/*
 *  clear_rX: Move #0 into a register. (Optimization hack.)
 */
X(clear_r0)  { cpu->cd.arm.r[ 0] = 0; } Y(clear_r0)
X(clear_r1)  { cpu->cd.arm.r[ 1] = 0; } Y(clear_r1)
X(clear_r2)  { cpu->cd.arm.r[ 2] = 0; } Y(clear_r2)
X(clear_r3)  { cpu->cd.arm.r[ 3] = 0; } Y(clear_r3)
X(clear_r4)  { cpu->cd.arm.r[ 4] = 0; } Y(clear_r4)
X(clear_r5)  { cpu->cd.arm.r[ 5] = 0; } Y(clear_r5)
X(clear_r6)  { cpu->cd.arm.r[ 6] = 0; } Y(clear_r6)
X(clear_r7)  { cpu->cd.arm.r[ 7] = 0; } Y(clear_r7)
X(clear_r8)  { cpu->cd.arm.r[ 8] = 0; } Y(clear_r8)
X(clear_r9)  { cpu->cd.arm.r[ 9] = 0; } Y(clear_r9)
X(clear_r10) { cpu->cd.arm.r[10] = 0; } Y(clear_r10)
X(clear_r11) { cpu->cd.arm.r[11] = 0; } Y(clear_r11)
X(clear_r12) { cpu->cd.arm.r[12] = 0; } Y(clear_r12)
X(clear_r13) { cpu->cd.arm.r[13] = 0; } Y(clear_r13)
X(clear_r14) { cpu->cd.arm.r[14] = 0; } Y(clear_r14)


/*
 *  mov1_rX: Move #1 into a register. (Optimization hack.)
 */
X(mov1_r0)  { cpu->cd.arm.r[ 0] = 1; } Y(mov1_r0)
X(mov1_r1)  { cpu->cd.arm.r[ 1] = 1; } Y(mov1_r1)
X(mov1_r2)  { cpu->cd.arm.r[ 2] = 1; } Y(mov1_r2)
X(mov1_r3)  { cpu->cd.arm.r[ 3] = 1; } Y(mov1_r3)
X(mov1_r4)  { cpu->cd.arm.r[ 4] = 1; } Y(mov1_r4)
X(mov1_r5)  { cpu->cd.arm.r[ 5] = 1; } Y(mov1_r5)
X(mov1_r6)  { cpu->cd.arm.r[ 6] = 1; } Y(mov1_r6)
X(mov1_r7)  { cpu->cd.arm.r[ 7] = 1; } Y(mov1_r7)
X(mov1_r8)  { cpu->cd.arm.r[ 8] = 1; } Y(mov1_r8)
X(mov1_r9)  { cpu->cd.arm.r[ 9] = 1; } Y(mov1_r9)
X(mov1_r10) { cpu->cd.arm.r[10] = 1; } Y(mov1_r10)
X(mov1_r11) { cpu->cd.arm.r[11] = 1; } Y(mov1_r11)
X(mov1_r12) { cpu->cd.arm.r[12] = 1; } Y(mov1_r12)
X(mov1_r13) { cpu->cd.arm.r[13] = 1; } Y(mov1_r13)
X(mov1_r14) { cpu->cd.arm.r[14] = 1; } Y(mov1_r14)


/*
 *  teqs0_rX: Compare a register to zero. (Optimization hack.)
 */
#define TEQS(n) X(teqs0_r ## n)  {					\
	if (cpu->cd.arm.r[n] == 0) {					\
		cpu->cd.arm.cpsr |= ARM_FLAG_Z;				\
		cpu->cd.arm.cpsr &= ~ARM_FLAG_N;			\
	} else {							\
		cpu->cd.arm.cpsr &= ~ARM_FLAG_Z;			\
		if (cpu->cd.arm.r[n] & 0x80000000)			\
			cpu->cd.arm.cpsr |= ARM_FLAG_N;			\
		else							\
			cpu->cd.arm.cpsr &= ~ARM_FLAG_N;		\
	}								\
} Y(teqs0_r ## n)

TEQS(0)
TEQS(1)
TEQS(2)
TEQS(3)
TEQS(4)
TEQS(5)
TEQS(6)
TEQS(7)
TEQS(8)
TEQS(9)
TEQS(10)
TEQS(11)
TEQS(12)
TEQS(13)
TEQS(14)

