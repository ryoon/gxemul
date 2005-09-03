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
 *  $Id: memory_ppc.c,v 1.2 2005-09-03 21:40:34 debug Exp $
 *
 *  Included from cpu_ppc.c.
 */


/*
 *  ppc_translate_address():
 *
 *  Don't call this function is userland_emul is non-NULL, or cpu is NULL.
 *
 *  Return values:
 *	0  Failure
 *	1  Success, the page is readable only
 *	2  Success, the page is read/write
 */
int ppc_translate_address(struct cpu *cpu, uint64_t vaddr,
	uint64_t *return_addr, int flags)
{
	int instr = flags & FLAG_INSTR;

	if (cpu->cd.ppc.bits == 32)
		vaddr &= 0xffffffff;

	if ((instr && !(cpu->cd.ppc.msr & PPC_MSR_IR)) ||
	    (!instr && !(cpu->cd.ppc.msr & PPC_MSR_DR))) {
		*return_addr = vaddr;
		return 2;
	}

	/*  TODO: This is not really correct.  */

	if (cpu->cd.ppc.sdr1 == 0) {
		/*  Try the BATs:  */
		int i;
		/*  TODO: This is just a quick (incorrect) hack:  */
		for (i=0; i<4; i++) {
			uint32_t p = 0, v = vaddr & 0xf0000000;
			int match = 0;
			if (instr && (cpu->cd.ppc.ibat_u[i]&0xf0000000) == v) {
				match = 1;
				p = cpu->cd.ppc.ibat_l[i] & 0xf0000000;
			}
			/*  Linux/BeBox seems to use data bats for
			    instructions?  */
			if (!match && (cpu->cd.ppc.dbat_u[i]&0xf0000000) == v) {
				match = 1;
				p = cpu->cd.ppc.dbat_l[i] & 0xf0000000;
			}
			if (match) {
				*return_addr = (vaddr & 0x0fffffff) | p;
				return 2;
			}
		}

		fatal("ppc_translate_address(): vaddr = 0x%016llx, no ",
		    "BAT hit?\n", (long long)vaddr);
		*return_addr = vaddr;
		return 2;
	}

	fatal("sdr1 = 0x%llx\n", (long long)cpu->cd.ppc.sdr1);


	/*  Return failure:  */
	if (flags & FLAG_NOEXCEPTIONS)
		return 0;

	/*  TODO: Cause exception.  */

	return 0;
}

