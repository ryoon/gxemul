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
 *  $Id: coproc.c,v 1.19 2004-03-17 21:08:11 debug Exp $
 *
 *  Emulation of MIPS coprocessors.
 *
 *  TODO: separate out math coprocessor stuff (?)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "misc.h"


extern int instruction_trace;
extern int register_dump;
extern int prom_emulation;

char *cop0_names[32] = COP0_NAMES;


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

		c->reg[COP0_STATUS] = 0;

		c->reg[COP0_COMPARE] = (uint64_t)-1;

		/*  For stand alone systems, this should probably be set during bootup:  */
		if (!prom_emulation)
			c->reg[COP0_STATUS] |= STATUS_BEV;

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
		case MIPS_R4000:
		case MIPS_RM5200:	/*  rm5200 is just a wild guess  */
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
		case MIPS_R5000:
			/*
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
			      (   1 << 31)	/*  Config 1 present bit  */
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

		switch (cpu->cpu_type.rev) {
		case MIPS_R2000:	fpu_rev = MIPS_R2010;	break;
		case MIPS_R3000:	fpu_rev = MIPS_R3010;
					other_stuff |= 0x40;	/*  or 0x30? TODO  */
					break;
		case MIPS_R6000:	fpu_rev = MIPS_R6010;	break;
		case MIPS_R4000:	fpu_rev = MIPS_R4010;	break;

		case MIPS_5K:		other_stuff = COP1_REVISION_DOUBLE | COP1_REVISION_SINGLE;
		case MIPS_R5000:
		case MIPS_RM5200:	fpu_rev = cpu->cpu_type.rev;
					other_stuff |= cpu->cpu_type.sub;
					break;
		case MIPS_R10000:	fpu_rev = MIPS_R10000;	break;
		default:		fpu_rev = MIPS_SOFT;
		}

		c->fcr[COP1_REVISION] = (fpu_rev << 8) | other_stuff;
	}

	return c;
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

	if (cp->coproc_nr==1)	unimpl = 0;

	if (unimpl) {
		fatal("cpu%i: warning: read from unimplemented coproc%i"
		    " register %i (%s)\n", cpu->cpu_id, cp->coproc_nr, reg_nr,
		    cp->coproc_nr==0? cop0_names[reg_nr] : "?");

		cpu_exception(cpu, EXCEPTION_CPU, 0, 0, 0, cp->coproc_nr, 0, 0, 0);
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
			    int offset;
			    symbol = get_symbol_name(cpu->pc_last, &offset);
			    debug("YO! pc = 0x%08llx <%s> lo=%016llx\n", (long long)cpu->pc_last, symbol? symbol : "no symbol", (long long)tmp); */
			tmp &= (R2K3K_ENTRYLO_PFN_MASK |
			    R2K3K_ENTRYLO_N | R2K3K_ENTRYLO_D |
			    R2K3K_ENTRYLO_V | R2K3K_ENTRYLO_G);
		} else if (cpu->cpu_type.mmu_model == MMU4K) {
			tmp &= (ENTRYLO_PFN_MASK | ENTRYLO_C_MASK |
			    ENTRYLO_D | ENTRYLO_V | ENTRYLO_G);
		}
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
		unimpl = 0;
		if (cpu->cpu_type.mmu_model == MMU3K && (tmp & 0x3f)!=0) {
			/* char *symbol;
			   int offset;
			   symbol = get_symbol_name(cpu->pc_last, &offset);
			   debug("YO! pc = 0x%08llx <%s> hi=%016llx\n", (long long)cpu->pc_last, symbol? symbol : "no symbol", (long long)tmp);  */
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
fatal("xcontext 0x%016llx\n", tmp);
		unimpl = 0;
	}

	if (cp->coproc_nr==0 && reg_nr==COP0_ERRCTL) {
		/*  TODO: R10000  */
		unimpl = 0;
	}

	if (cp->coproc_nr==1)	unimpl = 0;

	if (unimpl) {
		fatal("cpu%i: warning: write to unimplemented coproc%i "
		    "register %i (%s), data = 0x%016llx\n", cpu->cpu_id, cp->coproc_nr, reg_nr,
		    cp->coproc_nr==0? cop0_names[reg_nr] : "?", (long long)tmp);

		cpu_exception(cpu, EXCEPTION_CPU, 0, 0, 0, cp->coproc_nr, 0, 0, 0);
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
 *  coproc_function();
 *
 *  Execute a coprocessor specific instruction.
 */
void coproc_function(struct cpu *cpu, struct coproc *cp, uint32_t function)
{
	int co_bit, op, rt, fs, g_bit, index, found, i;
	uint64_t vpn2, xmask;

/*  Ugly R59000 hacks:  */
if ((function & 0xfffff) == 0x38) {		/*  ei  */
	cpu->coproc[0]->reg[COP0_STATUS] |= R5900_STATUS_EIE;
	return;
}
if ((function & 0xfffff) == 0x39) {		/*  di  */
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
#if 0
				if (op == COP0_TLBWI)
					debug("TLBWI");
				else
					debug("TLBWR");
				debug(": index = %08llx", (long long) cp->reg[COP0_INDEX]);
				debug(" random = %08llx", (long long) cp->reg[COP0_RANDOM]);
				debug(" mask = %016llx", (long long) cp->reg[COP0_PAGEMASK]);
				debug(" hi = %016llx", (long long) cp->reg[COP0_ENTRYHI]);
				debug(" lo0 = %016llx", (long long) cp->reg[COP0_ENTRYLO0]);
				debug(" lo1 = %016llx\n", (long long) cp->reg[COP0_ENTRYLO1]);
#endif

				if (op == COP0_TLBWR) {
#if 0
					/*
					 *  This is an experimental thing which finds the index
					 *  with lowest last_used value, instead of just a random
					 *  entry:
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
				if ((cp->reg[COP0_INDEX] >> 32) == 0 && cp->reg[COP0_INDEX] & 0x80000000)
					cp->reg[COP0_INDEX] |= 0xffffffff00000000;

				return;
			case COP0_RFE:		/*  R2000/R3000 only: Return from Exception  */
				/*  cpu->last_was_rfe = 1;  */
				/*  TODO: should this be delayed?  */
				cpu->coproc[0]->reg[COP0_STATUS] =
				    (cpu->coproc[0]->reg[COP0_STATUS] & ~0x3f) |
				    ((cpu->coproc[0]->reg[COP0_STATUS] & 0x3c) >> 2);
				return;
			case COP0_ERET:		/*  R4000: Return from exception  */
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

	if (cp->coproc_nr == 1) {
		op = (function >> 21) & 31;
		switch (op) {
		case COPz_CFCz:		/*  Copy from FPU control register  */
			rt = (function >> 16) & 31;
			fs = (function >> 11) & 31;
			cpu->gpr[rt] = cp->fcr[fs];
			/*  TODO: implement delay for gpr[rt] (for MIPS I,II,III only)  */
			return;
		case COPz_CTCz:		/*  Copy to FPU control register  */
			rt = (function >> 16) & 31;
			fs = (function >> 11) & 31;
			cp->fcr[fs] = cpu->gpr[rt];
			/*  TODO: implement delay for gpr[rt] (for MIPS I,II,III only)  */
			/*  TODO: writing to control register 31 should cause
				exceptions, depending on status bits!  */
			return;
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

	fatal("cpu%i: warning: unimplemented coproc%i function %08lx (pc = %016llx)\n",
	    cpu->cpu_id, cp->coproc_nr, function, (long long)cpu->pc_last);

	cpu_exception(cpu, EXCEPTION_CPU, 0, 0, 0, cp->coproc_nr, 0, 0, 0);
}

