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
 *  $Id: cpu_mips_coproc.c,v 1.3 2005-10-26 14:37:03 debug Exp $
 *
 *  Emulation of MIPS coprocessors.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "bintrans.h"
#include "cop0.h"
#include "cpu.h"
#include "cpu_mips.h"
#include "emul.h"
#include "machine.h"
#include "memory.h"
#include "mips_cpu_types.h"
#include "misc.h"
#include "opcodes_mips.h"


#ifndef ENABLE_MIPS


struct mips_coproc *mips_coproc_new(struct cpu *cpu, int coproc_nr)
{ return NULL; }

void mips_coproc_tlb_set_entry(struct cpu *cpu, int entrynr, int size,
	uint64_t vaddr, uint64_t paddr0, uint64_t paddr1,
	int valid0, int valid1, int dirty0, int dirty1, int global, int asid,
	int cachealgo0, int cachealgo1) { }


#else	/*  ENABLE_MIPS  */


extern volatile int single_step;

static char *cop0_names[] = COP0_NAMES;
static char *regnames[] = MIPS_REGISTER_NAMES;


/*  FPU control registers:  */
#define	FPU_FCIR	0
#define	FPU_FCCR	25
#define	FPU_FCSR	31
#define	  FCSR_FCC0_SHIFT	  23
#define	  FCSR_FCC1_SHIFT	  25


/*
 *  initialize_cop0_config():
 *
 *  Helper function, called from mips_coproc_new().
 */
static void initialize_cop0_config(struct cpu *cpu, struct mips_coproc *c)
{
#ifdef ENABLE_MIPS16
	const int m16 = 1;
#else
	const int m16 = 0;
#endif
	int cpu_type, IB, DB, SB, IC, DC, SC, IA, DA;

	/*  Default values:  */
	c->reg[COP0_CONFIG] =
	      (   0 << 31)	/*  config1 present  */
	    | (0x00 << 16)	/*  implementation dependent  */
	    | ((cpu->byte_order==EMUL_BIG_ENDIAN? 1 : 0) << 15)
				/*  endian mode  */
	    | (   2 << 13)	/*  0 = MIPS32,
				    1 = MIPS64 with 32-bit segments,
				    2 = MIPS64 with all segments,
				    3 = reserved  */
	    | (   0 << 10)	/*  architecture revision level,
				    0 = "Revision 1", other
				    values are reserved  */
	    | (   1 <<  7)	/*  MMU type:  0 = none,
				    1 = Standard TLB,
				    2 = Standard BAT,
				    3 = fixed mapping, 4-7=reserved  */
	    | (   0 <<  0)	/*  kseg0 coherency algorithm
				(TODO)  */
	    ;

	cpu_type = cpu->cd.mips.cpu_type.rev & 0xff;

	/*  AU1x00 are treated as 4Kc (MIPS32 cores):  */
	if ((cpu->cd.mips.cpu_type.rev & 0xffff) == 0x0301)
		cpu_type = MIPS_4Kc;

	switch (cpu_type) {
	case MIPS_R4000:	/*  according to the R4000 manual  */
	case MIPS_R4600:
		IB = cpu->machine->cache_picache_linesize - 4;
		IB = IB < 0? 0 : (IB > 1? 1 : IB);
		DB = cpu->machine->cache_pdcache_linesize - 4;
		DB = DB < 0? 0 : (DB > 1? 1 : DB);
		SB = cpu->machine->cache_secondary_linesize - 4;
		SB = SB < 0? 0 : (SB > 3? 3 : SB);
		IC = cpu->machine->cache_picache - 12;
		IC = IC < 0? 0 : (IC > 7? 7 : IC);
		DC = cpu->machine->cache_pdcache - 12;
		DC = DC < 0? 0 : (DC > 7? 7 : DC);
		SC = cpu->machine->cache_secondary? 0 : 1;
		c->reg[COP0_CONFIG] =
		      (   0 << 31)	/*  Master/Checker present bit  */
		    | (0x00 << 28)	/*  EC: system clock divisor,
					    0x00 = '2'  */
		    | (0x00 << 24)	/*  EP  */
		    | (  SB << 22)	/*  SB  */
		    | (0x00 << 21)	/*  SS: 0 = mixed i/d scache  */
		    | (0x00 << 20)	/*  SW  */
		    | (0x00 << 18)	/*  EW: 0=64-bit  */
		    | (  SC << 17)	/*  SC: 0=secondary cache present,
					    1=non-present  */
		    | (0x00 << 16)	/*  SM: (todo)  */
		    | ((cpu->byte_order==EMUL_BIG_ENDIAN? 1 : 0) << 15)
				 	/*  endian mode  */
		    | (0x01 << 14)	/*  ECC: 0=enabled, 1=disabled  */
		    | (0x00 << 13)	/*  EB: (todo)  */
		    | (0x00 << 12)	/*  0 (resered)  */
		    | (  IC <<  9)	/*  IC: I-cache = 2^(12+IC) bytes
					    (1 = 8KB, 4=64K)  */
		    | (  DC <<  6)	/*  DC: D-cache = 2^(12+DC) bytes
					    (1 = 8KB, 4=64K)  */
		    | (  IB <<  5)	/*  IB: I-cache line size (0=16,
					    1=32)  */
		    | (  DB <<  4)	/*  DB: D-cache line size (0=16,
					    1=32)  */
		    | (   0 <<  3)	/*  CU: todo  */
		    | (   0 <<  0)	/*  kseg0 coherency algorithm
						(TODO)  */
		    ;
		break;
	case MIPS_R4100:	/*  According to the VR4131 manual:  */
		IB = cpu->machine->cache_picache_linesize - 4;
		IB = IB < 0? 0 : (IB > 1? 1 : IB);
		DB = cpu->machine->cache_pdcache_linesize - 4;
		DB = DB < 0? 0 : (DB > 1? 1 : DB);
		IC = cpu->machine->cache_picache - 10;
		IC = IC < 0? 0 : (IC > 7? 7 : IC);
		DC = cpu->machine->cache_pdcache - 10;
		DC = DC < 0? 0 : (DC > 7? 7 : DC);
		c->reg[COP0_CONFIG] =
		      (   0 << 31)	/*  IS: Instruction Streaming bit  */
		    | (0x01 << 28)	/*  EC: system clock divisor,
					    0x01 = 2  */
		    | (0x00 << 24)	/*  EP  */
		    | (0x00 << 23)	/*  AD: Accelerate data mode
					    (0=VR4000-compatible)  */
		    | ( m16 << 20)	/*  M16: MIPS16 support  */
		    | (   1 << 17)	/*  '1'  */
		    | (0x00 << 16)	/*  BP: 'Branch forecast'
					    (0 = enabled)  */
		    | ((cpu->byte_order==EMUL_BIG_ENDIAN? 1 : 0) << 15)
				 	/*  endian mode  */
		    | (   2 << 13)	/*  '2' hardcoded on VR4131  */
		    | (   1 << 12)	/*  CS: Cache size mode
					    (1 on VR4131)  */
		    | (  IC <<  9)	/*  IC: I-cache = 2^(10+IC) bytes
					    (0 = 1KB, 4=16K)  */
		    | (  DC <<  6)	/*  DC: D-cache = 2^(10+DC) bytes
					    (0 = 1KB, 4=16K)  */
		    | (  IB <<  5)	/*  IB: I-cache line size (0=16,
					    1=32)  */
		    | (  DB <<  4)	/*  DB: D-cache line size (0=16,
					    1=32)  */
		    | (   0 <<  0)	/*  kseg0 coherency algorithm (TODO)  */
		    ;
		break;
	case MIPS_R5000:
	case MIPS_RM5200:	/*  rm5200 is just a wild guess  */
		/*  These are just guesses: (the comments are wrong) */
		c->reg[COP0_CONFIG] =
		      (   0 << 31)	/*  Master/Checker present bit  */
		    | (0x00 << 28)	/*  EC: system clock divisor,
					    0x00 = '2'  */
		    | (0x00 << 24)	/*  EP  */
		    | (0x00 << 22)	/*  SB  */
		    | (0x00 << 21)	/*  SS  */
		    | (0x00 << 20)	/*  SW  */
		    | (0x00 << 18)	/*  EW: 0=64-bit  */
		    | (0x01 << 17)	/*  SC: 0=secondary cache present,
					    1=non-present  */
		    | (0x00 << 16)	/*  SM: (todo)  */
		    | ((cpu->byte_order==EMUL_BIG_ENDIAN? 1 : 0) << 15)
				 	/*  endian mode  */
		    | (0x01 << 14)	/*  ECC: 0=enabled, 1=disabled  */
		    | (0x00 << 13)	/*  EB: (todo)  */
		    | (0x00 << 12)	/*  0 (resered)  */
		    | (   3 <<  9)	/*  IC: I-cache = 2^(12+IC) bytes
					    (1 = 8KB, 4=64K)  */
		    | (   3 <<  6)	/*  DC: D-cache = 2^(12+DC) bytes
					    (1 = 8KB, 4=64K)  */
		    | (   1 <<  5)	/*  IB: I-cache line size (0=16,
					    1=32)  */
		    | (   1 <<  4)	/*  DB: D-cache line size (0=16,
					    1=32)  */
		    | (   0 <<  3)	/*  CU: todo  */
		    | (   2 <<  0)	/*  kseg0 coherency algorithm
						(TODO)  */
		    ;
		break;
	case MIPS_R10000:
	case MIPS_R12000:
	case MIPS_R14000:
		IC = cpu->machine->cache_picache - 12;
		IC = IC < 0? 0 : (IC > 7? 7 : IC);
		DC = cpu->machine->cache_pdcache - 12;
		DC = DC < 0? 0 : (DC > 7? 7 : DC);
		SC = cpu->machine->cache_secondary - 19;
		SC = SC < 0? 0 : (SC > 7? 7 : SC);
		/*  According to the R10000 User's Manual:  */
		c->reg[COP0_CONFIG] =
		      (  IC << 29)	/*  Primary instruction cache size
					    (3 = 32KB)  */
		    | (  DC << 26)	/*  Primary data cache size (3 =
					    32KB)  */
		    | (   0 << 19)	/*  SCClkDiv  */
		    | (  SC << 16)	/*  SCSize, secondary cache size.
					    0 = 512KB. powers of two  */
		    | (   0 << 15)	/*  MemEnd  */
		    | (   0 << 14)	/*  SCCorEn  */
		    | (   1 << 13)	/*  SCBlkSize. 0=16 words,
					    1=32 words  */
		    | (   0 <<  9)	/*  SysClkDiv  */
		    | (   0 <<  7)	/*  PrcReqMax  */
		    | (   0 <<  6)	/*  PrcElmReq  */
		    | (   0 <<  5)	/*  CohPrcReqTar  */
		    | (   0 <<  3)	/*  Device number  */
		    | (   2 <<  0)	/*  Cache coherency algorithm for
					    kseg0  */
		    ;
		break;
	case MIPS_R5900:
		/*
		 *  R5900 is supposed to have the following (according
		 *  to NetBSD/playstation2):
		 *	cpu0: 16KB/64B 2-way set-associative L1 Instruction
		 *		cache, 48 TLB entries
		 *	cpu0: 8KB/64B 2-way set-associative write-back L1
		 *		Data cache
		 *  The following settings are just guesses:
		 *  (comments are incorrect)
		 */
		c->reg[COP0_CONFIG] =
		      (   0 << 31)	/*  Master/Checker present bit  */
		    | (0x00 << 28)	/*  EC: system clock divisor,
					    0x00 = '2'  */
		    | (0x00 << 24)	/*  EP  */
		    | (0x00 << 22)	/*  SB  */
		    | (0x00 << 21)	/*  SS  */
		    | (0x00 << 20)	/*  SW  */
		    | (0x00 << 18)	/*  EW: 0=64-bit  */
		    | (0x01 << 17)	/*  SC: 0=secondary cache present,
					    1=non-present  */
		    | (0x00 << 16)	/*  SM: (todo)  */
		    | ((cpu->byte_order==EMUL_BIG_ENDIAN? 1 : 0) << 15)
				 	/*  endian mode  */
		    | (0x01 << 14)	/*  ECC: 0=enabled, 1=disabled  */
		    | (0x00 << 13)	/*  EB: (todo)  */
		    | (0x00 << 12)	/*  0 (resered)  */
		    | (   3 <<  9)	/*  IC: I-cache = 2^(12+IC) bytes
					    (1 = 8KB, 4=64K)  */
		    | (   3 <<  6)	/*  DC: D-cache = 2^(12+DC) bytes
					    (1 = 8KB, 4=64K)  */
		    | (   1 <<  5)	/*  IB: I-cache line size (0=16,
					    1=32)  */
		    | (   1 <<  4)	/*  DB: D-cache line size (0=16,
					    1=32)  */
		    | (   0 <<  3)	/*  CU: todo  */
		    | (   0 <<  0)	/*  kseg0 coherency algorithm
						(TODO)  */
		    ;
		break;
	case MIPS_4Kc:
	case MIPS_5Kc:
		/*  According to the MIPS64 (5K) User's Manual:  */
		c->reg[COP0_CONFIG] =
		      (   (uint32_t)1 << 31)/*  Config 1 present bit  */
		    | (   0 << 20)	/*  ISD:  instruction scheduling
					    disable (=1)  */
		    | (   0 << 17)	/*  DID:  dual issue disable  */
		    | (   0 << 16)	/*  BM:   burst mode  */
		    | ((cpu->byte_order == EMUL_BIG_ENDIAN? 1 : 0) << 15)
				 	/*  endian mode  */
		    | ((cpu_type == MIPS_5Kc? 2 : 0) << 13)
					/*  0=MIPS32, 1=64S, 2=64  */
		    | (   0 << 10)	/*  Architecture revision  */
		    | (   1 <<  7)	/*  MMU type: 1=TLB, 3=FMT  */
		    | (   2 <<  0)	/*  kseg0 cache coherency algorithm  */
		    ;
		/*  Config select 1: caches etc. TODO: Don't use
			cpu->machine for this stuff!  */
		IB = cpu->machine->cache_picache_linesize - 1;
		IB = IB < 0? 0 : (IB > 7? 7 : IB);
		DB = cpu->machine->cache_pdcache_linesize - 1;
		DB = DB < 0? 0 : (DB > 7? 7 : DB);
		IC = cpu->machine->cache_picache -
		    cpu->machine->cache_picache_linesize - 7;
		DC = cpu->machine->cache_pdcache -
		    cpu->machine->cache_pdcache_linesize - 7;
		IA = cpu->cd.mips.cpu_type.piways - 1;
		DA = cpu->cd.mips.cpu_type.pdways - 1;
		cpu->cd.mips.cop0_config_select1 =
		    ((cpu->cd.mips.cpu_type.nr_of_tlb_entries - 1) << 25)
		    | (IC << 22)	/*  IS: I-cache sets per way  */
		    | (IB << 19)	/*  IL: I-cache line-size  */
		    | (IA << 16)	/*  IA: I-cache assoc. (ways-1)  */
		    | (DC << 13)	/*  DS: D-cache sets per way  */
		    | (DB << 10)	/*  DL: D-cache line-size  */
		    | (DA <<  7)	/*  DA: D-cache assoc. (ways-1)  */
		    | (16 * 0)		/*  Existance of PerformanceCounters  */
		    | ( 8 * 0)		/*  Existance of Watch Registers  */
		    | ( 4 * m16)	/*  Existance of MIPS16  */
		    | ( 2 * 0)		/*  Existance of EJTAG  */
		    | ( 1 * 1)		/*  Existance of FPU  */
		    ;
		break;
	default:
		;
	}
}


/*
 *  initialize_cop1():
 *
 *  Helper function, called from mips_coproc_new().
 */
static void initialize_cop1(struct cpu *cpu, struct mips_coproc *c)
{
	int fpu_rev;
	uint64_t other_stuff = 0;

	switch (cpu->cd.mips.cpu_type.rev & 0xff) {
	case MIPS_R2000:	fpu_rev = MIPS_R2010;	break;
	case MIPS_R3000:	fpu_rev = MIPS_R3010;
				other_stuff |= 0x40;	/*  or 0x30? TODO  */
				break;
	case MIPS_R6000:	fpu_rev = MIPS_R6010;	break;
	case MIPS_R4000:	fpu_rev = MIPS_R4010;	break;
	case MIPS_4Kc:		/*  TODO: Is this the same as 5Kc?  */
	case MIPS_5Kc:		other_stuff = COP1_REVISION_DOUBLE
				    | COP1_REVISION_SINGLE;
	case MIPS_R5000:
	case MIPS_RM5200:	fpu_rev = cpu->cd.mips.cpu_type.rev;
				other_stuff |= 0x10;
				/*  or cpu->cd.mips.cpu_type.sub ? TODO  */
				break;
	case MIPS_R10000:	fpu_rev = MIPS_R10000;	break;
	case MIPS_R12000:	fpu_rev = 0x9;	break;
	default:		fpu_rev = MIPS_SOFT;
	}

	c->fcr[COP1_REVISION] = (fpu_rev << 8) | other_stuff;

#if 0
	/*  These are mentioned in the MIPS64 documentation:  */
	    + (1 << 16)		/*  single  */
	    + (1 << 17)		/*  double  */
	    + (1 << 18)		/*  paired-single  */
	    + (1 << 19)		/*  3d  */
#endif
}


/*
 *  mips_coproc_new():
 *
 *  Create a new MIPS coprocessor object.
 */
struct mips_coproc *mips_coproc_new(struct cpu *cpu, int coproc_nr)
{
	struct mips_coproc *c;

	c = malloc(sizeof(struct mips_coproc));
	if (c == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}

	memset(c, 0, sizeof(struct mips_coproc));
	c->coproc_nr = coproc_nr;

	if (coproc_nr == 0) {
		c->nr_of_tlbs = cpu->cd.mips.cpu_type.nr_of_tlb_entries;
		c->tlbs = malloc(c->nr_of_tlbs * sizeof(struct mips_tlb));
		if (c->tlbs == NULL) {
			fprintf(stderr, "mips_coproc_new(): out of memory\n");
			exit(1);
		}

		/*
		 *  Start with nothing in the status register. This makes sure
		 *  that we are running in kernel mode with all interrupts
		 *  disabled.
		 */
		c->reg[COP0_STATUS] = 0;

		/*  For userland emulation, enable all four coprocessors:  */
		if (cpu->machine->userland_emul)
			c->reg[COP0_STATUS] |=
			    ((uint32_t)0xf << STATUS_CU_SHIFT);

		/*  Hm. Enable coprocessors 0 and 1 even if we're not just
		    emulating userland? TODO: Think about this.  */
		/*  if (cpu->machine->prom_emulation)  */
			c->reg[COP0_STATUS] |=
			    ((uint32_t)0x3 << STATUS_CU_SHIFT);

		if (!cpu->machine->prom_emulation)
			c->reg[COP0_STATUS] |= STATUS_BEV;

		/*  Default pagesize = 4 KB.  */
		c->reg[COP0_PAGEMASK] = 0x1fff;

		/*  Note: .rev may contain the company ID as well!  */
		c->reg[COP0_PRID] =
		      (0x00 << 24)		/*  Company Options  */
		    | (0x00 << 16)		/*  Company ID       */
		    | (cpu->cd.mips.cpu_type.rev <<  8)	/*  Processor ID     */
		    | (cpu->cd.mips.cpu_type.sub)	/*  Revision         */
		    ;

		c->reg[COP0_WIRED] = 0;

		initialize_cop0_config(cpu, c);

		/*  Make sure the status register is sign-extended nicely:  */
		c->reg[COP0_STATUS] = (int64_t)(int32_t)c->reg[COP0_STATUS];
	}

	if (coproc_nr == 1)
		initialize_cop1(cpu, c);

	return c;
}


/*
 *  mips_coproc_tlb_set_entry():
 *
 *  Used by machine setup code, if a specific machine emulation starts up
 *  with hardcoded virtual to physical mappings.
 */
void mips_coproc_tlb_set_entry(struct cpu *cpu, int entrynr, int size,
	uint64_t vaddr, uint64_t paddr0, uint64_t paddr1,
	int valid0, int valid1, int dirty0, int dirty1, int global, int asid,
	int cachealgo0, int cachealgo1)
{
	if (entrynr < 0 || entrynr >= cpu->cd.mips.coproc[0]->nr_of_tlbs) {
		printf("mips_coproc_tlb_set_entry(): invalid entry nr: %i\n",
		    entrynr);
		exit(1);
	}

	switch (cpu->cd.mips.cpu_type.mmu_model) {
	case MMU3K:
		if (size != 4096) {
			printf("mips_coproc_tlb_set_entry(): invalid pagesize "
			    "(%i) for MMU3K\n", size);
			exit(1);
		}
		cpu->cd.mips.coproc[0]->tlbs[entrynr].hi =
		    (vaddr & R2K3K_ENTRYHI_VPN_MASK) |
		    ((asid << R2K3K_ENTRYHI_ASID_SHIFT) & 
		    R2K3K_ENTRYHI_ASID_MASK);
		cpu->cd.mips.coproc[0]->tlbs[entrynr].lo0 =
		    (paddr0 & R2K3K_ENTRYLO_PFN_MASK) |
		    (cachealgo0? R2K3K_ENTRYLO_N : 0) |
		    (dirty0? R2K3K_ENTRYLO_D : 0) |
		    (valid0? R2K3K_ENTRYLO_V : 0) |
		    (global? R2K3K_ENTRYLO_G : 0);
		break;
	default:
		/*  MMU4K and MMU10K, etc:  */
		if (cpu->cd.mips.cpu_type.mmu_model == MMU10K)
			cpu->cd.mips.coproc[0]->tlbs[entrynr].hi =
			    (vaddr & ENTRYHI_VPN2_MASK_R10K) |
			    (vaddr & ENTRYHI_R_MASK) |
			    (asid & ENTRYHI_ASID) |
			    (global? TLB_G : 0);
		else
			cpu->cd.mips.coproc[0]->tlbs[entrynr].hi =
			    (vaddr & ENTRYHI_VPN2_MASK) |
			    (vaddr & ENTRYHI_R_MASK) |
			    (asid & ENTRYHI_ASID) |
			    (global? TLB_G : 0);
		/*  NOTE: The pagemask size is for a "dual" page:  */
		cpu->cd.mips.coproc[0]->tlbs[entrynr].mask =
		    (2*size - 1) & ~0x1fff;
		cpu->cd.mips.coproc[0]->tlbs[entrynr].lo0 =
		    (((paddr0 >> 12) << ENTRYLO_PFN_SHIFT) &
			ENTRYLO_PFN_MASK) |
		    (dirty0? ENTRYLO_D : 0) |
		    (valid0? ENTRYLO_V : 0) |
		    (global? ENTRYLO_G : 0) |
		    ((cachealgo0 << ENTRYLO_C_SHIFT) & ENTRYLO_C_MASK);
		cpu->cd.mips.coproc[0]->tlbs[entrynr].lo1 =
		    (((paddr1 >> 12) << ENTRYLO_PFN_SHIFT) &
			ENTRYLO_PFN_MASK) |
		    (dirty1? ENTRYLO_D : 0) |
		    (valid1? ENTRYLO_V : 0) |
		    (global? ENTRYLO_G : 0) |
		    ((cachealgo1 << ENTRYLO_C_SHIFT) & ENTRYLO_C_MASK);
		/*  TODO: R4100, 1KB pages etc  */
	}
}


/*
 *  old_update_translation_table():
 */
static void old_update_translation_table(struct cpu *cpu, uint64_t vaddr_page,
	unsigned char *host_page, int writeflag, uint64_t paddr_page)
{
	int a, b, index;
	struct vth32_table *tbl1;
	void *p_r, *p_w;
	uint32_t p_paddr;

	/*  This table stuff only works for 32-bit mode:  */
	if (vaddr_page & 0x80000000ULL) {
		if ((vaddr_page >> 32) != 0xffffffffULL)
			return;
	} else {
		if ((vaddr_page >> 32) != 0)
			return;
	}

	a = (vaddr_page >> 22) & 0x3ff;
	b = (vaddr_page >> 12) & 0x3ff;
	index = (vaddr_page >> 12) & 0xfffff;

	/*  printf("vaddr = %08x, a = %03x, b = %03x\n",
	    (int)vaddr_page,a, b);  */

	tbl1 = cpu->cd.mips.vaddr_to_hostaddr_table0_kernel[a];
	/*  printf("tbl1 = %p\n", tbl1);  */
	if (tbl1 == cpu->cd.mips.vaddr_to_hostaddr_nulltable) {
		/*  Allocate a new table1:  */
		/*  printf("ALLOCATING a new table1, 0x%08x - "
		    "0x%08x\n", a << 22, (a << 22) + 0x3fffff);  */
		if (cpu->cd.mips.next_free_vth_table == NULL) {
			tbl1 = malloc(sizeof(struct vth32_table));
			if (tbl1 == NULL) {
				fprintf(stderr, "out of mem\n");
				exit(1);
			}
			memset(tbl1, 0, sizeof(struct vth32_table));
		} else {
			tbl1 = cpu->cd.mips.next_free_vth_table;
			cpu->cd.mips.next_free_vth_table = tbl1->next_free;
			tbl1->next_free = NULL;
		}
		cpu->cd.mips.vaddr_to_hostaddr_table0_kernel[a] = tbl1;
		if (tbl1->refcount != 0) {
			printf("INTERNAL ERROR in coproc.c\n");
			exit(1);
		}
	}
	p_r = tbl1->haddr_entry[b*2];
	p_w = tbl1->haddr_entry[b*2+1];
	p_paddr = tbl1->paddr_entry[b];
	/*  printf("   p_r=%p p_w=%p\n", p_r, p_w);  */
	if (p_r == NULL && p_paddr == 0 &&
	    (host_page != NULL || paddr_page != 0)) {
		tbl1->refcount ++;
		/*  printf("ADDING %08x -> %p wf=%i (refcount is "
		    "now %i)\n", (int)vaddr_page, host_page,
		    writeflag, tbl1->refcount);  */
	}
	if (writeflag == -1) {
		/*  Forced downgrade to read-only:  */
		tbl1->haddr_entry[b*2 + 1] = NULL;
		if (cpu->cd.mips.host_store ==
		    cpu->cd.mips.host_store_orig)
			cpu->cd.mips.host_store[index] = NULL;
	} else if (writeflag==0 && p_w != NULL && host_page != NULL) {
		/*  Don't degrade a page from writable to readonly.  */
	} else {
		if (host_page != NULL) {
			tbl1->haddr_entry[b*2] = host_page;
			if (cpu->cd.mips.host_load ==
			    cpu->cd.mips.host_load_orig)
				cpu->cd.mips.host_load[index] = host_page;
			if (writeflag) {
				tbl1->haddr_entry[b*2+1] = host_page;
				if (cpu->cd.mips.host_store ==
				    cpu->cd.mips.host_store_orig)
					cpu->cd.mips.host_store[index] =
					    host_page;
			} else {
				tbl1->haddr_entry[b*2+1] = NULL;
				if (cpu->cd.mips.host_store ==
				    cpu->cd.mips.host_store_orig)
					cpu->cd.mips.host_store[index] = NULL;
			}
		} else {
			tbl1->haddr_entry[b*2] = NULL;
			tbl1->haddr_entry[b*2+1] = NULL;
			if (cpu->cd.mips.host_store ==
			    cpu->cd.mips.host_store_orig) {
				cpu->cd.mips.host_load[index] = NULL;
				cpu->cd.mips.host_store[index] = NULL;
			}
		}
		tbl1->paddr_entry[b] = paddr_page;
	}
	tbl1->bintrans_chunks[b] = NULL;
}


/*
 *  mips_update_translation_table():
 */
void mips_update_translation_table(struct cpu *cpu, uint64_t vaddr_page,
	unsigned char *host_page, int writeflag, uint64_t paddr_page)
{
	if (!cpu->machine->bintrans_enable)
		return;

	if (writeflag > 0)
		bintrans_invalidate(cpu, paddr_page);

	if (cpu->machine->old_bintrans_enable) {
		old_update_translation_table(cpu, vaddr_page, host_page,
		    writeflag, paddr_page);
		return;
	}

	/*  TODO  */
	/*  printf("update_translation_table(): TODO\n");  */
}


/*
 *  invalidate_table_entry():
 */
static void invalidate_table_entry(struct cpu *cpu, uint64_t vaddr)
{
	int a, b, index;
	struct vth32_table *tbl1;
	void *p_r, *p_w;
	uint32_t p_paddr;

	if (!cpu->machine->old_bintrans_enable) {
		/*  printf("invalidate_table_entry(): New: TODO\n");  */
		return;
	}

	/*  This table stuff only works for 32-bit mode:  */
	if (vaddr & 0x80000000ULL) {
		if ((vaddr >> 32) != 0xffffffffULL) {
			fatal("invalidate_table_entry(): vaddr = 0x%016llx\n",
			    (long long)vaddr);
			return;
		}
	} else {
		if ((vaddr >> 32) != 0) {
			fatal("invalidate_table_entry(): vaddr = 0x%016llx\n",
			    (long long)vaddr);
			return;
		}
	}

	a = (vaddr >> 22) & 0x3ff;
	b = (vaddr >> 12) & 0x3ff;
	index = (vaddr >> 12) & 0xfffff;

	/*  printf("vaddr = %08x, a = %03x, b = %03x\n", (int)vaddr,a, b);  */

	tbl1 = cpu->cd.mips.vaddr_to_hostaddr_table0_kernel[a];
	/*  printf("tbl1 = %p\n", tbl1);  */
	p_r = tbl1->haddr_entry[b*2];
	p_w = tbl1->haddr_entry[b*2+1];
	p_paddr = tbl1->paddr_entry[b];
	tbl1->bintrans_chunks[b] = NULL;
	/*  printf("B:  p_r=%p p_w=%p\n", p_r,p_w);  */
	cpu->cd.mips.host_load_orig[index] = NULL;
	cpu->cd.mips.host_store_orig[index] = NULL;
	if (p_r != NULL || p_paddr != 0) {
		/*  printf("Found a mapping, "
		    "vaddr = %08x, a = %03x, b = %03x\n", (int)vaddr,a, b);  */
		tbl1->haddr_entry[b*2] = NULL;
		tbl1->haddr_entry[b*2+1] = NULL;
		tbl1->paddr_entry[b] = 0;
		tbl1->refcount --;
		if (tbl1->refcount == 0) {
			cpu->cd.mips.vaddr_to_hostaddr_table0_kernel[a] =
			    cpu->cd.mips.vaddr_to_hostaddr_nulltable;
			/*  "free" tbl1:  */
			tbl1->next_free = cpu->cd.mips.next_free_vth_table;
			cpu->cd.mips.next_free_vth_table = tbl1;
		}
	}
}


/*
 *  clear_all_chunks_from_all_tables():
 */
void clear_all_chunks_from_all_tables(struct cpu *cpu)
{
	int a, b;
	struct vth32_table *tbl1;

	if (!cpu->machine->old_bintrans_enable) {
		printf("clear_all_chunks_from_all_tables(): New: TODO\n");
		return;
	}

	for (a=0; a<0x400; a++) {
		tbl1 = cpu->cd.mips.vaddr_to_hostaddr_table0_kernel[a];
		if (tbl1 != cpu->cd.mips.vaddr_to_hostaddr_nulltable) {
			for (b=0; b<0x400; b++) {
				int index;

				tbl1->haddr_entry[b*2] = NULL;
				tbl1->haddr_entry[b*2+1] = NULL;
				tbl1->paddr_entry[b] = 0;
				tbl1->bintrans_chunks[b] = NULL;

				if (cpu->cd.mips.host_store ==
				    cpu->cd.mips.host_store_orig) {
					index = (a << 10) + b;
					cpu->cd.mips.host_load[index] = NULL;
					cpu->cd.mips.host_store[index] = NULL;
				}
			}
		}
	}
}


/*
 *  mips_invalidate_translation_caches_paddr():
 *
 *  Invalidate based on physical address.
 */
void mips_invalidate_translation_caches_paddr(struct cpu *cpu,
	uint64_t paddr, int flags)
{
	paddr &= ~0xfff;

	if (cpu->machine->bintrans_enable) {
#if 1
		int i;
		uint64_t tlb_paddr0, tlb_paddr1;
		uint64_t tlb_vaddr;
		uint64_t p, p2;

		switch (cpu->cd.mips.cpu_type.mmu_model) {
		case MMU3K:
			for (i=0; i<64; i++) {
				tlb_paddr0 = cpu->cd.mips.coproc[0]->
				    tlbs[i].lo0 & R2K3K_ENTRYLO_PFN_MASK;
				tlb_vaddr = cpu->cd.mips.coproc[0]->
				    tlbs[i].hi & R2K3K_ENTRYHI_VPN_MASK;
				tlb_vaddr = (int64_t)(int32_t)tlb_vaddr;
				if ((cpu->cd.mips.coproc[0]->tlbs[i].lo0 &
				    R2K3K_ENTRYLO_V) && tlb_paddr0 == paddr)
					invalidate_table_entry(cpu, tlb_vaddr);
			}
			break;
		default:
			for (i=0; i<cpu->cd.mips.coproc[0]->nr_of_tlbs; i++) {
				int psize = 12;
				int or_pmask = 0x1fff;
				int phys_shift = 12;

				if (cpu->cd.mips.cpu_type.rev == MIPS_R4100) {
					or_pmask = 0x7ff;
					phys_shift = 10;
				}
				switch (cpu->cd.mips.coproc[0]->
				    tlbs[i].mask | or_pmask) {
				case 0x000007ff:	psize = 10; break;
				case 0x00001fff:	psize = 12; break;
				case 0x00007fff:	psize = 14; break;
				case 0x0001ffff:	psize = 16; break;
				case 0x0007ffff:	psize = 18; break;
				case 0x001fffff:	psize = 20; break;
				case 0x007fffff:	psize = 22; break;
				case 0x01ffffff:	psize = 24; break;
				case 0x07ffffff:	psize = 26; break;
				default:
					printf("invalidate_translation_caches"
					    "_paddr(): bad pagemask?\n");
				}
				tlb_paddr0 = (cpu->cd.mips.coproc[0]->tlbs[i].
				    lo0 & ENTRYLO_PFN_MASK)>>ENTRYLO_PFN_SHIFT;
				tlb_paddr1 = (cpu->cd.mips.coproc[0]->tlbs[i].
				    lo1 & ENTRYLO_PFN_MASK)>>ENTRYLO_PFN_SHIFT;
				tlb_paddr0 <<= phys_shift;
				tlb_paddr1 <<= phys_shift;
				if (cpu->cd.mips.cpu_type.mmu_model == MMU10K) {
					tlb_vaddr = cpu->cd.mips.coproc[0]->
					    tlbs[i].hi & ENTRYHI_VPN2_MASK_R10K;
					if (tlb_vaddr & ((int64_t)1 << 43))
						tlb_vaddr |=
						    0xfffff00000000000ULL;
				} else {
					tlb_vaddr = cpu->cd.mips.coproc[0]->
					    tlbs[i].hi & ENTRYHI_VPN2_MASK;
					if (tlb_vaddr & ((int64_t)1 << 39))
						tlb_vaddr |=
						    0xffffff0000000000ULL;
				}
				if ((cpu->cd.mips.coproc[0]->tlbs[i].lo0 &
				    ENTRYLO_V) && paddr >= tlb_paddr0 &&
				    paddr < tlb_paddr0 + (1<<psize)) {
					p2 = 1 << psize;
					for (p=0; p<p2; p+=4096)
						invalidate_table_entry(cpu,
						    tlb_vaddr + p);
				}
				if ((cpu->cd.mips.coproc[0]->tlbs[i].lo1 &
				    ENTRYLO_V) && paddr >= tlb_paddr1 &&
				    paddr < tlb_paddr1 + (1<<psize)) {
					p2 = 1 << psize;
					for (p=0; p<p2; p+=4096)
						invalidate_table_entry(cpu,
						    tlb_vaddr + p +
						    (1 << psize));
				}
			}
		}
#endif

		if (paddr < 0x20000000) {
			invalidate_table_entry(cpu, 0xffffffff80000000ULL
			    + paddr);
			invalidate_table_entry(cpu, 0xffffffffa0000000ULL
			    + paddr);
		}
	}

#if 0
{
	int i;

	/*  TODO: Don't invalidate everything.  */
	for (i=0; i<N_BINTRANS_VADDR_TO_HOST; i++)
		cpu->bintrans_data_hostpage[i] = NULL;
}
#endif
}


/*
 *  invalidate_translation_caches():
 *
 *  This is necessary for every change to the TLB, and when the ASID is changed,
 *  so that for example user-space addresses are not cached when they should
 *  not be.
 */
static void invalidate_translation_caches(struct cpu *cpu,
	int all, uint64_t vaddr, int kernelspace, int old_asid_to_invalidate)
{
	int i;

	/*  printf("inval(all=%i, kernel=%i, addr=%016llx)\n",
	    all, kernelspace, (long long)vaddr);  */

	if (!cpu->machine->bintrans_enable)
		goto nobintrans;

	if (all) {
		int i;
		uint64_t tlb_vaddr;
		switch (cpu->cd.mips.cpu_type.mmu_model) {
		case MMU3K:
			for (i=0; i<64; i++) {
				tlb_vaddr = cpu->cd.mips.coproc[0]->tlbs[i].hi
				    & R2K3K_ENTRYHI_VPN_MASK;
				tlb_vaddr = (int64_t)(int32_t)tlb_vaddr;
				if ((cpu->cd.mips.coproc[0]->tlbs[i].lo0 &
				    R2K3K_ENTRYLO_V) && (tlb_vaddr &
				    0xc0000000ULL) != 0x80000000ULL) {
					int asid = (cpu->cd.mips.coproc[0]->
					    tlbs[i].hi & R2K3K_ENTRYHI_ASID_MASK
					    ) >> R2K3K_ENTRYHI_ASID_SHIFT;
					if (old_asid_to_invalidate < 0 ||
					    old_asid_to_invalidate == asid)
						invalidate_table_entry(cpu,
						    tlb_vaddr);
				}
			}
			break;
		default:
			for (i=0; i<cpu->cd.mips.coproc[0]->nr_of_tlbs; i++) {
				int psize = 10, or_pmask = 0x1fff;
				int phys_shift = 12;

				if (cpu->cd.mips.cpu_type.rev == MIPS_R4100) {
					or_pmask = 0x7ff;
					phys_shift = 10;
				}

				switch (cpu->cd.mips.coproc[0]->tlbs[i].mask
				    | or_pmask) {
				case 0x000007ff:	psize = 10; break;
				case 0x00001fff:	psize = 12; break;
				case 0x00007fff:	psize = 14; break;
				case 0x0001ffff:	psize = 16; break;
				case 0x0007ffff:	psize = 18; break;
				case 0x001fffff:	psize = 20; break;
				case 0x007fffff:	psize = 22; break;
				case 0x01ffffff:	psize = 24; break;
				case 0x07ffffff:	psize = 26; break;
				default:
					printf("invalidate_translation_caches"
					    "(): bad pagemask?\n");
				}

				if (cpu->cd.mips.cpu_type.mmu_model == MMU10K) {
					tlb_vaddr = cpu->cd.mips.coproc[0]->
					    tlbs[i].hi & ENTRYHI_VPN2_MASK_R10K;
					if (tlb_vaddr & ((int64_t)1 << 43))
						tlb_vaddr |=
						    0xfffff00000000000ULL;
				} else {
					tlb_vaddr = cpu->cd.mips.coproc[0]->
					    tlbs[i].hi & ENTRYHI_VPN2_MASK;
					if (tlb_vaddr & ((int64_t)1 << 39))
						tlb_vaddr |=
						    0xffffff0000000000ULL;
				}

				/*  TODO: Check the ASID etc.  */

				invalidate_table_entry(cpu, tlb_vaddr);
				invalidate_table_entry(cpu, tlb_vaddr |
				    (1 << psize));
			}
		}
	} else
		invalidate_table_entry(cpu, vaddr);

nobintrans:

	/*  TODO: Don't invalidate everything.  */
	for (i=0; i<N_BINTRANS_VADDR_TO_HOST; i++)
		cpu->cd.mips.bintrans_data_hostpage[i] = NULL;

	if (kernelspace)
		all = 1;

#ifdef USE_TINY_CACHE
{
	vaddr >>= 12;

	/*  Invalidate the tiny translation cache...  */
	if (!cpu->machine->bintrans_enable)
		for (i=0; i<N_TRANSLATION_CACHE_INSTR; i++)
			if (all || vaddr == (cpu->cd.mips.
			    translation_cache_instr[i].vaddr_pfn))
				cpu->cd.mips.translation_cache_instr[i].wf = 0;

	if (!cpu->machine->bintrans_enable)
		for (i=0; i<N_TRANSLATION_CACHE_DATA; i++)
			if (all || vaddr == (cpu->cd.mips.
			    translation_cache_data[i].vaddr_pfn))
				cpu->cd.mips.translation_cache_data[i].wf = 0;
}
#endif
}


/*
 *  coproc_register_read();
 *
 *  Read a value from a MIPS coprocessor register.
 */
void coproc_register_read(struct cpu *cpu,
	struct mips_coproc *cp, int reg_nr, uint64_t *ptr, int select)
{
	int unimpl = 1;

	if (cp->coproc_nr==0 && reg_nr==COP0_INDEX)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_RANDOM)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_ENTRYLO0)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_ENTRYLO1)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_CONTEXT)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_PAGEMASK)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_WIRED)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_BADVADDR)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_COUNT) {
		/*
		 *  This speeds up delay-loops that just read the count
		 *  register until it has reached a certain value. (Only for
		 *  R4000 etc.)
		 *
		 *  TODO: Maybe this should be optional?
		 */
		if (cpu->cd.mips.cpu_type.exc_model != EXC3K) {
			int increase = 500;
			int32_t x = cp->reg[COP0_COUNT];
			int32_t y = cp->reg[COP0_COMPARE];
			int32_t diff = x - y;
			if (diff < 0 && diff + increase >= 0
			    && cpu->cd.mips.compare_register_set) {
				mips_cpu_interrupt(cpu, 7);
				cpu->cd.mips.compare_register_set = 0;
			}
			cp->reg[COP0_COUNT] = (int64_t)
			    (int32_t)(cp->reg[COP0_COUNT] + increase);
		}

		unimpl = 0;
	}
	if (cp->coproc_nr==0 && reg_nr==COP0_ENTRYHI)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_COMPARE)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_STATUS)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_CAUSE)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_EPC)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_PRID)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_CONFIG) {
		if (select > 0) {
			switch (select) {
			case 1:	*ptr = cpu->cd.mips.cop0_config_select1;
				break;
			default:fatal("coproc_register_read(): unimplemented"
				    " config register select %i\n", select);
				exit(1);
			}
			return;
		}
		unimpl = 0;
	}
	if (cp->coproc_nr==0 && reg_nr==COP0_LLADDR)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_WATCHLO)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_WATCHHI)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_XCONTEXT)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_ERRCTL)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_CACHEERR)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_TAGDATA_LO)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_TAGDATA_HI)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_ERROREPC)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_RESERV22) {
		/*  Used by Linux on Linksys WRT54G  */
		unimpl = 0;
	}
	if (cp->coproc_nr==0 && reg_nr==COP0_DEBUG)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_PERFCNT)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_DESAVE)	unimpl = 0;

	if (cp->coproc_nr==1)	unimpl = 0;

	if (unimpl) {
		fatal("cpu%i: warning: read from unimplemented coproc%i"
		    " register %i (%s)\n", cpu->cpu_id, cp->coproc_nr, reg_nr,
		    cp->coproc_nr==0? cop0_names[reg_nr] : "?");

		mips_cpu_exception(cpu, EXCEPTION_CPU, 0, 0,
		    cp->coproc_nr, 0, 0, 0);
		return;
	}

	*ptr = cp->reg[reg_nr];
}


/*
 *  coproc_register_write();
 *
 *  Write a value to a MIPS coprocessor register.
 */
void coproc_register_write(struct cpu *cpu,
	struct mips_coproc *cp, int reg_nr, uint64_t *ptr, int flag64,
	int select)
{
	int unimpl = 1;
	int readonly = 0;
	uint64_t tmp = *ptr;
	uint64_t tmp2 = 0, old;
	int inval = 0, old_asid, oldmode;

	switch (cp->coproc_nr) {
	case 0:
		/*  COPROC 0:  */
		switch (reg_nr) {
		case COP0_INDEX:
		case COP0_RANDOM:
			unimpl = 0;
			break;
		case COP0_ENTRYLO0:
			unimpl = 0;
			if (cpu->cd.mips.cpu_type.mmu_model == MMU3K &&
			    (tmp & 0xff)!=0) {
				/*  char *symbol;
				    uint64_t offset;
				    symbol = get_symbol_name(
				    cpu->cd.mips.pc_last, &offset);
				    fatal("YO! pc = 0x%08llx <%s> "
				    "lo=%016llx\n", (long long)
				    cpu->cd.mips.pc_last, symbol? symbol :
				    "no symbol", (long long)tmp); */
				tmp &= (R2K3K_ENTRYLO_PFN_MASK |
				    R2K3K_ENTRYLO_N | R2K3K_ENTRYLO_D |
				    R2K3K_ENTRYLO_V | R2K3K_ENTRYLO_G);
			} else if (cpu->cd.mips.cpu_type.mmu_model == MMU4K) {
				tmp &= (ENTRYLO_PFN_MASK | ENTRYLO_C_MASK |
				    ENTRYLO_D | ENTRYLO_V | ENTRYLO_G);
			}
			break;
		case COP0_BADVADDR:
			/*  Hm. Irix writes to this register. (Why?)  */
			unimpl = 0;
			break;
		case COP0_ENTRYLO1:
			unimpl = 0;
			if (cpu->cd.mips.cpu_type.mmu_model == MMU4K) {
				tmp &= (ENTRYLO_PFN_MASK | ENTRYLO_C_MASK |
				    ENTRYLO_D | ENTRYLO_V | ENTRYLO_G);
			}
			break;
		case COP0_CONTEXT:
			old = cp->reg[COP0_CONTEXT];
			cp->reg[COP0_CONTEXT] = tmp;
			if (cpu->cd.mips.cpu_type.mmu_model == MMU3K) {
				cp->reg[COP0_CONTEXT] &=
				    ~R2K3K_CONTEXT_BADVPN_MASK;
				cp->reg[COP0_CONTEXT] |=
				    (old & R2K3K_CONTEXT_BADVPN_MASK);
			} else if (cpu->cd.mips.cpu_type.rev == MIPS_R4100) {
				cp->reg[COP0_CONTEXT] &=
				    ~CONTEXT_BADVPN2_MASK_R4100;
				cp->reg[COP0_CONTEXT] |=
				    (old & CONTEXT_BADVPN2_MASK_R4100);
			} else {
				cp->reg[COP0_CONTEXT] &=
				    ~CONTEXT_BADVPN2_MASK;
				cp->reg[COP0_CONTEXT] |=
				    (old & CONTEXT_BADVPN2_MASK);
			}
			return;
		case COP0_PAGEMASK:
			tmp2 = tmp >> PAGEMASK_SHIFT;
			if (tmp2 != 0x000 &&
			    tmp2 != 0x003 &&
			    tmp2 != 0x00f &&
			    tmp2 != 0x03f &&
			    tmp2 != 0x0ff &&
			    tmp2 != 0x3ff &&
			    tmp2 != 0xfff)
				fatal("cpu%i: trying to write an invalid"
				    " pagemask 0x%08lx to COP0_PAGEMASK\n",
				    cpu->cpu_id, (long)tmp);
			unimpl = 0;
			break;
		case COP0_WIRED:
			if (cpu->cd.mips.cpu_type.mmu_model == MMU3K) {
				fatal("cpu%i: r2k/r3k wired register must "
				    "always be 8\n", cpu->cpu_id);
				tmp = 8;
			}
			cp->reg[COP0_RANDOM] = cp->nr_of_tlbs-1;
			tmp &= INDEX_MASK;
			unimpl = 0;
			break;
		case COP0_COUNT:
			if (tmp != (int64_t)(int32_t)tmp)
				fatal("WARNING: trying to write a 64-bit value"
				    " to the COUNT register!\n");
			tmp = (int64_t)(int32_t)tmp;
			unimpl = 0;
			break;
		case COP0_COMPARE:
			/*  Clear the timer interrupt bit (bit 7):  */
			cpu->cd.mips.compare_register_set = 1;
			mips_cpu_interrupt_ack(cpu, 7);
			if (tmp != (int64_t)(int32_t)tmp)
				fatal("WARNING: trying to write a 64-bit value"
				    " to the COMPARE register!\n");
			tmp = (int64_t)(int32_t)tmp;
			unimpl = 0;
			break;
		case COP0_ENTRYHI:
			/*
			 *  Translation caches must be invalidated, because the
			 *  address space might change (if the ASID changes).
			 */
			switch (cpu->cd.mips.cpu_type.mmu_model) {
			case MMU3K:
				old_asid = (cp->reg[COP0_ENTRYHI] &
				    R2K3K_ENTRYHI_ASID_MASK) >>
				    R2K3K_ENTRYHI_ASID_SHIFT;
				if ((cp->reg[COP0_ENTRYHI] &
				    R2K3K_ENTRYHI_ASID_MASK) !=
				    (tmp & R2K3K_ENTRYHI_ASID_MASK))
					inval = 1;
				break;
			default:
				old_asid = cp->reg[COP0_ENTRYHI] & ENTRYHI_ASID;
				if ((cp->reg[COP0_ENTRYHI] & ENTRYHI_ASID) !=
				    (tmp & ENTRYHI_ASID))
					inval = 1;
				break;
			}
			if (inval)
				invalidate_translation_caches(cpu, 1, 0, 0,
				    old_asid);
			unimpl = 0;
			if (cpu->cd.mips.cpu_type.mmu_model == MMU3K &&
			    (tmp & 0x3f)!=0) {
				/* char *symbol;
				   uint64_t offset;
				   symbol = get_symbol_name(cpu->
				    cd.mips.pc_last, &offset);
				   fatal("YO! pc = 0x%08llx <%s> "
				    "hi=%016llx\n", (long long)cpu->
				    cd.mips.pc_last, symbol? symbol :
				    "no symbol", (long long)tmp);  */
				tmp &= ~0x3f;
			}
			if (cpu->cd.mips.cpu_type.mmu_model == MMU3K)
				tmp &= (R2K3K_ENTRYHI_VPN_MASK |
				    R2K3K_ENTRYHI_ASID_MASK);
			else if (cpu->cd.mips.cpu_type.mmu_model == MMU10K)
				tmp &= (ENTRYHI_R_MASK |
				    ENTRYHI_VPN2_MASK_R10K | ENTRYHI_ASID);
			else if (cpu->cd.mips.cpu_type.rev == MIPS_R4100)
				tmp &= (ENTRYHI_R_MASK | ENTRYHI_VPN2_MASK |
				    0x1800 | ENTRYHI_ASID);
			else
				tmp &= (ENTRYHI_R_MASK | ENTRYHI_VPN2_MASK |
				    ENTRYHI_ASID);
			break;
		case COP0_EPC:
			unimpl = 0;
			break;
		case COP0_PRID:
			readonly = 1;
			break;
		case COP0_CONFIG:
			if (select > 0) {
				switch (select) {
				case 1:	cpu->cd.mips.cop0_config_select1 = tmp;
					break;
				default:fatal("coproc_register_write(): unimpl"
					    "emented config register select "
					    "%i\n", select);
					exit(1);
				}
				return;
			}

			/*  fatal("COP0_CONFIG: modifying K0 bits: "
			    "0x%08x => ", cp->reg[reg_nr]);  */
			tmp = *ptr;
			tmp &= 0x3;	/*  only bits 2..0 can be written  */
			cp->reg[reg_nr] &= ~(0x3);  cp->reg[reg_nr] |= tmp;
			/*  fatal("0x%08x\n", cp->reg[reg_nr]);  */
			return;
		case COP0_STATUS:
			oldmode = cp->reg[COP0_STATUS];
			tmp &= ~(1 << 21);	/*  bit 21 is read-only  */
#if 0
/*  Why was this here? It should not be necessary.  */

			/*  Changing from kernel to user mode? Then
			    invalidate some translation caches:  */
			if (cpu->cd.mips.cpu_type.mmu_model == MMU3K) {
				if (!(oldmode & MIPS1_SR_KU_CUR)
				    && (tmp & MIPS1_SR_KU_CUR))
					invalidate_translation_caches(cpu,
					    0, 0, 1, 0);
			} else {
				/*  TODO: don't hardcode  */
				if ((oldmode & 0xff) != (tmp & 0xff))
					invalidate_translation_caches(
					    cpu, 0, 0, 1, 0);
			}
#endif

			if (cpu->cd.mips.cpu_type.mmu_model == MMU3K &&
			    (oldmode & MIPS1_ISOL_CACHES) !=
			    (tmp & MIPS1_ISOL_CACHES)) {
				/*  R3000-style caches when isolated are
				    treated in bintrans mode by changing
				    the vaddr_to_hostaddr_table0 pointer:  */
				if (tmp & MIPS1_ISOL_CACHES) {
					/*  2-level table:  */
					cpu->cd.mips.vaddr_to_hostaddr_table0 =
					  tmp & MIPS1_SWAP_CACHES?
					  cpu->cd.mips.
					  vaddr_to_hostaddr_table0_cacheisol_i
					  : cpu->cd.mips.
					  vaddr_to_hostaddr_table0_cacheisol_d;

					/*  1M-entry table:  */
					cpu->cd.mips.host_load =
					    cpu->cd.mips.host_store =
					    cpu->cd.mips.huge_r2k3k_cache_table;
				} else {
					/*  2-level table:  */
					cpu->cd.mips.vaddr_to_hostaddr_table0 =
					    cpu->cd.mips.
						vaddr_to_hostaddr_table0_kernel;

					/*  TODO: cpu->cd.mips.
					    vaddr_to_hostaddr_table0_user;  */

					/*  1M-entry table:  */
					cpu->cd.mips.host_load =
					    cpu->cd.mips.host_load_orig;
					cpu->cd.mips.host_store =
					    cpu->cd.mips.host_store_orig;
				}
			}
			unimpl = 0;
			break;
		case COP0_CAUSE:
			/*  A write to the cause register only
			    affects IM bits 0 and 1:  */
			cp->reg[reg_nr] &= ~(0x3 << STATUS_IM_SHIFT);
			cp->reg[reg_nr] |= (tmp & (0x3 << STATUS_IM_SHIFT));
			if (!(cp->reg[COP0_CAUSE] & STATUS_IM_MASK))
		                cpu->cd.mips.cached_interrupt_is_possible = 0;
			else
		                cpu->cd.mips.cached_interrupt_is_possible = 1;
			return;
		case COP0_FRAMEMASK:
			/*  TODO: R10000  */
			unimpl = 0;
			break;
		case COP0_TAGDATA_LO:
		case COP0_TAGDATA_HI:
			/*  TODO: R4300 and others?  */
			unimpl = 0;
			break;
		case COP0_LLADDR:
			unimpl = 0;
			break;
		case COP0_WATCHLO:
		case COP0_WATCHHI:
			unimpl = 0;
			break;
		case COP0_XCONTEXT:
			/*
			 *  TODO:  According to the R10000 manual, the R4400
			 *  shares the PTEbase portion of the context registers
			 *  (that is, xcontext and context). On R10000, they
			 *  are separate registers.
			 */
			/*  debug("[ xcontext 0x%016llx ]\n", tmp);  */
			unimpl = 0;
			break;

		/*  Most of these are actually TODOs:  */
		case COP0_ERROREPC:
		case COP0_DEPC:
		case COP0_RESERV22:	/*  Used by Linux on Linksys WRT54G  */
		case COP0_DESAVE:
		case COP0_PERFCNT:
		case COP0_ERRCTL:	/*  R10000  */
			unimpl = 0;
			break;
		}
		break;

	case 1:
		/*  COPROC 1:  */
		unimpl = 0;
		break;
	}

	if (unimpl) {
		fatal("cpu%i: warning: write to unimplemented coproc%i "
		    "register %i (%s), data = 0x%016llx\n", cpu->cpu_id,
		    cp->coproc_nr, reg_nr, cp->coproc_nr==0?
		    cop0_names[reg_nr] : "?", (long long)tmp);

		mips_cpu_exception(cpu, EXCEPTION_CPU, 0, 0,
		    cp->coproc_nr, 0, 0, 0);
		return;
	}

	if (readonly) {
		fatal("cpu%i: warning: write to READONLY coproc%i register "
		    "%i ignored\n", cpu->cpu_id, cp->coproc_nr, reg_nr);
		return;
	}

	cp->reg[reg_nr] = tmp;

	if (!flag64)
		cp->reg[reg_nr] = (int64_t)(int32_t)cp->reg[reg_nr];
}


/*
 *  MIPS floating-point stuff:
 *
 *  TODO:  Move this to some other file?
 */
#define	FMT_S		16
#define	FMT_D		17
#define	FMT_W		20
#define	FMT_L		21
#define	FMT_PS		22

#define	FPU_OP_ADD	1
#define	FPU_OP_SUB	2
#define	FPU_OP_MUL	3
#define	FPU_OP_DIV	4
#define	FPU_OP_SQRT	5
#define	FPU_OP_MOV	6
#define	FPU_OP_CVT	7
#define	FPU_OP_C	8
#define	FPU_OP_ABS	9
#define	FPU_OP_NEG	10
/*  TODO: CEIL.L, CEIL.W, FLOOR.L, FLOOR.W, RECIP, ROUND.L, ROUND.W,
 RSQRT  */


struct internal_float_value {
	double	f;
	int	nan;
};


/*
 *  fpu_interpret_float_value():
 *
 *  Interprets a float value from binary IEEE format into an
 *  internal_float_value struct.
 */
static void fpu_interpret_float_value(uint64_t reg,
	struct internal_float_value *fvp, int fmt)
{
	int n_frac = 0, n_exp = 0;
	int i, nan, sign = 0, exponent;
	double fraction;

	memset(fvp, 0, sizeof(struct internal_float_value));

	/*  n_frac and n_exp:  */
	switch (fmt) {
	case FMT_S:	n_frac = 23; n_exp = 8; break;
	case FMT_W:	n_frac = 31; n_exp = 0; break;
	case FMT_D:	n_frac = 52; n_exp = 11; break;
	case FMT_L:	n_frac = 63; n_exp = 0; break;
	default:
		fatal("fpu_interpret_float_value(): "
		    "unimplemented format %i\n", fmt);
	}

	/*  exponent:  */
	exponent = 0;
	switch (fmt) {
	case FMT_W:
		reg &= 0xffffffffULL;
	case FMT_L:
		break;
	case FMT_S:
		reg &= 0xffffffffULL;
	case FMT_D:
		exponent = (reg >> n_frac) & ((1 << n_exp) - 1);
		exponent -= (1 << (n_exp-1)) - 1;
		break;
	default:
		fatal("fpu_interpret_float_value(): unimplemented "
		    "format %i\n", fmt);
	}

	/*  nan:  */
	nan = 0;
	switch (fmt) {
	case FMT_S:
		if (reg == 0x7fffffffULL || reg == 0x7fbfffffULL)
			nan = 1;
		break;
	case FMT_D:
		if (reg == 0x7fffffffffffffffULL ||
		    reg == 0x7ff7ffffffffffffULL)
			nan = 1;
		break;
	}

	if (nan) {
		fvp->f = 1.0;
		goto no_reasonable_result;
	}

	/*  fraction:  */
	fraction = 0.0;
	switch (fmt) {
	case FMT_W:
		{
			int32_t r_int = reg;
			fraction = r_int;
		}
		break;
	case FMT_L:
		{
			int64_t r_int = reg;
			fraction = r_int;
		}
		break;
	case FMT_S:
	case FMT_D:
		/*  sign:  */
		sign = (reg >> 31) & 1;
		if (fmt == FMT_D)
			sign = (reg >> 63) & 1;

		fraction = 0.0;
		for (i=0; i<n_frac; i++) {
			int bit = (reg >> i) & 1;
			fraction /= 2.0;
			if (bit)
				fraction += 1.0;
		}
		/*  Add implicit bit 0:  */
		fraction = (fraction / 2.0) + 1.0;
		break;
	default:
		fatal("fpu_interpret_float_value(): "
		    "unimplemented format %i\n", fmt);
	}

	/*  form the value:  */
	fvp->f = fraction;

	/*  fatal("load  reg=%016llx sign=%i exponent=%i fraction=%f ",
	    (long long)reg, sign, exponent, fraction);  */

	/*  TODO: this is awful for exponents of large magnitude.  */
	if (exponent > 0) {
		/*
		 *  NOTE / TODO:
		 *
		 *  This is an ulgy workaround on Alpha, where it seems that
		 *  multiplying by 2, 1024 times causes a floating point
		 *  exception. (Triggered by running for example NetBSD/pmax
		 *  2.0 on an Alpha.)
		 */
		if (exponent == 1024)
			exponent = 1023;

		while (exponent-- > 0)
			fvp->f *= 2.0;
	} else if (exponent < 0) {
		while (exponent++ < 0)
			fvp->f /= 2.0;
	}

	if (sign)
		fvp->f = -fvp->f;

no_reasonable_result:
	fvp->nan = nan;

	/*  fatal("f = %f\n", fvp->f);  */
}


/*
 *  fpu_store_float_value():
 *
 *  Stores a float value (actually a double) in fmt format.
 */
static void fpu_store_float_value(struct mips_coproc *cp, int fd,
	double nf, int fmt, int nan)
{
	int n_frac = 0, n_exp = 0, signofs=0;
	int i, exponent;
	uint64_t r = 0, r2;
	int64_t r3;

	/*  n_frac and n_exp:  */
	switch (fmt) {
	case FMT_S:	n_frac = 23; n_exp = 8; signofs = 31; break;
	case FMT_W:	n_frac = 31; n_exp = 0; signofs = 31; break;
	case FMT_D:	n_frac = 52; n_exp = 11; signofs = 63; break;
	case FMT_L:	n_frac = 63; n_exp = 0; signofs = 63; break;
	default:
		fatal("fpu_store_float_value(): unimplemented format"
		    " %i\n", fmt);
	}

	if ((fmt == FMT_S || fmt == FMT_D) && nan)
		goto store_nan;

	/*  fraction:  */
	switch (fmt) {
	case FMT_W:
	case FMT_L:
		/*
		 *  This causes an implicit conversion of double to integer.
		 *  If nf < 0.0, then r2 will begin with a sequence of binary
		 *  1's, which is ok.
		 */
		r3 = nf;
		r2 = r3;
		r |= r2;

		if (fmt == FMT_W)
			r &= 0xffffffffULL;
		break;
	case FMT_S:
	case FMT_D:
		/*  fatal("store f=%f ", nf);  */

		/*  sign bit:  */
		if (nf < 0.0) {
			r |= ((uint64_t)1 << signofs);
			nf = -nf;
		}

		/*
		 *  How to convert back from double to exponent + fraction:
		 *  We want fraction to be 1.xxx, that is
		 *  1.0 <= fraction < 2.0
		 *
		 *  This method is very slow but should work:
		 */
		exponent = 0;
		while (nf < 1.0 && exponent > -1023) {
			nf *= 2.0;
			exponent --;
		}
		while (nf >= 2.0 && exponent < 1023) {
			nf /= 2.0;
			exponent ++;
		}

		/*  Here:   1.0 <= nf < 2.0  */
		/*  fatal(" nf=%f", nf);  */
		nf -= 1.0;	/*  remove implicit first bit  */
		for (i=n_frac-1; i>=0; i--) {
			nf *= 2.0;
			if (nf >= 1.0) {
				r |= ((uint64_t)1 << i);
				nf -= 1.0;
			}
			/*  printf("\n i=%2i r=%016llx\n", i, (long long)r);  */
		}

		/*  Insert the exponent into the resulting word:  */
		/*  (First bias, then make sure it's within range)  */
		exponent += (((uint64_t)1 << (n_exp-1)) - 1);
		if (exponent < 0)
			exponent = 0;
		if (exponent >= ((int64_t)1 << n_exp))
			exponent = ((int64_t)1 << n_exp) - 1;
		r |= (uint64_t)exponent << n_frac;

		/*  Special case for 0.0:  */
		if (exponent == 0)
			r = 0;

		/*  fatal(" exp=%i, r = %016llx\n", exponent, (long long)r);  */

		break;
	default:
		/*  TODO  */
		fatal("fpu_store_float_value(): unimplemented format "
		    "%i\n", fmt);
	}

store_nan:
	if (nan) {
		if (fmt == FMT_S)
			r = 0x7fffffffULL;
		else if (fmt == FMT_D)
			r = 0x7fffffffffffffffULL;
		else
			r = 0x7fffffffULL;
	}

	/*
	 *  TODO:  this is for 32-bit mode. It has to be updated later
	 *		for 64-bit coprocessor stuff.
	 */
	if (fmt == FMT_D || fmt == FMT_L) {
		cp->reg[fd] = r & 0xffffffffULL;
		cp->reg[(fd+1) & 31] = (r >> 32) & 0xffffffffULL;

		if (cp->reg[fd] & 0x80000000ULL)
			cp->reg[fd] |= 0xffffffff00000000ULL;
		if (cp->reg[fd+1] & 0x80000000ULL)
			cp->reg[fd+1] |= 0xffffffff00000000ULL;
	} else {
		cp->reg[fd] = r & 0xffffffffULL;

		if (cp->reg[fd] & 0x80000000ULL)
			cp->reg[fd] |= 0xffffffff00000000ULL;
	}
}


/*
 *  fpu_op():
 *
 *  Perform a floating-point operation.  For those of fs and ft
 *  that are >= 0, those numbers are interpreted into local
 *  variables.
 *
 *  Only FPU_OP_C (compare) returns anything of interest, 1 for
 *  true, 0 for false.
 */
static int fpu_op(struct cpu *cpu, struct mips_coproc *cp, int op, int fmt,
	int ft, int fs, int fd, int cond, int output_fmt)
{
	/*  Potentially two input registers, fs and ft  */
	struct internal_float_value float_value[2];
	int unordered, nan;
	uint64_t fs_v = 0;
	double nf;

	if (fs >= 0) {
		fs_v = cp->reg[fs];
		/*  TODO: register-pair mode and plain
		    register mode? "FR" bit?  */
		if (fmt == FMT_D || fmt == FMT_L)
			fs_v = (fs_v & 0xffffffffULL) +
			    (cp->reg[(fs + 1) & 31] << 32);
		fpu_interpret_float_value(fs_v, &float_value[0], fmt);
	}
	if (ft >= 0) {
		uint64_t v = cp->reg[ft];
		/*  TODO: register-pair mode and
		    plain register mode? "FR" bit?  */
		if (fmt == FMT_D || fmt == FMT_L)
			v = (v & 0xffffffffULL) +
			    (cp->reg[(ft + 1) & 31] << 32);
		fpu_interpret_float_value(v, &float_value[1], fmt);
	}

	switch (op) {
	case FPU_OP_ADD:
		nf = float_value[0].f + float_value[1].f;
		/*  debug("  add: %f + %f = %f\n",
		    float_value[0].f, float_value[1].f, nf);  */
		fpu_store_float_value(cp, fd, nf, output_fmt,
		    float_value[0].nan || float_value[1].nan);
		break;
	case FPU_OP_SUB:
		nf = float_value[0].f - float_value[1].f;
		/*  debug("  sub: %f - %f = %f\n",
		    float_value[0].f, float_value[1].f, nf);  */
		fpu_store_float_value(cp, fd, nf, output_fmt,
		    float_value[0].nan || float_value[1].nan);
		break;
	case FPU_OP_MUL:
		nf = float_value[0].f * float_value[1].f;
		/*  debug("  mul: %f * %f = %f\n",
		    float_value[0].f, float_value[1].f, nf);  */
		fpu_store_float_value(cp, fd, nf, output_fmt,
		    float_value[0].nan || float_value[1].nan);
		break;
	case FPU_OP_DIV:
		nan = float_value[0].nan || float_value[1].nan;
		if (fabs(float_value[1].f) > 0.00000000001)
			nf = float_value[0].f / float_value[1].f;
		else {
			fatal("DIV by zero !!!!\n");
			nf = 0.0;	/*  TODO  */
			nan = 1;
		}
		/*  debug("  div: %f / %f = %f\n",
		    float_value[0].f, float_value[1].f, nf);  */
		fpu_store_float_value(cp, fd, nf, output_fmt, nan);
		break;
	case FPU_OP_SQRT:
		nan = float_value[0].nan;
		if (float_value[0].f >= 0.0)
			nf = sqrt(float_value[0].f);
		else {
			fatal("SQRT by less than zero, %f !!!!\n",
			    float_value[0].f);
			nf = 0.0;	/*  TODO  */
			nan = 1;
		}
		/*  debug("  sqrt: %f => %f\n", float_value[0].f, nf);  */
		fpu_store_float_value(cp, fd, nf, output_fmt, nan);
		break;
	case FPU_OP_ABS:
		nf = fabs(float_value[0].f);
		/*  debug("  abs: %f => %f\n", float_value[0].f, nf);  */
		fpu_store_float_value(cp, fd, nf, output_fmt,
		    float_value[0].nan);
		break;
	case FPU_OP_NEG:
		nf = - float_value[0].f;
		/*  debug("  neg: %f => %f\n", float_value[0].f, nf);  */
		fpu_store_float_value(cp, fd, nf, output_fmt,
		    float_value[0].nan);
		break;
	case FPU_OP_CVT:
		nf = float_value[0].f;
		/*  debug("  mov: %f => %f\n", float_value[0].f, nf);  */
		fpu_store_float_value(cp, fd, nf, output_fmt,
		    float_value[0].nan);
		break;
	case FPU_OP_MOV:
		/*  Non-arithmetic move:  */
		/*
		 *  TODO:  this is for 32-bit mode. It has to be updated later
		 *		for 64-bit coprocessor stuff.
		 */
		if (output_fmt == FMT_D || output_fmt == FMT_L) {
			cp->reg[fd] = fs_v & 0xffffffffULL;
			cp->reg[(fd+1) & 31] = (fs_v >> 32) & 0xffffffffULL;
			if (cp->reg[fd] & 0x80000000ULL)
				cp->reg[fd] |= 0xffffffff00000000ULL;
			if (cp->reg[fd+1] & 0x80000000ULL)
				cp->reg[fd+1] |= 0xffffffff00000000ULL;
		} else {
			cp->reg[fd] = fs_v & 0xffffffffULL;
			if (cp->reg[fd] & 0x80000000ULL)
				cp->reg[fd] |= 0xffffffff00000000ULL;
		}
		break;
	case FPU_OP_C:
		/*  debug("  c: cond=%i\n", cond);  */

		unordered = 0;
		if (float_value[0].nan || float_value[1].nan)
			unordered = 1;

		switch (cond) {
		case 2:	/*  Equal  */
			return (float_value[0].f == float_value[1].f);
		case 4:	/*  Ordered or Less than  */
			return (float_value[0].f < float_value[1].f)
			    || !unordered;
		case 5:	/*  Unordered or Less than  */
			return (float_value[0].f < float_value[1].f)
			    || unordered;
		case 6:	/*  Ordered or Less than or Equal  */
			return (float_value[0].f <= float_value[1].f)
			    || !unordered;
		case 7:	/*  Unordered or Less than or Equal  */
			return (float_value[0].f <= float_value[1].f)
			    || unordered;
		case 12:/*  Less than  */
			return (float_value[0].f < float_value[1].f);
		case 14:/*  Less than or equal  */
			return (float_value[0].f <= float_value[1].f);

		/*  The following are not commonly used, so I'll move these out
		    of the if-0 on a case-by-case basis.  */
#if 0
case 0:	return 0;					/*  False  */
case 1:	return 0;					/*  Unordered  */
case 3:	return (float_value[0].f == float_value[1].f);
			/*  Unordered or Equal  */
case 8:	return 0;				/*  Signaling false  */
case 9:	return 0;	/*  Not Greater than or Less than or Equal  */
case 10:return (float_value[0].f == float_value[1].f);	/*  Signaling Equal  */
case 11:return (float_value[0].f == float_value[1].f);	/*  Not Greater
		than or Less than  */
case 13:return !(float_value[0].f >= float_value[1].f);	/*  Not greater
		than or equal */
case 15:return !(float_value[0].f > float_value[1].f);	/*  Not greater than  */
#endif

		default:
			fatal("fpu_op(): unimplemented condition "
			    "code %i. see cpu_mips_coproc.c\n", cond);
		}
		break;
	default:
		fatal("fpu_op(): unimplemented op %i\n", op);
	}

	return 0;
}


/*
 *  fpu_function():
 *
 *  Returns 1 if function was implemented, 0 otherwise.
 *  Debug trace should be printed for known instructions.
 */
static int fpu_function(struct cpu *cpu, struct mips_coproc *cp,
	uint32_t function, int unassemble_only)
{
	int fd, fs, ft, fmt, cond, cc;

	fmt = (function >> 21) & 31;
	ft = (function >> 16) & 31;
	fs = (function >> 11) & 31;
	cc = (function >> 8) & 7;
	fd = (function >> 6) & 31;
	cond = (function >> 0) & 15;


	/*  bc1f, bc1t, bc1fl, bc1tl:  */
	if ((function & 0x03e00000) == 0x01000000) {
		int nd, tf, imm, cond_true;
		char *instr_mnem;

		/*  cc are bits 20..18:  */
		cc = (function >> 18) & 7;
		nd = (function >> 17) & 1;
		tf = (function >> 16) & 1;
		imm = function & 65535;
		if (imm >= 32768)
			imm -= 65536;

		instr_mnem = NULL;
		if (nd == 0 && tf == 0)  instr_mnem = "bc1f";
		if (nd == 0 && tf == 1)  instr_mnem = "bc1t";
		if (nd == 1 && tf == 0)  instr_mnem = "bc1fl";
		if (nd == 1 && tf == 1)  instr_mnem = "bc1tl";

		if (cpu->machine->instruction_trace || unassemble_only)
			debug("%s\t%i,0x%016llx\n", instr_mnem, cc,
			    (long long) (cpu->pc + (imm << 2)));
		if (unassemble_only)
			return 1;

		if (cpu->cd.mips.delay_slot) {
			fatal("%s: jump inside a jump's delay slot, "
			    "or similar. TODO\n", instr_mnem);
			cpu->running = 0;
			return 1;
		}

		/*  Both the FCCR and FCSR contain condition code bits...  */
		if (cc == 0)
			cond_true = (cp->fcr[FPU_FCSR] >> FCSR_FCC0_SHIFT) & 1;
		else
			cond_true = (cp->fcr[FPU_FCSR] >>
			    (FCSR_FCC1_SHIFT + cc-1)) & 1;

		if (!tf)
			cond_true = !cond_true;

		if (cond_true) {
			cpu->cd.mips.delay_slot = TO_BE_DELAYED;
			cpu->cd.mips.delay_jmpaddr = cpu->pc + (imm << 2);
		} else {
			/*  "likely":  */
			if (nd) {
				/*  nullify the delay slot  */
				cpu->cd.mips.nullify_next = 1;
			}
		}

		return 1;
	}

	/*  add.fmt: Floating-point add  */
	if ((function & 0x0000003f) == 0x00000000) {
		if (cpu->machine->instruction_trace || unassemble_only)
			debug("add.%i\tr%i,r%i,r%i\n", fmt, fd, fs, ft);
		if (unassemble_only)
			return 1;

		fpu_op(cpu, cp, FPU_OP_ADD, fmt, ft, fs, fd, -1, fmt);
		return 1;
	}

	/*  sub.fmt: Floating-point subtract  */
	if ((function & 0x0000003f) == 0x00000001) {
		if (cpu->machine->instruction_trace || unassemble_only)
			debug("sub.%i\tr%i,r%i,r%i\n", fmt, fd, fs, ft);
		if (unassemble_only)
			return 1;

		fpu_op(cpu, cp, FPU_OP_SUB, fmt, ft, fs, fd, -1, fmt);
		return 1;
	}

	/*  mul.fmt: Floating-point multiply  */
	if ((function & 0x0000003f) == 0x00000002) {
		if (cpu->machine->instruction_trace || unassemble_only)
			debug("mul.%i\tr%i,r%i,r%i\n", fmt, fd, fs, ft);
		if (unassemble_only)
			return 1;

		fpu_op(cpu, cp, FPU_OP_MUL, fmt, ft, fs, fd, -1, fmt);
		return 1;
	}

	/*  div.fmt: Floating-point divide  */
	if ((function & 0x0000003f) == 0x00000003) {
		if (cpu->machine->instruction_trace || unassemble_only)
			debug("div.%i\tr%i,r%i,r%i\n", fmt, fd, fs, ft);
		if (unassemble_only)
			return 1;

		fpu_op(cpu, cp, FPU_OP_DIV, fmt, ft, fs, fd, -1, fmt);
		return 1;
	}

	/*  sqrt.fmt: Floating-point square-root  */
	if ((function & 0x001f003f) == 0x00000004) {
		if (cpu->machine->instruction_trace || unassemble_only)
			debug("sqrt.%i\tr%i,r%i\n", fmt, fd, fs);
		if (unassemble_only)
			return 1;

		fpu_op(cpu, cp, FPU_OP_SQRT, fmt, -1, fs, fd, -1, fmt);
		return 1;
	}

	/*  abs.fmt: Floating-point absolute value  */
	if ((function & 0x001f003f) == 0x00000005) {
		if (cpu->machine->instruction_trace || unassemble_only)
			debug("abs.%i\tr%i,r%i\n", fmt, fd, fs);
		if (unassemble_only)
			return 1;

		fpu_op(cpu, cp, FPU_OP_ABS, fmt, -1, fs, fd, -1, fmt);
		return 1;
	}

	/*  mov.fmt: Floating-point (non-arithmetic) move  */
	if ((function & 0x0000003f) == 0x00000006) {
		if (cpu->machine->instruction_trace || unassemble_only)
			debug("mov.%i\tr%i,r%i\n", fmt, fd, fs);
		if (unassemble_only)
			return 1;

		fpu_op(cpu, cp, FPU_OP_MOV, fmt, -1, fs, fd, -1, fmt);
		return 1;
	}

	/*  neg.fmt: Floating-point negate  */
	if ((function & 0x001f003f) == 0x00000007) {
		if (cpu->machine->instruction_trace || unassemble_only)
			debug("neg.%i\tr%i,r%i\n", fmt, fd, fs);
		if (unassemble_only)
			return 1;

		fpu_op(cpu, cp, FPU_OP_NEG, fmt, -1, fs, fd, -1, fmt);
		return 1;
	}

	/*  trunc.l.fmt: Truncate  */
	if ((function & 0x001f003f) == 0x00000009) {
		if (cpu->machine->instruction_trace || unassemble_only)
			debug("trunc.l.%i\tr%i,r%i\n", fmt, fd, fs);
		if (unassemble_only)
			return 1;

		/*  TODO: not CVT?  */

		fpu_op(cpu, cp, FPU_OP_CVT, fmt, -1, fs, fd, -1, FMT_L);
		return 1;
	}

	/*  trunc.w.fmt: Truncate  */
	if ((function & 0x001f003f) == 0x0000000d) {
		if (cpu->machine->instruction_trace || unassemble_only)
			debug("trunc.w.%i\tr%i,r%i\n", fmt, fd, fs);
		if (unassemble_only)
			return 1;

		/*  TODO: not CVT?  */

		fpu_op(cpu, cp, FPU_OP_CVT, fmt, -1, fs, fd, -1, FMT_W);
		return 1;
	}

	/*  c.cond.fmt: Floating-point compare  */
	if ((function & 0x000000f0) == 0x00000030) {
		int cond_true;
		int bit;

		if (cpu->machine->instruction_trace || unassemble_only)
			debug("c.%i.%i\tr%i,r%i,r%i\n", cond, fmt, cc, fs, ft);
		if (unassemble_only)
			return 1;

		cond_true = fpu_op(cpu, cp, FPU_OP_C, fmt,
		    ft, fs, -1, cond, fmt);

		/*
		 *  Both the FCCR and FCSR contain condition code bits:
		 *	FCCR:  bits 7..0
		 *	FCSR:  bits 31..25 and 23
		 */
		cp->fcr[FPU_FCCR] &= ~(1 << cc);
		if (cond_true)
			cp->fcr[FPU_FCCR] |= (1 << cc);

		if (cc == 0) {
			bit = 1 << FCSR_FCC0_SHIFT;
			cp->fcr[FPU_FCSR] &= ~bit;
			if (cond_true)
				cp->fcr[FPU_FCSR] |= bit;
		} else {
			bit = 1 << (FCSR_FCC1_SHIFT + cc-1);
			cp->fcr[FPU_FCSR] &= ~bit;
			if (cond_true)
				cp->fcr[FPU_FCSR] |= bit;
		}

		return 1;
	}

	/*  cvt.s.fmt: Convert to single floating-point  */
	if ((function & 0x001f003f) == 0x00000020) {
		if (cpu->machine->instruction_trace || unassemble_only)
			debug("cvt.s.%i\tr%i,r%i\n", fmt, fd, fs);
		if (unassemble_only)
			return 1;

		fpu_op(cpu, cp, FPU_OP_CVT, fmt, -1, fs, fd, -1, FMT_S);
		return 1;
	}

	/*  cvt.d.fmt: Convert to double floating-point  */
	if ((function & 0x001f003f) == 0x00000021) {
		if (cpu->machine->instruction_trace || unassemble_only)
			debug("cvt.d.%i\tr%i,r%i\n", fmt, fd, fs);
		if (unassemble_only)
			return 1;

		fpu_op(cpu, cp, FPU_OP_CVT, fmt, -1, fs, fd, -1, FMT_D);
		return 1;
	}

	/*  cvt.w.fmt: Convert to word fixed-point  */
	if ((function & 0x001f003f) == 0x00000024) {
		if (cpu->machine->instruction_trace || unassemble_only)
			debug("cvt.w.%i\tr%i,r%i\n", fmt, fd, fs);
		if (unassemble_only)
			return 1;

		fpu_op(cpu, cp, FPU_OP_CVT, fmt, -1, fs, fd, -1, FMT_W);
		return 1;
	}

	return 0;
}


/*
 *  coproc_tlbpr():
 *
 *  'tlbp' and 'tlbr'.
 */
void coproc_tlbpr(struct cpu *cpu, int readflag)
{
	struct mips_coproc *cp = cpu->cd.mips.coproc[0];
	int i, found, g_bit;
	uint64_t vpn2, xmask;

	/*  Read:  */
	if (readflag) {
		if (cpu->cd.mips.cpu_type.mmu_model == MMU3K) {
			i = (cp->reg[COP0_INDEX] & R2K3K_INDEX_MASK) >>
			    R2K3K_INDEX_SHIFT;
			if (i >= cp->nr_of_tlbs) {
				/*  TODO:  exception?  */
				fatal("warning: tlbr from index %i (too "
				    "high)\n", i);
				return;
			}

			/*
			 *  TODO: Hm. Earlier I had an & ~0x3f on the high
			 *  assignment and an & ~0xff on the lo0 assignment.
			 *  I wonder why.
			 */

			cp->reg[COP0_ENTRYHI]  = cp->tlbs[i].hi; /* & ~0x3f; */
			cp->reg[COP0_ENTRYLO0] = cp->tlbs[i].lo0;/* & ~0xff; */
		} else {
			/*  R4000:  */
			i = cp->reg[COP0_INDEX] & INDEX_MASK;
			if (i >= cp->nr_of_tlbs)	/*  TODO:  exception  */
				return;

			cp->reg[COP0_PAGEMASK] = cp->tlbs[i].mask;
			cp->reg[COP0_ENTRYHI]  = cp->tlbs[i].hi;
			cp->reg[COP0_ENTRYLO1] = cp->tlbs[i].lo1;
			cp->reg[COP0_ENTRYLO0] = cp->tlbs[i].lo0;

			if (cpu->cd.mips.cpu_type.rev == MIPS_R4100) {
				/*  R4100 don't have the G bit in entryhi  */
			} else {
				/*  R4000 etc:  */
				cp->reg[COP0_ENTRYHI] &= ~TLB_G;
				g_bit = cp->tlbs[i].hi & TLB_G;

				cp->reg[COP0_ENTRYLO0] &= ~ENTRYLO_G;
				cp->reg[COP0_ENTRYLO1] &= ~ENTRYLO_G;
				if (g_bit) {
					cp->reg[COP0_ENTRYLO0] |= ENTRYLO_G;
					cp->reg[COP0_ENTRYLO1] |= ENTRYLO_G;
				}
			}
		}

		return;
	}

	/*  Probe:  */
	if (cpu->cd.mips.cpu_type.mmu_model == MMU3K) {
		vpn2 = cp->reg[COP0_ENTRYHI] & R2K3K_ENTRYHI_VPN_MASK;
		found = -1;
		for (i=0; i<cp->nr_of_tlbs; i++)
			if ( ((cp->tlbs[i].hi & R2K3K_ENTRYHI_ASID_MASK) ==
			    (cp->reg[COP0_ENTRYHI] & R2K3K_ENTRYHI_ASID_MASK))
			    || cp->tlbs[i].lo0 & R2K3K_ENTRYLO_G)
				if ((cp->tlbs[i].hi & R2K3K_ENTRYHI_VPN_MASK)
				    == vpn2) {
					found = i;
					break;
				}
	} else {
		/*  R4000 and R10000:  */
		if (cpu->cd.mips.cpu_type.mmu_model == MMU10K)
			xmask = ENTRYHI_R_MASK | ENTRYHI_VPN2_MASK_R10K;
		else if (cpu->cd.mips.cpu_type.rev == MIPS_R4100)
			xmask = ENTRYHI_R_MASK | ENTRYHI_VPN2_MASK | 0x1800;
		else
			xmask = ENTRYHI_R_MASK | ENTRYHI_VPN2_MASK;
		vpn2 = cp->reg[COP0_ENTRYHI] & xmask;
		found = -1;
		for (i=0; i<cp->nr_of_tlbs; i++) {
			int gbit = cp->tlbs[i].hi & TLB_G;
			if (cpu->cd.mips.cpu_type.rev == MIPS_R4100)
				gbit = (cp->tlbs[i].lo0 & ENTRYLO_G) &&
				    (cp->tlbs[i].lo1 & ENTRYLO_G);

			if ( ((cp->tlbs[i].hi & ENTRYHI_ASID) ==
			    (cp->reg[COP0_ENTRYHI] & ENTRYHI_ASID)) || gbit) {
				uint64_t a = vpn2 & ~cp->tlbs[i].mask;
				uint64_t b = (cp->tlbs[i].hi & xmask) &
				    ~cp->tlbs[i].mask;
				if (a == b) {
					found = i;
					break;
				}
			}
		}
	}
	if (found == -1)
		cp->reg[COP0_INDEX] = INDEX_P;
	else {
		if (cpu->cd.mips.cpu_type.mmu_model == MMU3K)
			cp->reg[COP0_INDEX] = found << R2K3K_INDEX_SHIFT;
		else
			cp->reg[COP0_INDEX] = found;
	}

	/*  Sign extend the index register:  */
	if ((cp->reg[COP0_INDEX] >> 32) == 0 &&
	    cp->reg[COP0_INDEX] & 0x80000000)
		cp->reg[COP0_INDEX] |=
		    0xffffffff00000000ULL;
}


/*
 *  coproc_tlbwri():
 *
 *  'tlbwr' and 'tlbwi'
 */
void coproc_tlbwri(struct cpu *cpu, int randomflag)
{
	struct mips_coproc *cp = cpu->cd.mips.coproc[0];
	int index, g_bit;
	uint64_t oldvaddr;
	int old_asid = -1;

	/*
	 *  ... and the last instruction page:
	 *
	 *  Some thoughts about this:  Code running in
	 *  the kernel's physical address space has the
	 *  same vaddr->paddr translation, so the last
	 *  virtual page invalidation only needs to
	 *  happen if we are for some extremely weird
	 *  reason NOT running in the kernel's physical
	 *  address space.
	 *
	 *  (An even insaner (but probably useless)
	 *  optimization would be to only invalidate
	 *  the last virtual page stuff if the TLB
	 *  update actually affects the vaddr in
	 *  question.)
	 */

	if (cpu->pc < (uint64_t)0xffffffff80000000ULL ||
	    cpu->pc >= (uint64_t)0xffffffffc0000000ULL)
		cpu->cd.mips.pc_last_virtual_page =
		    PC_LAST_PAGE_IMPOSSIBLE_VALUE;

	if (randomflag) {
		if (cpu->cd.mips.cpu_type.mmu_model == MMU3K)
			index = (cp->reg[COP0_RANDOM] & R2K3K_RANDOM_MASK)
			    >> R2K3K_RANDOM_SHIFT;
		else
			index = cp->reg[COP0_RANDOM] & RANDOM_MASK;
	} else {
		if (cpu->cd.mips.cpu_type.mmu_model == MMU3K)
			index = (cp->reg[COP0_INDEX] & R2K3K_INDEX_MASK)
			    >> R2K3K_INDEX_SHIFT;
		else
			index = cp->reg[COP0_INDEX] & INDEX_MASK;
	}

	if (index >= cp->nr_of_tlbs) {
		fatal("warning: tlb index %i too high (max is %i)\n",
		    index, cp->nr_of_tlbs - 1);
		/*  TODO:  cause an exception?  */
		return;
	}

#if 0
	/*  Debug dump of the previous entry at that index:  */
	debug(" old entry at index = %04x", index);
	debug(" mask = %016llx", (long long) cp->tlbs[index].mask);
	debug(" hi = %016llx", (long long) cp->tlbs[index].hi);
	debug(" lo0 = %016llx", (long long) cp->tlbs[index].lo0);
	debug(" lo1 = %016llx\n", (long long) cp->tlbs[index].lo1);
#endif

	/*  Translation caches must be invalidated:  */
	switch (cpu->cd.mips.cpu_type.mmu_model) {
	case MMU3K:
		oldvaddr = cp->tlbs[index].hi & R2K3K_ENTRYHI_VPN_MASK;
		oldvaddr &= 0xffffffffULL;
		if (oldvaddr & 0x80000000ULL)
			oldvaddr |= 0xffffffff00000000ULL;
		old_asid = (cp->tlbs[index].hi & R2K3K_ENTRYHI_ASID_MASK)
		    >> R2K3K_ENTRYHI_ASID_SHIFT;

/*  TODO: Bug? Why does this if need to be commented out?  */

		/*  if (cp->tlbs[index].lo0 & ENTRYLO_V)  */
			invalidate_translation_caches(cpu, 0, oldvaddr, 0, 0);
		break;
	default:
		if (cpu->cd.mips.cpu_type.mmu_model == MMU10K) {
			oldvaddr = cp->tlbs[index].hi & ENTRYHI_VPN2_MASK_R10K;
			/*  44 addressable bits:  */
			if (oldvaddr & 0x80000000000ULL)
				oldvaddr |= 0xfffff00000000000ULL;
		} else {
			/*  Assume MMU4K  */
			oldvaddr = cp->tlbs[index].hi & ENTRYHI_VPN2_MASK;
			/*  40 addressable bits:  */
			if (oldvaddr & 0x8000000000ULL)
				oldvaddr |= 0xffffff0000000000ULL;
		}

		/*
		 *  Both pages:
		 *
		 *  TODO: non-4KB page sizes!
		 */
		invalidate_translation_caches(
		    cpu, 0, oldvaddr & ~0x1fff, 0, 0);
		invalidate_translation_caches(
		    cpu, 0, (oldvaddr & ~0x1fff) | 0x1000, 0, 0);
	}


	/*
	 *  Check for duplicate entries.  (There should not be two mappings
	 *  from one virtual address to physical addresses.)
	 *
	 *  TODO: Do this for MMU3K and R4100 too.
	 *
	 *  TODO: Make this detection more robust.
	 */
	if (cpu->cd.mips.cpu_type.mmu_model != MMU3K &&
	    cpu->cd.mips.cpu_type.rev != MIPS_R4100) {
		uint64_t vaddr1, vaddr2;
		int i, asid;

		vaddr1 = cp->reg[COP0_ENTRYHI] & ENTRYHI_VPN2_MASK_R10K;
		asid = cp->reg[COP0_ENTRYHI] & ENTRYHI_ASID;
		/*  Since this is just a warning, it's probably not necessary
		    to use R4000 masks etc.  */

		for (i=0; i<cp->nr_of_tlbs; i++) {
			if (i == index && !randomflag)
				continue;

			if (!(cp->tlbs[i].hi & TLB_G) &&
			    (cp->tlbs[i].hi & ENTRYHI_ASID) != asid)
				continue;

			vaddr2 = cp->tlbs[i].hi & ENTRYHI_VPN2_MASK_R10K;
			if (vaddr1 == vaddr2 && ((cp->tlbs[i].lo0 &
			    ENTRYLO_V) || (cp->tlbs[i].lo1 & ENTRYLO_V)))
				fatal("\n[ WARNING! tlbw%s to index 0x%02x "
				    "vaddr=0x%llx (asid 0x%02x) is already in"
				    " the TLB (entry 0x%02x) ! ]\n\n",
				    randomflag? "r" : "i", index,
				    (long long)vaddr1, asid, i);
		}
	}


	/*  Write the new entry:  */

	if (cpu->cd.mips.cpu_type.mmu_model == MMU3K) {
		uint64_t vaddr, paddr;
		int wf = cp->reg[COP0_ENTRYLO0] & R2K3K_ENTRYLO_D? 1 : 0;
		unsigned char *memblock = NULL;

		cp->tlbs[index].hi = cp->reg[COP0_ENTRYHI];
		cp->tlbs[index].lo0 = cp->reg[COP0_ENTRYLO0];

		vaddr =  cp->reg[COP0_ENTRYHI] & R2K3K_ENTRYHI_VPN_MASK;
		paddr = cp->reg[COP0_ENTRYLO0] & R2K3K_ENTRYLO_PFN_MASK;

		/*  TODO: This is ugly.  */
		if (paddr < 0x10000000)
			memblock = memory_paddr_to_hostaddr(
			    cpu->mem, paddr, 1);

		if (memblock != NULL &&
		    cp->reg[COP0_ENTRYLO0] & R2K3K_ENTRYLO_V) {
			memblock += (paddr & ((1 << BITS_PER_PAGETABLE) - 1));

			/*
			 *  TODO: Hahaha, this is even uglier than the thing
			 *  above. Some OSes seem to map code pages read/write,
			 *  which causes the bintrans cache to be invalidated
			 *  even when it doesn't have to be.
			 */
/*			if (vaddr < 0x10000000)  */
				wf = 0;

			cpu->update_translation_table(cpu, vaddr, memblock,
			    wf, paddr);
		}
	} else {
		/*  R4000:  */
		g_bit = (cp->reg[COP0_ENTRYLO0] &
		    cp->reg[COP0_ENTRYLO1]) & ENTRYLO_G;
		cp->tlbs[index].mask = cp->reg[COP0_PAGEMASK];
		cp->tlbs[index].hi   = cp->reg[COP0_ENTRYHI];
		cp->tlbs[index].lo1  = cp->reg[COP0_ENTRYLO1];
		cp->tlbs[index].lo0  = cp->reg[COP0_ENTRYLO0];

		if (cpu->cd.mips.cpu_type.rev == MIPS_R4100) {
			/*  NOTE: The VR4131 (and possibly others) don't have
			    a Global bit in entryhi  */
			cp->tlbs[index].hi &= ~cp->reg[COP0_PAGEMASK];
		} else {
			cp->tlbs[index].lo0 &= ~ENTRYLO_G;
			cp->tlbs[index].lo1 &= ~ENTRYLO_G;

			cp->tlbs[index].hi &= ~TLB_G;
			if (g_bit)
				cp->tlbs[index].hi |= TLB_G;
		}
	}

	if (randomflag) {
		if (cpu->cd.mips.cpu_type.exc_model == EXC3K) {
			cp->reg[COP0_RANDOM] =
			    ((random() % (cp->nr_of_tlbs - 8)) + 8)
			    << R2K3K_RANDOM_SHIFT;
		} else {
			cp->reg[COP0_RANDOM] = cp->reg[COP0_WIRED] + (random()
			    % (cp->nr_of_tlbs - cp->reg[COP0_WIRED]));
		}
	}
}


/*
 *  coproc_rfe():
 *
 *  Return from exception. (R3000 etc.)
 */
void coproc_rfe(struct cpu *cpu)
{
	int oldmode;

	oldmode = cpu->cd.mips.coproc[0]->reg[COP0_STATUS] & MIPS1_SR_KU_CUR;

	cpu->cd.mips.coproc[0]->reg[COP0_STATUS] =
	    (cpu->cd.mips.coproc[0]->reg[COP0_STATUS] & ~0x3f) |
	    ((cpu->cd.mips.coproc[0]->reg[COP0_STATUS] & 0x3c) >> 2);

	/*  Changing from kernel to user mode? Then this is necessary:  */
	if (!oldmode && 
	    (cpu->cd.mips.coproc[0]->reg[COP0_STATUS] &
	    MIPS1_SR_KU_CUR))
		invalidate_translation_caches(cpu, 0, 0, 1, 0);
}


/*
 *  coproc_eret():
 *
 *  Return from exception. (R4000 etc.)
 */
void coproc_eret(struct cpu *cpu)
{
	int oldmode, newmode;

	/*  Kernel mode flag:  */
	oldmode = 0;
	if ((cpu->cd.mips.coproc[0]->reg[COP0_STATUS] & MIPS3_SR_KSU_MASK)
			!= MIPS3_SR_KSU_USER
	    || (cpu->cd.mips.coproc[0]->reg[COP0_STATUS] & (STATUS_EXL |
	    STATUS_ERL)) ||
	    (cpu->cd.mips.coproc[0]->reg[COP0_STATUS] & 1) == 0)
		oldmode = 1;

	if (cpu->cd.mips.coproc[0]->reg[COP0_STATUS] & STATUS_ERL) {
		cpu->pc = cpu->cd.mips.pc_last =
		    cpu->cd.mips.coproc[0]->reg[COP0_ERROREPC];
		cpu->cd.mips.coproc[0]->reg[COP0_STATUS] &= ~STATUS_ERL;
	} else {
		cpu->pc = cpu->cd.mips.pc_last =
		    cpu->cd.mips.coproc[0]->reg[COP0_EPC];
		cpu->cd.mips.delay_slot = 0;
		cpu->cd.mips.coproc[0]->reg[COP0_STATUS] &= ~STATUS_EXL;
	}

	cpu->cd.mips.rmw = 0;	/*  the "LL bit"  */

	/*  New kernel mode flag:  */
	newmode = 0;
	if ((cpu->cd.mips.coproc[0]->reg[COP0_STATUS] & MIPS3_SR_KSU_MASK)
			!= MIPS3_SR_KSU_USER
	    || (cpu->cd.mips.coproc[0]->reg[COP0_STATUS] & (STATUS_EXL |
	    STATUS_ERL)) ||
	    (cpu->cd.mips.coproc[0]->reg[COP0_STATUS] & 1) == 0)
		newmode = 1;

#if 0
	/*  Changing from kernel to user mode?
	    Then this is necessary:  TODO  */
	if (oldmode && !newmode)
		invalidate_translation_caches(cpu, 0, 0, 1, 0);
#endif
}


/*
 *  coproc_function():
 *
 *  Execute a coprocessor specific instruction. cp must be != NULL.
 *  Debug trace should be printed for known instructions, if
 *  unassemble_only is non-zero. (This will NOT execute the instruction.)
 *
 *  TODO:  This is a mess and should be restructured (again).
 */
void coproc_function(struct cpu *cpu, struct mips_coproc *cp, int cpnr,
	uint32_t function, int unassemble_only, int running)
{
	int co_bit, op, rt, rd, fs, copz;
	uint64_t tmpvalue;

	if (cp == NULL) {
		if (unassemble_only) {
			debug("cop%i\t0x%08x (coprocessor not available)\n",
			    cpnr, (int)function);
			return;
		}
		fatal("[ pc=0x%016llx cop%i\t0x%08x (coprocessor not "
		    "available)\n", (long long)cpu->pc, cpnr, (int)function);
		return;
	}

#if 0
	/*  No FPU?  */
	if (cpnr == 1 && (cpu->cd.mips.cpu_type.flags & NOFPU)) {
		mips_cpu_exception(cpu, EXCEPTION_CPU, 0, 0, cpnr, 0, 0, 0);
		return;
	}
#endif

	/*  For quick reference:  */
	copz = (function >> 21) & 31;
	rt = (function >> 16) & 31;
	rd = (function >> 11) & 31;

	if (cpnr < 2 && (((function & 0x03e007f8) == (COPz_MFCz << 21))
	              || ((function & 0x03e007f8) == (COPz_DMFCz << 21)))) {
		if (unassemble_only) {
			debug("%s%i\t%s,", copz==COPz_DMFCz? "dmfc" : "mfc",
			    cpnr, regnames[rt]);
			if (cpnr == 0)
				debug("%s", cop0_names[rd]);
			else
				debug("cpreg%i", rd);
			if (function & 7)
				debug(",%i", (int)(function & 7));
			debug("\n");
			return;
		}
		coproc_register_read(cpu, cpu->cd.mips.coproc[cpnr],
		    rd, &tmpvalue, function & 7);
		cpu->cd.mips.gpr[rt] = tmpvalue;
		if (copz == COPz_MFCz) {
			/*  Sign-extend:  */
			cpu->cd.mips.gpr[rt] &= 0xffffffffULL;
			if (cpu->cd.mips.gpr[rt] & 0x80000000ULL)
				cpu->cd.mips.gpr[rt] |= 0xffffffff00000000ULL;
		}
		return;
	}

	if (cpnr < 2 && (((function & 0x03e007f8) == (COPz_MTCz << 21))
	              || ((function & 0x03e007f8) == (COPz_DMTCz << 21)))) {
		if (unassemble_only) {
			debug("%s%i\t%s,", copz==COPz_DMTCz? "dmtc" : "mtc",
			    cpnr, regnames[rt]);
			if (cpnr == 0)
				debug("%s", cop0_names[rd]);
			else
				debug("cpreg%i", rd);
			if (function & 7)
				debug(",%i", (int)(function & 7));
			debug("\n");
			return;
		}
		tmpvalue = cpu->cd.mips.gpr[rt];
		if (copz == COPz_MTCz) {
			/*  Sign-extend:  */
			tmpvalue &= 0xffffffffULL;
			if (tmpvalue & 0x80000000ULL)
				tmpvalue |= 0xffffffff00000000ULL;
		}
		coproc_register_write(cpu, cpu->cd.mips.coproc[cpnr], rd,
		    &tmpvalue, copz == COPz_DMTCz, function & 7);
		return;
	}

	if (cpnr <= 1 && (((function & 0x03e007ff) == (COPz_CFCz << 21))
	              || ((function & 0x03e007ff) == (COPz_CTCz << 21)))) {
		switch (copz) {
		case COPz_CFCz:		/*  Copy from FPU control register  */
			rt = (function >> 16) & 31;
			fs = (function >> 11) & 31;
			if (unassemble_only) {
				debug("cfc%i\t%s,r%i\n", cpnr,
				    regnames[rt], fs);
				return;
			}
			cpu->cd.mips.gpr[rt] = cp->fcr[fs] & 0xffffffffULL;
			if (cpu->cd.mips.gpr[rt] & 0x80000000ULL)
				cpu->cd.mips.gpr[rt] |= 0xffffffff00000000ULL;
			/*  TODO: implement delay for gpr[rt]
			    (for MIPS I,II,III only)  */
			return;
		case COPz_CTCz:		/*  Copy to FPU control register  */
			rt = (function >> 16) & 31;
			fs = (function >> 11) & 31;
			if (unassemble_only) {
				debug("ctc%i\t%s,r%i\n", cpnr,
				    regnames[rt], fs);
				return;
			}

			switch (cpnr) {
			case 0:	/*  System coprocessor  */
				fatal("[ warning: unimplemented ctc%i, "
				    "0x%08x -> ctl reg %i ]\n", cpnr,
				    (int)cpu->cd.mips.gpr[rt], fs);
				break;
			case 1:	/*  FPU  */
				if (fs == 0)
					fatal("[ Attempt to write to FPU "
					    "control register 0 (?) ]\n");
				else {
					uint64_t tmp = cpu->cd.mips.gpr[rt];
					cp->fcr[fs] = tmp;

					/*  TODO: writing to control register 31
					    should cause exceptions, depending
					    on status bits!  */

					switch (fs) {
					case FPU_FCCR:
						cp->fcr[FPU_FCSR] =
						    (cp->fcr[FPU_FCSR] &
						    0x017fffffULL) | ((tmp & 1)
						    << FCSR_FCC0_SHIFT)
						    | (((tmp & 0xfe) >> 1) <<
						    FCSR_FCC1_SHIFT);
						break;
					case FPU_FCSR:
						cp->fcr[FPU_FCCR] =
						    (cp->fcr[FPU_FCCR] &
						    0xffffff00ULL) | ((tmp >>
						    FCSR_FCC0_SHIFT) & 1) |
						    (((tmp >> FCSR_FCC1_SHIFT)
						    & 0x7f) << 1);
						break;
					default:
						;
					}
				}
				break;
			}

			/*  TODO: implement delay for gpr[rt]
			    (for MIPS I,II,III only)  */
			return;
		default:
			;
		}
	}

	/*  Math (Floating point) coprocessor calls:  */
	if (cpnr==1) {
		if (fpu_function(cpu, cp, function, unassemble_only))
			return;
	}

	/*  For AU1500 and probably others:  deret  */
	if (function == 0x0200001f) {
		if (unassemble_only) {
			debug("deret\n");
			return;
		}

		/*
		 *  According to the MIPS64 manual, deret loads PC from the
		 *  DEPC cop0 register, and jumps there immediately. No
		 *  delay slot.
		 *
		 *  TODO: This instruction is only available if the processor
		 *  is in debug mode. (What does that mean?)
		 *  TODO: This instruction is undefined in a delay slot.
		 */

		cpu->pc = cpu->cd.mips.pc_last = cp->reg[COP0_DEPC];
		cpu->cd.mips.delay_slot = 0;
		cp->reg[COP0_STATUS] &= ~STATUS_EXL;

		return;
	}


	/*  Ugly R5900 hacks:  */
	if ((function & 0xfffff) == 0x38) {		/*  ei  */
		if (unassemble_only) {
			debug("ei\n");
			return;
		}
		cpu->cd.mips.coproc[0]->reg[COP0_STATUS] |= R5900_STATUS_EIE;
		return;
	}

	if ((function & 0xfffff) == 0x39) {		/*  di  */
		if (unassemble_only) {
			debug("di\n");
			return;
		}
		cpu->cd.mips.coproc[0]->reg[COP0_STATUS] &= ~R5900_STATUS_EIE;
		return;
	}

	co_bit = (function >> 25) & 1;

	/*  TLB operations and other things:  */
	if (cp->coproc_nr == 0) {
		op = (function) & 0xff;
		switch (co_bit) {
		case 1:
			switch (op) {
			case COP0_TLBR:		/*  Read indexed TLB entry  */
				if (unassemble_only) {
					debug("tlbr\n");
					return;
				}
				coproc_tlbpr(cpu, 1);
				return;
			case COP0_TLBWI:	/*  Write indexed  */
			case COP0_TLBWR:	/*  Write random  */
				if (unassemble_only) {
					if (op == COP0_TLBWI)
						debug("tlbwi");
					else
						debug("tlbwr");
					if (!running) {
						debug("\n");
						return;
					}
					debug("\tindex=%08llx",
					    (long long)cp->reg[COP0_INDEX]);
					debug(", random=%08llx",
					    (long long)cp->reg[COP0_RANDOM]);
					debug(", mask=%016llx",
					    (long long)cp->reg[COP0_PAGEMASK]);
					debug(", hi=%016llx",
					    (long long)cp->reg[COP0_ENTRYHI]);
					debug(", lo0=%016llx",
					    (long long)cp->reg[COP0_ENTRYLO0]);
					debug(", lo1=%016llx\n",
					    (long long)cp->reg[COP0_ENTRYLO1]);
				}
				coproc_tlbwri(cpu, op == COP0_TLBWR);
				return;
			case COP0_TLBP:		/*  Probe TLB for
						    matching entry  */
				if (unassemble_only) {
					debug("tlbp\n");
					return;
				}
				coproc_tlbpr(cpu, 0);
				return;
			case COP0_RFE:		/*  R2000/R3000 only:
						    Return from Exception  */
				if (unassemble_only) {
					debug("rfe\n");
					return;
				}
				coproc_rfe(cpu);
				return;
			case COP0_ERET:	/*  R4000: Return from exception  */
				if (unassemble_only) {
					debug("eret\n");
					return;
				}
				coproc_eret(cpu);
				return;
			case COP0_STANDBY:
				if (unassemble_only) {
					debug("standby\n");
					return;
				}
				/*  TODO: Hm. Do something here?  */
				return;
			case COP0_SUSPEND:
				if (unassemble_only) {
					debug("suspend\n");
					return;
				}
				/*  TODO: Hm. Do something here?  */
				return;
			case COP0_HIBERNATE:
				if (unassemble_only) {
					debug("hibernate\n");
					return;
				}
				/*  TODO: Hm. Do something here?  */
				return;
			default:
				;
			}
		default:
			;
		}
	}

	/*  TODO: coprocessor R2020 on DECstation?  */
	if ((cp->coproc_nr==0 || cp->coproc_nr==3) && function == 0x0100ffff) {
		if (unassemble_only) {
			debug("decstation_r2020_writeback\n");
			return;
		}
		/*  TODO  */
		return;
	}

	/*  TODO: RM5200 idle (?)  */
	if ((cp->coproc_nr==0 || cp->coproc_nr==3) && function == 0x02000020) {
		if (unassemble_only) {
			debug("idle(?)\n");	/*  TODO  */
			return;
		}

		/*  Idle? TODO  */
		return;
	}

	if (unassemble_only) {
		debug("cop%i\t0x%08x (unimplemented)\n", cpnr, (int)function);
		return;
	}

	fatal("cpu%i: UNIMPLEMENTED coproc%i function %08lx "
	    "(pc = %016llx)\n", cpu->cpu_id, cp->coproc_nr, function,
	    (long long)cpu->cd.mips.pc_last);
#if 1
	single_step = 1;
#else
	mips_cpu_exception(cpu, EXCEPTION_CPU, 0, 0, cp->coproc_nr, 0, 0, 0);
#endif
}

#endif	/*  ENABLE_MIPS  */
