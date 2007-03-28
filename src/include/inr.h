#ifndef	INR_H
#define	INR_H

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
 *  $Id: inr.h,v 1.1 2007-03-28 18:33:36 debug Exp $
 *
 *  Intermediate Native Representation
 *
 *  Data structures etc. used during native code generation.
 */

#ifndef NATIVE_CODE_GENERATION
#error Huh? inr.h should not be included.
#endif


struct inr_entry {
	int	opcode;
};

#define	INR_OPCODE_UNKNOWN			0
#define	INR_OPCODE_NOP				1


/*
 *  Max nr of intermediate opcodes in the inr_entries array. The most common
 *  case is that one "basic block" of the emulated machine code is translated,
 *  so this should be large enough to hold a large basic block plus margin.
 */
#define	INR_MAX_ENTRIES		256

struct inr {
	struct inr_entry	*inr_entries;
	int			nr_inr_entries_used;

	uint64_t		paddr;
};


#endif	/*  INR_H  */
