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
 *  $Id: memory_x86.c,v 1.13 2005-05-27 14:40:43 debug Exp $
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
	unsigned char pded[4];
	unsigned char pted[4];
	uint64_t table_addr;
	uint32_t pte, pde;
	int a, b, res, writable, usermode = 0;
	int writeflag = flags & FLAG_WRITEFLAG? MEM_WRITE : MEM_READ;
	int no_exceptions = flags & FLAG_NOEXCEPTIONS;
	int no_segmentation = flags & NO_SEGMENTATION;
	struct descriptor_cache *dc;

	if (cpu->cd.x86.cursegment < 0 || cpu->cd.x86.cursegment >= 8) {
		fatal("TODO: Weird x86 segment nr %i\n",
		    cpu->cd.x86.cursegment);
		cpu->running = 0;
		return 0;
	}

	if ((vaddr >> 32) == 0xffffffff)
		vaddr &= 0xffffffff;

	dc = &cpu->cd.x86.descr_cache[cpu->cd.x86.cursegment & 7];

	if (no_segmentation) {
		/*  linear address  */
		writable = 1;
	} else {
		if (PROTECTED_MODE && vaddr > dc->limit) {
			fatal("TODO: vaddr=0x%llx > limit (0x%llx)\n",
			    (long long)vaddr, (long long)dc->limit);
/*			goto fail;  */
		}

		/*  TODO: Check the Privilege Level  */
		vaddr = (vaddr + dc->base) & 0xffffffff;
		writable = dc->writable;
	}

	usermode = (cpu->cd.x86.s[X86_S_CS] & X86_PL_MASK) ==
	    X86_RING3;

	/*  Paging:  */
	if (cpu->cd.x86.cr[0] & X86_CR0_PG) {
		/*  TODO: This should be cached somewhere, in some
			kind of simulated TLB.  */
		if (cpu->cd.x86.cr[3] & 0xfff) {
			fatal("TODO: cr3=%016llx (lowest bits non-zero)\n",
			    (long long)cpu->cd.x86.cr[3]);
			goto fail;
		}

		a = (vaddr >> 22) & 1023;
		b = (vaddr >> 12) & 1023;
		/*  fatal("vaddr = 0x%08x ==> %i, %i\n", (int)vaddr, a, b);  */

		/*  Read the Page Directory Entry:  */
		table_addr = cpu->cd.x86.cr[3] & ~0xfff;
		if (table_addr == 0)
			fatal("WARNING: The page directory (cr3) is at"
			    " physical address 0 (?)\n");
		res = cpu->memory_rw(cpu, cpu->mem, table_addr + 4*a, pded,
		    sizeof(pded), MEM_READ, PHYSICAL);
		if (!res) {
			fatal("TODO: could not read pde (table = 0x%llx)\n",
			    (long long)table_addr);
			goto fail;
		}
		pde = pded[0] + (pded[1] << 8) + (pded[2] << 16) +
		    (pded[3] << 24);
		/*  fatal("  pde: 0x%08x\n", (int)pde);  */
		/*  TODO: lowest bits of the pde  */
		if (!(pde & 0x01)) {
			fatal("PAGE FAULT: pde not present: vaddr=0x%08x, "
			    "usermode=%i\n", (int)vaddr, usermode);
			fatal("            CS:EIP = 0x%04x:0x%016llx\n",
			    (int)cpu->cd.x86.s[X86_S_CS],
			    (long long)cpu->pc);
			if (!no_exceptions) {
				x86_interrupt(cpu, 14, (writeflag? 2 : 0) +
				    (usermode? 4 : 0));
				cpu->cd.x86.cr[2] = vaddr;
			}
			return 0;
		}

		/*  Read the Page Table Entry:  */
		table_addr = pde & ~0xfff;
		res = cpu->memory_rw(cpu, cpu->mem, table_addr + 4*b, pted,
		    sizeof(pted), MEM_READ, PHYSICAL);
		if (!res) {
			fatal("TODO: could not read pte (pt = 0x%llx)\n",
			    (long long)table_addr);
			goto fail;
		}
		pte = pted[0] + (pted[1] << 8) + (pted[2] << 16) +
		    (pted[3] << 24);
		/*  fatal("  pte: 0x%08x\n", (int)pte);  */
		if (!(pte & 0x02))
			writable = 0;
		if (!(pte & 0x01)) {
			fatal("TODO: pte not present: table_addr=0x%08x "
			    "vaddr=0x%08x, usermode=%i\n",
			    (int)table_addr, (int)vaddr, usermode);
			if (!no_exceptions) {
				x86_interrupt(cpu, 14, (writeflag? 2 : 0)
				    + (usermode? 4 : 0));
				cpu->cd.x86.cr[2] = vaddr;
			}
			return 0;
		}

		(*return_addr) = (pte & ~0xfff) | (vaddr & 0xfff);
	} else
		*return_addr = vaddr;

	/*  Code:  */
	if (flags & FLAG_INSTR) {
		if (dc->descr_type == DESCR_TYPE_CODE)
			return 1;
		fatal("TODO instr load but not code descriptor?\n");
		goto fail;
	}

	/*  We are here on non-instruction fetch.  */

	if (writeflag == MEM_WRITE && !writable) {
		fatal("TODO write to nonwritable segment or page\n");
		x86_interrupt(cpu, 14, (writeflag? 2 : 0)
		    + (usermode? 4 : 0) + 1);
		/*  goto fail;  */
		return 0;
	}

	return 1 + writable;

fail:
	fatal("memory_x86 FAIL: TODO\n");
	cpu->running = 0;
	return 0;
}

