#ifndef	NATIVE_H
#define	NATIVE_H

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
 *  $Id: native.h,v 1.9 2007-06-24 01:57:33 debug Exp $
 *
 *  Framework for native code generation during runtime.
 *  See src/native/ for more details.
 */

struct cpu;


#define	NATIVE_OPCODE_MAKE_FALLBACK_SIMPLE	-2
#define	NATIVE_OPCODE_MAKE_FALLBACK_SAFE	-1
#define	NATIVE_OPCODE_FALLBACK_SAFE		0
#define	NATIVE_OPCODE_FALLBACK_SIMPLE		1
#define	NATIVE_OPCODE_SET_NEXT_IC		2

#define	NATIVE_FALLBACK_SIMPLE						\
	if (cpu->translation_phys_page != NULL) {			\
		cpu->native_instruction_buffer[				\
		    cpu->native_instruction_buffer_curpos].opcode =	\
		    NATIVE_OPCODE_MAKE_FALLBACK_SIMPLE;			\
	}

#define	NATIVE_OPCODE_ADD_O32_O32_S32		10
#define	NATIVE_ADD_O32_O32_S32(dest,src,simm32)				\
	if (cpu->translation_phys_page != NULL) {			\
		int p = cpu->native_instruction_buffer_curpos ++;	\
		cpu->native_instruction_buffer[p].opcode =		\
		    NATIVE_OPCODE_ADD_O32_O32_S32;			\
		cpu->native_instruction_buffer[p].arg1 = (size_t) &(dest) - \
		    (size_t) cpu;					\
		cpu->native_instruction_buffer[p].arg2 = (size_t) &(src) - \
		    (size_t) cpu;					\
		cpu->native_instruction_buffer[p].arg3 = (int32_t) (simm32); \
	}

#define	NATIVE_OPCODE_SUB_O32_O32_S32		11
#define	NATIVE_SUB_O32_O32_S32(dest,src,simm32)				\
	if (cpu->translation_phys_page != NULL) {			\
		int p = cpu->native_instruction_buffer_curpos ++;	\
		cpu->native_instruction_buffer[p].opcode =		\
		    NATIVE_OPCODE_SUB_O32_O32_S32;			\
		cpu->native_instruction_buffer[p].arg1 = (size_t) &(dest) - \
		    (size_t) cpu;					\
		cpu->native_instruction_buffer[p].arg2 = (size_t) &(src) - \
		    (size_t) cpu;					\
		cpu->native_instruction_buffer[p].arg3 = (int32_t) (simm32); \
	}


struct native_instruction {
	int		opcode;

	size_t		arg1;
	size_t		arg2;
	size_t		arg3;
};

struct native_code_generation_backend {
	struct native_code_generation_backend *next;

	char		*name;
	void		*extra;

	void		*(*generate_code)(struct cpu *, void *);
};


/*
 *  native.c:
 *
 *  The main entry points to the native code generation framework.
 */

void native_init(void);
void *native_generate_code(struct cpu *);
void native_icache_invalidate(void *p, size_t len);


/*
 *  Individual native code generation backends:
 */

/*  native_ccld.c:  */
void native_ccld_init(struct native_code_generation_backend *);


#endif	/*  NATIVE_H  */
