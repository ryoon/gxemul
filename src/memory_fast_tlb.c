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
 *  $Id: memory_fast_tlb.c,v 1.2 2004-11-21 23:29:48 debug Exp $
 *
 *  Fast virtual memory to host address, used by binary translated code.
 *
 *  NOTE: Included from memory.c.
 *
 *  Use #define to set FAST_VADDR_TO_HOSTADDR_NAME to the right name,
 *  and for example FAST_VADDR_R3000.
 */


/*
 *  fast_vaddr_to_hostaddr():
 *
 *  Used by dynamically translated code. The caller should have made sure
 *  that the access is aligned correctly.
 *
 *  Return value is a pointer to a host page + offset, if the virtual
 *  address was aligned, if the page was writable (or writeflag was zero),
 *  it the virtual address was translatable to a paddr, and if the paddr was
 *  translatable to a host address.
 *
 *  On error, NULL is returned. The caller (usually the dynamically
 *  generated machine code) must check for this.
 */
unsigned char *FAST_VADDR_TO_HOSTADDR(struct cpu *cpu,
	uint64_t vaddr, int writeflag)
{
	int ok, i, n, start_and_stop;
	uint64_t paddr, vaddr_page;
	unsigned char *memblock;
	size_t offset;

#if 0
	if (!(cpu->coproc[0]->reg[COP0_STATUS] & MIPS1_SR_KU_CUR))
		kernel_mode = 1;
#endif

#ifndef FAST_VADDR_R3000
#if 0
	int kernel_mode = 0;
	if ((cpu->coproc[0]->reg[COP0_STATUS] & MIPS3_SR_KSU_MASK)
			!= MIPS3_SR_KSU_USER
	    || (cpu->coproc[0]->reg[COP0_STATUS] & (STATUS_EXL | STATUS_ERL))
	    || (cpu->coproc[0]->reg[COP0_STATUS] & 1) == 0)
		kernel_mode = 1;
#endif
#endif

	/*  printf("fast_vaddr_to_hostaddr(): cpu=%p, vaddr=%016llx, wf=%i\n",
	    cpu, (long long)vaddr, writeflag);  */

	vaddr_page = vaddr & ~0xfff;
	i = start_and_stop = cpu->bintrans_next_index;
	n = 0;
	for (;;) {
		if (cpu->bintrans_data_vaddr[i] == vaddr_page &&
		    cpu->bintrans_data_hostpage[i] != NULL &&
		    cpu->bintrans_data_writable[i] >= writeflag) {
			uint64_t tmpaddr;
			unsigned char *tmpptr;
			int tmpwf;

#ifndef FAST_VADDR_R3000
#if 0
			if (vaddr > 0x7fffffff && !kernel_mode) {
				printf("bug: vaddr=%016llx in usermode\n", (long long)vaddr);
				goto urk_fulkod;
			}
#endif
#endif
			if (n < 4)
				return cpu->bintrans_data_hostpage[i]
				    + (vaddr & 0xfff);

			cpu->bintrans_next_index = start_and_stop - 1;
			if (cpu->bintrans_next_index < 0)
				cpu->bintrans_next_index = N_BINTRANS_VADDR_TO_HOST-1;

			tmpptr  = cpu->bintrans_data_hostpage[cpu->bintrans_next_index];
			tmpaddr = cpu->bintrans_data_vaddr[cpu->bintrans_next_index];
			tmpwf   = cpu->bintrans_data_writable[cpu->bintrans_next_index];

			cpu->bintrans_data_hostpage[cpu->bintrans_next_index] = cpu->bintrans_data_hostpage[i];
			cpu->bintrans_data_vaddr[cpu->bintrans_next_index] = cpu->bintrans_data_vaddr[i];
			cpu->bintrans_data_writable[cpu->bintrans_next_index] = cpu->bintrans_data_writable[i];

			cpu->bintrans_data_hostpage[i] = tmpptr;
			cpu->bintrans_data_vaddr[i] = tmpaddr;
			cpu->bintrans_data_writable[i] = tmpwf;

			return cpu->bintrans_data_hostpage[cpu->bintrans_next_index] + (vaddr & 0xfff);
		}

#ifndef FAST_VADDR_R3000
#if 0
urk_fulkod:
#endif
#endif

		n ++;
		i ++;
		if (i == N_BINTRANS_VADDR_TO_HOST)
			i = 0;
		if (i == start_and_stop)
			break;
	}

	ok = translate_address(cpu, vaddr, &paddr,
	    (writeflag? FLAG_WRITEFLAG : 0) + FLAG_NOEXCEPTIONS);
	/*  printf("ok=%i\n", ok);  */
	if (!ok)
		return NULL;

	if (cpu->emul->emulation_type == EMULTYPE_DEC)
		paddr &= 0x1fffffff;
	else
		paddr &= (((uint64_t)1<<(uint64_t)48) - 1);

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

				cpu->bintrans_next_index --;
				if (cpu->bintrans_next_index < 0)
					cpu->bintrans_next_index = N_BINTRANS_VADDR_TO_HOST-1;
				cpu->bintrans_data_hostpage[cpu->bintrans_next_index] = cpu->mem->dev_bintrans_data[i] + (paddr & ~0xfff);
				cpu->bintrans_data_vaddr[cpu->bintrans_next_index] = vaddr_page;
				cpu->bintrans_data_writable[cpu->bintrans_next_index] = writeflag;
				return cpu->mem->dev_bintrans_data[i] + paddr;
			} else
				return NULL;
		}

	memblock = memory_paddr_to_hostaddr(cpu->mem, paddr,
	    writeflag? MEM_WRITE : MEM_READ);
	if (memblock == NULL)
		return NULL;

	offset = paddr & (cpu->mem->memblock_size - 1);

	if (writeflag)
		bintrans_invalidate(cpu, paddr);

	cpu->bintrans_next_index --;
	if (cpu->bintrans_next_index < 0)
		cpu->bintrans_next_index = N_BINTRANS_VADDR_TO_HOST-1;
	cpu->bintrans_data_hostpage[cpu->bintrans_next_index] = memblock + (offset & ~0xfff);
	cpu->bintrans_data_vaddr[cpu->bintrans_next_index] = vaddr_page;
	cpu->bintrans_data_writable[cpu->bintrans_next_index] = ok - 1;
	return memblock + offset;
}

