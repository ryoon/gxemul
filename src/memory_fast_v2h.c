/*
 *  Copyright (C) 2004  Anders Gavare.  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright  
 *     notice, this list of conditions and the following disclaimer in the 
 *     documentation and/or other materials provided with the distribution.
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
 *  $Id: memory_fast_v2h.c,v 1.1 2004-12-01 14:23:02 debug Exp $
 *
 *  Fast virtual memory to host address, used by binary translated code.
 */

#include <stdio.h>
#include <stdlib.h>
#include "misc.h"
#include "bintrans.h"
#include "memory.h"


/*
 *  fast_vaddr_to_hostaddr():
 *
 *  Used by dynamically translated code. The caller should have made sure
 *  that the access is aligned correctly.
 *
 *  Return value is a pointer to a host page + offset, if the page was
 *  writable (or writeflag was zero), if the virtual address was translatable
 *  to a paddr, and if the paddr was translatable to a host address.
 *
 *  On error, NULL is returned. The caller (usually the dynamically
 *  generated machine code) must check for this.
 */
unsigned char *fast_vaddr_to_hostaddr(struct cpu *cpu,
	uint64_t vaddr, int writeflag)
{
	int ok, i, n, start_and_stop;
	uint64_t paddr, vaddr_page;
	unsigned char *memblock;
	size_t offset;

	/*  printf("yo cpu=%p vaddr=%016llx wf=%i\n", cpu, (long long)vaddr,
	    writeflag);  */

	ok = cpu->translate_address(cpu, vaddr, &paddr,
	    (writeflag? FLAG_WRITEFLAG : 0) + FLAG_NOEXCEPTIONS);
	/*  printf("ok=%i\n", ok);  */
	if (!ok)
		return NULL;

	for (i=0; i<cpu->mem->n_mmapped_devices; i++)
		if (paddr >= cpu->mem->dev_baseaddr[i] &&
		    paddr < cpu->mem->dev_baseaddr[i] + cpu->mem->dev_length[i]) {
			if (cpu->mem->dev_flags[i] & MEM_BINTRANS_OK) {
				paddr -= cpu->mem->dev_baseaddr[i];

				if (writeflag) {
					uint64_t low_paddr = paddr & ~0xfff;
					uint64_t high_paddr = paddr | 0xfff;
					if (!(cpu->mem->dev_flags[i] & MEM_BINTRANS_WRITE_OK))
						return NULL;

					if (low_paddr < cpu->mem->dev_bintrans_write_low[i])
					    cpu->mem->dev_bintrans_write_low[i] = low_paddr;
					if (high_paddr > cpu->mem->dev_bintrans_write_high[i])
					    cpu->mem->dev_bintrans_write_high[i] = high_paddr;
				}

				return cpu->mem->dev_bintrans_data[i] + paddr;
			} else
				return NULL;
		}

	memblock = memory_paddr_to_hostaddr(cpu->mem, paddr,
	    writeflag? MEM_WRITE : MEM_READ);
	if (memblock == NULL)
		return NULL;

	offset = paddr & ((1 << BITS_PER_MEMBLOCK) - 1);

	if (writeflag)
		bintrans_invalidate(cpu, paddr);

	return memblock + offset;
}

