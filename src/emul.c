/*
 *  Copyright (C) 2003 by Anders Gavare.  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright  
 *     notice, this list of conditions and the following disclaimer in the 
 *     documentation and/or other materials provided with the distribution.
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
 *  $Id: emul.c,v 1.2 2003-11-06 13:56:08 debug Exp $
 *
 *  Emulation startup.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "misc.h"


extern int optind;
extern char *optarg;
int extra_argc;
char **extra_argv;


extern char emul_cpu_name[50];
extern int emulation_type;
extern int machine;
extern int physical_ram_in_mb;
extern int random_mem_contents;
extern int bootstrap_cpu;
extern int use_random_bootstrap_cpu;
extern int ncpus;
extern struct cpu **cpus;
extern int use_x11;
extern int x11_scaledown;
extern int quiet_mode;


void fix_console(void)
{
	console_deinit();
}


/*
 *  emul():
 *
 *	o)  Initialize the hardware (RAM, devices, CPUs, ...) to emulate.
 *
 *	o)  Load ROM code and/or other programs into emulated memory.
 *
 *	o)  Start running instructions on the bootstrap cpu.
 */
void emul(void)
{
	struct memory *mem;
	int i;
	uint64_t addr;

	srandom(time(NULL));

	atexit(fix_console);

	/*  Create the system's memory:  */
	debug("adding memory: %i MB\n", physical_ram_in_mb);
	mem = memory_new(DEFAULT_BITS_PER_PAGETABLE, DEFAULT_BITS_PER_MEMBLOCK, physical_ram_in_mb * 1048576, DEFAULT_MAX_BITS);

	/*  Create CPUs:  */
	cpus = malloc(sizeof(struct cpu *) * ncpus);
	memset(cpus, 0, sizeof(struct cpu *) * ncpus);

	debug("adding cpu0");
	if (ncpus > 1)
		debug(" .. cpu%i", ncpus-1);
	debug(": %s\n", emul_cpu_name);
	for (i=0; i<ncpus; i++)
		cpus[i] = cpu_new(mem, i, emul_cpu_name);

	if (use_random_bootstrap_cpu)
		bootstrap_cpu = random() % ncpus;
	else
		bootstrap_cpu = 0;

	/*  Fill memory with random bytes:  */
	if (random_mem_contents) {
		for (i=0; i<physical_ram_in_mb*1048576; i+=256) {
			unsigned char data[256];
			int j;
			for (j=0; j<sizeof(data); j++)
				data[j] = random() & 255;
			addr = 0x80000000 + i;
			memory_rw(cpus[0], mem, addr, data, sizeof(data), MEM_WRITE, CACHE_NONE | NO_EXCEPTIONS);
		}
	}

	/*  Load files (ROM code, boot code, ...) into memory:  */
	if (extra_argc > 0)
		debug("loading files into emulation memory:\n");
	while (extra_argc > 0) {
		file_load(mem, extra_argv[0], cpus[bootstrap_cpu]);
		extra_argc --;  extra_argv ++;
	}

	if ((cpus[bootstrap_cpu]->pc >> 32) == 0 &&
	    (cpus[bootstrap_cpu]->pc & 0x80000000))
		cpus[bootstrap_cpu]->pc |= 0xffffffff00000000;

	if ((cpus[bootstrap_cpu]->gpr[GPR_GP] >> 32) == 0 &&
	    (cpus[bootstrap_cpu]->gpr[GPR_GP] & 0x80000000))
		cpus[bootstrap_cpu]->gpr[GPR_GP] |= 0xffffffff00000000;

	/*  Startup the bootstrap CPU:  */
	cpus[bootstrap_cpu]->bootstrap_cpu_flag = 1;
	cpus[bootstrap_cpu]->running            = 1;

	if (use_x11)
		x11_init();

	machine_init(mem);

	add_symbol_name(0x9fff0000, 0x10000, "r2k3k_cache", 0);
	symbol_recalc_sizes();

	debug("starting emulation: cpu%i pc=0x%016llx gp=0x%016llx\n\n",
	    bootstrap_cpu,
	    cpus[bootstrap_cpu]->pc, cpus[bootstrap_cpu]->gpr[GPR_GP]);

	console_init();

	cpu_run(cpus, ncpus);

	debug("done\n");

	if (use_x11) {
		printf("Press enter to quit.\n");
		while (!console_charavail()) {
			x11_check_event();
			usleep(1);
		}
		console_readchar();
	}

	console_deinit();
}

