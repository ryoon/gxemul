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
 *  $Id: cpu_ppc.c,v 1.6 2005-02-01 14:20:38 debug Exp $
 *
 *  PowerPC/POWER CPU emulation.
 *
 *  TODO: This is just a dummy so far.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "cpu.h"
#include "cpu_ppc.h"
#include "machine.h"
#include "misc.h"
#include "symbol.h"


/*
 *  ppc_cpu_new():
 *
 *  Create a new PPC cpu object.
 */
struct cpu *ppc_cpu_new(struct memory *mem, struct machine *machine,
	int cpu_id, char *cpu_type_name)
{
	struct cpu *cpu;
	int any_cache = 0;
	int i, found;
	struct ppc_cpu_type_def cpu_type_defs[] = PPC_CPU_TYPE_DEFS;

	/*  Scan the cpu_type_defs list for this cpu type:  */
	i = 0;
	found = -1;
	while (i >= 0 && cpu_type_defs[i].name != NULL) {
		if (strcasecmp(cpu_type_defs[i].name, cpu_type_name) == 0) {
			found = i;
			break;
		}
		i++;
	}
	if (found == -1)
		return NULL;

	cpu = malloc(sizeof(struct cpu));
	if (cpu == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}

	memset(cpu, 0, sizeof(struct cpu));
	cpu->cd.ppc.cpu_type    = cpu_type_defs[found];
	cpu->name               = cpu->cd.ppc.cpu_type.name;
	cpu->mem                = mem;
	cpu->machine            = machine;
	cpu->cpu_id             = cpu_id;
	cpu->byte_order         = EMUL_BIG_ENDIAN;
	cpu->bootstrap_cpu_flag = 0;
	cpu->running            = 0;

	/*  Only show name and caches etc for CPU nr 0 (in SMP machines):  */
	if (cpu_id == 0) {
		debug("%s", cpu->cd.ppc.cpu_type.name);

		if (cpu->cd.ppc.cpu_type.icache_shift != 0)
			any_cache = 1;
		if (cpu->cd.ppc.cpu_type.dcache_shift != 0)
			any_cache = 1;
		if (cpu->cd.ppc.cpu_type.l2cache_shift != 0)
			any_cache = 1;

		if (any_cache) {
			debug(" (I+D = %i+%i KB",
			    (int)(1 << (cpu->cd.ppc.cpu_type.icache_shift-10)),
			    (int)(1 << (cpu->cd.ppc.cpu_type.dcache_shift-10)));
			if (cpu->cd.ppc.cpu_type.l2cache_shift != 0) {
				debug(", L2 = %i KB",
				    (int)(1 << (cpu->cd.ppc.cpu_type.
				    l2cache_shift-10)));
			}
			debug(")");
		}
	}

	return cpu;
}


/*
 *  ppc_cpu_list_available_types():
 *
 *  Print a list of available PPC CPU types.
 */
void ppc_cpu_list_available_types(void)
{
	int i, j;
	struct ppc_cpu_type_def tdefs[] = PPC_CPU_TYPE_DEFS;

	i = 0;
	while (tdefs[i].name != NULL) {
		debug("%s", tdefs[i].name);
		for (j=10 - strlen(tdefs[i].name); j>0; j--)
			debug(" ");
		i++;
		if ((i % 6) == 0 || tdefs[i].name == NULL)
			debug("\n");
	}
}


/*
 *  ppc_cpu_dumpinfo():
 */
void ppc_cpu_dumpinfo(struct cpu *cpu)
{
	struct ppc_cpu_type_def *ct = &cpu->cd.ppc.cpu_type;

	debug(" (");

	debug("I+D = %i+%i KB",
	    (1 << ct->icache_shift) / 1024,
	    (1 << ct->dcache_shift) / 1024);

	if (ct->l2cache_shift) {
		int kb = (1 << ct->l2cache_shift) / 1024;
		debug(", L2 = %i %cB",
		    kb >= 1024? kb / 1024 : kb,
		    kb >= 1024? 'M' : 'K');
	}

	debug(")\n");
}


/*
 *  ppc_cpu_register_dump():
 *
 *  Dump cpu registers in a relatively readable format.
 *
 *  gprs: set to non-zero to dump GPRs and hi/lo/pc
 *  coprocs: set bit 0..3 to dump registers in coproc 0..3.
 */
void ppc_cpu_register_dump(struct cpu *cpu, int gprs, int coprocs)
{
	char *symbol;
	uint64_t offset;
	int i, x = cpu->cpu_id;

	if (gprs) {
		/*  Special registers (pc, ...) first:  */
		symbol = get_symbol_name(&cpu->machine->symbol_context,
		    cpu->cd.ppc.pc, &offset);

		debug("cpu%i: pc  = 0x%016llx", x, (long long)cpu->cd.ppc.pc);
		debug("  <%s>\n", symbol != NULL? symbol : " no symbol ");

		debug("cpu%i: lr  = 0x%016llx  cr = 0x%08x\n", x,
		    (long long)cpu->cd.ppc.lr, (int)cpu->cd.ppc.cr);

		debug("cpu%i: ctr = 0x%016llx\n", x,
		    (long long)cpu->cd.ppc.ctr);

		for (i=0; i<PPC_NGPRS; i++) {
			if ((i % 2) == 0)
				debug("cpu%i:", x);
			debug(" r%02i = 0x%016llx ", i,
			    (long long)cpu->cd.ppc.gpr[i]);
			if ((i % 2) == 1)
				debug("\n");
		}
	}

	if (coprocs) {
		debug("cpu%i: xer = 0x%016llx  fpscr = 0x%08x\n", x,
		    (long long)cpu->cd.ppc.xer, (int)cpu->cd.ppc.fpscr);

		/*  TODO: show floating-point values :-)  */

		for (i=0; i<PPC_NFPRS; i++) {
			if ((i % 2) == 0)
				debug("cpu%i:", x);
			debug(" f%02i = 0x%016llx ", i,
			    (long long)cpu->cd.ppc.fpr[i]);
			if ((i % 2) == 1)
				debug("\n");
		}
	}
}


/*
 *  ppc_cpu_register_match():
 */
void ppc_cpu_register_match(struct machine *m, char *name,
	int writeflag, uint64_t *valuep, int *match_register)
{
	int cpunr = 0;

	/*  CPU number:  */

	/*  TODO  */

	/*  Register name:  */
	if (strcasecmp(name, "pc") == 0) {
		if (writeflag) {
			m->cpus[cpunr]->cd.ppc.pc = *valuep;
		} else
			*valuep = m->cpus[cpunr]->cd.ppc.pc;
		*match_register = 1;
	} else if (strcasecmp(name, "lr") == 0) {
		if (writeflag)
			m->cpus[cpunr]->cd.ppc.lr = *valuep;
		else
			*valuep = m->cpus[cpunr]->cd.ppc.lr;
		*match_register = 1;
	} else if (strcasecmp(name, "cr") == 0) {
		if (writeflag)
			m->cpus[cpunr]->cd.ppc.cr = *valuep;
		else
			*valuep = m->cpus[cpunr]->cd.ppc.cr;
		*match_register = 1;
	} else if (strcasecmp(name, "ctr") == 0) {
		if (writeflag)
			m->cpus[cpunr]->cd.ppc.ctr = *valuep;
		else
			*valuep = m->cpus[cpunr]->cd.ppc.ctr;
		*match_register = 1;
	} else if (name[0] == 'r' && isdigit((int)name[1])) {
		int nr = atoi(name + 1);
		if (nr >= 0 && nr < PPC_NGPRS) {
			if (writeflag) {
				m->cpus[cpunr]->cd.ppc.gpr[nr] = *valuep;
			} else
				*valuep = m->cpus[cpunr]->cd.ppc.gpr[nr];
			*match_register = 1;
		}
	} else if (strcasecmp(name, "xer") == 0) {
		if (writeflag)
			m->cpus[cpunr]->cd.ppc.xer = *valuep;
		else
			*valuep = m->cpus[cpunr]->cd.ppc.xer;
		*match_register = 1;
	} else if (strcasecmp(name, "fpscr") == 0) {
		if (writeflag)
			m->cpus[cpunr]->cd.ppc.fpscr = *valuep;
		else
			*valuep = m->cpus[cpunr]->cd.ppc.fpscr;
		*match_register = 1;
	} else if (name[0] == 'f' && isdigit((int)name[1])) {
		int nr = atoi(name + 1);
		if (nr >= 0 && nr < PPC_NFPRS) {
			if (writeflag) {
				m->cpus[cpunr]->cd.ppc.fpr[nr] = *valuep;
			} else
				*valuep = m->cpus[cpunr]->cd.ppc.fpr[nr];
			*match_register = 1;
		}
	}
}

