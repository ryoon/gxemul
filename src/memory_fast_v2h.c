/*
 *  Copyright (C) 2004-2005  Anders Gavare.  All rights reserved.
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
 *  $Id: memory_fast_v2h.c,v 1.11 2005-02-11 09:29:51 debug Exp $
 *
 *  Fast virtual memory to host address, used by binary translated code.
 */

#include <stdio.h>
#include <stdlib.h>

#include "bintrans.h"
#include "cpu.h"
#include "memory.h"
#include "cpu_mips.h"
#include "misc.h"


#ifdef BINTRANS

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
	const int MAX = N_BINTRANS_VADDR_TO_HOST;

	/*  printf("fast_vaddr_to_hostaddr(): cpu=%p, vaddr=%016llx, wf=%i\n",
	    cpu, (long long)vaddr, writeflag);  */

#if 0
/*  Hm. This seems to work now, so this #if X can be removed. (?)  */

/*
 *  TODO:
 *
 *  Why doesn't this work yet?
 */
if ((vaddr & 0xc0000000ULL) >= 0xc0000000ULL && writeflag) {
	return NULL;
}
#endif


	vaddr_page = vaddr & ~0xfff;
	i = start_and_stop = cpu->cd.mips.bintrans_next_index;
	n = 0;
	for (;;) {
		if (cpu->cd.mips.bintrans_data_vaddr[i] == vaddr_page &&
		    cpu->cd.mips.bintrans_data_hostpage[i] != NULL &&
		    cpu->cd.mips.bintrans_data_writable[i] >= writeflag) {
			uint64_t tmpaddr;
			unsigned char *tmpptr;
			int tmpwf;

			if (n < 3)
				return cpu->cd.mips.bintrans_data_hostpage[i]
				    + (vaddr & 0xfff);

			cpu->cd.mips.bintrans_next_index = start_and_stop - 1;
			if (cpu->cd.mips.bintrans_next_index < 0)
				cpu->cd.mips.bintrans_next_index = MAX - 1;

			tmpptr  = cpu->cd.mips.bintrans_data_hostpage[cpu->cd.mips.bintrans_next_index];
			tmpaddr = cpu->cd.mips.bintrans_data_vaddr[cpu->cd.mips.bintrans_next_index];
			tmpwf   = cpu->cd.mips.bintrans_data_writable[cpu->cd.mips.bintrans_next_index];

			cpu->cd.mips.bintrans_data_hostpage[cpu->cd.mips.bintrans_next_index] = cpu->cd.mips.bintrans_data_hostpage[i];
			cpu->cd.mips.bintrans_data_vaddr[cpu->cd.mips.bintrans_next_index] = cpu->cd.mips.bintrans_data_vaddr[i];
			cpu->cd.mips.bintrans_data_writable[cpu->cd.mips.bintrans_next_index] = cpu->cd.mips.bintrans_data_writable[i];

			cpu->cd.mips.bintrans_data_hostpage[i] = tmpptr;
			cpu->cd.mips.bintrans_data_vaddr[i] = tmpaddr;
			cpu->cd.mips.bintrans_data_writable[i] = tmpwf;

			return cpu->cd.mips.bintrans_data_hostpage[cpu->cd.mips.bintrans_next_index] + (vaddr & 0xfff);
		}

		n ++;
		i ++;
		if (i == MAX)
			i = 0;
		if (i == start_and_stop)
			break;
	}

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

				cpu->cd.mips.bintrans_next_index --;
				if (cpu->cd.mips.bintrans_next_index < 0)
					cpu->cd.mips.bintrans_next_index = MAX - 1;
				cpu->cd.mips.bintrans_data_hostpage[cpu->cd.mips.bintrans_next_index] = cpu->mem->dev_bintrans_data[i] + (paddr & ~0xfff);
				cpu->cd.mips.bintrans_data_vaddr[cpu->cd.mips.bintrans_next_index] = vaddr_page;
				cpu->cd.mips.bintrans_data_writable[cpu->cd.mips.bintrans_next_index] = writeflag;
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

	cpu->cd.mips.bintrans_next_index --;
	if (cpu->cd.mips.bintrans_next_index < 0)
		cpu->cd.mips.bintrans_next_index = MAX - 1;
	cpu->cd.mips.bintrans_data_hostpage[cpu->cd.mips.bintrans_next_index] = memblock + (offset & ~0xfff);
	cpu->cd.mips.bintrans_data_vaddr[cpu->cd.mips.bintrans_next_index] = vaddr_page;
	cpu->cd.mips.bintrans_data_writable[cpu->cd.mips.bintrans_next_index] = writeflag;  /*  ok - 1;  */
	return memblock + offset;
}

#endif	/*  BINTRANS  */
