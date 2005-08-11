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
 *  $Id: cpu_ppc_instr_loadstore.c,v 1.2 2005-08-11 21:22:31 debug Exp $
 *
 *  POWER/PowerPC load/store instructions.
 *
 *
 *  Load/store instructions have the following arguments:
 *
 *  arg[0] = pointer to the register to load to or store from
 *  arg[1] = pointer to the base register
 *  arg[2] = offset (as an int32_t)
 */


#ifndef LS_IGNOREOFS
void LS_GENERIC_N(struct cpu *cpu, struct ppc_instr_call *ic)
{
#ifdef MODE32
	uint32_t addr = reg(ic->arg[1]) + (int32_t)ic->arg[2];
	unsigned char data[LS_SIZE];

#ifdef LS_LOAD
	if (!cpu->memory_rw(cpu, cpu->mem, addr, data, sizeof(data),
	    MEM_READ, CACHE_DATA)) {
		fatal("load failed: TODO\n");
		exit(1);
	}
#ifdef LS_B
	reg(ic->arg[0]) =
#ifndef LS_ZERO
	    (int8_t)
#endif
	    data[0];
#endif
#ifdef LS_H
	reg(ic->arg[0]) =
#ifndef LS_ZERO
	    (int16_t)
#endif
	    ((data[0] << 8) + data[1]);
#endif
#ifdef LS_W
	reg(ic->arg[0]) =
#ifndef LS_ZERO
	    (int32_t)
#endif
	    ((data[0] << 24) + (data[1] << 16) +
	    (data[2] << 8) + data[3]);
#endif
#ifdef LS_D
	reg(ic->arg[0]) =
	    ((uint64_t)data[0] << 56) +
	    ((uint64_t)data[1] << 48) +
	    ((uint64_t)data[2] << 40) +
	    ((uint64_t)data[3] << 32) +
	    (data[4] << 24) + (data[5] << 16) + (data[6] << 8) + data[7];
#endif

#else	/*  store:  */

#ifdef LS_B
	data[0] = reg(ic->arg[0]);
#endif
#ifdef LS_H
	data[0] = reg(ic->arg[0]) >> 8;
	data[1] = reg(ic->arg[0]);
#endif
#ifdef LS_W
	data[0] = reg(ic->arg[0]) >> 24;
	data[1] = reg(ic->arg[0]) >> 16;
	data[2] = reg(ic->arg[0]) >> 8;
	data[3] = reg(ic->arg[0]);
#endif
#ifdef LS_D
	data[0] = (uint64_t)reg(ic->arg[0]) >> 56;
	data[1] = (uint64_t)reg(ic->arg[0]) >> 48;
	data[2] = (uint64_t)reg(ic->arg[0]) >> 40;
	data[3] = (uint64_t)reg(ic->arg[0]) >> 32;
	data[4] = reg(ic->arg[0]) >> 24;
	data[5] = reg(ic->arg[0]) >> 16;
	data[6] = reg(ic->arg[0]) >> 8;
	data[7] = reg(ic->arg[0]);
#endif
	if (!cpu->memory_rw(cpu, cpu->mem, addr, data, sizeof(data),
	    MEM_WRITE, CACHE_DATA)) {
		fatal("store failed: TODO\n");
		exit(1);
	}
#endif

#ifdef LS_UPDATE
	reg(ic->arg[1]) = addr;
#endif
#else	/*  !MODE32  */
	fatal("TODO: mode64\n");
#endif	/*  !MODE32  */
}
#endif


void LS_N(struct cpu *cpu, struct ppc_instr_call *ic)
{
#ifdef MODE32
	uint32_t addr = reg(ic->arg[1])
#ifndef LS_IGNOREOFS
	    + (int32_t)ic->arg[2];
#endif
	    ;

	unsigned char *page = cpu->cd.ppc.
#ifdef LS_LOAD
	    host_load
#else
	    host_store
#endif
	    [addr >> 12];
#ifdef LS_UPDATE
	uint32_t new_addr = addr;
#endif

#ifndef LS_B
	if (addr & (LS_SIZE-1)) {
		fatal("PPC LOAD/STORE misalignment: TODO\n");
		exit(1);
	}
#endif

	if (page == NULL) {
		LS_GENERIC_N(cpu, ic);
		return;
	} else {
		addr &= 4095;
#ifdef LS_LOAD
		/*  Load:  */
#ifdef LS_B
		reg(ic->arg[0]) =
#ifndef LS_ZERO
		    (int8_t)
#endif
		    page[addr];
#endif
#ifdef LS_H
		reg(ic->arg[0]) =
#ifndef LS_ZERO
		    (int16_t)
#endif
		    ((page[addr] << 8) + page[addr+1]);
#endif
#ifdef LS_W
		reg(ic->arg[0]) =
#ifndef LS_ZERO
		    (int32_t)
#endif
		    ((page[addr] << 24) + (page[addr+1] << 16) +
		    (page[addr+2] << 8) + page[addr+3]);
#endif
#ifdef LS_D
		reg(ic->arg[0]) =
		    ((uint64_t)page[addr+0] << 56) +
		    ((uint64_t)page[addr+1] << 48) +
		    ((uint64_t)page[addr+2] << 40) +
		    ((uint64_t)page[addr+3] << 32) +
		    (page[addr+4] << 24) + (page[addr+5] << 16) +
		    (page[addr+6] << 8) + page[addr+7];
#endif

#else	/*  !LS_LOAD  */

		/*  Store:  */
#ifdef LS_B
		page[addr] = reg(ic->arg[0]);
#endif
#ifdef LS_H
		page[addr]   = reg(ic->arg[0]) >> 8;
		page[addr+1] = reg(ic->arg[0]);
#endif
#ifdef LS_W
		page[addr]   = reg(ic->arg[0]) >> 24;
		page[addr+1] = reg(ic->arg[0]) >> 16;
		page[addr+2] = reg(ic->arg[0]) >> 8;
		page[addr+3] = reg(ic->arg[0]);
#endif
#ifdef LS_D
		page[addr]   = (uint64_t)reg(ic->arg[0]) >> 56;
		page[addr+1] = (uint64_t)reg(ic->arg[0]) >> 48;
		page[addr+2] = (uint64_t)reg(ic->arg[0]) >> 40;
		page[addr+3] = (uint64_t)reg(ic->arg[0]) >> 32;
		page[addr+4] = reg(ic->arg[0]) >> 24;
		page[addr+5] = reg(ic->arg[0]) >> 16;
		page[addr+6] = reg(ic->arg[0]) >> 8;
		page[addr+7] = reg(ic->arg[0]);
#endif
#endif	/*  !LS_LOAD  */
	}

#ifdef LS_UPDATE
	reg(ic->arg[1]) = new_addr;
#endif

#else	/*  !MODE32  */
	fatal("ppc load/store mode64: TODO\n");
	exit(1);
#endif
}

