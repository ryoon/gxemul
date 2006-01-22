/*
 *  Copyright (C) 2005-2006  Anders Gavare.  All rights reserved.
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
 *  $Id: cpu_arm_coproc.c,v 1.18 2006-01-22 23:20:35 debug Exp $
 *
 *  ARM coprocessor emulation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "cpu.h"
#include "misc.h"
#include "symbol.h"


/*
 *  arm_coproc_15():
 *
 *  The system control coprocessor.
 */
void arm_coproc_15(struct cpu *cpu, int opcode1, int opcode2, int l_bit,
	int crn, int crm, int rd)
{
	uint32_t old_control;

	/*  Some sanity checks:  */
	if (opcode1 != 0) {
		fatal("arm_coproc_15: opcode1 = %i, should be 0\n", opcode1);
		exit(1);
	}
	if (rd == ARM_PC) {
		fatal("arm_coproc_15: rd = PC\n");
		exit(1);
	}

	switch (crn) {

	case 0:	/*  Main ID register:  */
		if (opcode2 != 0) {
			fatal("[ arm_coproc_15: TODO: cr0, opcode2=%i ]\n",
			    opcode2);
		}
		if (l_bit)
			cpu->cd.arm.r[rd] = cpu->cd.arm.cpu_type.cpu_id;
		else
			fatal("[ arm_coproc_15: attempt to write to cr0? ]\n");
		break;

	case 1:	/*  Control Register:  */
		if (l_bit) {
			/*  Load from the normal/aux control register:  */
			switch (opcode2) {
			case 0:	cpu->cd.arm.r[rd] = cpu->cd.arm.control;
				break;
			case 1:	cpu->cd.arm.r[rd] = cpu->cd.arm.auxctrl;
				break;
			default:fatal("Unimplemented opcode2 = %i\n", opcode2);
				fatal("(opcode1=%i crn=%i crm=%i rd=%i l=%i)\n",
				    opcode1, crn, crm, rd, l_bit);
				exit(1);
			}
			return;
		}

		if (opcode2 == 1) {
			old_control = cpu->cd.arm.auxctrl;
			cpu->cd.arm.auxctrl = cpu->cd.arm.r[rd];
			fatal("[ TODO: Write to ARM auxctrl: 0x%08x ]\n",
			    (int)cpu->cd.arm.auxctrl);
			return;
		} else if (opcode2 != 0) {
			fatal("Unimplemented write, opcode2 = %i\n", opcode2);
			fatal("(opcode1=%i crn=%i crm=%i rd=%i l=%i)\n",
			    opcode1, crn, crm, rd, l_bit);
			exit(1);
		}
			
		/*
		 *  Write to control:  Check each bit individually:
		 */
		old_control = cpu->cd.arm.control;
		cpu->cd.arm.control = cpu->cd.arm.r[rd];
		if ((old_control & ARM_CONTROL_MMU) !=
		    (cpu->cd.arm.control & ARM_CONTROL_MMU)) {
			debug("[ %s the MMU ]\n", cpu->cd.arm.control &
			    ARM_CONTROL_MMU? "enabling" : "disabling");
			cpu->translate_address =
			    cpu->cd.arm.control & ARM_CONTROL_MMU?
			    arm_translate_address_mmu : arm_translate_address;
		}
		if ((old_control & ARM_CONTROL_ALIGN) !=
		    (cpu->cd.arm.control & ARM_CONTROL_ALIGN))
			debug("[ %s alignment checks ]\n", cpu->cd.arm.control &
			    ARM_CONTROL_ALIGN? "enabling" : "disabling");
		if ((old_control & ARM_CONTROL_CACHE) !=
		    (cpu->cd.arm.control & ARM_CONTROL_CACHE))
			debug("[ %s the [data] cache ]\n", cpu->cd.arm.control &
			    ARM_CONTROL_CACHE? "enabling" : "disabling");
		if ((old_control & ARM_CONTROL_WBUFFER) !=
		    (cpu->cd.arm.control & ARM_CONTROL_WBUFFER))
			debug("[ %s the write buffer ]\n", cpu->cd.arm.control &
			    ARM_CONTROL_WBUFFER? "enabling" : "disabling");
		if ((old_control & ARM_CONTROL_BIG) !=
		    (cpu->cd.arm.control & ARM_CONTROL_BIG)) {
			fatal("ERROR: Trying to switch endianness. Not "
			    "supported yet.\n");
			exit(1);
		}
		if ((old_control & ARM_CONTROL_ICACHE) !=
		    (cpu->cd.arm.control & ARM_CONTROL_ICACHE))
			debug("[ %s the icache ]\n", cpu->cd.arm.control &
			    ARM_CONTROL_ICACHE? "enabling" : "disabling");
		/*  TODO: More bits.  */
		break;

	case 2:	/*  Translation Table Base register:  */
		/*  NOTE: 16 KB aligned.  */
		if (l_bit)
			cpu->cd.arm.r[rd] = cpu->cd.arm.ttb & 0xffffc000;
		else {
			cpu->cd.arm.ttb = cpu->cd.arm.r[rd];
			if (cpu->cd.arm.ttb & 0x3fff)
				fatal("[ WARNING! low bits of new TTB non-"
				    "zero? 0x%08x ]\n", cpu->cd.arm.ttb);
			cpu->cd.arm.ttb &= 0xffffc000;
		}
		break;

	case 3:	/*  Domain Access Control Register:  */
		if (l_bit)
			cpu->cd.arm.r[rd] = cpu->cd.arm.dacr;
		else
			cpu->cd.arm.dacr = cpu->cd.arm.r[rd];
		break;

	case 5:	/*  Fault Status Register:  */
		/*  Note: Only the lowest 8 bits are defined.  */
		if (l_bit)
			cpu->cd.arm.r[rd] = cpu->cd.arm.fsr & 0xff;
		else
			cpu->cd.arm.fsr = cpu->cd.arm.r[rd] & 0xff;
		break;

	case 6:	/*  Fault Address Register:  */
		if (l_bit)
			cpu->cd.arm.r[rd] = cpu->cd.arm.far;
		else
			cpu->cd.arm.far = cpu->cd.arm.r[rd];
		break;

	case 7:	/*  Cache functions:  */
		if (l_bit) {
			fatal("[ arm_coproc_15: attempt to read cr7? ]\n");
			return;
		}
		/*  debug("[ arm_coproc_15: cache op: TODO ]\n");  */
		/*  TODO:  */
		break;

	case 8:	/*  TLB functions:  */
		if (l_bit) {
			fatal("[ arm_coproc_15: attempt to read cr8? ]\n");
			return;
		}
		/*  fatal("[ arm_coproc_15: TLB: op2=%i crm=%i rd=0x%08x ]\n",
		    opcode2, crm, cpu->cd.arm.r[rd]);  */
		if (opcode2 == 0)
			cpu->invalidate_translation_caches(cpu, 0,
			    INVALIDATE_ALL);
		else
			cpu->invalidate_translation_caches(cpu,
			    cpu->cd.arm.r[rd], INVALIDATE_VADDR);
		break;

	case 9:	/*  Cache lockdown:  */
		fatal("[ arm_coproc_15: cache lockdown: TODO ]\n");
		/*  TODO  */
		break;

	case 13:/*  Process ID Register:  */
		if (opcode2 != 0)
			fatal("[ arm_coproc_15: PID access, but opcode2 "
			    "= %i? (should be 0) ]\n", opcode2);
		if (crm != 0)
			fatal("[ arm_coproc_15: PID access, but crm "
			    "= %i? (should be 0) ]\n", crm);
		if (l_bit)
			cpu->cd.arm.r[rd] = cpu->cd.arm.pid;
		else
			cpu->cd.arm.pid = cpu->cd.arm.r[rd];
		if (cpu->cd.arm.pid != 0) {
			fatal("ARM TODO: pid!=0. Fast Context Switch"
			    " Extension not implemented yet\n");
			exit(1);
		}
		break;

	case 15:/*  IMPLEMENTATION DEPENDENT!  */
		fatal("[ arm_coproc_15: TODO: IMPLEMENTATION DEPENDENT! ]\n");
		exit(1);
		break;

	default:fatal("arm_coproc_15: unimplemented crn = %i\n", crn);
		fatal("(opcode1=%i opcode2=%i crm=%i rd=%i l=%i)\n",
		    opcode1, opcode2, crm, rd, l_bit);
		exit(1);
	}
}


/*****************************************************************************/


/*
 *  arm_coproc_i80321():
 *
 *  Intel 80321 coprocessor.
 */
void arm_coproc_i80321(struct cpu *cpu, int opcode1, int opcode2, int l_bit,
	int crn, int crm, int rd)
{
	switch (crm) {
#if 0
	case 0:	fatal("[ 80321: crm 0: TODO ]\n");
		break;
	case 1:	fatal("[ 80321: crm 1: TODO ]\n");
		switch (crn) {
		case 0:	/*  tmr0:  */
			break;
		case 2:	/*  tcr0:  */
			break;
		case 4:	/*  trr0:  */
			break;
		case 6:	/*  tisr:  */
			break;
		default:fatal("arm_coproc_i80321: unimplemented crn = %i\n",
			    crn);
			fatal("(opcode1=%i opcode2=%i crm=%i rd=%i l=%i)\n",
			    opcode1, opcode2, crm, rd, l_bit);
			exit(1);
		}
		break;
#endif
	default:fatal("arm_coproc_i80321: unimplemented opcode1=%i opcode2=%i"
		    " crn=%i crm=%i rd=%i l=%i)\n", opcode1, opcode2,
		    crn, crm, rd, l_bit);
		exit(1);
	}
}


/*
 *  arm_coproc_i80321_14():
 *
 *  Intel 80321 coprocessor 14.
 */
void arm_coproc_i80321_14(struct cpu *cpu, int opcode1, int opcode2, int l_bit,
	int crn, int crm, int rd)
{
	switch (crm) {
#if 0
	case 0:	fatal("[ 80321_14: crm 0: TODO ]\n");
		break;
#endif
	default:fatal("arm_coproc_i80321_14: unimplemented opcode1=%i opcode2="
		    "%i crn=%i crm=%i rd=%i l=%i)\n", opcode1, opcode2,
		    crn, crm, rd, l_bit);
		exit(1);
	}
}

