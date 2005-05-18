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
 *  $Id: memory_x86.c,v 1.1 2005-05-18 10:07:53 debug Exp $
 *
 *  Included from cpu_x86.c.
 *
 *
 *  TODO: This is basically just a skeleton so far.
 */


/*
 *  translate_address():
 *
 *  Return values:
 *	0  Failure
 *	1  Success, the page is readable only
 *	2  Success, the page is read/write
 */
int TRANSLATE_ADDRESS(struct cpu *cpu, uint64_t vaddr,
	uint64_t *return_addr, int flags)
{
	int writeflag = flags & FLAG_WRITEFLAG? MEM_WRITE : MEM_READ;
	int no_exceptions = flags & FLAG_NOEXCEPTIONS;
	int selector, res, i;
	struct descriptor_cache *dc;

	if (cpu->cd.x86.cr[0] & 0x80000000) {
		fatal("TODO: PAGING not yet supported\n");
		cpu->running = 0;
		return 0;
	}

	/*  Real-mode:  */
	if (!(cpu->cd.x86.cr[0] & X86_CR0_PE)) {
		/*  TODO: A20 stuff  */
		vaddr = (cpu->cd.x86.s[cpu->cd.x86.cursegment] << 4)
		    + (vaddr & 0xffff);
		*return_addr = vaddr;
		return 2;
	}

	if ((vaddr >> 32) == 0xffffffff)
		vaddr &= 0xffffffff;

	dc = &cpu->cd.x86.descr_cache[cpu->cd.x86.cursegment & 7];

	/*  TODO: Check the limit. (depends on granularity?)  */

	/*  TODO: Check the Privilege Level  */

	/*  Code:  */
	if (flags & FLAG_INSTR) {
		if (dc->descr_type == DESCR_TYPE_CODE) {
			*return_addr = (vaddr + dc->base) & 0xffffffffULL;
			return 1;
		}
		fatal("TODO instr load but not code descriptor?\n");
		goto fail;
	}

	/*  We are here on non-instruction fetch.  */

	if (dc->descr_type == DESCR_TYPE_DATA) {
		if (writeflag == MEM_WRITE && !dc->writable) {
			fatal("TODO write to nonwritable segment\n");
			goto fail;
		}
		*return_addr = (vaddr + dc->base) & 0xffffffffULL;
		return 1 + dc->writable;
	}

fail:
	cpu->running = 0;
	return 0;
}

