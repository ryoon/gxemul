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
 *  $Id: cpu_alpha_instr_alu.c,v 1.1 2005-07-19 00:04:42 debug Exp $
 *
 *  Alpha ALU instructions.  (Included from cpu_alpha_instr_inc.c.)
 *
 *
 *  Most ALU instructions have the following arguments:
 *  
 *  arg[0] = pointer to destination uint64_t
 *  arg[1] = pointer to source uint64_t nr 1
 *  arg[2] = pointer to source uint64_t nr 2
 */

void ALU_N(struct cpu *cpu, struct alpha_instr_call *ic)
{
#ifdef ALU_LONG
	/*  Long  */
	int32_t x;
#else
	/*  Quad  */
	int64_t x;
#endif

	x = (
	    (*((uint64_t *)ic->arg[1]))
#ifdef ALU_S4
	    * 4
#endif
#ifdef ALU_S8
	    * 8
#endif
	    )
#ifdef ALU_ADD
	    +
#endif
#ifdef ALU_SUB
	    -
#endif
#ifdef ALU_OR
	    |
#endif
#ifdef ALU_AND
	    &
#endif

#ifdef ALU_IMM
	    ic->arg[2]
#else
	    (*((uint64_t *)ic->arg[2]))
#endif
	    ;

	*((uint64_t *)ic->arg[0]) = x;
}

