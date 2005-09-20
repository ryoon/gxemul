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
 *  $Id: memory_arm.c,v 1.10 2005-09-20 21:05:22 debug Exp $
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "memory.h"
#include "misc.h"

#include "armreg.h"


/*
 *  arm_translate_address():
 *
 *  Don't call this function is userland_emul is non-NULL, or cpu is NULL.
 *
 *  Return values:
 *	0  Failure
 *	1  Success, the page is readable only
 *	2  Success, the page is read/write
 */
int arm_translate_address(struct cpu *cpu, uint64_t vaddr,
	uint64_t *return_addr, int flags)
{
	unsigned char descr[4];
	uint32_t addr, d, d2 = (uint32_t)(int32_t)-1, ptba;
	int instr = flags & FLAG_INSTR, d2_in_use = 0, d_in_use = 1;
	int no_exceptions = flags & FLAG_NOEXCEPTIONS;

	if (!(cpu->cd.arm.control & ARM_CONTROL_MMU)) {
		*return_addr = vaddr & 0xffffffff;
		return 2;
	}

	addr = cpu->cd.arm.ttb + ((vaddr & 0xfff00000ULL) >> 18);
	if (!cpu->memory_rw(cpu, cpu->mem, addr, &descr[0],
	    sizeof(descr), MEM_READ, PHYSICAL | NO_EXCEPTIONS)) {
		fatal("arm_translate_address(): huh?\n");
		exit(1);
	}
	if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
		d = descr[0] + (descr[1] << 8) + (descr[2] << 16)
		    + (descr[3] << 24);
	else
		d = descr[3] + (descr[2] << 8) + (descr[1] << 16)
		    + (descr[0] << 24);

	/*  fatal("vaddr=0x%08x ttb=0x%08x addr=0x%08x d=0x%08x\n",
	    vaddr, cpu->cd.arm.ttb, addr, d);  */

	switch (d & 3) {

	case 0:	d_in_use = 0;
		goto exception_return;

	case 1:	/*  Course Pagetable:  */
		ptba = d & 0xfffffc00;
		addr = ptba + ((vaddr & 0x000ff000) >> 10);
		if (!cpu->memory_rw(cpu, cpu->mem, addr, &descr[0],
		    sizeof(descr), MEM_READ, PHYSICAL | NO_EXCEPTIONS)) {
			fatal("arm_translate_address(): huh 2?\n");
			exit(1);
		}
		if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
			d2 = descr[0] + (descr[1] << 8) + (descr[2] << 16)
			    + (descr[3] << 24);
		else
			d2 = descr[3] + (descr[2] << 8) + (descr[1] << 16)
			    + (descr[0] << 24);
		d2_in_use = 1;

		switch (d2 & 3) {
		case 0:	goto exception_return;
		case 1:	/*  16KB page:  */
			*return_addr = (d2 & 0xffff0000) | (vaddr & 0x0000ffff);
			break;
		case 2:	/*  4KB page:  */
			*return_addr = (d2 & 0xfffff000) | (vaddr & 0x00000fff);
			break;
		case 3:	/*  1KB page:  */
			*return_addr = (d2 & 0xfffffc00) | (vaddr & 0x000003ff);
			break;
		}
		/*  TODO: access rights etc.  */
		return 2;

	case 2:	/*  Section descriptor:  */
		*return_addr = (d & 0xfff00000) | (vaddr & 0x000fffff);
		/*  TODO: access rights etc.  */
		return 2;

	default:fatal("TODO: descriptor for vaddr 0x%08x: 0x%08x ("
		    "unimplemented type %i)\n", vaddr, d, d&3);
		exit(1);
	}

exception_return:
	if (no_exceptions)
		return 0;

	fatal("TODO: arm memory fault: vaddr=%08x ", vaddr);
	if (d_in_use)
		fatal(" d=0x%08x", d);
	if (d2_in_use)
		fatal(" d2=0x%08x", d2);
	fatal("\n");

	cpu->cd.arm.far = vaddr;
	cpu->cd.arm.fsr = 0;

	if ((cpu->cd.arm.cpsr & ARM_FLAG_MODE) == ARM_MODE_USR32)
		cpu->cd.arm.fsr |= FAULT_USER;

cpu->cd.arm.fsr |= FAULT_TRANS_P;
#if 0
	if (!d_in_use)
		cpu->cd.arm.fsr |= FAULT_BUSTRNL1;
	else if (!d2_in_use)
		cpu->cd.arm.fsr |= FAULT_BUSTRNL2;
	else {
		/*
		 *  TODO: More fsr stuff!
		 *
		 *  Alignment (FAULT_ALIGN_0, 1),
		 *  translation (FAULT_TRANS_S, P),
		 *  domain (FAULT_DOMAIN_S, P),
		 *  and permision (FAULT_PERM_S, P).
		 */
	}
#endif

	arm_exception(cpu, ARM_EXCEPTION_DATA_ABT);
	return 0;
}

