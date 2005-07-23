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
 *  $Id: cpu_alpha_instr_alu.c,v 1.6 2005-07-23 07:35:55 debug Exp $
 *
 *  Alpha ALU instructions.  (Included from tmp_alpha_misc.c.)
 *
 *
 *  Most ALU instructions have the following arguments:
 *  
 *  arg[0] = pointer to destination uint64_t
 *  arg[1] = pointer to source uint64_t nr 1
 *  arg[2] = pointer to source uint64_t nr 2
 *
 *  or, if ALU_IMM is set, arg[1] contains an 8-bit immediate value.
 *
 *  The main function groups are:
 *
 *	ALU_CMOV			conditional moves
 *	ALU_CMP				compare instructions
 *	none of the above		everything else
 */

void ALU_N(struct cpu *cpu, struct alpha_instr_call *ic)
{

#ifdef ALU_CMOV

	if (
#ifdef ALU_CMP_lbc
	    !(
#endif
	    (*((int64_t *)ic->arg[1]))
#ifdef ALU_CMP_eq
	    == 0
#endif
#ifdef ALU_CMP_ne
	    != 0
#endif
#ifdef ALU_CMP_le
	    <= 0
#endif
#ifdef ALU_CMP_lt
	    < 0
#endif
#ifdef ALU_CMP_ge
	    >= 0
#endif
#ifdef ALU_CMP_gt
	    > 0
#endif
#ifdef ALU_CMP_lbs
	    & 1
#endif
#ifdef ALU_CMP_lbc
	    & 1)
#endif
	    )
		*((uint64_t *)ic->arg[0]) =
#ifdef ALU_IMM
		    (uint64_t)ic->arg[2]
#else
		    (*((uint64_t *)ic->arg[2]))
#endif
		    ;

#else	/*  ! CMOV  */

#ifdef ALU_CMP

	uint64_t x;

	x = (*((
#ifdef ALU_UNSIGNED
	    uint64_t
#else
	    int64_t
#endif
	    *)ic->arg[1]))

#ifdef ALU_CMP_EQ
	    ==
#endif
#ifdef ALU_CMP_LE
	    <=
#endif
#ifdef ALU_CMP_LT
	    <
#endif

#ifdef ALU_IMM
#ifdef ALU_UNSIGNED
	    (uint64_t)ic->arg[2]
#else
	    (int64_t)ic->arg[2]
#endif
#else
#ifdef ALU_UNSIGNED
	    (*((uint64_t *)ic->arg[2]))
#else
	    (*((int64_t *)ic->arg[2]))
#endif
#endif
	    ;

#else	/*  !ALU_CMP  */

#ifdef ALU_LONG
	/*  Long  */
	int32_t x;
#else
	/*  Quad  */
	int64_t x;
#endif

#ifdef ALU_ZAP
	/*  Prepare for zapping:  */
	uint64_t zapmask = 0xffffffffffffffffULL;
	int zapbytes =
#ifdef ALU_NOT
	    ~
#endif
#ifdef ALU_IMM
	    (int64_t)ic->arg[2]
#else
	    (*((uint64_t *)ic->arg[2]))
#endif
	    ;
	if (zapbytes & 0x80)
		zapmask &= ~0xff00000000000000ULL;
	if (zapbytes & 0x40)
		zapmask &= ~0xff000000000000ULL;
	if (zapbytes & 0x20)
		zapmask &= ~0xff0000000000ULL;
	if (zapbytes & 0x10)
		zapmask &= ~0xff00000000ULL;
	if (zapbytes & 0x08)
		zapmask &= ~0xff000000ULL;
	if (zapbytes & 0x04)
		zapmask &= ~0xff0000ULL;
	if (zapbytes & 0x02)
		zapmask &= ~0xff00ULL;
	if (zapbytes & 0x01)
		zapmask &= ~0xffULL;
#endif	/*  ZAP  */

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
#ifdef ALU_XOR
	    ^
#endif
#ifdef ALU_AND
	    &
#endif

#ifdef ALU_ZAP
	    & zapmask
#else	/*  !ZAP  */
	    (
#ifdef ALU_NOT
	    ~
#endif
#ifdef ALU_IMM
	    (int64_t)ic->arg[2]
#else
	    (*((uint64_t *)ic->arg[2]))
#endif
	    )
#endif	/*  !ZAP  */

	    ;

#endif	/*  !ALU_CMP  */

	*((uint64_t *)ic->arg[0]) = x;
#endif	/*  ! CMOV  */
}

