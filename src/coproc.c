/*
 *  Copyright (C) 2003,2004 by Anders Gavare.  All rights reserved.
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
 *  $Id: coproc.c,v 1.65 2004-09-05 03:15:04 debug Exp $
 *
 *  Emulation of MIPS coprocessors.
 *
 *  TODO: separate out math coprocessor stuff (?)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "misc.h"


extern int instruction_trace;
extern int register_dump;

char *cop0_names[32] = COP0_NAMES;


/*  FPU control registers:  */
#define	FPU_FCIR	0
#define	FPU_FCCR	25
#define	FPU_FCSR	31
#define	  FCSR_FCC0_SHIFT	  23
#define	  FCSR_FCC1_SHIFT	  25


/*
 *  coproc_new():
 *
 *  Create a new coprocessor object.
 */
struct coproc *coproc_new(struct cpu *cpu, int coproc_nr)
{
	struct coproc *c;

	c = malloc(sizeof(struct coproc));
	if (c == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}

	memset(c, 0, sizeof(struct coproc));
	c->coproc_nr = coproc_nr;

	if (coproc_nr == 0) {
		c->nr_of_tlbs = cpu->cpu_type.nr_of_tlb_entries;

		/*
		 *  Start with nothing in the status register. This makes sure
		 *  that we are running in kernel mode with all interrupts
		 *  disabled.
		 */
		c->reg[COP0_STATUS] = 0;

		/*  For userland emulation, enable all four coprocessors:  */
		if (cpu->emul->userland_emul)
			c->reg[COP0_STATUS] |=
			    ((uint32_t)0xf << STATUS_CU_SHIFT);

		c->reg[COP0_COMPARE] = (uint64_t) -1;

		if (!cpu->emul->prom_emulation)
			c->reg[COP0_STATUS] |= STATUS_BEV;

		/*  Note: .rev may contain the company ID as well!  */
		c->reg[COP0_PRID] =
		      (0x00 << 24)		/*  Company Options  */
		    | (0x00 << 16)		/*  Company ID       */
		    | (cpu->cpu_type.rev <<  8)	/*  Processor ID     */
		    | (cpu->cpu_type.sub)	/*  Revision         */
		    ;

		c->reg[COP0_WIRED] = 0;

		c->reg[COP0_CONFIG] =
		      (   0 << 31)	/*  config1 present  */
		    | (0x00 << 16)	/*  implementation dependant  */
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

		switch (cpu->cpu_type.rev) {
		case MIPS_R4000:	/*  according to the R4000 manual  */
		case MIPS_R4600:
			c->reg[COP0_CONFIG] =
			      (   0 << 31)	/*  Master/Checker present bit  */
			    | (0x00 << 28)	/*  EC: system clock divisor, 0x00 = '2'  */
			    | (0x00 << 24)	/*  EP  */
			    | (0x01 << 22)	/*  SB  */
			    | (0x00 << 21)	/*  SS  */
			    | (0x00 << 20)	/*  SW  */
			    | (0x00 << 18)	/*  EW: 0=64-bit  */
			    | (0x00 << 17)	/*  SC: 0=secondary cache present, 1=non-present  */
			    | (0x00 << 16)	/*  SM: (todo)  */
			    | ((cpu->byte_order==EMUL_BIG_ENDIAN? 1 : 0) << 15) 	/*  endian mode  */
			    | (0x01 << 14)	/*  ECC: 0=enabled, 1=disabled  */
			    | (0x00 << 13)	/*  EB: (todo)  */
			    | (0x00 << 12)	/*  0 (resered)  */
			    | (   3 <<  9)	/*  IC: I-cache = 2^(12+IC) bytes  (1 = 8KB, 4=64K)  */
			    | (   3 <<  6)	/*  DC: D-cache = 2^(12+DC) bytes  (1 = 8KB, 4=64K)  */
			    | (   1 <<  5)	/*  IB: I-cache line size (0=16, 1=32)  */
			    | (   1 <<  4)	/*  DB: D-cache line size (0=16, 1=32)  */
			    | (   0 <<  3)	/*  CU: todo  */
			    | (   0 <<  0)	/*  kseg0 coherency algorithm
							(TODO)  */
			    ;
			break;
		case MIPS_R5000:
		case MIPS_RM5200:	/*  rm5200 is just a wild guess  */
			/*  These are just guesses: (the comments are wrong) */
			c->reg[COP0_CONFIG] =
			      (   0 << 31)	/*  Master/Checker present bit  */
			    | (0x00 << 28)	/*  EC: system clock divisor, 0x00 = '2'  */
			    | (0x00 << 24)	/*  EP  */
			    | (0x00 << 22)	/*  SB  */
			    | (0x00 << 21)	/*  SS  */
			    | (0x00 << 20)	/*  SW  */
			    | (0x00 << 18)	/*  EW: 0=64-bit  */
			    | (0x01 << 17)	/*  SC: 0=secondary cache present, 1=non-present  */
			    | (0x00 << 16)	/*  SM: (todo)  */
			    | ((cpu->byte_order==EMUL_BIG_ENDIAN? 1 : 0) << 15) 	/*  endian mode  */
			    | (0x01 << 14)	/*  ECC: 0=enabled, 1=disabled  */
			    | (0x00 << 13)	/*  EB: (todo)  */
			    | (0x00 << 12)	/*  0 (resered)  */
			    | (   3 <<  9)	/*  IC: I-cache = 2^(12+IC) bytes  (1 = 8KB, 4=64K)  */
			    | (   3 <<  6)	/*  DC: D-cache = 2^(12+DC) bytes  (1 = 8KB, 4=64K)  */
			    | (   1 <<  5)	/*  IB: I-cache line size (0=16, 1=32)  */
			    | (   1 <<  4)	/*  DB: D-cache line size (0=16, 1=32)  */
			    | (   0 <<  3)	/*  CU: todo  */
			    | (   2 <<  0)	/*  kseg0 coherency algorithm
							(TODO)  */
			    ;
			break;
		case MIPS_R10000:
			/*  According to the R10000 User's Manual:  */
			c->reg[COP0_CONFIG] =
			      (   3 << 29)	/*  Primary instruction cache size, hardwired to 32KB  */
			    | (   3 << 26)	/*  Primary data cache size, hardwired to 32KB  */
			    | (   0 << 19)	/*  SCClkDiv  */
			    | (   0 << 16)	/*  SCSize, secondary cache size. 0 = 512KB. powers of two  */
			    | (   0 << 15)	/*  MemEnd  */
			    | (   0 << 14)	/*  SCCorEn  */
			    | (   1 << 13)	/*  SCBlkSize. 0=16 words, 1=32 words  */
			    | (   0 <<  9)	/*  SysClkDiv  */
			    | (   0 <<  7)	/*  PrcReqMax  */
			    | (   0 <<  6)	/*  PrcElmReq  */
			    | (   0 <<  5)	/*  CohPrcReqTar  */
			    | (   0 <<  3)	/*  Device number  */
			    | (   2 <<  0)	/*  Cache coherency algorithm for kseg0  */
			    ;
			break;
		case MIPS_R5900:
			/*
			 *  R5900 is supposed to have the following (according to NetBSD/playstation2):
			 *	cpu0: 16KB/64B 2-way set-associative L1 Instruction cache, 48 TLB entries
			 *	cpu0: 8KB/64B 2-way set-associative write-back L1 Data cache
			 *  The following settings are just guesses: (comments are incorrect)
			 */
			c->reg[COP0_CONFIG] =
			      (   0 << 31)	/*  Master/Checker present bit  */
			    | (0x00 << 28)	/*  EC: system clock divisor, 0x00 = '2'  */
			    | (0x00 << 24)	/*  EP  */
			    | (0x00 << 22)	/*  SB  */
			    | (0x00 << 21)	/*  SS  */
			    | (0x00 << 20)	/*  SW  */
			    | (0x00 << 18)	/*  EW: 0=64-bit  */
			    | (0x01 << 17)	/*  SC: 0=secondary cache present, 1=non-present  */
			    | (0x00 << 16)	/*  SM: (todo)  */
			    | ((cpu->byte_order==EMUL_BIG_ENDIAN? 1 : 0) << 15) 	/*  endian mode  */
			    | (0x01 << 14)	/*  ECC: 0=enabled, 1=disabled  */
			    | (0x00 << 13)	/*  EB: (todo)  */
			    | (0x00 << 12)	/*  0 (resered)  */
			    | (   3 <<  9)	/*  IC: I-cache = 2^(12+IC) bytes  (1 = 8KB, 4=64K)  */
			    | (   3 <<  6)	/*  DC: D-cache = 2^(12+DC) bytes  (1 = 8KB, 4=64K)  */
			    | (   1 <<  5)	/*  IB: I-cache line size (0=16, 1=32)  */
			    | (   1 <<  4)	/*  DB: D-cache line size (0=16, 1=32)  */
			    | (   0 <<  3)	/*  CU: todo  */
			    | (   0 <<  0)	/*  kseg0 coherency algorithm
							(TODO)  */
			    ;
			break;
		case MIPS_5K:
			/*  According to the MIPS64 5K User's Manual:  */
			c->reg[COP0_CONFIG] =
			      (   (uint32_t)1 << 31)/*  Config 1 present bit  */
			    | (   0 << 20)	/*  ISD:  instruction scheduling disable (=1)  */
			    | (   0 << 17)	/*  DID:  dual issue disable  */
			    | (   0 << 16)	/*  BM:   burst mode  */
			    | ((cpu->byte_order==EMUL_BIG_ENDIAN? 1 : 0) << 15) 	/*  endian mode  */
			    | (   2 << 13)	/*  1=32-bit only, 2=32/64  */
			    | (   0 << 10)	/*  Architecture revision  */
			    | (   1 <<  7)	/*  MMU type: 1=TLB, 3=FMT  */
			    | (   2 <<  0)	/*  kseg0 cache coherency algorithm  */
			    ;
			/*  TODO:  Config select 1: caches and such  */
			break;
		default:
			;
		}
	}

	if (coproc_nr == 1) {
		int fpu_rev;
		uint64_t other_stuff = 0;

		switch (cpu->cpu_type.rev & 0xff) {
		case MIPS_R2000:	fpu_rev = MIPS_R2010;	break;
		case MIPS_R3000:	fpu_rev = MIPS_R3010;
					other_stuff |= 0x40;	/*  or 0x30? TODO  */
					break;
		case MIPS_R6000:	fpu_rev = MIPS_R6010;	break;
		case MIPS_R4000:	fpu_rev = MIPS_R4010;	break;

		case MIPS_5K:		other_stuff = COP1_REVISION_DOUBLE | COP1_REVISION_SINGLE;
		case MIPS_R5000:
		case MIPS_RM5200:	fpu_rev = cpu->cpu_type.rev;
					other_stuff |= 0x10;	/*  or cpu->cpu_type.sub ? TODO  */
					break;
		case MIPS_R10000:
		case MIPS_R12000:	fpu_rev = MIPS_R10000;	break;
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

	return c;
}


/*
 *  invalidate_translation_caches():
 *
 *  This is neccessary for every change to the TLB, and when the ASID is
 *  changed, so that for example user-space addresses are not cached when
 *  they should not be.
 */
static void invalidate_translation_caches(struct cpu *cpu)
{
#ifdef USE_TINY_CACHE
	int i;

	/*  Invalidate the tiny translation cache...  */
	for (i=0; i<N_TRANSLATION_CACHE_INSTR; i++)
		cpu->translation_cache_instr[i].wf = 0;
	for (i=0; i<N_TRANSLATION_CACHE_DATA; i++)
		cpu->translation_cache_data[i].wf = 0;
#endif
}


/*
 *  coproc_register_read();
 *
 *  Read a value from a coprocessor register.
 */
void coproc_register_read(struct cpu *cpu,
	struct coproc *cp, int reg_nr, uint64_t *ptr)
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
	if (cp->coproc_nr==0 && reg_nr==COP0_COUNT)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_ENTRYHI)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_COMPARE)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_STATUS)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_CAUSE)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_EPC)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_PRID)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_CONFIG)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_LLADDR)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_WATCHLO)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_WATCHHI)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_XCONTEXT)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_ERRCTL)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_CACHEERR)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_TAGDATA_LO)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_TAGDATA_HI)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_ERROREPC)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_DEBUG)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_DESAVE)	unimpl = 0;

	if (cp->coproc_nr==1)	unimpl = 0;

	if (unimpl) {
		fatal("cpu%i: warning: read from unimplemented coproc%i"
		    " register %i (%s)\n", cpu->cpu_id, cp->coproc_nr, reg_nr,
		    cp->coproc_nr==0? cop0_names[reg_nr] : "?");

		cpu_exception(cpu, EXCEPTION_CPU, 0, 0, cp->coproc_nr, 0, 0, 0);
		return;
	}

	*ptr = cp->reg[reg_nr];
}


/*
 *  coproc_register_write();
 *
 *  Write a value to a coprocessor register.
 */
void coproc_register_write(struct cpu *cpu,
	struct coproc *cp, int reg_nr, uint64_t *ptr)
{
	int unimpl = 1;
	int readonly = 0;
	uint64_t tmp = *ptr;
	uint64_t tmp2 = 0;

	if (cp->coproc_nr==0 && reg_nr==COP0_INDEX)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_RANDOM)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_ENTRYLO0) {
		unimpl = 0;
		if (cpu->cpu_type.mmu_model == MMU3K && (tmp & 0xff)!=0) {
			/*  char *symbol;
			    uint64_t offset;
			    symbol = get_symbol_name(cpu->pc_last, &offset);
			    fatal("YO! pc = 0x%08llx <%s> lo=%016llx\n", (long long)cpu->pc_last, symbol? symbol : "no symbol", (long long)tmp); */
			tmp &= (R2K3K_ENTRYLO_PFN_MASK |
			    R2K3K_ENTRYLO_N | R2K3K_ENTRYLO_D |
			    R2K3K_ENTRYLO_V | R2K3K_ENTRYLO_G);
		} else if (cpu->cpu_type.mmu_model == MMU4K) {
			tmp &= (ENTRYLO_PFN_MASK | ENTRYLO_C_MASK |
			    ENTRYLO_D | ENTRYLO_V | ENTRYLO_G);
		}
	}
	if (cp->coproc_nr==0 && reg_nr == COP0_BADVADDR) {
		/*  Hm. Irix writes to this register. (Why?)  */
		unimpl = 0;
	}
	if (cp->coproc_nr==0 && reg_nr==COP0_ENTRYLO1) {
		unimpl = 0;
		if (cpu->cpu_type.mmu_model == MMU4K) {
			tmp &= (ENTRYLO_PFN_MASK | ENTRYLO_C_MASK |
			    ENTRYLO_D | ENTRYLO_V | ENTRYLO_G);
		}
	}
	if (cp->coproc_nr==0 && reg_nr==COP0_CONTEXT) {
		uint64_t old = cp->reg[COP0_CONTEXT];
		cp->reg[COP0_CONTEXT] = tmp;
		if (cpu->cpu_type.mmu_model == MMU3K) {
			cp->reg[COP0_CONTEXT] &= ~R2K3K_CONTEXT_BADVPN_MASK;
			cp->reg[COP0_CONTEXT] |= (old & R2K3K_CONTEXT_BADVPN_MASK);
		} else {
			cp->reg[COP0_CONTEXT] &= ~CONTEXT_BADVPN2_MASK;
			cp->reg[COP0_CONTEXT] |= (old & CONTEXT_BADVPN2_MASK);
		}
		return;
	}
	if (cp->coproc_nr==0 && reg_nr==COP0_PAGEMASK) {
		tmp2 = tmp >> PAGEMASK_SHIFT;
		if (tmp2 != 0x000 &&
		    tmp2 != 0x003 &&
		    tmp2 != 0x00f &&
		    tmp2 != 0x03f &&
		    tmp2 != 0x0ff &&
		    tmp2 != 0x3ff &&
		    tmp2 != 0xfff)
			debug("cpu%i: trying to write an invalid pagemask %08lx to COP0_PAGEMASK\n",
			    cpu->cpu_id, (long)tmp2);
		unimpl = 0;
	}
	if (cp->coproc_nr==0 && reg_nr==COP0_WIRED) {
		if (cpu->cpu_type.mmu_model == MMU3K) {
			debug("cpu%i: r2k/r3k wired register must always be 8\n", cpu->cpu_id);
			tmp = 8;
		}
		cp->reg[COP0_RANDOM] = cp->nr_of_tlbs-1;
		tmp &= INDEX_MASK;
		unimpl = 0;
	}
	if (cp->coproc_nr==0 && reg_nr==COP0_COUNT) {
		unimpl = 0;
	}
	if (cp->coproc_nr==0 && reg_nr==COP0_COMPARE) {
		/*  Clear the timer interrupt bit (bit 7):  */
		cpu_interrupt_ack(cpu, 7);
		unimpl = 0;
	}
	if (cp->coproc_nr==0 && reg_nr==COP0_ENTRYHI) {
		/*
		 *  Translation caches must be invalidated, because the
		 *  address space might change (if the ASID changes).
		 */
		invalidate_translation_caches(cpu);

		unimpl = 0;
		if (cpu->cpu_type.mmu_model == MMU3K && (tmp & 0x3f)!=0) {
			/* char *symbol;
			   uint64_t offset;
			   symbol = get_symbol_name(cpu->pc_last, &offset);
			   fatal("YO! pc = 0x%08llx <%s> hi=%016llx\n", (long long)cpu->pc_last, symbol? symbol : "no symbol", (long long)tmp);  */
			tmp &= ~0x3f;
		}

		if (cpu->cpu_type.mmu_model == MMU3K)
			tmp &= (R2K3K_ENTRYHI_VPN_MASK | R2K3K_ENTRYHI_ASID_MASK);
		else if (cpu->cpu_type.mmu_model == MMU10K)
			tmp &= (ENTRYHI_R_MASK | ENTRYHI_VPN2_MASK_R10K | ENTRYHI_ASID);
		else
			tmp &= (ENTRYHI_R_MASK | ENTRYHI_VPN2_MASK | ENTRYHI_ASID);
	}
	if (cp->coproc_nr==0 && reg_nr==COP0_EPC) {
		/*
		 *  According to the R4000 manual:
		 *	"The processor does not write to the EPC register
		 *	 when the EXL bit in the Status register is set to a 1."
		 *
		 *  Perhaps that refers to hardware updates of the register,
		 *  not software.  If this code is enabled, NetBSD crashes.
		 */
/*		if (cpu->cpu_type.exc_model == EXC4K &&
		    cpu->coproc[0]->reg[COP0_STATUS] & STATUS_EXL)
			return;
*/		/*  Otherwise, allow the write:  */
		unimpl = 0;
	}

	if (cp->coproc_nr==0 && reg_nr==COP0_PRID)
		readonly = 1;

	if (cp->coproc_nr==0 && reg_nr==COP0_CONFIG) {
		/*  fatal("COP0_CONFIG: modifying K0 bits: 0x%08x => ", cp->reg[reg_nr]);  */
		tmp = *ptr;
		tmp &= 0x3;	/*  only bits 2..0 can be written  */
		cp->reg[reg_nr] &= ~(0x3);  cp->reg[reg_nr] |= tmp;
		/*  fatal("0x%08x\n", cp->reg[reg_nr]);  */
		return;
	}

	if (cp->coproc_nr==0 && reg_nr==COP0_STATUS) {
		tmp &= ~(1 << 21);	/*  bit 21 is read-only  */
		unimpl = 0;
	}

	if (cp->coproc_nr==0 && reg_nr==COP0_CAUSE) {
		/*  A write to the cause register only affects IM bits 0 and 1:  */
		cp->reg[reg_nr] &= ~(0x3 << STATUS_IM_SHIFT);
		cp->reg[reg_nr] |= (tmp & (0x3 << STATUS_IM_SHIFT));
		return;
	}

	if (cp->coproc_nr==0 && reg_nr==COP0_FRAMEMASK) {
		/*  TODO: R10000  */
		unimpl = 0;
	}

	if (cp->coproc_nr==0 && (reg_nr==COP0_TAGDATA_LO || reg_nr==COP0_TAGDATA_HI)) {
		/*  TODO: R4300 and others?  */
		unimpl = 0;
	}

	if (cp->coproc_nr==0 && reg_nr==COP0_LLADDR)
		unimpl = 0;

	if (cp->coproc_nr==0 && (reg_nr==COP0_WATCHLO || reg_nr==COP0_WATCHHI)) {
		/*  TODO  */
		unimpl = 0;
	}

	if (cp->coproc_nr==0 && reg_nr==COP0_XCONTEXT) {
		/*
		 *  TODO:  According to the R10000 manual, the R4400 shares the PTEbase
		 *  portion of the context registers (that is, xcontext and context).
		 *  on R10000, they are separate registers.
		 */
		/*  debug("[ xcontext 0x%016llx ]\n", tmp);  */
		unimpl = 0;
	}

	/*  Most of these are actually TODOs:  */
	if (cp->coproc_nr==0 && reg_nr==COP0_ERROREPC)
		unimpl = 0;

	if (cp->coproc_nr==0 && reg_nr==COP0_DEPC)
		unimpl = 0;

	if (cp->coproc_nr==0 && reg_nr==COP0_DESAVE)
		unimpl = 0;

	if (cp->coproc_nr==0 && reg_nr==COP0_PERFCNT)
		unimpl = 0;

	if (cp->coproc_nr==0 && reg_nr==COP0_ERRCTL) {
		/*  TODO: R10000  */
		unimpl = 0;
	}

	if (cp->coproc_nr==1) {
		switch (reg_nr) {
		case 31:	/*  FCSR:  */

			break;
		default:
			;
		}
		unimpl = 0;
	}

	if (unimpl) {
		fatal("cpu%i: warning: write to unimplemented coproc%i "
		    "register %i (%s), data = 0x%016llx\n", cpu->cpu_id, cp->coproc_nr, reg_nr,
		    cp->coproc_nr==0? cop0_names[reg_nr] : "?", (long long)tmp);

		cpu_exception(cpu, EXCEPTION_CPU, 0, 0, cp->coproc_nr, 0, 0, 0);
		return;
	}

	if (readonly) {
		debug("cpu%i: warning: write to READONLY coproc%i register "
		    "%i ignored\n", cpu->cpu_id, cp->coproc_nr, reg_nr);
		return;
	}

	cp->reg[reg_nr] = tmp;
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
#define	FPU_OP_C	7
#define	FPU_OP_ABS	8
#define	FPU_OP_NEG	9
/*  TODO: CEIL.L, CEIL.W, FLOOR.L, FLOOR.W, RECIP, ROUND.L, ROUND.W, RSQRT, TRUNC.L, TRUNC.W  */


struct internal_float_value {
	double f;
};


/*
 *  fpu_interpret_float_value():
 *
 *  Interprets a float value from binary IEEE format into
 *  a internal_float_value struct.
 */
void fpu_interpret_float_value(uint64_t reg, struct internal_float_value *fvp, int fmt)
{
	int n_frac = 0, n_exp = 0;
	int i;
	int sign = 0, exponent;
	double fraction;

	memset(fvp, 0, sizeof(struct internal_float_value));

	/*  n_frac and n_exp:  */
	switch (fmt) {
	case FMT_S:	n_frac = 23; n_exp = 8; break;
	case FMT_W:	n_frac = 31; n_exp = 0; break;
	case FMT_D:	n_frac = 52; n_exp = 11; break;
	case FMT_L:	n_frac = 63; n_exp = 0; break;
	default:
		fatal("fpu_interpret_float_value(): unimplemented format %i\n", fmt);
	}

	/*  exponent:  */
	exponent = 0;
	switch (fmt) {
	case FMT_W:
	case FMT_L:
		break;
	case FMT_S:
	case FMT_D:
		exponent = (reg >> n_frac) & ((1 << n_exp) - 1);
		exponent -= (1 << (n_exp-1)) - 1;
		break;
	default:
		fatal("fpu_interpret_float_value(): unimplemented format %i\n", fmt);
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
		if (fmt == FMT_D || fmt == FMT_L)
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
		fatal("fpu_interpret_float_value(): unimplemented format %i\n", fmt);
	}

	/*  form the value:  */
	fvp->f = fraction;

	/*  fatal("load  reg=%016llx sign=%i exponent=%i fraction=%f ", (long long)reg, sign, exponent, fraction);  */

	/*  TODO: this is awful for exponents of large magnitude.  */
	if (exponent > 0) {
		while (exponent-- > 0)
			fvp->f *= 2.0;
	} else if (exponent < 0) {
		while (exponent++ < 0)
			fvp->f /= 2.0;
	}

	if (sign)
		fvp->f = -fvp->f;

	/*  fatal("f = %f\n", fvp->f);  */
}


/*
 *  fpu_store_float_value():
 *
 *  Stores a float value (actually a double) in fmt format.
 */
void fpu_store_float_value(struct coproc *cp, int fd, double nf, int fmt)
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
		fatal("fpu_store_float_value(): unimplemented format %i\n", fmt);
	}

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
		 *  We want fraction to be 1.xxx, that is   1.0 <= fraction < 2.0
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
		fatal("fpu_store_float_value(): unimplemented format %i\n", fmt);
	}

	/*  TODO:  this is for 32-bit mode:  */
	if (fmt == FMT_D || fmt == FMT_L) {
		cp->reg[fd] = r & 0xffffffffULL;
		cp->reg[(fd+1) & 31] = (r >> 32) & 0xffffffffULL;
	} else {
		cp->reg[fd] = r;
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
int fpu_op(struct cpu *cpu, struct coproc *cp, int op, int fmt,
	int ft, int fs, int cc, int fd, int cond, int output_fmt)
{
	/*  Potentially two input registers, fs and ft  */
	struct internal_float_value float_value[2];
	double nf;

	if (fs >= 0) {
		uint64_t v = cp->reg[fs];
		/*  TODO: register-pair mode and plain register mode? "FR" bit?  */
		v = (v & 0xffffffffULL) + (cp->reg[(fs + 1) & 31] << 32);
		fpu_interpret_float_value(v, &float_value[0], fmt);
	}
	if (ft >= 0) {
		uint64_t v = cp->reg[ft];
		/*  TODO: register-pair mode and plain register mode? "FR" bit?  */
		v = (v & 0xffffffffULL) + (cp->reg[(ft + 1) & 31] << 32);
		fpu_interpret_float_value(v, &float_value[1], fmt);
	}

	switch (op) {
	case FPU_OP_ADD:
		nf = float_value[0].f + float_value[1].f;
		debug("  add: %f + %f = %f\n", float_value[0].f, float_value[1].f, nf);
		fpu_store_float_value(cp, fd, nf, output_fmt);
		break;
	case FPU_OP_SUB:
		nf = float_value[0].f - float_value[1].f;
		debug("  sub: %f - %f = %f\n", float_value[0].f, float_value[1].f, nf);
		fpu_store_float_value(cp, fd, nf, output_fmt);
		break;
	case FPU_OP_MUL:
		nf = float_value[0].f * float_value[1].f;
		debug("  mul: %f * %f = %f\n", float_value[0].f, float_value[1].f, nf);
		fpu_store_float_value(cp, fd, nf, output_fmt);
		break;
	case FPU_OP_DIV:
		if (fabs(float_value[1].f) > 0.00000000001)
			nf = float_value[0].f / float_value[1].f;
		else {
			debug("DIV by zero !!!!\n");
			nf = 0.0;	/*  TODO  */
		}
		debug("  div: %f / %f = %f\n", float_value[0].f, float_value[1].f, nf);
		fpu_store_float_value(cp, fd, nf, output_fmt);
		break;
	case FPU_OP_SQRT:
		if (float_value[0].f >= 0.0)
			nf = sqrt(float_value[0].f);
		else {
			debug("SQRT by less than zero, %f !!!!\n", float_value[0].f);
			nf = 0.0;	/*  TODO  */
		}
		debug("  sqrt: %f => %f\n", float_value[0].f, nf);
		fpu_store_float_value(cp, fd, nf, output_fmt);
		break;
	case FPU_OP_ABS:
		nf = fabs(float_value[0].f);
		debug("  abs: %f => %f\n", float_value[0].f, nf);
		fpu_store_float_value(cp, fd, nf, output_fmt);
		break;
	case FPU_OP_NEG:
		nf = - float_value[0].f;
		debug("  neg: %f => %f\n", float_value[0].f, nf);
		fpu_store_float_value(cp, fd, nf, output_fmt);
		break;
	case FPU_OP_MOV:
		nf = float_value[0].f;
		debug("  mov: %f => %f\n", float_value[0].f, nf);
		fpu_store_float_value(cp, fd, nf, output_fmt);
		break;
	case FPU_OP_C:
		/*  TODO: how to detect unordered-ness and such?  */
		debug("  c: %f, %f cond=%i\n", float_value[0].f, float_value[1].f, cond);
		switch (cond) {
		case 0:		return 0;					/*  False  */
		case 1:		return 0;					/*  Unordered  */
		case 2:		return (float_value[0].f == float_value[1].f);	/*  Equal  */
		case 3:		return (float_value[0].f == float_value[1].f);	/*  Unordered or Equal  */
		case 4:		return 1; /* (float_value[0].f < float_value[1].f);  */	/*  Ordered or Less than  TODO (?)  */
		case 5:		return (float_value[0].f < float_value[1].f);	/*  Unordered or Less than  */
		case 6:		return 1; /* (float_value[0].f <= float_value[1].f);  */  /*  Ordered or Less than or Equal  TODO (?)  */
		case 7:		return (float_value[0].f <= float_value[1].f);	/*  Unordered or Less than or Equal  */
		case 8:		return 0;					/*  Signaling false  */
		case 9:		return 0;					/*  Not Greater than or Less than or Equal  */
		case 10:	return (float_value[0].f == float_value[1].f);	/*  Signaling Equal  */
		case 11:	return (float_value[0].f == float_value[1].f);	/*  Not Greater than or Less than  */
		case 12:	return (float_value[0].f < float_value[1].f);	/*  Less than  */
		case 13:	return !(float_value[0].f >= float_value[1].f);	/*  Not greater than or equal */
		case 14:	return (float_value[0].f <= float_value[1].f);	/*  Less than or equal  */
		case 15:	return !(float_value[0].f > float_value[1].f);	/*  Not greater than  */
		default:
			fatal("fpu_op(): unknown condition code %i\n", cond);
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
int fpu_function(struct cpu *cpu, struct coproc *cp, uint32_t function)
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
		int nd, tf, imm, cond;
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

		if (instruction_trace)
			debug("%s\t%i,0x%016llx\n", instr_mnem, cc, (long long) (cpu->pc + (imm << 2)));

		if (cpu->delay_slot) {
			fatal("%s: jump inside a jump's delay slot, or similar. TODO\n", instr_mnem);
			cpu->running = 0;
			return 1;
		}

		/*  Both the FCCR and FCSR contain condition code bits...  */
		cond = (cp->fcr[FPU_FCCR] >> cc) & 1;

		if (!tf)
			cond = !cond;

		if (cond) {
			cpu->delay_slot = TO_BE_DELAYED;
			cpu->delay_jmpaddr = cpu->pc + (imm << 2);
		} else {
			/*  "likely":  */
			if (nd)
				cpu->nullify_next = 1;	/*  nullify delay slot  */
		}

		return 1;
	}

	/*  add.fmt: Floating-point add  */
	if ((function & 0x0000003f) == 0x00000000) {
		if (instruction_trace)
			debug("add.%i\tr%i,r%i,r%i\n", fmt, fd, fs, ft);

		fpu_op(cpu, cp, FPU_OP_ADD, fmt, ft, fs, -1, fd, -1, fmt);
		return 1;
	}

	/*  sub.fmt: Floating-point subtract  */
	if ((function & 0x0000003f) == 0x00000001) {
		if (instruction_trace)
			debug("sub.%i\tr%i,r%i,r%i\n", fmt, fd, fs, ft);

		fpu_op(cpu, cp, FPU_OP_SUB, fmt, ft, fs, -1, fd, -1, fmt);
		return 1;
	}

	/*  mul.fmt: Floating-point multiply  */
	if ((function & 0x0000003f) == 0x00000002) {
		if (instruction_trace)
			debug("mul.%i\tr%i,r%i,r%i\n", fmt, fd, fs, ft);

		fpu_op(cpu, cp, FPU_OP_MUL, fmt, ft, fs, -1, fd, -1, fmt);
		return 1;
	}

	/*  div.fmt: Floating-point divide  */
	if ((function & 0x0000003f) == 0x00000003) {
		if (instruction_trace)
			debug("div.%i\tr%i,r%i,r%i\n", fmt, fd, fs, ft);

		fpu_op(cpu, cp, FPU_OP_DIV, fmt, ft, fs, -1, fd, -1, fmt);
		return 1;
	}

	/*  sqrt.fmt: Floating-point square-root  */
	if ((function & 0x001f003f) == 0x00000004) {
		if (instruction_trace)
			debug("sqrt.%i\tr%i,r%i\n", fmt, fd, fs);

		fpu_op(cpu, cp, FPU_OP_SQRT, fmt, -1, fs, -1, fd, -1, fmt);
		return 1;
	}

	/*  abs.fmt: Floating-point absolute value  */
	if ((function & 0x001f003f) == 0x00000005) {
		if (instruction_trace)
			debug("abs.%i\tr%i,r%i\n", fmt, fd, fs);

		fpu_op(cpu, cp, FPU_OP_ABS, fmt, -1, fs, -1, fd, -1, fmt);
		return 1;
	}

	/*  mov.fmt: Floating-point move  */
	if ((function & 0x0000003f) == 0x00000006) {
		if (instruction_trace)
			debug("mov.%i\tr%i,r%i\n", fmt, fd, fs);

		fpu_op(cpu, cp, FPU_OP_MOV, fmt, -1, fs, -1, fd, -1, fmt);
		return 1;
	}

	/*  neg.fmt: Floating-point negate  */
	if ((function & 0x001f003f) == 0x00000007) {
		if (instruction_trace)
			debug("neg.%i\tr%i,r%i\n", fmt, fd, fs);

		fpu_op(cpu, cp, FPU_OP_NEG, fmt, -1, fs, -1, fd, -1, fmt);
		return 1;
	}

	/*  c.cond.fmt: Floating-point compare  */
	if ((function & 0x000000f0) == 0x00000030) {
		int cond_true;

		if (instruction_trace)
			debug("c.%i.%i\tr%i,r%i,r%i\n", cond, fmt, cc, fs, ft);

		cond_true = fpu_op(cpu, cp, FPU_OP_C, fmt, ft, fs, -1, -1, cond, fmt);

		/*
		 *  Both the FCCR and FCSR contain condition code bits:
		 *	FCCR:  bits 7..0
		 *	FCSR:  bits 31..25 and 23
		 */
		cp->fcr[FPU_FCCR] &= ~(1 << cc);
		if (cond_true)
			cp->fcr[FPU_FCCR] |= (1 << cc);

		if (cc == 0) {
			cp->fcr[FPU_FCSR] &= ~(1 << FCSR_FCC0_SHIFT);
			if (cond_true)
				cp->fcr[FPU_FCSR] |= (1 << FCSR_FCC0_SHIFT);
		} else {
			cp->fcr[FPU_FCSR] &= ~((cc-1) << FCSR_FCC1_SHIFT);
			if (cond_true)
				cp->fcr[FPU_FCSR] |= ((cc-1) << FCSR_FCC1_SHIFT);
		}

		return 1;
	}

	/*  cvt.s.fmt: Convert to single floating-point  */
	if ((function & 0x001f003f) == 0x00000020) {
		if (instruction_trace)
			debug("cvt.s.%i\tr%i,r%i\n", fmt, fd, fs);

		fpu_op(cpu, cp, FPU_OP_MOV, fmt, -1, fs, -1, fd, -1, FMT_S);
		return 1;
	}

	/*  cvt.d.fmt: Convert to double floating-point  */
	if ((function & 0x001f003f) == 0x00000021) {
		if (instruction_trace)
			debug("cvt.d.%i\tr%i,r%i\n", fmt, fd, fs);

		fpu_op(cpu, cp, FPU_OP_MOV, fmt, -1, fs, -1, fd, -1, FMT_D);
		return 1;
	}

	/*  cvt.w.fmt: Convert to word fixed-point  */
	if ((function & 0x001f003f) == 0x00000024) {
		if (instruction_trace)
			debug("cvt.w.%i\tr%i,r%i\n", fmt, fd, fs);

		fpu_op(cpu, cp, FPU_OP_MOV, fmt, -1, fs, -1, fd, -1, FMT_W);
		return 1;
	}

	return 0;
}


/*
 *  coproc_function():
 *
 *  Execute a coprocessor specific instruction.
 *  cp must be != NULL.
 *  Debug trace should be printed for known instructions.
 *
 *  TODO:  This is a mess and should be restructured (again).
 */
void coproc_function(struct cpu *cpu, struct coproc *cp, uint32_t function)
{
	int co_bit, op, rt, rd, fs, g_bit, index, found, i;
	int copz;
	uint64_t vpn2, xmask, tmpvalue;
	int cpnr = cp->coproc_nr;

	/*  For quick reference:  */
	copz = (function >> 21) & 31;
	rt = (function >> 16) & 31;
	rd = (function >> 11) & 31;

	if (cpnr < 2 && (((function & 0x03e007f8) == (COPz_MFCz << 21))
	              || ((function & 0x03e007f8) == (COPz_DMFCz << 21)))) {
		if (instruction_trace)
			debug("%s%i\tr%i,r%i\n", copz==COPz_DMFCz? "dmfc" : "mfc", cpnr, rt, rd);

		coproc_register_read(cpu, cpu->coproc[cpnr], rd, &tmpvalue);
		cpu->gpr[rt] = tmpvalue;
		if (copz == COPz_MFCz) {
			/*  Sign-extend:  */
			cpu->gpr[rt] &= 0xffffffffULL;
			if (cpu->gpr[rt] & 0x80000000ULL)
				cpu->gpr[rt] |= 0xffffffff00000000ULL;
		}
		return;
	}

	if (cpnr < 2 && (((function & 0x03e007f8) == (COPz_MTCz << 21))
	              || ((function & 0x03e007f8) == (COPz_DMTCz << 21)))) {
		if (instruction_trace)
			debug("%s%i\tr%i,r%i\n", copz==COPz_DMTCz? "dmtc" : "mtc", cpnr, rt, rd);

		tmpvalue = cpu->gpr[rt];
		if (copz == COPz_MTCz) {
			/*  Sign-extend:  */
			tmpvalue &= 0xffffffffULL;
			if (tmpvalue & 0x80000000ULL)
				tmpvalue |= 0xffffffff00000000ULL;
		}
		coproc_register_write(cpu, cpu->coproc[cpnr], rd, &tmpvalue);
		return;
	}

	if (cpnr < 2 && (((function & 0x03e007ff) == (COPz_CFCz << 21))
	              || ((function & 0x03e007ff) == (COPz_CTCz << 21)))) {
		switch (copz) {
		case COPz_CFCz:		/*  Copy from FPU control register  */
			rt = (function >> 16) & 31;
			fs = (function >> 11) & 31;
			if (instruction_trace)
				debug("cfc%i\tr%i,r%i\n", cpnr, rt, fs);
			cpu->gpr[rt] = cp->fcr[fs];
			/*  TODO: implement delay for gpr[rt] (for MIPS I,II,III only)  */
			return;
		case COPz_CTCz:		/*  Copy to FPU control register  */
			rt = (function >> 16) & 31;
			fs = (function >> 11) & 31;
			if (instruction_trace)
				debug("ctc%i\tr%i,r%i\n", cpnr, rt, fs);
			if (fs == 0)
				fatal("[ Attempt to write to FPU control register 0 (?) ]\n");
			else
				cp->fcr[fs] = cpu->gpr[rt];
			/*  TODO: implement delay for gpr[rt] (for MIPS I,II,III only)  */
			/*  TODO: writing to control register 31 should cause
				exceptions, depending on status bits!  */
			return;
		default:
			;
		}
	}

#if 1
	if (cpnr==1) {
		if (fpu_function(cpu, cp, function))
			return;
	}
#endif

	/*  For AU1500 and probably others:  deret  */
	if (function == 0x0200001f) {
		if (instruction_trace)
			debug("deret\n");

		/*
		 *  According to the MIPS64 manual, deret loads PC from the
		 *  DEPC cop0 register, and jumps there immediately. No
		 *  delay slot.
		 *
		 *  TODO: This instruction is only available if the processor
		 *  is in debug mode. (What does that mean?)
		 *  TODO: This instruction is undefined in a delay slot.
		 */

		cpu->pc = cpu->pc_last = cp->reg[COP0_DEPC];
		cpu->delay_slot = 0;
		cp->reg[COP0_STATUS] &= ~STATUS_EXL;

		return;
	}


	/*  Ugly R59000 hacks:  */
	if ((function & 0xfffff) == 0x38) {		/*  ei  */
		if (instruction_trace)
			debug("ei\n");
		cpu->coproc[0]->reg[COP0_STATUS] |= R5900_STATUS_EIE;
		return;
	}

	if ((function & 0xfffff) == 0x39) {		/*  di  */
		if (instruction_trace)
			debug("di\n");
		cpu->coproc[0]->reg[COP0_STATUS] &= ~R5900_STATUS_EIE;
		return;
	}

	co_bit = (function >> 25) & 1;

	if (cp->coproc_nr == 0) {
		op = (function) & 31;
		switch (co_bit) {
		case 1:
			switch (op) {
			case COP0_TLBR:		/*  Read indexed TLB entry  */
				if (instruction_trace)
					debug("tlbr\n");
				if (cpu->cpu_type.mmu_model == MMU3K) {
					i = (cp->reg[COP0_INDEX] & R2K3K_INDEX_MASK) >> R2K3K_INDEX_SHIFT;
					if (i >= cp->nr_of_tlbs) {
						/*  TODO:  exception?  */
						fatal("warning: tlbr from index %i (too high)\n", i);
						return;
					}

					cp->reg[COP0_ENTRYHI]  = cp->tlbs[i].hi  & ~0x3f;
					cp->reg[COP0_ENTRYLO0] = cp->tlbs[i].lo0 & ~0xff;
				} else {
					/*  R4000:  */
					i = cp->reg[COP0_INDEX] & INDEX_MASK;
					if (i >= cp->nr_of_tlbs)	/*  TODO:  exception  */
						return;

					g_bit = cp->tlbs[i].hi & TLB_G;

					cp->reg[COP0_PAGEMASK] = cp->tlbs[i].mask;
					cp->reg[COP0_ENTRYHI]  = cp->tlbs[i].hi & ~TLB_G;
					cp->reg[COP0_ENTRYLO1] = cp->tlbs[i].lo1;
					cp->reg[COP0_ENTRYLO0] = cp->tlbs[i].lo0;

					cp->reg[COP0_ENTRYLO0] &= ~ENTRYLO_G;
					cp->reg[COP0_ENTRYLO1] &= ~ENTRYLO_G;
					if (g_bit) {
						cp->reg[COP0_ENTRYLO0] |= ENTRYLO_G;
						cp->reg[COP0_ENTRYLO1] |= ENTRYLO_G;
					}
				}
				return;
			case COP0_TLBWI:	/*  Write indexed  */
			case COP0_TLBWR:	/*  Write random  */
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
					cpu->pc_last_virtual_page =
					    PC_LAST_PAGE_IMPOSSIBLE_VALUE;

				/*  Translation caches must be invalidated:  */
				invalidate_translation_caches(cpu);

				if (instruction_trace) {
					if (op == COP0_TLBWI)
						debug("tlbwi");
					else
						debug("tlbwr");

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

				if (op == COP0_TLBWR) {
#ifdef LAST_USED_TLB_EXPERIMENT
					/*
					 *  This is an experimental thing which
					 *  finds the index with lowest
					 *  last_used value, instead of just a
					 *  random entry:
					 */
					int i, found=-1;
					uint64_t minimum_last_used;
					for (i=(cpu->cpu_type.mmu_model == MMU3K)? 8 : cp->reg[COP0_WIRED]; i<cp->nr_of_tlbs; i++)
						if (found==-1 || cp->tlbs[i].last_used < minimum_last_used) {
							minimum_last_used = cp->tlbs[i].last_used;
							found = i;
						}
					index = found;
#else
					/*  This is the non-experimental, normal behaviour:  */
					if (cpu->cpu_type.mmu_model == MMU3K)
						index = (cp->reg[COP0_RANDOM] & R2K3K_RANDOM_MASK) >> R2K3K_RANDOM_SHIFT;
					else
						index = cp->reg[COP0_RANDOM] & RANDOM_MASK;
#endif
				} else {
					if (cpu->cpu_type.mmu_model == MMU3K)
						index = (cp->reg[COP0_INDEX] & R2K3K_INDEX_MASK) >> R2K3K_INDEX_SHIFT;
					else
						index = cp->reg[COP0_INDEX] & INDEX_MASK;
				}

				if (index >= cp->nr_of_tlbs) {
					fatal("warning: tlb index %i too high (max is %i)\n", index, cp->nr_of_tlbs-1);
					/*  TODO:  cause an exception?  */
					return;
				}

#if 0
				/*  Debug dump of the previous entry at that index:  */
				debug("                old entry at index = %04x", index);
				debug(" mask = %016llx", (long long) cp->tlbs[index].mask);
				debug(" hi = %016llx", (long long) cp->tlbs[index].hi);
				debug(" lo0 = %016llx", (long long) cp->tlbs[index].lo0);
				debug(" lo1 = %016llx\n", (long long) cp->tlbs[index].lo1);
#endif

				/*  Write the entry:  */

				if (cpu->cpu_type.mmu_model == MMU3K) {
					cp->tlbs[index].hi = cp->reg[COP0_ENTRYHI];	/*  & R2K3K_ENTRYHI_VPN_MASK;  */
					cp->tlbs[index].lo0 = cp->reg[COP0_ENTRYLO0];
				} else {
					/*  R4000:  */
					g_bit = (cp->reg[COP0_ENTRYLO0] & cp->reg[COP0_ENTRYLO1]) & ENTRYLO_G;
					cp->tlbs[index].mask = cp->reg[COP0_PAGEMASK];
					cp->tlbs[index].hi   = cp->reg[COP0_ENTRYHI];
					cp->tlbs[index].lo1  = cp->reg[COP0_ENTRYLO1] & ~ENTRYLO_G;
					cp->tlbs[index].lo0  = cp->reg[COP0_ENTRYLO0] & ~ENTRYLO_G;

					cp->tlbs[index].hi &= ~TLB_G;
					if (g_bit)
						cp->tlbs[index].hi |= TLB_G;
				}

				return;
			case COP0_TLBP:		/*  Probe TLB for matching entry  */
				if (instruction_trace)
					debug("tlbp\n");
				if (cpu->cpu_type.mmu_model == MMU3K) {
					vpn2 = cp->reg[COP0_ENTRYHI] & R2K3K_ENTRYHI_VPN_MASK;
					found = -1;
					for (i=0; i<cp->nr_of_tlbs; i++)
						if ( ((cp->tlbs[i].hi & R2K3K_ENTRYHI_ASID_MASK) == (cp->reg[COP0_ENTRYHI] & R2K3K_ENTRYHI_ASID_MASK))
						    || cp->tlbs[i].lo0 & R2K3K_ENTRYLO_G)
							if ((cp->tlbs[i].hi & R2K3K_ENTRYHI_VPN_MASK) == vpn2) {
								found = i;
								break;
							}
				} else {
					/*  R4000 and R10000:  */
					if (cpu->cpu_type.mmu_model == MMU10K)
						xmask = ENTRYHI_R_MASK | ENTRYHI_VPN2_MASK_R10K;
					else
						xmask = ENTRYHI_R_MASK | ENTRYHI_VPN2_MASK;

					vpn2 = cp->reg[COP0_ENTRYHI] & xmask;
					found = -1;
					for (i=0; i<cp->nr_of_tlbs; i++)
						if ( ((cp->tlbs[i].hi & ENTRYHI_ASID) == (cp->reg[COP0_ENTRYHI] & ENTRYHI_ASID))
						    || cp->tlbs[i].hi & TLB_G)
							if ((cp->tlbs[i].hi & xmask) == vpn2) {
								found = i;
								break;
							}
				}
				if (found == -1)
					cp->reg[COP0_INDEX] = INDEX_P;
				else {
					if (cpu->cpu_type.mmu_model == MMU3K)
						cp->reg[COP0_INDEX] = found << R2K3K_INDEX_SHIFT;
					else
						cp->reg[COP0_INDEX] = found;
				}

				/*  Sign extend the index register:  */
				if ((cp->reg[COP0_INDEX] >> 32) == 0 &&
				    cp->reg[COP0_INDEX] & 0x80000000)
					cp->reg[COP0_INDEX] |=
					    0xffffffff00000000ULL;

				return;
			case COP0_RFE:		/*  R2000/R3000 only: Return from Exception  */
				if (instruction_trace)
					debug("rfe\n");
				cpu->last_was_rfe = 1;
				cpu->coproc[0]->reg[COP0_STATUS] =
				    (cpu->coproc[0]->reg[COP0_STATUS] & ~0x3f) |
				    ((cpu->coproc[0]->reg[COP0_STATUS] & 0x3c) >> 2);
				return;
			case COP0_ERET:		/*  R4000: Return from exception  */
				if (instruction_trace)
					debug("eret\n");
				if (cp->reg[COP0_STATUS] & STATUS_ERL) {
					cpu->pc = cpu->pc_last = cp->reg[COP0_ERROREPC];
					cp->reg[COP0_STATUS] &= ~STATUS_ERL;
				} else {
/*  printf("ERET: A status = 0x%016llx pc=%016llx\n", cp->reg[COP0_STATUS], (long long)cpu->pc);  */
					cpu->pc = cpu->pc_last = cp->reg[COP0_EPC];
					cpu->delay_slot = 0;
					cp->reg[COP0_STATUS] &= ~STATUS_EXL;
/*  printf("ERET: B status = 0x%016llx pc=%016llx\n", cp->reg[COP0_STATUS], (long long)cpu->pc);  */
				}
				cpu->rmw = 0;	/*  the "LL bit"  */
				return;
			default:
				;
			}
		default:
			;
		}
	}

	/*  TODO: coprocessor R2020 on DECstation?  */
	if ((cp->coproc_nr==0 || cp->coproc_nr==3) && function == 0x0100ffff)
		return;

	/*  TODO: RM5200 idle (?)  */
	if ((cp->coproc_nr==0 || cp->coproc_nr==3) && function == 0x02000020)
		return;

	if (instruction_trace)
		debug("cop%i\t%08lx\n", cpnr, function);

	fatal("cpu%i: warning: unimplemented coproc%i function %08lx (pc = %016llx)\n",
	    cpu->cpu_id, cp->coproc_nr, function, (long long)cpu->pc_last);

	cpu_exception(cpu, EXCEPTION_CPU, 0, 0, cp->coproc_nr, 0, 0, 0);
}

