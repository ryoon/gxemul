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
 *  $Id: cpu_arm_instr_loadstore.c,v 1.1 2005-06-29 23:25:37 debug Exp $
 */

#ifdef A__REG
void A__NAME__general(struct cpu *cpu, struct arm_instr_call *ic) { }
void A__NAME(struct cpu *cpu, struct arm_instr_call *ic)
{fatal("TODO: blah...\n");}


#else	/*  !A__REG  */


void A__NAME__general(struct cpu *cpu, struct arm_instr_call *ic)
{
#ifdef A__B
	unsigned char data[1];
#else
	unsigned char data[4];
#endif
	uint32_t addr;

	addr = *((uint32_t *)ic->arg[0])
#ifdef A__P
#ifdef A__U
	    +
#else
	    -
#endif
	    ic->arg[1];
#endif
	    ;

#ifdef A__L
	if (!cpu->memory_rw(cpu, cpu->mem, addr, data, sizeof(data),
	    MEM_READ, CACHE_DATA)) {
		fatal("load failed: TODO\n");
		exit(1);
	}
#ifdef A__B
	*((uint32_t *)ic->arg[2]) = data[0];
#else
	*((uint32_t *)ic->arg[2]) = data[0] + (data[1] << 8) +
	    (data[2] << 16) + (data[3] << 24);
#endif
#else
#ifdef A__B
	data[0] = *((uint32_t *)ic->arg[2]);
#else
	data[0] = (*((uint32_t *)ic->arg[2]));
	data[1] = (*((uint32_t *)ic->arg[2])) >> 8;
	data[2] = (*((uint32_t *)ic->arg[2])) >> 16;
	data[3] = (*((uint32_t *)ic->arg[2])) >> 24;
#endif
	if (!cpu->memory_rw(cpu, cpu->mem, addr, data, sizeof(data),
	    MEM_WRITE, CACHE_DATA)) {
		fatal("store failed: TODO\n");
		exit(1);
	}
#endif

#ifdef A__P
#ifdef A__W
	*((uint32_t *)ic->arg[0]) = addr;
#endif
#else	/*  post-index writeback  */
	*((uint32_t *)ic->arg[0]) = addr
#ifdef A__U
	    +
#else
	    -
#endif
	    ic->arg[1];
#endif
}

void A__NAME(struct cpu *cpu, struct arm_instr_call *ic)
{
	uint32_t addr = *((uint32_t *)ic->arg[0])
#ifdef A__P
#ifdef A__U
	    +
#else
	    -
#endif
	    ic->arg[1];
#endif
	    ;
	struct vph_page *vph_p = cpu->cd.arm.vph_table0[addr >> 22];
	unsigned char *page = vph_p->
#ifdef A__L
	    host_load
#else
	    host_store
#endif
	    [(addr >> 12) & 1023];

	if (page != NULL) {
#ifdef A__P
#ifdef A__W
		*((uint32_t *)ic->arg[0]) = addr;
#endif
#else	/*  post-index writeback  */
		*((uint32_t *)ic->arg[0]) = addr
#ifdef A__U
		    +
#else
		    -
#endif
		    ic->arg[1];
#endif

#ifdef A__L
#ifdef A__B
		*((uint32_t *)ic->arg[2]) = page[addr & 4095];
#else
		addr &= 4095;
		*((uint32_t *)ic->arg[2]) = page[addr] +
		    (page[addr + 1] << 8) +
		    (page[addr + 2] << 16) +
		    (page[addr + 3] << 24);
#endif
#else
#ifdef A__B
		page[addr & 4095] = *((uint32_t *)ic->arg[2]);
#else
		addr &= 4095;
		page[addr] = *((uint32_t *)ic->arg[2]);
		page[addr+1] = (*((uint32_t *)ic->arg[2])) >> 8;
		page[addr+2] = (*((uint32_t *)ic->arg[2])) >> 16;
		page[addr+3] = (*((uint32_t *)ic->arg[2])) >> 24;
#endif
#endif
	} else
	        A__NAME__general(cpu, ic);
}
#endif

void A__NAME__eq(struct cpu *cpu, struct arm_instr_call *ic)
{ if (cpu->cd.arm.flags & ARM_FLAG_Z) A__NAME(cpu, ic); }
void A__NAME__ne(struct cpu *cpu, struct arm_instr_call *ic)
{ if (!(cpu->cd.arm.flags & ARM_FLAG_Z)) A__NAME(cpu, ic); }
void A__NAME__cs(struct cpu *cpu, struct arm_instr_call *ic)
{ if (cpu->cd.arm.flags & ARM_FLAG_C) A__NAME(cpu, ic); }
void A__NAME__cc(struct cpu *cpu, struct arm_instr_call *ic)
{ if (!(cpu->cd.arm.flags & ARM_FLAG_C)) A__NAME(cpu, ic); }
void A__NAME__mi(struct cpu *cpu, struct arm_instr_call *ic)
{ if (cpu->cd.arm.flags & ARM_FLAG_N) A__NAME(cpu, ic); }
void A__NAME__pl(struct cpu *cpu, struct arm_instr_call *ic)
{ if (!(cpu->cd.arm.flags & ARM_FLAG_N)) A__NAME(cpu, ic); }
void A__NAME__vs(struct cpu *cpu, struct arm_instr_call *ic)
{ if (cpu->cd.arm.flags & ARM_FLAG_V) A__NAME(cpu, ic); }
void A__NAME__vc(struct cpu *cpu, struct arm_instr_call *ic)
{ if (!(cpu->cd.arm.flags & ARM_FLAG_V)) A__NAME(cpu, ic); }

void A__NAME__hi(struct cpu *cpu, struct arm_instr_call *ic)
{ if (cpu->cd.arm.flags & ARM_FLAG_C &&
!(cpu->cd.arm.flags & ARM_FLAG_Z)) A__NAME(cpu, ic); }
void A__NAME__ls(struct cpu *cpu, struct arm_instr_call *ic)
{ if (cpu->cd.arm.flags & ARM_FLAG_Z &&
!(cpu->cd.arm.flags & ARM_FLAG_C)) A__NAME(cpu, ic); }
void A__NAME__ge(struct cpu *cpu, struct arm_instr_call *ic)
{ if (((cpu->cd.arm.flags & ARM_FLAG_N)?1:0) ==
((cpu->cd.arm.flags & ARM_FLAG_V)?1:0)) A__NAME(cpu, ic); }
void A__NAME__lt(struct cpu *cpu, struct arm_instr_call *ic)
{ if (((cpu->cd.arm.flags & ARM_FLAG_N)?1:0) !=
((cpu->cd.arm.flags & ARM_FLAG_V)?1:0)) A__NAME(cpu, ic); }
void A__NAME__gt(struct cpu *cpu, struct arm_instr_call *ic)
{ if (((cpu->cd.arm.flags & ARM_FLAG_N)?1:0) ==
((cpu->cd.arm.flags & ARM_FLAG_V)?1:0) &&
!(cpu->cd.arm.flags & ARM_FLAG_Z)) A__NAME(cpu, ic); }
void A__NAME__le(struct cpu *cpu, struct arm_instr_call *ic)
{ if (((cpu->cd.arm.flags & ARM_FLAG_N)?1:0) !=
((cpu->cd.arm.flags & ARM_FLAG_V)?1:0) ||
(cpu->cd.arm.flags & ARM_FLAG_Z)) A__NAME(cpu, ic); }

