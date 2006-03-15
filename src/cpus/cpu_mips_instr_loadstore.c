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
 *  $Id: cpu_mips_instr_loadstore.c,v 1.2 2006-03-15 19:22:56 debug Exp $
 *
 *  MIPS load/store instructions; the following args are used:
 *  
 *  arg[0] = pointer to the register to load to or store from
 *  arg[1] = pointer to the base register
 *  arg[2] = offset (as an int32_t)
 *
 *  The GENERIC function always checks for alignment, and supports both big
 *  and little endian byte order.
 *
 *  The quick function is included multiple times (alignment check on/off,
 *  big/little endian) for each GENERIC function.
 */


#ifdef LS_INCLUDE_GENERIC
void LS_GENERIC_N(struct cpu *cpu, struct mips_instr_call *ic)
{
	MODE_int_t addr = reg(ic->arg[1]) + (int32_t)ic->arg[2];
	uint8_t data[LS_SIZE];
#ifdef LS_LOAD
	uint64_t x;
#endif

	/*  Synchronize the PC:  */
	int low_pc = ((size_t)ic - (size_t)cpu->cd.mips.cur_ic_page)
	    / sizeof(struct mips_instr_call);
	cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1)<<MIPS_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (low_pc << MIPS_INSTR_ALIGNMENT_SHIFT);

#ifndef LS_1
	/*  Check alignment:  */
	if (addr & (LS_SIZE - 1)) {
		fatal("TODO: mips dyntrans alignment exception, size = %i,"
		    " addr = %016llx\n", LS_SIZE, (long long)addr);
		exit(1);
	}
#endif

#ifdef LS_LOAD
	if (!cpu->memory_rw(cpu, cpu->mem, addr, data, sizeof(data),
	    MEM_READ, CACHE_DATA)) {
		/*  Exception.  */
		return;
	}
	x = memory_readmax64(cpu, data, LS_SIZE);
#ifdef LS_SIGNED
#ifdef LS_1
	x = (int8_t)x;
#endif
#ifdef LS_2
	x = (int16_t)x;
#endif
#ifdef LS_4
	x = (int32_t)x;
#endif
#endif
	reg(ic->arg[0]) = x;
#else	/*  LS_STORE:  */
	memory_writemax64(cpu, data, LS_SIZE, reg(ic->arg[0]));
	if (!cpu->memory_rw(cpu, cpu->mem, addr, data, sizeof(data),
	    MEM_WRITE, CACHE_DATA)) {
		/*  Exception.  */
		return;
	}
#endif
}
#endif	/*  LS_INCLUDE_GENERIC  */


void LS_N(struct cpu *cpu, struct mips_instr_call *ic)
{
/*	uint64_t addr = reg(ic->arg[1]) + (int32_t)ic->arg[2];  */

	/*  TEMPORARY:  */
	LS_GENERIC_N(cpu, ic);
}

