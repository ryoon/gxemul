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
 *  $Id: generate_arm_multi.c,v 1.1 2005-10-21 15:19:25 debug Exp $
 *
 *  Generation of commonly used ARM load/store multiple instructions.
 *  The main idea is to first check whether a load/store would be possible
 *  without going outside a page, and if so, use the host_load or _store
 *  arrays for quick access to emulated RAM. Otherwise, fall back to using
 *  the generic bdt_load() or bdt_store().
 */

#include <stdio.h>
#include <stdlib.h>
#include "misc.h"


void generate_opcode(uint32_t opcode)
{
	int p, u, s, w, load, r, n_regs, i, x;

	if ((opcode & 0x0e000000) != 0x08000000) {
		fprintf(stderr, "opcode 0x%08x is not an ldm/stm\n", opcode);
		exit(1);
	}

	r = (opcode >> 16) & 15;
	p = opcode & 0x01000000? 1 : 0;
	u = opcode & 0x00800000? 1 : 0;
	s = opcode & 0x00400000? 1 : 0;
	w = opcode & 0x00200000? 1 : 0;
	load = opcode & 0x00100000? 1 : 0;
	n_regs = 0;
	for (i=0; i<16; i++)
		if (opcode & (1 << i))
			n_regs ++;

	if (n_regs == 0) {
		fprintf(stderr, "opcode 0x%08x has no registers set\n", opcode);
		exit(1);
	}

	if (s) {
		fprintf(stderr, "opcode 0x%08x has s-bit set\n", opcode);
		exit(1);
	}

	if (r == 15) {
		fprintf(stderr, "opcode 0x%08x has r=15\n", opcode);
		exit(1);
	}

	printf("\nX(multi_0x%08x) {\n", opcode);

	printf("\tuint32_t addr = cpu->cd.arm.r[%i];\n", r);
	if (w)
		printf("\tuint32_t orig;\n");

	if (!load && opcode & 0x8000) {
		/*  Sync the PC:  */
		printf("\tuint32_t low_pc = ((size_t)ic - (size_t)\n\t"
		    "    cpu->cd.arm.cur_ic_page) / sizeof(struct "
		    "arm_instr_call);\n"
		    "\tcpu->cd.arm.r[ARM_PC] &= ~((ARM_IC_ENTRIES_PER_PAGE-1)"
		    "\n\t    << ARM_INSTR_ALIGNMENT_SHIFT);\n"
		    "\tcpu->cd.arm.r[ARM_PC] += (low_pc << "
		    "ARM_INSTR_ALIGNMENT_SHIFT);\n"
		    "\tcpu->pc = cpu->cd.arm.r[ARM_PC];\n");
	}

	printf("\tunsigned char *page;\n");

	if (w)
		printf("\torig = addr;\n");

	if (p)
		printf("\taddr %s 4;\n", u? "+=" : "-=");

	printf("\tpage = cpu->cd.arm.host_%s[addr >> 12];\n",
	    load? "load" : "store");

	printf("\taddr &= 0xffc;\n");

	printf("\tif (");
	switch (p*2 + u) {
	case 0:	/*  post-decrement  */
		if (n_regs > 1)
			printf("addr >= 0x%x && ", 4*(n_regs-1));
		break;
	case 1:	/*  post-increment  */
		if (n_regs > 1)
			printf("addr <= 0x%x && ", 0x1000 - 4*n_regs);
		break;
	case 2:	/*  pre-decrement  */
		printf("addr >= 0x%x && ", 4*n_regs);
		break;
	case 3:	/*  pre-increment  */
		printf("addr <= 0x%x && ", 0xffc - 4*n_regs);
		break;
	}
	printf("page != NULL) {\n");

	printf("\t\tuint32_t *p = (uint32_t *) (page + addr);\n");
	x = 0;
	for (i=(u?0:15); i>=0 && i<=15; i+=(u?1:-1)) {
		if (!(opcode & (1 << i)))
			continue;

		if (load && w && i == r) {
			/*  Skip the load if we're using writeback.  */
		} else if (load)
			printf("\t\tcpu->cd.arm.r[%i] = p[%i];\n", i, x);
		else {
			printf("\t\tp[%i] = cpu->cd.arm.r[%i]", x, i);
			if (i == 15)
				printf(" + 12");
			printf(";\n");
		}

		if (u)
			x ++;
		else
			x --;
	}

	if (w)
		printf("\t\tcpu->cd.arm.r[%i] = orig %s %i;\n",
		    r, u? "+" : "-", 4*n_regs);

	if (load && opcode & 0x8000) {
		printf("\t\tcpu->pc = cpu->cd.arm.r[15];\n"
		    "\t\tarm_pc_to_pointers(cpu);\n");
	}

	printf("\t} else {\n");
	printf("\t\tinstr(bdt_%s)(cpu, ic);\n\t}\n", load? "load" : "store");

	printf("}\nY(multi_0x%08x)\n", opcode);
}


int main(int argc, char *argv[])
{
	int i;

	if (argc < 2) {
		fprintf(stderr, "usage: %s opcode [..]\n", argv[0]);
		exit(1);
	}

	printf("\n/*  AUTOMATICALLY GENERATED! Do not edit.  */\n\n");

	for (i=1; i<argc; i++)
		generate_opcode(strtol(argv[i], NULL, 0));

	printf("\nuint32_t multi_opcode[%i] = {\n", argc);
	for (i=1; i<argc; i++)
		printf("\t0x%08lx,\n", strtol(argv[i], NULL, 0));
	printf("0 };\n");

	printf("\nvoid (*multi_opcode_f[%i])(struct cpu *,"
	    " struct arm_instr_call *) = {\n", (argc-1)*16);
	for (i=1; i<argc; i++) {
		int n = strtol(argv[i], NULL, 0);
		printf("arm_instr_multi_0x%08x__eq,", n);
		printf("arm_instr_multi_0x%08x__ne,", n);
		printf("arm_instr_multi_0x%08x__cs,", n);
		printf("arm_instr_multi_0x%08x__cc,", n);
		printf("arm_instr_multi_0x%08x__mi,", n);
		printf("arm_instr_multi_0x%08x__pl,", n);
		printf("arm_instr_multi_0x%08x__vs,", n);
		printf("arm_instr_multi_0x%08x__vc,", n);
		printf("arm_instr_multi_0x%08x__hi,", n);
		printf("arm_instr_multi_0x%08x__ls,", n);
		printf("arm_instr_multi_0x%08x__ge,", n);
		printf("arm_instr_multi_0x%08x__lt,", n);
		printf("arm_instr_multi_0x%08x__gt,", n);
		printf("arm_instr_multi_0x%08x__le,", n);
		printf("arm_instr_multi_0x%08x,", n);
		printf("arm_instr_nop%s", i<argc-1? "," : "");
	}
	printf("};\n");

	return 0;
}

