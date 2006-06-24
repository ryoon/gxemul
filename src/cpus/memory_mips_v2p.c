/*
 *  Copyright (C) 2003-2005  Anders Gavare.  All rights reserved.
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
 *  $Id: memory_mips_v2p.c,v 1.6 2006-06-24 21:47:23 debug Exp $
 *
 *  Included from memory.c.
 */


/*
 *  translate_v2p():
 *
 *  Don't call this function is userland_emul is non-NULL, or cpu is NULL.
 *
 *  TODO:  vpn2 is a bad name for R2K/R3K, as it is the actual framenumber.
 *
 *  Return values:
 *	0  Failure
 *	1  Success, the page is readable only
 *	2  Success, the page is read/write
 */
int TRANSLATE_ADDRESS(struct cpu *cpu, uint64_t vaddr,
	uint64_t *return_paddr, int flags)
{
	int writeflag = flags & FLAG_WRITEFLAG? MEM_WRITE : MEM_READ;
	int no_exceptions = flags & FLAG_NOEXCEPTIONS;
	int ksu, use_tlb, status, i;
	uint64_t vaddr_vpn2=0, vaddr_asid=0;
	int exccode, tlb_refill;
	struct mips_coproc *cp0;

#ifdef V2P_MMU3K
	const int x_64 = 0;
	const int n_tlbs = 64;
	const int pmask = 0xfff;
#else
#ifdef V2P_MMU10K
	const uint64_t vpn2_mask = ENTRYHI_VPN2_MASK_R10K;
#else
#ifdef V2P_MMU4100
/* This is ugly  */
	const uint64_t vpn2_mask = ENTRYHI_VPN2_MASK | 0x1800;
#else
	const uint64_t vpn2_mask = ENTRYHI_VPN2_MASK;
#endif
#endif
	int x_64;	/*  non-zero for 64-bit address space accesses  */
	int pageshift, n_tlbs;
	int pmask;
#ifdef V2P_MMU4100
	const int pagemask_mask = PAGEMASK_MASK_R4100;
	const int pagemask_shift = PAGEMASK_SHIFT_R4100;
	const int pfn_shift = 10;
#else
	const int pagemask_mask = PAGEMASK_MASK;
	const int pagemask_shift = PAGEMASK_SHIFT;
	const int pfn_shift = 12;
#endif
#endif	/*  !V2P_MMU3K  */


	exccode = -1;
	tlb_refill = 1;

	/*  Cached values:  */
	cp0 = cpu->cd.mips.coproc[0];
	status = cp0->reg[COP0_STATUS];

	/*
	 *  R4000 Address Translation:
	 *
	 *  An address may be in one of the kernel segments, that
	 *  are directly mapped, or the address can go through the
	 *  TLBs to be turned into a physical address.
	 *
	 *  KSU: EXL: ERL: X:  Name:   Range:
	 *  ---- ---- ---- --  -----   ------
	 *
	 *   10   0    0    0  useg    0 - 0x7fffffff    (2GB)  (via TLB)
	 *   10   0    0    1  xuseg   0 - 0xffffffffff  (1TB)  (via TLB)
	 *
	 *   01   0    0    0  suseg   0          - 0x7fffffff  (2GB via TLB)
	 *   01   0    0    0  ssseg   0xc0000000 - 0xdfffffff  (0.5 GB via TLB)
	 *   01   0    0    1  xsuseg  0 - 0xffffffffff         (1TB)  (via TLB)
	 *   01   0    0    1  xsseg   0x4000000000000000 - 0x400000ffffffffff
	 *					  (1TB)  (via TLB)
	 *   01   0    0    1  csseg   0xffffffffc0000000 - 0xffffffffdfffffff
	 *					  (0.5TB)  (via TLB)
	 *
	 *   00   x    x    0  kuseg   0 - 0x7fffffff  (2GB)  (via TLB)  (*)
	 *   00   x    x    0  kseg0   0x80000000 - 0x9fffffff (0.5GB)
	 *					  unmapped, cached
	 *   00   x    x    0  kseg1   0xa0000000 - 0xbfffffff (0.5GB)
	 *					  unmapped, uncached
	 *   00   x    x    0  ksseg   0xc0000000 - 0xdfffffff (0.5GB)
	 *					  (via TLB)
	 *   00   x    x    0  kseg3   0xe0000000 - 0xffffffff (0.5GB)
	 *					  (via TLB)
	 *   00   x    x    1  xksuseg 0 - 0xffffffffff (1TB) (via TLB) (*)
	 *   00   x    x    1  xksseg  0x4000000000000000 - 0x400000ffffffffff
	 *					  (1TB)  (via TLB)
	 *   00   x    x    1  xkphys  0x8000000000000000 - 0xbfffffffffffffff
	 *					  todo
	 *   00   x    x    1  xkseg   0xc000000000000000 - 0xc00000ff7fffffff
	 *					  todo
	 *   00   x    x    1  ckseg0  0xffffffff80000000 - 0xffffffff9fffffff
	 *					  like kseg0
	 *   00   x    x    1  ckseg1  0xffffffffa0000000 - 0xffffffffbfffffff
	 *					  like kseg1
	 *   00   x    x    1  cksseg  0xffffffffc0000000 - 0xffffffffdfffffff
	 *					  like ksseg
	 *   00   x    x    1  ckseg3  0xffffffffe0000000 - 0xffffffffffffffff
	 *					  like kseg2
	 *
	 *  (*) = if ERL=1 then kuseg is not via TLB, but unmapped,
	 *  uncached physical memory.
	 *
	 *  (KSU==0 or EXL=1 or ERL=1 is enough to use k*seg*.)
	 *
	 *  An invalid address causes an Address Error.
	 *
	 *  See chapter 4, page 96, in the R4000 manual for more info!
	 */

#ifdef V2P_MMU3K
	if (status & MIPS1_SR_KU_CUR)
		ksu = KSU_USER;
	else
		ksu = KSU_KERNEL;

	/*  These are needed later:  */
	vaddr_asid = cp0->reg[COP0_ENTRYHI] & R2K3K_ENTRYHI_ASID_MASK;
	vaddr_vpn2 = vaddr & R2K3K_ENTRYHI_VPN_MASK;
#else
	/*
	 *  R4000 and others:
	 *
	 *  kx,sx,ux = 0 for 32-bit addressing,
	 *  1 for 64-bit addressing. 
	 */
	n_tlbs = cpu->cd.mips.cpu_type.nr_of_tlb_entries;

	ksu = (status & STATUS_KSU_MASK) >> STATUS_KSU_SHIFT;
	if (status & (STATUS_EXL | STATUS_ERL))
		ksu = KSU_KERNEL;

	/*  Assume KSU_USER.  */
	x_64 = status & STATUS_UX;

	if (ksu == KSU_KERNEL)
		x_64 = status & STATUS_KX;
	else if (ksu == KSU_SUPERVISOR)
		x_64 = status & STATUS_SX;

	/*  This suppresses a compiler warning:  */
	pageshift = 12;

	/*
	 *  Physical addressing on R10000 etc:
	 *
	 *  TODO: Probably only accessible in kernel mode.
	 *
	 *  0x9000000080000000 = disable L2 cache (?)
	 *  TODO:  Make this correct.
	 */
	if ((vaddr >> 62) == 0x2) {
		/*
		 *  On IP30, addresses such as 0x900000001f600050 are used,
		 *  but also things like 0x90000000a0000000.  (TODO)
		 *
		 *  On IP27 (and probably others), addresses such as
		 *  0x92... and 0x96... have to do with NUMA stuff.
		 */
		*return_paddr = vaddr & (((uint64_t)1 << 44) - 1);
		return 2;
	}

	/*  This is needed later:  */
	vaddr_asid = cp0->reg[COP0_ENTRYHI] & ENTRYHI_ASID;
	/*  vpn2 depends on pagemask, which is not fixed on R4000  */
#endif


	if (vaddr <= 0x7fffffff)
		use_tlb = 1;
	else {
#if 1
/*  TODO: This should be removed, but it seems that other
bugs are triggered.  */
		/*  Sign-extend vaddr, if necessary:  */
		if ((vaddr >> 32) == 0 && vaddr & (uint32_t)0x80000000ULL)
			vaddr |= 0xffffffff00000000ULL;
#endif
		if (ksu == KSU_KERNEL) {
			/*  kseg0, kseg1:  */
			if (vaddr >= (uint64_t)0xffffffff80000000ULL &&
			    vaddr <= (uint64_t)0xffffffffbfffffffULL) {
				*return_paddr = vaddr & 0x1fffffff;
				return 2;
			}

			/*  TODO: supervisor stuff  */

			/*  other segments:  */
			use_tlb = 1;
		} else
			use_tlb = 0;
	}

	if (use_tlb) {
#ifndef V2P_MMU3K
		int odd = 0;
		uint64_t cached_lo1 = 0;
#endif
		int g_bit, v_bit, d_bit;
		uint64_t cached_hi, cached_lo0;
		uint64_t entry_vpn2 = 0, entry_asid, pfn;

		for (i=0; i<n_tlbs; i++) {
#ifdef V2P_MMU3K
			/*  R3000 or similar:  */
			cached_hi = cp0->tlbs[i].hi;
			cached_lo0 = cp0->tlbs[i].lo0;

			entry_vpn2 = cached_hi & R2K3K_ENTRYHI_VPN_MASK;
			entry_asid = cached_hi & R2K3K_ENTRYHI_ASID_MASK;
			g_bit = cached_lo0 & R2K3K_ENTRYLO_G;
			v_bit = cached_lo0 & R2K3K_ENTRYLO_V;
			d_bit = cached_lo0 & R2K3K_ENTRYLO_D;
#else
			/*  R4000 or similar:  */
			pmask = cp0->tlbs[i].mask & pagemask_mask;
			cached_hi = cp0->tlbs[i].hi;
			cached_lo0 = cp0->tlbs[i].lo0;
			cached_lo1 = cp0->tlbs[i].lo1;

			/*  Optimized for minimum page size:  */
			if (pmask == 0) {
				pageshift = pagemask_shift - 1;
				entry_vpn2 = (cached_hi & vpn2_mask)
				    >> pagemask_shift;
				vaddr_vpn2 = (vaddr & vpn2_mask)
				    >> pagemask_shift;
				pmask = (1 << (pagemask_shift-1)) - 1;
				odd = (vaddr >> (pagemask_shift-1)) & 1;
			} else {
				/*  Non-standard page mask:  */
				switch (pmask | ((1 << pagemask_shift) - 1)) {
				case 0x00007ff:	pageshift = 10; break;
				case 0x0001fff:	pageshift = 12; break;
				case 0x0007fff:	pageshift = 14; break;
				case 0x001ffff:	pageshift = 16; break;
				case 0x007ffff:	pageshift = 18; break;
				case 0x01fffff:	pageshift = 20; break;
				case 0x07fffff:	pageshift = 22; break;
				case 0x1ffffff:	pageshift = 24; break;
				case 0x7ffffff:	pageshift = 26; break;
				default:fatal("pmask=%08x\n", pmask);
					exit(1);
				}

				entry_vpn2 = (cached_hi &
				    vpn2_mask) >> (pageshift + 1);
				vaddr_vpn2 = (vaddr & vpn2_mask) >>
				    (pageshift + 1);
				pmask = (1 << pageshift) - 1;
				odd = (vaddr >> pageshift) & 1;
			}

			/*  Assume even virtual page...  */
			v_bit = cached_lo0 & ENTRYLO_V;
			d_bit = cached_lo0 & ENTRYLO_D;

#ifdef V2P_MMU8K
			/*
			 *  TODO:  I don't really know anything about the R8000.
			 *  http://futuretech.mirror.vuurwerk.net/i2sec7.html
			 *  says that it has a three-way associative TLB with
			 *  384 entries, 16KB page size, and some other things.
			 *
			 *  It feels like things like the valid bit (ala R4000)
			 *  and dirty bit are not implemented the same on R8000.
			 *
			 *  http://sgistuff.tastensuppe.de/documents/
			 *		R8000_chipset.html
			 *  also has some info, but no details.
			 */
			v_bit = 1;	/*  Big TODO  */
			d_bit = 1;
#endif

			entry_asid = cached_hi & ENTRYHI_ASID;

			/*  ... reload pfn, v_bit, d_bit if
			    it was the odd virtual page:  */
			if (odd) {
				v_bit = cached_lo1 & ENTRYLO_V;
				d_bit = cached_lo1 & ENTRYLO_D;
			}
#ifdef V2P_MMU4100
			g_bit = cached_lo1 & cached_lo0 & ENTRYLO_G;
#else
			g_bit = cached_hi & TLB_G;
#endif

#endif

			/*  Is there a VPN and ASID match?  */
			if (entry_vpn2 == vaddr_vpn2 &&
			    (entry_asid == vaddr_asid || g_bit)) {
				/*  debug("OK MAP 1, i=%i { vaddr=%016"PRIx64" "
				    "==> paddr %016"PRIx64" v=%i d=%i "
				    "asid=0x%02x }\n", i, (uint64_t) vaddr,
				    (uint64_t) *return_paddr, v_bit?1:0,
				    d_bit?1:0, vaddr_asid);  */
				if (v_bit) {
					if (d_bit || (!d_bit &&
					    writeflag == MEM_READ)) {
						uint64_t paddr;
						/*  debug("OK MAP 2!!! { w=%i "
						    "vaddr=%016"PRIx64" ==> "
						    "d=%i v=%i paddr %016"
						    PRIx64" ",
						    writeflag, (uint64_t)vaddr,
						    d_bit?1:0, v_bit?1:0,
						    (uint64_t) *return_paddr);
						    debug(", tlb entry %2i: ma"
						    "sk=%016"PRIx64" hi=%016"
						    PRIx64" lo0=%016"PRIx64
						    " lo1=%016"PRIx64"\n",
						    i, cp0->tlbs[i].mask, cp0->
						    tlbs[i].hi, cp0->tlbs[i].
						    lo0, cp0->tlbs[i].lo1);
						*/
#ifdef V2P_MMU3K
						pfn = cached_lo0 &
						    R2K3K_ENTRYLO_PFN_MASK;
						paddr = pfn | (vaddr & pmask);
#else
						pfn = ((odd? cached_lo1 :
						    cached_lo0)
						    & ENTRYLO_PFN_MASK)
						    >> ENTRYLO_PFN_SHIFT;
						paddr = (pfn << pfn_shift) |
						    (vaddr & pmask);
#endif

						*return_paddr = paddr;
						return d_bit? 2 : 1;
					} else {
						/*  TLB modif. exception  */
						tlb_refill = 0;
						exccode = EXCEPTION_MOD;
						goto exception;
					}
				} else {
					/*  TLB invalid exception  */
					tlb_refill = 0;
					goto exception;
				}
			}
		}
	}

	/*
	 *  We are here if for example userland code tried to access
	 *  kernel memory, OR if there was a TLB refill.
	 */

	if (!use_tlb) {
		tlb_refill = 0;
		if (writeflag == MEM_WRITE)
			exccode = EXCEPTION_ADES;
		else
			exccode = EXCEPTION_ADEL;
	}

exception:
	if (no_exceptions)
		return 0;

	/*  TLB Load or Store exception:  */
	if (exccode == -1) {
		if (writeflag == MEM_WRITE)
			exccode = EXCEPTION_TLBS;
		else
			exccode = EXCEPTION_TLBL;
	}

#ifdef V2P_MMU3K
	vaddr_asid >>= R2K3K_ENTRYHI_ASID_SHIFT;
	vaddr_vpn2 >>= 12;
#endif

	mips_cpu_exception(cpu, exccode, tlb_refill, vaddr,
	    0, vaddr_vpn2, vaddr_asid, x_64);

	/*  Return failure:  */
	return 0;
}

