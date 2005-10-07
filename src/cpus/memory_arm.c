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
 *  $Id: memory_arm.c,v 1.23 2005-10-07 15:19:48 debug Exp $
 *
 *
 *  TODO/NOTE: There are probably two solutions to the subpage access
 *  permission problem:
 *
 *  a) the obvious (almost trivial) solution is to decrease the native page
 *     size from 4 KB to 1 KB. That would ruin the rest of the translation
 *     system though. (It would be infeasible to hold the entire address
 *     space in 1-level tables.)
 *
 *  b) to return something else than just 0, 1, or 2 from arm_memory_rw().
 *     Perhaps |4, which would indicate that the vaddr => paddr conversion
 *     was done, but that it should not be entered into the cache. This could
 *     also be used in combination with the B and C bits (which are currently
 *     ignored).
 *
 *  b would probably be the best solution.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "memory.h"
#include "misc.h"

#include "armreg.h"

extern int quiet_mode;


/*
 *  arm_check_access():
 *
 *  Helper function.  Returns 0 for no access, 1 for read-only, and 2 for
 *  read/write.
 */
static int arm_check_access(struct cpu *cpu, int ap, int dav, int user)
{
	int s, r;

	switch (dav) {
	case 0:	/*  No access at all.  */
		return 0;
	case 1:	/*  Normal access check.  */
		break;
	case 2:	fatal("arm_check_access(): 1 shouldn't be used\n");
		exit(1);
	case 3:	/*  Anything is allowed.  */
		return 2;
	}

	switch (ap) {
	case 0:	s = (cpu->cd.arm.control & ARM_CONTROL_S)? 1 : 0;
		r = (cpu->cd.arm.control & ARM_CONTROL_R)? 2 : 0;
		switch (s + r) {
		case 0:	return 0;
		case 1:	return user? 0 : 1;
		case 2:	return 1;
		}
		fatal("arm_check_access: UNPREDICTABLE s+r value!\n");
		return 0;
	case 1:	return user? 0 : 2;
	case 2:	return user? 1 : 2;
	}

	/*  "case 3":  */
	return 2;
}


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
int arm_translate_address(struct cpu *cpu, uint64_t vaddr64,
	uint64_t *return_addr, int flags)
{
	unsigned char descr[4];
	uint32_t addr, d, d2 = (uint32_t)(int32_t)-1, ptba, vaddr = vaddr64;
	int d2_in_use = 0, d_in_use = 1;
	int instr = flags & FLAG_INSTR;
	int writeflag = (flags & FLAG_WRITEFLAG)? 1 : 0;
	int useraccess = flags & MEMORY_USER_ACCESS;
	int no_exceptions = flags & FLAG_NOEXCEPTIONS;
	int user = (cpu->cd.arm.cpsr & ARM_FLAG_MODE) == ARM_MODE_USR32;
	int domain, dav, ap0,ap1,ap2,ap3, ap = 0, access = 0;
	int fs = 2;		/*  fault status (2 = terminal exception)  */

	if (!(cpu->cd.arm.control & ARM_CONTROL_MMU)) {
		*return_addr = vaddr;
		return 2;
	}

	if (useraccess)
		user = 1;

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

	/*  Get the domain from the descriptor, and the Domain Access Value:  */
	domain = (d >> 5) & 15;
	dav = (cpu->cd.arm.dacr >> (domain * 2)) & 3;

	switch (d & 3) {

	case 0:	d_in_use = 0;
		domain = 0;
		fs = FAULT_TRANS_S;
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
		case 0:	fs = FAULT_TRANS_P;
			goto exception_return;
		case 1:	/*  16KB page:  */
			ap = (d2 >> 4) & 255;
			switch (vaddr & 0x0000c000) {
			case 0x4000:	ap >>= 2; break;
			case 0x8000:	ap >>= 4; break;
			case 0xc000:	ap >>= 6; break;
			}
			ap &= 3;
			*return_addr = (d2 & 0xffff0000) | (vaddr & 0x0000ffff);
			break;
		case 2:	/*  4KB page:  */
			ap3 = (d2 >> 10) & 3;
			ap2 = (d2 >>  8) & 3;
			ap1 = (d2 >>  6) & 3;
			ap0 = (d2 >>  4) & 3;
			switch (vaddr & 0x00000c00) {
			case 0x000: ap = ap0; break;
			case 0x400: ap = ap1; break;
			case 0x800: ap = ap2; break;
			default:    ap = ap3;
			}
#if 0
			if ((ap0 != ap1 || ap0 != ap2 || ap0 != ap3) &&
			    !no_exceptions)
				fatal("WARNING: vaddr = 0x%08x, small page, but"
				    " different access permissions for the sub"
				    "pages! This is not really implemented "
				    "yet.\n", (int)vaddr);
#endif
			*return_addr = (d2 & 0xfffff000) | (vaddr & 0x00000fff);
			break;
		case 3:	/*  1KB page:  */
			fatal("WARNING: 1 KB page! Not implemented yet.\n");
			ap = (d2 >> 4) & 3;
			*return_addr = (d2 & 0xfffffc00) | (vaddr & 0x000003ff);
			break;
		}
		if (dav == 0) {
			fs = FAULT_DOMAIN_P;
			goto exception_return;
		}
		access = arm_check_access(cpu, ap, dav, user);
		if (access > writeflag)
			return access;
		fs = FAULT_PERM_P;
		goto exception_return;

	case 2:	/*  Section descriptor:  */
		*return_addr = (d & 0xfff00000) | (vaddr & 0x000fffff);
		if (dav == 0) {
			fs = FAULT_DOMAIN_S;
			goto exception_return;
		}
		ap = (d >> 10) & 3;
		access = arm_check_access(cpu, ap, dav, user);
		if (access > writeflag)
			return access;
		fs = FAULT_PERM_S;
		goto exception_return;

	default:fatal("TODO: descriptor for vaddr 0x%08x: 0x%08x ("
		    "unimplemented type %i)\n", vaddr, d, d&3);
		exit(1);
	}

exception_return:
	if (no_exceptions)
		return 0;

	if (!quiet_mode) {
		fatal("{ arm memory fault: vaddr=0x%08x domain=%i dav=%i ap=%i "
		    "access=%i user=%i", (int)vaddr, domain, dav, ap,
		    access, user);
		if (d_in_use)
			fatal(" d=0x%08x", d);
		if (d2_in_use)
			fatal(" d2=0x%08x", d2);
		fatal(" }\n");
	}

	if (instr)
		arm_exception(cpu, ARM_EXCEPTION_PREF_ABT);
	else {
		cpu->cd.arm.far = vaddr;
		cpu->cd.arm.fsr = (domain << 4) | fs;
		arm_exception(cpu, ARM_EXCEPTION_DATA_ABT);
	}

	return 0;
}

