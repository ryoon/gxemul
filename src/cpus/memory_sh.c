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
 *  $Id: memory_sh.c,v 1.9 2006-10-17 07:56:35 debug Exp $
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"

#include "sh4_exception.h"
#include "sh4_mmu.h"


/*
 *  translate_via_mmu():
 *
 *  Scan the UTLB for a matching virtual address. If a match was found, then
 *  check permission bits etc. If everything was ok, then return the physical
 *  page address, otherwise cause an exception.
 *
 *  The implementation should (hopefully) be quite complete, except for the
 *  following:
 *
 *	o)  There is really no ITLB yet. The UTLB is searched for both data
 *	    and instruction accesses.
 *	o)  Multiple matching entries are not detected. (On a real CPU, these
 *	    would cause an exception.)
 *
 *  Same return values as sh_translate_v2p().
 */
static int translate_via_mmu(struct cpu *cpu, uint32_t vaddr,
	uint64_t *return_paddr, int flags)
{
	int wf = flags & FLAG_WRITEFLAG;
	int i, urc, require_asid_match, cur_asid, expevt = 0;
	uint32_t hi, lo, mask;
	int sh;		/*  Shared  */
	int d;		/*  Dirty bit  */
	int v;		/*  Valid bit  */
	int pr;		/*  Protection  */

	cur_asid = cpu->cd.sh.pteh & SH4_PTEH_ASID_MASK;
	require_asid_match = !(cpu->cd.sh.mmucr & SH4_MMUCR_SV)
	    || !(cpu->cd.sh.sr & SH_SR_MD);

	/*
	 *  Increase URC every time the UTLB is accessed. (Note: According to
	 *  the SH4 manual, the URC should not be increased when running the
	 *  ldtlb instruction. Perhaps this is a good place? Perhaps it is
	 *  better to just set it to a random value? TODO: Find out.)
	 */
	urc = ((cpu->cd.sh.mmucr & SH4_MMUCR_URC_MASK) >>
	    SH4_MMUCR_URC_SHIFT) + 1;
	if (urc == SH_N_UTLB_ENTRIES)
		urc = 0;

	cpu->cd.sh.mmucr &= ~SH4_MMUCR_URC_MASK;
	cpu->cd.sh.mmucr |= (urc << SH4_MMUCR_URC_SHIFT);

	for (i=0; i<SH_N_UTLB_ENTRIES; i++) {
		hi = cpu->cd.sh.utlb_hi[i];
		lo = cpu->cd.sh.utlb_lo[i];
		mask = 0xfff00000;

		v = lo & SH4_PTEL_V;

		switch (lo & SH4_PTEL_SZ_MASK) {
		case SH4_PTEL_SZ_1K:  mask = 0xfffffc00; break;
		case SH4_PTEL_SZ_4K:  mask = 0xfffff000; break;
		case SH4_PTEL_SZ_64K: mask = 0xffff0000; break;
		/*  case SH4_PTEL_SZ_1M:  mask = 0xfff00000; break;  */
		}

		if (!v || (hi & mask) != (vaddr & mask))
			continue;

		sh = lo & SH4_PTEL_SH;

		if (!sh && require_asid_match) {
			int asid = hi & SH4_PTEH_ASID_MASK;
			if (asid != cur_asid)
				continue;
		}

		/*  Note/TODO: Check for multiple matches is not implemented. */

		break;
	}

	/*  Virtual address not found? Then it's a TLB miss.  */
	if (i == SH_N_UTLB_ENTRIES)
		goto tlb_miss;

	/*  Matching address found! Let's see it is readable/writable, etc:  */
	d = lo & SH4_PTEL_D;
	pr = (lo & SH4_PTEL_PR_MASK) >> SH4_PTEL_PR_SHIFT;

	*return_paddr = (vaddr & ~mask) | (lo & mask & 0x1fffffff);

	if (flags & FLAG_INSTR) {
		/*  Instruction access:  */
		if (cpu->cd.sh.sr & SH_SR_MD)
			return 1;

		if (!(pr & 2))
			goto protection_violation;

		return 1;
	}

	/*  Data access:  */
	if (cpu->cd.sh.sr & SH_SR_MD) {
		/*  Kernel access:  */
		switch (pr) {
		case 0:
		case 2:	if (wf)
				goto protection_violation;
			return 1;
		case 1:
		case 3:	if (wf && !d)
				goto initial_write_exception;
			return 1;
		}
	}

	/*  User access  */
	switch (pr) {
	case 0:
	case 1:	goto protection_violation;
	case 2:	if (wf)
			goto protection_violation;
		return 1;
	case 3:	if (wf && !d)
			goto initial_write_exception;
		return 1;
	}


tlb_miss:
	expevt = wf? EXPEVT_TLB_MISS_ST : EXPEVT_TLB_MISS_LD;
	goto exception;

protection_violation:
	expevt = wf? EXPEVT_TLB_PROT_ST : EXPEVT_TLB_PROT_LD;
	goto exception;

initial_write_exception:
	expevt = EXPEVT_TLB_MOD;


exception:
	if (flags & FLAG_NOEXCEPTIONS) {
		*return_paddr = 0;
		return 2;
	}

	sh_exception(cpu, expevt, vaddr);

	return 0;
}


/*
 *  sh_translate_v2p():
 *
 *  Return values:
 *
 *	0	No access to the virtual address.
 *	1	return_paddr contains the physical address, the page is
 *		available as read-only.
 *	2	Same as 1, but the page is available as read/write.
 */
int sh_translate_v2p(struct cpu *cpu, uint64_t vaddr, uint64_t *return_paddr,
	int flags)
{
	int user = cpu->cd.sh.sr & SH_SR_MD? 0 : 1;

	vaddr = (uint32_t)vaddr;

	/*  U0/P0: Userspace addresses, or P3: Kernel virtual memory.  */
	if (!(vaddr & 0x80000000) ||
	    (vaddr >= 0xc0000000 && vaddr < 0xe0000000)) {
		/*  Address translation turned off?  */
		if (!(cpu->cd.sh.mmucr & SH4_MMUCR_AT)) {
			/*  Then return raw physical address:  */
			*return_paddr = vaddr & 0x1fffffff;
			return 2;
		}

		/*  Perform translation via the MMU:  */
		return translate_via_mmu(cpu, vaddr, return_paddr, flags);
	}

	/*  Store queue region:  */
	if (vaddr >= 0xe0000000 && vaddr < 0xe4000000) {
		/*  Note/TODO: Take SH4_MMUCR_SQMD into account.  */
		*return_paddr = vaddr;
		return 2;
	}

	if (user) {
		fatal("Userspace tried to access non-user space memory."
		    " TODO: cause exception! (vaddr=0x%08"PRIx32"\n",
		    (uint32_t) vaddr);
		exit(1);
	}

	/*  P1,P2: Direct-mapped physical memory.  */
	if (vaddr >= 0x80000000 && vaddr < 0xc0000000) {
		*return_paddr = vaddr & 0x1fffffff;
		return 2;
	}

	if (flags & FLAG_INSTR) {
		fatal("TODO: instr at 0x%08"PRIx32"\n", (uint32_t)vaddr);
		exit(1);
	}

	/*  P4: Special registers mapped at 0xf0000000 .. 0xffffffff:  */
	if ((vaddr & 0xf0000000) == 0xf0000000) {
		*return_paddr = vaddr;
		return 2;
	}

	if (flags & FLAG_NOEXCEPTIONS) {
		*return_paddr = 0;
		return 2;
	}

	/*  TODO  */
	fatal("Unimplemented SH vaddr 0x%08"PRIx32"\n", (uint32_t)vaddr);
	exit(1);
}

