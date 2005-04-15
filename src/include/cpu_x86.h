#ifndef	CPU_X86_H
#define	CPU_X86_H

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
 *  $Id: cpu_x86.h,v 1.2 2005-04-15 00:42:03 debug Exp $
 */

#include "misc.h"


struct cpu_family;

struct x86_cpu {
	int		bits;

	/*  TODO: change into 64-bit registers for amd64  */
	uint32_t	eax;
	uint32_t	ebx;
	uint32_t	ecx;
	uint32_t	edx;
	uint32_t	esi;
	uint32_t	edi;
	uint32_t	ebp;
	uint32_t	esp;

	uint32_t	eflags;
};


#define	X86_EFLAGS_CF	(1)		/*  Carry Flag  */
#define	X86_EFLAGS_PF	(4)		/*  Parity Flag  */
#define	X86_EFLAGS_AF	(16)		/*  Adjust Flag  */
#define	X86_EFLAGS_ZF	(64)		/*  Zero Flag  */
#define	X86_EFLAGS_SF	(128)		/*  Sign Flag  */
#define	X86_EFLAGS_OF	(2048)		/*  Overflow Flag  */


/*  cpu_x86.c:  */
int x86_memory_rw(struct cpu *cpu, struct memory *mem, uint64_t vaddr,
	unsigned char *data, size_t len, int writeflag, int cache_flags);
int x86_cpu_family_init(struct cpu_family *);


#endif	/*  CPU_X86_H  */
