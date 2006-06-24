/*
 *  Copyright (C) 2006  Anders Gavare.  All rights reserved.
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
 *  $Id: memory_alpha.c,v 1.3 2006-06-24 21:47:23 debug Exp $
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"


/*
 *  alpha_translate_v2p():
 */
int alpha_translate_v2p(struct cpu *cpu, uint64_t vaddr,
	uint64_t *return_paddr, int flags)
{
	*return_paddr = vaddr & 0x000003ffffffffffULL;

	/*  UGLY hack for now:  */
	/*  TODO: Real virtual memory support.  */

	if ((vaddr & ~0xffff) == 0xfffffc0010000000ULL ||
	    (vaddr & ~0xffff) == 0x0000000010000000ULL)
		*return_paddr = (vaddr & 0x0fffffff) +
		    (cpu->machine->physical_ram_in_mb-1) * 1048576;

	/*  At 0x20000000, NetBSD stores temp prom data  */
	if ((vaddr & ~0x1fff) == 0xfffffc0020000000ULL ||
	    (vaddr & ~0x1fff) == 0x0000000020000000ULL)
		*return_paddr = (vaddr & 0x0fffffff) + 512*1024 +
		    (cpu->machine->physical_ram_in_mb-1) * 1048576;

	/*  printf("yo %016"PRIx64" %016"PRIx64"\n", vaddr, *return_paddr);  */

	return 2;
}

