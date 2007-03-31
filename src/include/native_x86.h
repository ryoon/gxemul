#ifndef	NATIVE_X86_H
#define	NATIVE_X86_H

/*
 *  Copyright (C) 2007  Anders Gavare.  All rights reserved.
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
 *  $Id: native_x86.h,v 1.2 2007-03-31 15:52:44 debug Exp $
 *
 *  AMD64/i386 native code generation; pseudo opcodes.
 *
 *  NOTE/TODO 1: There is no working native code generation in GXemul 0.4.x.
 *               This is just a skeleton.
 *
 *  NOTE/TODO 2: The translation actually doesn't exist for i386 hosts,
 *               only AMD64 hosts, so far.
 */

/*
 *  native_op:
 *
 *  One native_op struct corresponds to exactly 1 actual AMD64/x86 instruction.
 */
struct native_op {
	struct native_op	*prev, *next;

	int			opcode;

	uint64_t		arg1;
	uint64_t		arg2;
	uint64_t		arg3;
};

/*  Misc.:  */
#define	NATIVE_X86_OPCODE_UNKNOWN		0

/*  Load/store:  */
#define	NATIVE_X86_OPCODE_LOAD_CR64_R64		101
#define	NATIVE_X86_OPCODE_STORE_CR64_R64	102

/*  Arithmetic, logic, etc.:  */
#define	NATIVE_X86_OPCODE_XOR_R64_I32		201
#define	NATIVE_X86_OPCODE_OR_R64_I32		202



#ifdef TEST_NATIVE_X86
void test_native_x86(void);
#endif


#endif	/*  NATIVE_X86_H  */
