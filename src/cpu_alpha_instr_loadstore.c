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
 *  $Id: cpu_alpha_instr_loadstore.c,v 1.4 2005-07-19 11:23:25 debug Exp $
 *
 *  Alpha load/store instructions.  (Included from cpu_alpha_instr_inc.c.)
 *
 *
 *  Load/store instructions have the following arguments:
 *  
 *  arg[0] = pointer to the register to load to or store from (uint64_t)
 *  arg[1] = pointer to the base register (uint64_t)
 *  arg[2] = offset (as an int32_t)
 */


#ifndef LS_IGNORE_OFFSET
#ifndef LS_ALIGN_CHECK
void LS_GENERIC_N(struct cpu *cpu, struct alpha_instr_call *ic)
{
#ifdef LS_B
	unsigned char data[1];
#endif
#ifdef LS_W
	unsigned char data[2];
#endif
#ifdef LS_L
	unsigned char data[4];
#endif
#ifdef LS_Q
	unsigned char data[8];
#endif
	uint64_t addr = *((uint64_t *)ic->arg[1]);
	uint64_t data_x;

	addr += (int32_t)ic->arg[2];

#ifdef LS_LOAD
	/*  Load:  */
	if (!cpu->memory_rw(cpu, cpu->mem, addr, data, sizeof(data),
	    MEM_READ, CACHE_DATA)) {
		fatal("store failed: TODO\n");
		exit(1);
	}

	data_x = data[0];
#ifndef LS_B
	data_x += (data[1] << 8);
#ifndef LS_W
	data_x += (data[2] << 16);
	data_x += (data[3] << 24);
#ifndef LS_L
	data_x += ((uint64_t)data[4] << 32);
	data_x += ((uint64_t)data[5] << 40);
	data_x += ((uint64_t)data[6] << 48);
	data_x += ((uint64_t)data[7] << 56);
#endif
#endif
#endif
	*((uint64_t *)ic->arg[0]) = data_x;
#else
	/*  Store:  */
	data_x = *((uint64_t *)ic->arg[0]);
	data[0] = data_x;
#ifndef LS_B
	data[1] = data_x >> 8;
#ifndef LS_W
	data[2] = data_x >> 16;
	data[3] = data_x >> 24;
#ifndef LS_L
	data[4] = data_x >> 32;
	data[5] = data_x >> 40;
	data[6] = data_x >> 48;
	data[7] = data_x >> 56;
#endif
#endif
#endif

	if (!cpu->memory_rw(cpu, cpu->mem, addr, data, sizeof(data),
	    MEM_WRITE, CACHE_DATA)) {
		fatal("store failed: TODO\n");
		exit(1);
	}
#endif
}
#endif
#endif


void LS_N(struct cpu *cpu, struct alpha_instr_call *ic)
{
	int first, a, b, c;
	uint64_t addr;

	addr = (*((uint64_t *)ic->arg[1]))
#ifndef LS_IGNORE_OFFSET
	    + (int32_t)ic->arg[2]
#endif
	    ;

	first = addr >> ALPHA_TOPSHIFT;
	a = (addr >> ALPHA_LEVEL0_SHIFT) & (ALPHA_LEVEL0 - 1);
	b = (addr >> ALPHA_LEVEL1_SHIFT) & (ALPHA_LEVEL1 - 1);
	c = addr & 8191;

#ifdef LS_ALIGN_CHECK
#ifndef LS_B
	if (c &
#ifdef LS_W
	    1
#endif
#ifdef LS_L
	    3
#endif
#ifdef LS_Q
	    7
#endif
	    ) {
		LS_GENERIC_N(cpu, ic);
		return;
	}
	else
#endif
#endif

	if (first == 0) {
		struct alpha_vph_page *vph_p;
		unsigned char *page;
		vph_p = cpu->cd.alpha.vph_table0[a];
		page = vph_p->host_load[b];
		if (page != NULL) {
#ifdef LS_LOAD
#ifdef LS_B
			*((uint64_t *)ic->arg[0]) = page[c];
#endif
#ifdef LS_W
			int32_t d;
			d = page[c];
			d += (page[c+1] << 8);
			*((uint64_t *)ic->arg[0]) = d;
#endif
#ifdef LS_L
			int32_t d;
			d = page[c];
			d += (page[c+1] << 8);
			d += (page[c+2] << 16);
			d += (page[c+3] << 24);
			*((uint64_t *)ic->arg[0]) = d;
#endif
#ifdef LS_Q
			uint64_t d;
			d = page[c];
			d += (page[c+1] << 8);
			d += (page[c+2] << 16);
			d += (page[c+3] << 24);
			d += ((uint64_t)page[c+4] << 32);
			d += ((uint64_t)page[c+5] << 40);
			d += ((uint64_t)page[c+6] << 48);
			d += ((uint64_t)page[c+7] << 56);
			*((uint64_t *)ic->arg[0]) = d;
#endif
#else
			/*  Store:  */
#ifdef LS_B
			page[c] = *((uint64_t *)ic->arg[0]);
#endif
#ifdef LS_W
			uint32_t d = *((uint64_t *)ic->arg[0]);
			*((uint16_t *) (page + c)) = d;
#endif
#ifdef LS_L
			uint32_t d = *((uint64_t *)ic->arg[0]);
			*((uint32_t *) (page + c)) = d;
#endif
#ifdef LS_Q
			uint64_t d = *((uint64_t *)ic->arg[0]);
			*((uint64_t *) (page + c)) = d;
#endif
#endif	/*  !LS_LOAD  */
		} else
			LS_GENERIC_N(cpu, ic);
	} else if (first == ALPHA_TOP_KERNEL) {
		struct alpha_vph_page *vph_p;
		unsigned char *page;
		vph_p = cpu->cd.alpha.vph_table0_kernel[a];
		page = vph_p->host_load[b];
		if (page != NULL) {
#ifdef LS_LOAD
#ifdef LS_B
			*((uint64_t *)ic->arg[0]) = page[c];
#endif
#ifdef LS_W
			uint32_t d;
			d = page[c];
			d += (page[c+1] << 8);
			*((uint64_t *)ic->arg[0]) = d;
#endif
#ifdef LS_L
			uint32_t d;
			d = page[c];
			d += (page[c+1] << 8);
			d += (page[c+2] << 16);
			d += (page[c+3] << 24);
			*((uint64_t *)ic->arg[0]) = d;
#endif
#ifdef LS_Q
			uint64_t d;
			d = page[c];
			d += (page[c+1] << 8);
			d += (page[c+2] << 16);
			d += (page[c+3] << 24);
			d += ((uint64_t)page[c+4] << 32);
			d += ((uint64_t)page[c+5] << 40);
			d += ((uint64_t)page[c+6] << 48);
			d += ((uint64_t)page[c+7] << 56);
			*((uint64_t *)ic->arg[0]) = d;
#endif
#else
			/*  Store:  */
#ifdef HOST_BIG_ENDIAN
			uint64_t data_x = *((uint64_t *)ic->arg[0]);
			data[0] = data_x;
#ifndef LS_B
			data[1] = data_x >> 8;
#ifndef LS_W
			data[2] = data_x >> 16;
			data[3] = data_x >> 24;
#ifndef LS_L
			data[4] = data_x >> 32;
			data[5] = data_x >> 40;
			data[6] = data_x >> 48;
			data[7] = data_x >> 56;
#endif
#endif
#endif
#else
			/*  Native byte order:  */
#ifdef LS_B
			page[c] = *((uint64_t *)ic->arg[0]);
#endif
#ifdef LS_W
			uint32_t d = *((uint64_t *)ic->arg[0]);
			*((uint16_t *) (page + c)) = d;
#endif
#ifdef LS_L
			uint32_t d = *((uint64_t *)ic->arg[0]);
			*((uint32_t *) (page + c)) = d;
#endif
#ifdef LS_Q
			uint64_t d = *((uint64_t *)ic->arg[0]);
			*((uint64_t *) (page + c)) = d;
#endif
#endif
#endif	/*  !LS_LOAD  */
		} else
			LS_GENERIC_N(cpu, ic);
	} else
		LS_GENERIC_N(cpu, ic);
}

