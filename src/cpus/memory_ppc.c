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
 *  $Id: memory_ppc.c,v 1.17 2005-11-21 11:10:10 debug Exp $
 *
 *  Included from cpu_ppc.c.
 */


#include "ppc_bat.h"


/*
 *  ppc_bat():
 *
 *  BAT translation. Returns -1 if there was no BAT hit, >= 0 for a hit.
 */
int ppc_bat(struct cpu *cpu, uint64_t vaddr, uint64_t *return_addr, int flags,
	int user)
{
	int i;

	if (cpu->cd.ppc.bits != 32) {
		fatal("TODO: ppc_bat() for non-32-bit\n");
		exit(1);
	}
	if (cpu->cd.ppc.cpu_type.flags & PPC_601) {
		fatal("TODO: ppc_bat() for PPC 601\n");
		exit(1);
	}

	for (i=0; i<8; i++) {
		int regnr = SPR_IBAT0U + i * 2;
		uint32_t ux = cpu->cd.ppc.spr[regnr];
		uint32_t lx = cpu->cd.ppc.spr[regnr + 1];
		uint32_t phys = lx & BAT_RPN, ebs = ux & BAT_EPI;
		uint32_t mask = ((ux & BAT_BL) << 15) | 0x1ffff;
		int pp = lx & BAT_PP;

		/*  Instruction BAT, but not instruction lookup? Then skip.  */
		if (i < 4 && !(flags & FLAG_INSTR))
			continue;
		if (i >= 4 && (flags & FLAG_INSTR))
			continue;

		/*  Not valid in either supervisor or user mode?  */
		if (user && !(ux & BAT_Vu))
			continue;
		if (!user && !(ux & BAT_Vs))
			continue;

		/*  Virtual address mismatch? Then skip.  */
		if ((vaddr & ~mask) != (ebs & ~mask))
			continue;

		*return_addr = (vaddr & mask) | (phys & ~mask);

		/*  pp happens to (almost) match our return values :-)  */
		if (flags & FLAG_WRITEFLAG && pp != BAT_PP_RW)
			return 0;
		if (pp == BAT_PP_RO)
			pp = 1;
		return pp;
	}

	return -1;
}


/*
 *  get_pte_low():
 *
 *  Scan a PTE group for a cmp (compare) value.
 *
 *  Returns 1 if the value was found, and *lowp is set to the low PTE word.
 *
 *  Returns 0 if no match was found.
 */
static int get_pte_low(struct cpu *cpu, uint64_t pteg_select,
	uint32_t *lowp, uint32_t cmp)
{
	int i;
	uint32_t upper;
	unsigned char d[8];

	for (i=0; i<8; i++) {
		cpu->memory_rw(cpu, cpu->mem, pteg_select + i*8,
		    &d[0], 8, MEM_READ, PHYSICAL | NO_EXCEPTIONS);
		upper = (d[0]<<24)+(d[1]<<16)+(d[2]<<8)+d[3];

		/*  Valid PTE, and correct api and vsid?  */
		if (upper == cmp) {
			*lowp = ((d[4]<<24)+(d[5]<<16)+(d[6]<<8)+d[7]);
			return 1;
		}
	}

	return 0;
}


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
	int instr = flags & FLAG_INSTR, res = 0, match, user, access = 0;
	int writeflag = flags & FLAG_WRITEFLAG? 1 : 0;
	uint64_t sdr1 = cpu->cd.ppc.spr[SPR_SDR1], htaborg, msr;
	uint32_t hash1, hash2, pteg_select, tmp;
	uint32_t lower_pte, cmp;

	reg_access_msr(cpu, &msr, 0, 0);
	user = msr & PPC_MSR_PR? 1 : 0;

	if (cpu->cd.ppc.bits == 32)
		vaddr &= 0xffffffff;

	if ((instr && !(msr & PPC_MSR_IR)) || (!instr && !(msr & PPC_MSR_DR))) {
		*return_addr = vaddr;
		return 2;
	}

	/*  Try the BATs first:  */
	if (cpu->cd.ppc.bits == 32) {
		res = ppc_bat(cpu, vaddr, return_addr, flags, user);
		if (res > 0)
			return res;
		if (res == 0)
			fatal("[ TODO: BAT exception ]\n");
	}

	/*
	 *  Virtual page translation 
	 */
	/*  fatal("{ vaddr = 0x%llx }\n", (long long)vaddr);  */

	if (cpu->cd.ppc.bits == 32) {
		int srn = (vaddr >> 28) & 15, api = (vaddr >> 22) & 0x3f;
		uint32_t vsid = cpu->cd.ppc.sr[srn] & 0x00ffffff;

		htaborg = sdr1 & 0xffff0000UL;

		/*  Primary hash:  */
		hash1 = (vsid & 0x7ffff) ^ ((vaddr >> 12) & 0xffff);
		tmp = (hash1 >> 10) & (sdr1 & 0x1ff);
		pteg_select = htaborg & 0xfe000000;
		pteg_select |= ((hash1 & 0x3ff) << 6);
		pteg_select |= (htaborg & 0x01ff0000) | (tmp << 16);
		cpu->cd.ppc.spr[SPR_HASH1] = pteg_select;
		cmp = cpu->cd.ppc.spr[instr? SPR_ICMP : SPR_DCMP] =
		    0x80000000 | api | (vsid<<7);
		match = get_pte_low(cpu, pteg_select, &lower_pte, cmp);

		/*  Secondary hash:  */
		hash2 = hash1 ^ 0x7ffff;
		tmp = (hash2 >> 10) & (sdr1 & 0x1ff);
		pteg_select = htaborg & 0xfe000000;
		pteg_select |= ((hash2 & 0x3ff) << 6);
		pteg_select |= (htaborg & 0x01ff0000) | (tmp << 16);
		cpu->cd.ppc.spr[SPR_HASH2] = pteg_select;
		if (!match) {
			cmp |= 0x40;
			match = get_pte_low(cpu, pteg_select, &lower_pte, cmp);
		}

		res = 0;

		while (match) {
			access = lower_pte & 3;
			*return_addr = (lower_pte & 0xfffff000)|(vaddr & 0xfff);

			if (lower_pte & 0x80) {
				res = 2;
				break;
			}

			if (lower_pte & 0x100) {
				res = 1;
				break;
			}

			if (user) {
				switch (access) {
				case 1:
				case 3:	res = writeflag? 0 : 1;
					break;
				case 2:	res = 2;
					break;
				}
			} else {
				/*  Supervisor access:  */
				switch (access) {
				case 3:	res = writeflag? 0 : 1;
					break;
				default:res = 2;
				}
			}

#if 0
			/*  Write-protect if change-bit isn't set:  */
			if (!(lower_pte & 0x80) && res == 2)
				res = writeflag? 0 : 1;
#endif

#if 0
			/*  Guarded?  */
			if (lower_pte & 0x8) {
				fatal("guarded page: TODO\n");
				res = 0;
			}
#endif

#if 0
			/*  User protected segment?  */
			if (user && !(cpu->cd.ppc.sr[srn] & 0x20000000)) {
				fatal("user protected? TODO\n");
				res = 0;
			}
			/*  Non-executable?  */
			if (instr && !(cpu->cd.ppc.sr[srn] & 0x10000000))
				res = 0;
#endif
			break;
		}
		if (res > 0)
			return res;
	} else {
		htaborg = sdr1 & 0xfffffffffffc0000ULL;

		fatal("TODO: ppc 64-bit translation\n");
		exit(1);
	}

	/*  Return failure:  */
	if (flags & FLAG_NOEXCEPTIONS)
		return 0;

	/*  Cause an exception:  */
	fatal("[ memory_ppc: exception! vaddr=0x%llx pc=0x%llx "
	    "instr=%i user=%i wf=%i pte_lo=%08x ]\n",
	    (long long)vaddr, (long long)cpu->pc, instr, user,
	    writeflag, lower_pte);

	if (cpu->cd.ppc.cpu_type.flags & PPC_603) {
		cpu->cd.ppc.spr[instr? SPR_IMISS : SPR_DMISS] = vaddr;

		msr |= PPC_MSR_TGPR;
		reg_access_msr(cpu, &msr, 1, 0);

		ppc_exception(cpu, instr? 0x10 : (writeflag? 0x12 : 0x11));
	} else {
		if (!instr) {
			cpu->cd.ppc.spr[SPR_DAR] = vaddr;
			cpu->cd.ppc.spr[SPR_DSISR] = match?
			    DSISR_PROTECT : DSISR_NOTFOUND;
			if (writeflag)
				cpu->cd.ppc.spr[SPR_DSISR] |= DSISR_STORE;
		}
		ppc_exception(cpu, instr?
		    PPC_EXCEPTION_ISI : PPC_EXCEPTION_DSI);
	}

	return 0;
}

