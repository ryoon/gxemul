/*
 *  Copyright (C) 2003-2004 by Anders Gavare.  All rights reserved.
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
 *  $Id: emul.c,v 1.18 2004-07-01 12:01:42 debug Exp $
 *
 *  Emulation startup.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "bintrans.h"
#include "memory.h"
#include "misc.h"
#include "diskimage.h"
#include "console.h"


extern int optind;
extern char *optarg;
int extra_argc;
char **extra_argv;

extern int booting_from_diskimage;

extern int bintrans_enable;
extern char emul_cpu_name[50];
extern int emulation_type;
extern int machine;
extern int physical_ram_in_mb;
extern int random_mem_contents;
extern int bootstrap_cpu;
extern int use_random_bootstrap_cpu;
extern int ncpus;
extern struct cpu **cpus;
extern int userland_emul;
extern int use_x11;
extern int x11_scaledown;
extern int quiet_mode;
extern int verbose;
extern int n_dumppoints;
extern char *dumppoint_string[];
extern uint64_t dumppoint_pc[];
extern int dumppoint_flag_r[];


/*
 *  add_pc_dump_points():
 *
 *  Take the strings dumppoint_string[] and convert to addresses
 *  (and store them in dumppoint_pc[]).
 */
void add_pc_dump_points(void)
{
	int i;
	int string_flag;
	uint64_t dp;

	for (i=0; i<n_dumppoints; i++) {
		string_flag = 0;
		dp = strtoull(dumppoint_string[i], NULL, 0);

		/*  If conversion resulted in 0, then perhaps it is a symbol:  */
		if (dp == 0) {
			uint64_t addr;
			int res = get_symbol_addr(dumppoint_string[i], &addr);
			if (!res)
				fprintf(stderr, "WARNING! PC dumppoint '%s' could not be parsed\n", dumppoint_string[i]);
			else {
				dp = addr;
				string_flag = 1;
			}
		}

		/*
		 *  TODO:  It would be nice if things like   symbolname+0x1234
		 *  were automatically converted into the correct address.
		 */

		if ((dp >> 32) == 0 && ((dp >> 31) & 1))
			dp |= 0xffffffff00000000;
		dumppoint_pc[i] = dp;

		debug("pc dumppoint %i: %016llx", i, (long long)dp);
		if (string_flag)
			debug(" (%s)", dumppoint_string[i]);
		debug("\n");
	}
}


/*
 *  fix_console():
 */
void fix_console(void)
{
	console_deinit();
}


/*
 *  load_bootblock():
 *
 *  For some emulation modes, it is possible to boot from a harddisk image by
 *  loading a bootblock from a specific disk offset into memory, and executing
 *  that, instead of requiring a separate kernel file.  It is then up to the
 *  bootblock to load a kernel.
 */
void load_bootblock(void)
{
	int boot_disk_id = diskimage_bootdev();
	int res;
	unsigned char minibuf[4];
	unsigned char bootblock_buf[8192];
	uint64_t bootblock_offset;

	switch (emulation_type) {
	case EMULTYPE_DEC:
		/*
		 *  The bootblock for DECstations is 8KB large.  We first
		 *  read the 32-bit word at offset 0x1c. This word tells
		 *  us the starting sector number of the bootblock.
		 *
		 *  (The bootblock is usually at offset 0x200, but for
		 *  example the NetBSD/pmax 1.6.2 CDROM uses offset
		 *  0x4a10000, so it is probably best to trust the offset
		 *  given at offset 0x1c.)
		 *
		 *  Ultrix seems to expect two copies, one at 0x80600000 and
		 *  one at 0x80700000. We start running at 0x80700000 though,
		 *  because that works with both NetBSD and Ultrix.
		 */
		res = diskimage_access(boot_disk_id, 0, 0x1c,
		    minibuf, sizeof(minibuf));

		bootblock_offset = (minibuf[0] + (minibuf[1] << 8)
		  + (minibuf[2] << 16) + (minibuf[3] << 24)) * 512;

		res = diskimage_access(boot_disk_id, 0, bootblock_offset,
		    bootblock_buf, sizeof(bootblock_buf));

		/*  Ultrix boots at 0x80600000, NetBSD at 0x80700000:  */
		store_buf(0x80600000, bootblock_buf, sizeof(bootblock_buf));
		store_buf(0x80700000, bootblock_buf, sizeof(bootblock_buf));

		cpus[bootstrap_cpu]->pc = 0x80700000;
		break;
	default:
		fatal("Booting from disk without a separate kernel doesn't work in this emulation mode.\n");
		exit(1);
	}
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
	uint64_t addr, memory_amount;

	srandom(time(NULL));

	atexit(fix_console);

	/*  Initialize dynamic binary translation, if available:  */
	if (bintrans_enable)
		bintrans_init();

	/*
	 *  Create the system's memory:
	 *
	 *  A special hack is used for some SGI models,
	 *  where memory is offset by 128MB to leave room for
	 *  EISA space and other things.
	 */
	debug("adding memory: %i MB", physical_ram_in_mb);
	memory_amount = (uint64_t)physical_ram_in_mb * 1048576;
	if (emulation_type == EMULTYPE_SGI && (machine == 20 || machine == 22
	    || machine == 24 || machine == 26)) {
		debug(" (offset by 128MB, SGI hack)");
		memory_amount += 128 * 1048576;
	}
	mem = memory_new(DEFAULT_BITS_PER_PAGETABLE, DEFAULT_BITS_PER_MEMBLOCK,
	    memory_amount, DEFAULT_MAX_BITS);
	debug("\n");

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

	diskimage_dump_info();

	if (use_x11)
		x11_init();

	/*  For userland only emulation, no machine emulation is needed.  */
	if (!userland_emul)
		machine_init(mem);

	/*  Fill memory with random bytes:  */
	if (random_mem_contents) {
		for (i=0; i<physical_ram_in_mb*1048576; i+=256) {
			unsigned char data[256];
			unsigned int j;
			for (j=0; j<sizeof(data); j++)
				data[j] = random() & 255;
			addr = 0x80000000 + i;
			memory_rw(cpus[0], mem, addr, data, sizeof(data), MEM_WRITE, CACHE_NONE | NO_EXCEPTIONS);
		}
	}

	/*  Load files (ROM code, boot code, ...) into memory:  */
	if (extra_argc > 0)
		debug("loading files into emulation memory:\n");

	if (booting_from_diskimage)
		load_bootblock();

	while (extra_argc > 0) {
		file_load(mem, extra_argv[0], cpus[bootstrap_cpu]);

		/*
		 *  For userland emulation, the remainding items
		 *  on the command line will be passed as parameters
		 *  to the emulated program, and will not be treated
		 *  as filenames to load into the emulator.
		 *  The program's name will be in argv[0], and the
		 *  rest of the parameters in argv[1] and up.
		 */
		if (userland_emul)
			break;

		extra_argc --;  extra_argv ++;
	}

	if ((cpus[bootstrap_cpu]->pc >> 32) == 0 &&
	    (cpus[bootstrap_cpu]->pc & 0x80000000))
		cpus[bootstrap_cpu]->pc |= 0xffffffff00000000;

	if ((cpus[bootstrap_cpu]->gpr[GPR_GP] >> 32) == 0 &&
	    (cpus[bootstrap_cpu]->gpr[GPR_GP] & 0x80000000))
		cpus[bootstrap_cpu]->gpr[GPR_GP] |= 0xffffffff00000000;

	/*  Same byte order for all CPUs:  */
	for (i=0; i<ncpus; i++)
		if (i != bootstrap_cpu)
			cpus[i]->byte_order = cpus[bootstrap_cpu]->byte_order;

	if (userland_emul)
		useremul_init(cpus[bootstrap_cpu], extra_argc, extra_argv);

	/*  Startup the bootstrap CPU:  */
	cpus[bootstrap_cpu]->bootstrap_cpu_flag = 1;
	cpus[bootstrap_cpu]->running            = 1;

	add_symbol_name(0x9fff0000, 0x10000, "r2k3k_cache", 0);
	symbol_recalc_sizes();

	/*  Add PC dump points:  */
	add_pc_dump_points();

	debug("starting emulation: cpu%i pc=0x%016llx gp=0x%016llx\n\n",
	    bootstrap_cpu,
	    cpus[bootstrap_cpu]->pc, cpus[bootstrap_cpu]->gpr[GPR_GP]);

	console_init();

	if (!verbose)
		quiet_mode = 1;


	cpu_run(cpus, ncpus);


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

