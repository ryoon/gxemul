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
 *  $Id: cpu_arm_instr_loadstore.c,v 1.4 2005-09-22 09:06:59 debug Exp $
 *
 *
 *  TODO:
 *
 *	o)  Big-endian ARM loads/stores.
 *
 *	o)  Alignment checks!
 *
 *	o)  Native load/store if the endianness is the same as the host's
 *
 *	o)  All load/store variants with the PC register are not really
 *	    valid. (E.g. a byte load into the PC register. What should that
 *	    accomplish?)
 */


/*
 *  General load/store, by using memory_rw(). If at all possible, memory_rw()
 *  then inserts the page into the translation array, so that the fast
 *  load/store routine below can be used for further accesses.
 */
void A__NAME__general(struct cpu *cpu, struct arm_instr_call *ic)
{
#if !defined(A__P) && defined(A__W)
	const int memory_rw_flags = CACHE_DATA | MEMORY_USER_ACCESS;
#else
	const int memory_rw_flags = CACHE_DATA;
#endif
#ifdef A__B
	unsigned char data[1];
#else
#ifdef A__H
	unsigned char data[2];
#else
	unsigned char data[4];
#endif
#endif
	uint32_t addr, low_pc;
	uint32_t offset =
#ifndef A__U
	    -
#endif
#ifdef A__REG
	    R(cpu, ic, ic->arg[1], 0)
#else
	    ic->arg[1]
#endif
	    ;

	addr = *((uint32_t *)ic->arg[0])
#ifdef A__P
	    + offset
#endif
	    ;

	low_pc = ((size_t)ic - (size_t)cpu->cd.arm.
	    cur_ic_page) / sizeof(struct arm_instr_call);
	cpu->cd.arm.r[ARM_PC] &= ~((ARM_IC_ENTRIES_PER_PAGE-1)
	    << ARM_INSTR_ALIGNMENT_SHIFT);
	cpu->cd.arm.r[ARM_PC] += (low_pc << ARM_INSTR_ALIGNMENT_SHIFT);
	cpu->pc = cpu->cd.arm.r[ARM_PC];

#ifdef A__L
	/*  Load:  */
	if (!cpu->memory_rw(cpu, cpu->mem, addr, data, sizeof(data),
	    MEM_READ, memory_rw_flags)) {
		fatal("load failed: TODO\n");
		return;
	}
#ifdef A__B
	*((uint32_t *)ic->arg[2]) =
#ifdef A__SIGNED
	    (int8_t)
#endif
	    data[0];
#else
#ifdef A__H
	*((uint32_t *)ic->arg[2]) =
#ifdef A__SIGNED
	    (int16_t)
#endif
	    (data[0] + (data[1] << 8));
#else
	*((uint32_t *)ic->arg[2]) = data[0] + (data[1] << 8) +
	    (data[2] << 16) + (data[3] << 24);
#endif
#endif
#else
	/*  Store:  */
#ifdef A__B
	data[0] = *((uint32_t *)ic->arg[2]);
#else
#ifdef A__H
	data[0] = (*((uint32_t *)ic->arg[2]));
	data[1] = (*((uint32_t *)ic->arg[2])) >> 8;
#else
	data[0] = (*((uint32_t *)ic->arg[2]));
	data[1] = (*((uint32_t *)ic->arg[2])) >> 8;
	data[2] = (*((uint32_t *)ic->arg[2])) >> 16;
	data[3] = (*((uint32_t *)ic->arg[2])) >> 24;
#endif
#endif
	if (!cpu->memory_rw(cpu, cpu->mem, addr, data, sizeof(data),
	    MEM_WRITE, memory_rw_flags)) {
		fatal("store failed: TODO\n");
		return;
	}
#endif

#ifdef A__P
#ifdef A__W
	*((uint32_t *)ic->arg[0]) = addr;
#endif
#else	/*  post-index writeback  */
	*((uint32_t *)ic->arg[0]) = addr + offset;
#endif
}


/*
 *  Fast load/store, if the page is in the translation array.
 */
void A__NAME(struct cpu *cpu, struct arm_instr_call *ic)
{
#if !defined(A__P) && defined(A__W)
	/*  T-bit: userland access. Use the general routine for that.  */
	A__NAME__general(cpu, ic);
#else
	uint32_t offset =
#ifndef A__U
	    -
#endif
#ifdef A__REG
	    R(cpu, ic, ic->arg[1], 0)
#else
	    ic->arg[1]
#endif
	    ;
	uint32_t addr = *((uint32_t *)ic->arg[0])
#ifdef A__P
	    + offset
#endif
	    ;
	unsigned char *page = cpu->cd.arm.
#ifdef A__L
	    host_load
#else
	    host_store
#endif
	    [addr >> 12];

	if (page == NULL) {
	        A__NAME__general(cpu, ic);
	} else {
#ifdef A__P
#ifdef A__W
		*((uint32_t *)ic->arg[0]) = addr;
#endif
#else	/*  post-index writeback  */
		*((uint32_t *)ic->arg[0]) = addr + offset;
#endif

#ifdef A__L
#ifdef A__B
		*((uint32_t *)ic->arg[2]) =
#ifdef A__SIGNED
		    (int8_t)
#endif
		    page[addr & 4095];
#else
#ifdef A__H
		addr &= 4095;
		*((uint32_t *)ic->arg[2]) =
#ifdef A__SIGNED
		    (int16_t)
#endif
		    (page[addr] + (page[addr + 1] << 8));
#else
		addr &= 4095;
		*((uint32_t *)ic->arg[2]) = page[addr] +
		    (page[addr + 1] << 8) +
		    (page[addr + 2] << 16) +
		    (page[addr + 3] << 24);
#endif
#endif
#else
#ifdef A__B
		page[addr & 4095] = *((uint32_t *)ic->arg[2]);
#else
#ifdef A__H
		addr &= 4095;
		page[addr] = *((uint32_t *)ic->arg[2]);
		page[addr+1] = (*((uint32_t *)ic->arg[2])) >> 8;
#else
		addr &= 4095;
		page[addr] = *((uint32_t *)ic->arg[2]);
		page[addr+1] = (*((uint32_t *)ic->arg[2])) >> 8;
		page[addr+2] = (*((uint32_t *)ic->arg[2])) >> 16;
		page[addr+3] = (*((uint32_t *)ic->arg[2])) >> 24;
#endif
#endif
#endif
	}
#endif	/*  not T-bit  */
}


/*
 *  Special case when loading or storing the ARM's PC register, or when
 *  the PC register is used as the base address register.
 *
 *	o)  Loads into the PC register cause a branch.
 *	    (If an exception occured during the load, then the pc register
 *	    should already point to the exception handler; in that case, we
 *	    simply recalculate the pointers a second time and no harm is
 *	    done.)
 *
 *	o)  Stores store the PC of the current instruction + 8.
 *	    The solution I have choosen is to calculate this value and
 *	    place it into a temporary variable, which is then used for
 *	    the store.
 */
void A__NAME_PC(struct cpu *cpu, struct arm_instr_call *ic)
{
#ifdef A__L
	/*  Load:  */
	if (ic->arg[0] == (size_t)(&cpu->cd.arm.r[ARM_PC]) ||
	    ic->arg[0] == (size_t)(&cpu->cd.arm.tmp_pc)) {
		/*  tmp_pc = current PC + 8:  */
		uint32_t low_pc, tmp;
		low_pc = ((size_t)ic - (size_t) cpu->cd.arm.cur_ic_page) /
		    sizeof(struct arm_instr_call);
		tmp = cpu->pc & ~((ARM_IC_ENTRIES_PER_PAGE-1) <<
		    ARM_INSTR_ALIGNMENT_SHIFT);
		tmp += (low_pc << ARM_INSTR_ALIGNMENT_SHIFT);
		cpu->cd.arm.tmp_pc = tmp + 8;
		ic->arg[0] = (size_t)(&cpu->cd.arm.tmp_pc);
	}
	A__NAME(cpu, ic);
	if (ic->arg[2] == (size_t)(&cpu->cd.arm.r[ARM_PC])) {
		cpu->pc = cpu->cd.arm.r[ARM_PC];
		arm_pc_to_pointers(cpu);
	}
#else
	/*  Store:  */
	uint32_t low_pc, tmp;
	/*  Calculate tmp from this instruction's PC + 8  */
	low_pc = ((size_t)ic - (size_t) cpu->cd.arm.cur_ic_page) /
	    sizeof(struct arm_instr_call);
	tmp = cpu->pc & ~((ARM_IC_ENTRIES_PER_PAGE-1) <<
	    ARM_INSTR_ALIGNMENT_SHIFT);
	tmp += (low_pc << ARM_INSTR_ALIGNMENT_SHIFT);
	cpu->cd.arm.tmp_pc = tmp + 8;
	if (ic->arg[0] == (size_t)(&cpu->cd.arm.r[ARM_PC]) ||
	    ic->arg[0] == (size_t)(&cpu->cd.arm.tmp_pc))
		ic->arg[0] = (size_t)(&cpu->cd.arm.tmp_pc);
	if (ic->arg[2] == (size_t)(&cpu->cd.arm.r[ARM_PC]) ||
	    ic->arg[2] == (size_t)(&cpu->cd.arm.tmp_pc))
		ic->arg[2] = (size_t)(&cpu->cd.arm.tmp_pc);
	A__NAME(cpu, ic);
#endif
}


#ifndef A__NOCONDITIONS
/*  Load/stores with all registers except the PC register:  */
void A__NAME__eq(struct cpu *cpu, struct arm_instr_call *ic)
{ if (cpu->cd.arm.cpsr & ARM_FLAG_Z) A__NAME(cpu, ic); }
void A__NAME__ne(struct cpu *cpu, struct arm_instr_call *ic)
{ if (!(cpu->cd.arm.cpsr & ARM_FLAG_Z)) A__NAME(cpu, ic); }
void A__NAME__cs(struct cpu *cpu, struct arm_instr_call *ic)
{ if (cpu->cd.arm.cpsr & ARM_FLAG_C) A__NAME(cpu, ic); }
void A__NAME__cc(struct cpu *cpu, struct arm_instr_call *ic)
{ if (!(cpu->cd.arm.cpsr & ARM_FLAG_C)) A__NAME(cpu, ic); }
void A__NAME__mi(struct cpu *cpu, struct arm_instr_call *ic)
{ if (cpu->cd.arm.cpsr & ARM_FLAG_N) A__NAME(cpu, ic); }
void A__NAME__pl(struct cpu *cpu, struct arm_instr_call *ic)
{ if (!(cpu->cd.arm.cpsr & ARM_FLAG_N)) A__NAME(cpu, ic); }
void A__NAME__vs(struct cpu *cpu, struct arm_instr_call *ic)
{ if (cpu->cd.arm.cpsr & ARM_FLAG_V) A__NAME(cpu, ic); }
void A__NAME__vc(struct cpu *cpu, struct arm_instr_call *ic)
{ if (!(cpu->cd.arm.cpsr & ARM_FLAG_V)) A__NAME(cpu, ic); }

void A__NAME__hi(struct cpu *cpu, struct arm_instr_call *ic)
{ if (cpu->cd.arm.cpsr & ARM_FLAG_C &&
!(cpu->cd.arm.cpsr & ARM_FLAG_Z)) A__NAME(cpu, ic); }
void A__NAME__ls(struct cpu *cpu, struct arm_instr_call *ic)
{ if (cpu->cd.arm.cpsr & ARM_FLAG_Z ||
!(cpu->cd.arm.cpsr & ARM_FLAG_C)) A__NAME(cpu, ic); }
void A__NAME__ge(struct cpu *cpu, struct arm_instr_call *ic)
{ if (((cpu->cd.arm.cpsr & ARM_FLAG_N)?1:0) ==
((cpu->cd.arm.cpsr & ARM_FLAG_V)?1:0)) A__NAME(cpu, ic); }
void A__NAME__lt(struct cpu *cpu, struct arm_instr_call *ic)
{ if (((cpu->cd.arm.cpsr & ARM_FLAG_N)?1:0) !=
((cpu->cd.arm.cpsr & ARM_FLAG_V)?1:0)) A__NAME(cpu, ic); }
void A__NAME__gt(struct cpu *cpu, struct arm_instr_call *ic)
{ if (((cpu->cd.arm.cpsr & ARM_FLAG_N)?1:0) ==
((cpu->cd.arm.cpsr & ARM_FLAG_V)?1:0) &&
!(cpu->cd.arm.cpsr & ARM_FLAG_Z)) A__NAME(cpu, ic); }
void A__NAME__le(struct cpu *cpu, struct arm_instr_call *ic)
{ if (((cpu->cd.arm.cpsr & ARM_FLAG_N)?1:0) !=
((cpu->cd.arm.cpsr & ARM_FLAG_V)?1:0) ||
(cpu->cd.arm.cpsr & ARM_FLAG_Z)) A__NAME(cpu, ic); }


/*  Load/stores with the PC register:  */
void A__NAME_PC__eq(struct cpu *cpu, struct arm_instr_call *ic)
{ if (cpu->cd.arm.cpsr & ARM_FLAG_Z) A__NAME_PC(cpu, ic); }
void A__NAME_PC__ne(struct cpu *cpu, struct arm_instr_call *ic)
{ if (!(cpu->cd.arm.cpsr & ARM_FLAG_Z)) A__NAME_PC(cpu, ic); }
void A__NAME_PC__cs(struct cpu *cpu, struct arm_instr_call *ic)
{ if (cpu->cd.arm.cpsr & ARM_FLAG_C) A__NAME_PC(cpu, ic); }
void A__NAME_PC__cc(struct cpu *cpu, struct arm_instr_call *ic)
{ if (!(cpu->cd.arm.cpsr & ARM_FLAG_C)) A__NAME_PC(cpu, ic); }
void A__NAME_PC__mi(struct cpu *cpu, struct arm_instr_call *ic)
{ if (cpu->cd.arm.cpsr & ARM_FLAG_N) A__NAME_PC(cpu, ic); }
void A__NAME_PC__pl(struct cpu *cpu, struct arm_instr_call *ic)
{ if (!(cpu->cd.arm.cpsr & ARM_FLAG_N)) A__NAME_PC(cpu, ic); }
void A__NAME_PC__vs(struct cpu *cpu, struct arm_instr_call *ic)
{ if (cpu->cd.arm.cpsr & ARM_FLAG_V) A__NAME_PC(cpu, ic); }
void A__NAME_PC__vc(struct cpu *cpu, struct arm_instr_call *ic)
{ if (!(cpu->cd.arm.cpsr & ARM_FLAG_V)) A__NAME_PC(cpu, ic); }

void A__NAME_PC__hi(struct cpu *cpu, struct arm_instr_call *ic)
{ if (cpu->cd.arm.cpsr & ARM_FLAG_C &&
!(cpu->cd.arm.cpsr & ARM_FLAG_Z)) A__NAME_PC(cpu, ic); }
void A__NAME_PC__ls(struct cpu *cpu, struct arm_instr_call *ic)
{ if (cpu->cd.arm.cpsr & ARM_FLAG_Z ||
!(cpu->cd.arm.cpsr & ARM_FLAG_C)) A__NAME_PC(cpu, ic); }
void A__NAME_PC__ge(struct cpu *cpu, struct arm_instr_call *ic)
{ if (((cpu->cd.arm.cpsr & ARM_FLAG_N)?1:0) ==
((cpu->cd.arm.cpsr & ARM_FLAG_V)?1:0)) A__NAME_PC(cpu, ic); }
void A__NAME_PC__lt(struct cpu *cpu, struct arm_instr_call *ic)
{ if (((cpu->cd.arm.cpsr & ARM_FLAG_N)?1:0) !=
((cpu->cd.arm.cpsr & ARM_FLAG_V)?1:0)) A__NAME_PC(cpu, ic); }
void A__NAME_PC__gt(struct cpu *cpu, struct arm_instr_call *ic)
{ if (((cpu->cd.arm.cpsr & ARM_FLAG_N)?1:0) ==
((cpu->cd.arm.cpsr & ARM_FLAG_V)?1:0) &&
!(cpu->cd.arm.cpsr & ARM_FLAG_Z)) A__NAME_PC(cpu, ic); }
void A__NAME_PC__le(struct cpu *cpu, struct arm_instr_call *ic)
{ if (((cpu->cd.arm.cpsr & ARM_FLAG_N)?1:0) !=
((cpu->cd.arm.cpsr & ARM_FLAG_V)?1:0) ||
(cpu->cd.arm.cpsr & ARM_FLAG_Z)) A__NAME_PC(cpu, ic); }
#endif
