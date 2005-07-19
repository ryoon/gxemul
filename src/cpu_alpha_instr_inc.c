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
 *  $Id: cpu_alpha_instr_inc.c,v 1.2 2005-07-19 06:53:31 debug Exp $
 *
 *  Alpha instructions. TODO: This function should probably be generated
 *  automatically, but for now, it is hand-written.
 *
 *  Included from cpu_alpha_instr.c.
 */


/*
 *  3-register:
 */

/*  3-register long:  */
#define	ALU_LONG

#define	ALU_ADD

#define ALU_N alpha_instr_addl
#include "cpu_alpha_instr_alu.c"
#undef ALU_N

#define ALU_N alpha_instr_s4addl
#define	ALU_S4
#include "cpu_alpha_instr_alu.c"
#undef ALU_N
#undef ALU_S4

#define ALU_N alpha_instr_s8addl
#define	ALU_S8
#include "cpu_alpha_instr_alu.c"
#undef ALU_N
#undef ALU_S8

#undef ALU_ADD
#define	ALU_SUB

#define ALU_N alpha_instr_subl
#include "cpu_alpha_instr_alu.c"
#undef ALU_N

#define ALU_N alpha_instr_s4subl
#define	ALU_S4
#include "cpu_alpha_instr_alu.c"
#undef ALU_N
#undef ALU_S4

#define ALU_N alpha_instr_s8subl
#define	ALU_S8
#include "cpu_alpha_instr_alu.c"
#undef ALU_N
#undef ALU_S8

#undef ALU_SUB

#undef ALU_LONG


/*  3-register quad:  */

#define	ALU_ADD

#define ALU_N alpha_instr_addq
#include "cpu_alpha_instr_alu.c"
#undef ALU_N

#define ALU_N alpha_instr_s4addq
#define	ALU_S4
#include "cpu_alpha_instr_alu.c"
#undef ALU_N
#undef ALU_S4

#define ALU_N alpha_instr_s8addq
#define	ALU_S8
#include "cpu_alpha_instr_alu.c"
#undef ALU_N
#undef ALU_S8

#undef ALU_ADD
#define	ALU_SUB

#define ALU_N alpha_instr_subq
#include "cpu_alpha_instr_alu.c"
#undef ALU_N

#define ALU_N alpha_instr_s4subq
#define	ALU_S4
#include "cpu_alpha_instr_alu.c"
#undef ALU_N
#undef ALU_S4

#define ALU_N alpha_instr_s8subq
#define	ALU_S8
#include "cpu_alpha_instr_alu.c"
#undef ALU_N
#undef ALU_S8

#undef ALU_SUB


/*  3-register misc:  */

#define ALU_N alpha_instr_and
#define	ALU_AND
#include "cpu_alpha_instr_alu.c"
#undef ALU_N
#undef ALU_AND

#define ALU_N alpha_instr_or
#define	ALU_OR
#include "cpu_alpha_instr_alu.c"
#undef ALU_N
#undef ALU_OR



/*
 *  2-register plus IMM:
 */

#define	ALU_IMM

/*  3-register long:  */
#define	ALU_LONG

#define	ALU_ADD

#define ALU_N alpha_instr_addl_imm
#include "cpu_alpha_instr_alu.c"
#undef ALU_N

#define ALU_N alpha_instr_s4addl_imm
#define	ALU_S4
#include "cpu_alpha_instr_alu.c"
#undef ALU_N
#undef ALU_S4

#define ALU_N alpha_instr_s8addl_imm
#define	ALU_S8
#include "cpu_alpha_instr_alu.c"
#undef ALU_N
#undef ALU_S8

#undef ALU_ADD
#define	ALU_SUB

#define ALU_N alpha_instr_subl_imm
#include "cpu_alpha_instr_alu.c"
#undef ALU_N

#define ALU_N alpha_instr_s4subl_imm
#define	ALU_S4
#include "cpu_alpha_instr_alu.c"
#undef ALU_N
#undef ALU_S4

#define ALU_N alpha_instr_s8subl_imm
#define	ALU_S8
#include "cpu_alpha_instr_alu.c"
#undef ALU_N
#undef ALU_S8

#undef ALU_SUB

#undef ALU_LONG


/*  3-register quad:  */

#define	ALU_ADD

#define ALU_N alpha_instr_addq_imm
#include "cpu_alpha_instr_alu.c"
#undef ALU_N

#define ALU_N alpha_instr_s4addq_imm
#define	ALU_S4
#include "cpu_alpha_instr_alu.c"
#undef ALU_N
#undef ALU_S4

#define ALU_N alpha_instr_s8addq_imm
#define	ALU_S8
#include "cpu_alpha_instr_alu.c"
#undef ALU_N
#undef ALU_S8

#undef ALU_ADD
#define	ALU_SUB

#define ALU_N alpha_instr_subq_imm
#include "cpu_alpha_instr_alu.c"
#undef ALU_N

#define ALU_N alpha_instr_s4subq_imm
#define	ALU_S4
#include "cpu_alpha_instr_alu.c"
#undef ALU_N
#undef ALU_S4

#define ALU_N alpha_instr_s8subq_imm
#define	ALU_S8
#include "cpu_alpha_instr_alu.c"
#undef ALU_N
#undef ALU_S8

#undef ALU_SUB


/*  3-register misc:  */

#define ALU_N alpha_instr_and_imm
#define	ALU_AND
#include "cpu_alpha_instr_alu.c"
#undef ALU_N
#undef ALU_AND

#define ALU_N alpha_instr_or_imm
#define	ALU_OR
#include "cpu_alpha_instr_alu.c"
#undef ALU_N
#undef ALU_OR


#undef ALU_IMM



/*
 *  Load/store:
 */

#define	LS_B
#define	LS_GENERIC_N	alpha_generic_stb
#define	LS_N		alpha_instr_stb
#include "cpu_alpha_instr_loadstore.c"
#undef LS_N
#define LS_IGNORE_OFFSET
#define	LS_N		alpha_instr_stb_0
#include "cpu_alpha_instr_loadstore.c"
#undef LS_N
#undef LS_IGNORE_OFFSET
#undef LS_GENERIC_N
#undef LS_B

#define	LS_W
#define	LS_GENERIC_N	alpha_generic_stw
#define	LS_N		alpha_instr_stw
#include "cpu_alpha_instr_loadstore.c"
#undef LS_N
#define LS_IGNORE_OFFSET
#define	LS_N		alpha_instr_stw_0
#include "cpu_alpha_instr_loadstore.c"
#undef LS_N
#undef LS_IGNORE_OFFSET
#undef LS_GENERIC_N
#undef LS_W

#define	LS_L
#define	LS_GENERIC_N	alpha_generic_stl
#define	LS_N		alpha_instr_stl
#include "cpu_alpha_instr_loadstore.c"
#undef LS_N
#define LS_IGNORE_OFFSET
#define	LS_N		alpha_instr_stl_0
#include "cpu_alpha_instr_loadstore.c"
#undef LS_N
#undef LS_IGNORE_OFFSET
#undef LS_GENERIC_N
#undef LS_L

#define	LS_Q
#define	LS_GENERIC_N	alpha_generic_stq
#define	LS_N		alpha_instr_stq
#include "cpu_alpha_instr_loadstore.c"
#undef LS_N
#define LS_IGNORE_OFFSET
#define	LS_N		alpha_instr_stq_0
#include "cpu_alpha_instr_loadstore.c"
#undef LS_N
#undef LS_IGNORE_OFFSET
#undef LS_GENERIC_N
#undef LS_Q


#define LS_LOAD

#define	LS_B
#define	LS_GENERIC_N	alpha_generic_ldbu
#define	LS_N		alpha_instr_ldbu
#include "cpu_alpha_instr_loadstore.c"
#undef LS_N
#define LS_IGNORE_OFFSET
#define	LS_N		alpha_instr_ldbu_0
#include "cpu_alpha_instr_loadstore.c"
#undef LS_N
#undef LS_IGNORE_OFFSET
#undef LS_GENERIC_N
#undef LS_B

#define	LS_W
#define	LS_GENERIC_N	alpha_generic_ldwu
#define	LS_N		alpha_instr_ldwu
#include "cpu_alpha_instr_loadstore.c"
#undef LS_N
#define LS_IGNORE_OFFSET
#define	LS_N		alpha_instr_ldwu_0
#include "cpu_alpha_instr_loadstore.c"
#undef LS_N
#undef LS_IGNORE_OFFSET
#undef LS_GENERIC_N
#undef LS_W

#define	LS_L
#define	LS_GENERIC_N	alpha_generic_ldl
#define	LS_N		alpha_instr_ldl
#include "cpu_alpha_instr_loadstore.c"
#undef LS_N
#define LS_IGNORE_OFFSET
#define	LS_N		alpha_instr_ldl_0
#include "cpu_alpha_instr_loadstore.c"
#undef LS_N
#undef LS_IGNORE_OFFSET
#undef LS_GENERIC_N
#undef LS_L

#define	LS_Q
#define	LS_GENERIC_N	alpha_generic_ldq
#define	LS_N		alpha_instr_ldq
#include "cpu_alpha_instr_loadstore.c"
#undef LS_N
#define LS_IGNORE_OFFSET
#define	LS_N		alpha_instr_ldq_0
#include "cpu_alpha_instr_loadstore.c"
#undef LS_N
#undef LS_IGNORE_OFFSET
#undef LS_GENERIC_N
#undef LS_Q

#undef LS_LOAD


void (*alpha_loadstore[16])(struct cpu *, struct alpha_instr_call *) = {
	alpha_instr_stb, alpha_instr_stw,
	alpha_instr_stl, alpha_instr_stq,

	alpha_instr_stb_0, alpha_instr_stw_0,
	alpha_instr_stl_0, alpha_instr_stq_0,

	alpha_instr_ldbu, alpha_instr_ldwu,
	alpha_instr_ldl, alpha_instr_ldq,

	alpha_instr_ldbu_0, alpha_instr_ldwu_0,
	alpha_instr_ldl_0, alpha_instr_ldq_0
};

