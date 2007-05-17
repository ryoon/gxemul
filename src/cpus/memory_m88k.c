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
 *  $Id: memory_m88k.c,v 1.2 2007-05-17 02:00:30 debug Exp $
 *
 *  Virtual to physical memory translation for M88K emulation.
 *
 *  (This is where the actual work of the M8820x chips is emulated.)
 *
 *  TODO:
 *	Exceptions
 *	M and U bits
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"


#include "m8820x.h"
#include "m8820x_pte.h"


/*  #define	M8820X_TABLE_SEARCH_DEBUG  */


/*
 *  m88k_translate_v2p():
 */
int m88k_translate_v2p(struct cpu *cpu, uint64_t vaddr64,
	uint64_t *return_paddr, int flags)
{
//	int instr = flags & FLAG_INSTR;
	int writeflag = (flags & FLAG_WRITEFLAG)? 1 : 0;
//	int no_exceptions = flags & FLAG_NOEXCEPTIONS;
//	int exception_type = instr? M88K_EXCEPTION_INSTRUCTION_ACCESS
//	    : M88K_EXCEPTION_DATA_ACCESS;
	int supervisor = cpu->cd.m88k.cr[M88K_CR_PSR] & M88K_PSR_MODE;
	struct m8820x_cmmu *cmmu = cpu->cd.m88k.cmmu[0];
	uint32_t vaddr = vaddr64;
	uint32_t apr;
	uint32_t *seg_base, *page_base;
	sdt_entry_t seg_descriptor;
	pt_entry_t page_descriptor;
	int accumulated_flags;
	int i, seg_nr = vaddr >> 22, page_nr = (vaddr >> 12) & 0x3ff;


	/*
	 *  Is the CMMU not yet initialized? Then return physical = virtual.
	 */
	*return_paddr = vaddr;
	if (cmmu == NULL)
		return 2;

	if (supervisor)
		apr = cmmu->reg[CMMU_SAPR];
	else
		apr = cmmu->reg[CMMU_UAPR];

	/*
	 *  Address translation not enabled? Then return physical = virtual.
	 */
	if (!(apr & APR_V))
		return 2;

	/*
	 *  BATC lookup:
	 *
	 *  The BATC is a 10-entry array of virtual to physical mappings,
	 *  where the top 13 bits of the virtual address must match.
	 */
	for (i=0; i<N_M88200_BATC_REGS; i++) {
		uint32_t batc = cmmu->batc[i];

		/*  The batc entry must be valid:  */
		if (!(batc & BATC_V))
			continue;

		/*  ... and have a matching supervisor/user bit:  */
		if ((supervisor && !(batc & BATC_SO)) ||
		    (!supervisor && (batc & BATC_SO)))
			continue;

		/*  ... and matching virtual address:  */
		if ((vaddr & 0xfff80000) != (batc & 0xfff80000))
			continue;

		/*  A matching entry was found!  */

		/*  Is it write protected?  */
		if ((batc & BATC_PROT) && writeflag) {
			fatal("TODO: batc write protection exception\n");
			exit(1);
		}

		*return_paddr = ((batc & 0x0007ffc0) << 13)
		    | (vaddr & 0x0007ffff);

		return batc & BATC_PROT? 1 : 2;
	}


	/*
	 *  PATC lookup:
	 *
	 *  The PATC is a 56-entry array of virtual to physical mappings for
	 *  4 KB pages.
	 */
	for (i=0; i<N_M88200_PATC_ENTRIES; i++) {
		uint32_t vaddr_and_control = cmmu->patc_v_and_control[i];
		uint32_t paddr_and_sbit = cmmu->patc_p_and_supervisorbit[i];

		/*  Skip this entry if the valid bit isn't set:  */
		if (!(vaddr_and_control & PG_V))
			continue;

		/*  ... or the virtual addresses don't match:  */
		if ((vaddr & 0xfffff000) != (vaddr_and_control & 0xfffff000))
			continue;

		/*  ... or if the supervisor bit doesn't match:  */
		if (((paddr_and_sbit & M8820X_PATC_SUPERVISOR_BIT)
		    && !supervisor) || (supervisor &&
		    !(paddr_and_sbit & M8820X_PATC_SUPERVISOR_BIT)))
			continue;

		/*  A matching PATC entry was found!  */

		/*  Is it write protected?  */
		if ((vaddr_and_control & PG_PROT) && writeflag) {
			fatal("TODO: page write protection exception\n");
			exit(1);
		}

		*return_paddr = (paddr_and_sbit & 0xfffff000) | (vaddr & 0xfff);
		return vaddr_and_control & PG_PROT? 1 : 2;
	}

	/*
	 *  The address was neither found in the BATC, nor the PATC.
	 *  Attempt a search through page tables, to refill the PATC:
	 */

	seg_base = (uint32_t *) memory_paddr_to_hostaddr(
	    cpu->mem, apr & 0xfffff000, 1);

	seg_descriptor = seg_base[seg_nr];
	if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
		seg_descriptor = LE32_TO_HOST(seg_descriptor);
	else
		seg_descriptor = BE32_TO_HOST(seg_descriptor);

#ifdef M8820X_TABLE_SEARCH_DEBUG
	printf("+---   M8820x page table search debug:\n");
	printf("| vaddr     0x%08"PRIx32"\n", vaddr);
	printf("| apr       0x%08"PRIx32"\n", apr);
	printf("| seg_base  %p (on the host)\n", seg_base);
	printf("| seg_nr    0x%03x\n", seg_nr);
	printf("| page_nr   0x%03x\n", page_nr);
	printf("| sd        0x%08"PRIx32"\n", seg_descriptor);
#endif

	/*  Segment descriptor invalid? Then cause a segfault exception.  */
	if (!(seg_descriptor & SG_V)) {
		fatal("segment descriptor not valid: TODO exception\n");
		/*  UPDATE PFSR!  */
		exit(1);
	}

	/*  Usermode attempted to access a supervisor segment?  */
	if ((seg_descriptor & SG_SO) && !supervisor) {
		fatal("user access of supervisor segment: TODO exception\n");
		/*  UPDATE PFSR!  */
		exit(1);
	}

	accumulated_flags = seg_descriptor & SG_RO;

	page_base = (uint32_t *) memory_paddr_to_hostaddr(
	    cpu->mem, seg_descriptor & 0xfffff000, 1);

	page_descriptor = page_base[page_nr];
	if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
		page_descriptor = LE32_TO_HOST(page_descriptor);
	else
		page_descriptor = BE32_TO_HOST(page_descriptor);

#ifdef M8820X_TABLE_SEARCH_DEBUG
	printf("| page_base %p (on the host)\n", page_base);
	printf("| pd        0x%08"PRIx32"\n", page_descriptor);
#endif

	/*  Page descriptor invalid? Then cause a page fault exception.  */
	if (!(page_descriptor & PG_V)) {
		fatal("page descriptor not valid: TODO exception\n");
		/*  UPDATE PFSR!  */
		exit(1);
	}

	/*  Usermode attempted to access a supervisor page?  */
	if ((page_descriptor & PG_SO) && !supervisor) {
		fatal("user access of supervisor page: TODO exception\n");
		/*  UPDATE PFSR!  */
		exit(1);
	}

	accumulated_flags |= (page_descriptor & PG_RO);


	/*
	 *  Overwrite the next entry in the PATC with a new entry:
	 */

	/*  Invalidate the current entry, if it is valid:  */
	if (cmmu->patc_v_and_control[cmmu->patc_update_index] & PG_V) {
		uint32_t vaddr_to_invalidate = cmmu->patc_v_and_control[
		    cmmu->patc_update_index] & 0xfffff000;
		cpu->invalidate_translation_caches(cpu,
		    vaddr_to_invalidate, INVALIDATE_VADDR);
	}

	/*  ... and write the new one:  */
	cmmu->patc_v_and_control[cmmu->patc_update_index] =
	    (vaddr & 0xfffff000) | accumulated_flags | PG_V;
	cmmu->patc_p_and_supervisorbit[cmmu->patc_update_index] =
	    (page_descriptor & 0xfffff000) |
	    (supervisor? M8820X_PATC_SUPERVISOR_BIT : 0);

	cmmu->patc_update_index ++;
	cmmu->patc_update_index %= N_M88200_PATC_ENTRIES;


	/*  Check for writes to read-only pages:  */
	if (writeflag && (accumulated_flags & PG_RO)) {
		fatal("page write protection: TODO exception\n");
		/*  UPDATE PFSR!  */
		exit(1);
	}

	/*  Now return with the translated page address:  */
	*return_paddr = (page_descriptor & 0xfffff000) | (vaddr & 0xfff);
	return (accumulated_flags & PG_RO)? 1 : 2;
}

