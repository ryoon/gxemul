/*
 *  Copyright (C) 2003-2004  Anders Gavare.  All rights reserved.
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
 *  $Id: emul.c,v 1.96 2004-12-18 08:51:19 debug Exp $
 *
 *  Emulation startup and misc. routines.
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "misc.h"

#include "bintrans.h"
#include "console.h"
#include "diskimage.h"
#include "memory.h"
#include "net.h"

#ifdef HACK_STRTOLL
#define strtoll strtol
#define strtoull strtoul
#endif


extern int extra_argc;
extern char **extra_argv;

extern int quiet_mode;

extern struct emul *debugger_emul;


/*
 *  add_dump_points():
 *
 *  Take the strings breakpoint_string[] and convert to addresses
 *  (and store them in breakpoint_addr[]).
 */
static void add_dump_points(struct emul *emul)
{
	int i;
	int string_flag;
	uint64_t dp;

	for (i=0; i<emul->n_breakpoints; i++) {
		string_flag = 0;
		dp = strtoull(emul->breakpoint_string[i], NULL, 0);

		/*
		 *  If conversion resulted in 0, then perhaps it is a
		 *  symbol:
		 */
		if (dp == 0) {
			uint64_t addr;
			int res = get_symbol_addr(&emul->symbol_context,
			    emul->breakpoint_string[i], &addr);
			if (!res)
				fprintf(stderr,
				    "WARNING! Breakpoint '%s' could not be parsed\n",
				    emul->breakpoint_string[i]);
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
			dp |= 0xffffffff00000000ULL;
		emul->breakpoint_addr[i] = dp;

		debug("breakpoint %i: 0x%016llx", i, (long long)dp);
		if (string_flag)
			debug(" (%s)", emul->breakpoint_string[i]);
		debug("\n");
	}
}


/*
 *  fix_console():
 */
static void fix_console(void)
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
static void load_bootblock(struct emul *emul, struct cpu *cpu)
{
	int boot_disk_id = diskimage_bootdev();
	unsigned char minibuf[0x20];
	unsigned char *bootblock_buf;
	uint64_t bootblock_offset;
	uint64_t bootblock_loadaddr, bootblock_pc;
	int n_blocks, res, readofs;

	if (boot_disk_id < 0)
		return;

	switch (emul->emulation_type) {
	case EMULTYPE_DEC:
		/*
		 *  The first few bytes of a disk contains information about
		 *  where the bootblock(s) are located. (These are all 32-bit
		 *  little-endian words.)
		 *
		 *  Offset 0x10 = load address
		 *         0x14 = initial PC value
		 *         0x18 = nr of 512-byte blocks to read
		 *         0x1c = offset on disk to where the bootblocks
		 *                are (in 512-byte units)
		 *         0x20 = nr of blocks to read...
		 *         0x24 = offset...
		 *
		 *  nr of blocks to read and offset are repeated until nr of
		 *  blocks to read is zero.
		 */
		res = diskimage_access(boot_disk_id, 0, 0,
		    minibuf, sizeof(minibuf));

		bootblock_loadaddr = minibuf[0x10] + (minibuf[0x11] << 8)
		  + (minibuf[0x12] << 16) + (minibuf[0x13] << 24);

		/*  Convert loadaddr to uncached:  */
		if ((bootblock_loadaddr & 0xf0000000ULL) != 0x80000000 &&
		    (bootblock_loadaddr & 0xf0000000ULL) != 0xa0000000)
			fatal("\nWARNING! Weird load address 0x%08x.\n\n",
			    (int)bootblock_loadaddr);
		bootblock_loadaddr &= 0x0fffffff;
		bootblock_loadaddr |= 0xa0000000;

		bootblock_pc = minibuf[0x14] + (minibuf[0x15] << 8)
		  + (minibuf[0x16] << 16) + (minibuf[0x17] << 24);

		bootblock_pc &= 0x0fffffff;
		bootblock_pc |= 0xa0000000;
		cpu->pc = bootblock_pc;

		readofs = 0x18;

		for (;;) {
			res = diskimage_access(boot_disk_id, 0, readofs,
			    minibuf, sizeof(minibuf));
			if (!res) {
				printf("couldn't read disk?\n");
				exit(1);
			}

			n_blocks = minibuf[0] + (minibuf[1] << 8)
			  + (minibuf[2] << 16) + (minibuf[3] << 24);

			bootblock_offset = (minibuf[4] + (minibuf[5] << 8)
			  + (minibuf[6] << 16) + (minibuf[7] << 24)) * 512;

			if (n_blocks < 1)
				break;

			if (n_blocks * 512 > 65536)
				fatal("\nWARNING! Unusually large bootblock (%i bytes)\n\n",
				    n_blocks * 512);

			bootblock_buf = malloc(n_blocks * 512);
			if (bootblock_buf == NULL) {
				fprintf(stderr, "out of memory in load_bootblock()\n");
				exit(1);
			}

			res = diskimage_access(boot_disk_id, 0, bootblock_offset,
			    bootblock_buf, n_blocks * 512);
			if (!res) {
				fatal("WARNING: could not load bootblocks from disk offset 0x%llx\n",
				    (long long)bootblock_offset);
			}

			store_buf(cpu, bootblock_loadaddr, (char *)bootblock_buf,
			    n_blocks * 512);

			bootblock_loadaddr += 512*n_blocks;
			free(bootblock_buf);
			readofs += 8;
		}

		break;
	default:
		fatal("Booting from disk without a separate kernel doesn't work in this emulation mode.\n");
		exit(1);
	}
}


/*
 *  emul_new():
 *
 *  Returns a reasonably initialized struct emul.
 */
struct emul *emul_new(void)
{
	struct emul *e;
	e = malloc(sizeof(struct emul));
	if (e == NULL)
		return NULL;

	memset(e, 0, sizeof(struct emul));

	/*  Sane default values:  */
	e->emulation_type = EMULTYPE_NONE;
	e->machine = MACHINE_NONE;
	e->prom_emulation = 1;
	e->speed_tricks = 1;
	e->boot_kernel_filename = "";
	e->boot_string_argument = "";
	e->ncpus = DEFAULT_NCPUS;
	e->automatic_clock_adjustment = 1;
	e->x11_scaledown = 1;
	e->n_gfx_cards = 1;
	e->dbe_on_nonexistant_memaccess = 1;

	return e;
}


/*
 *  emul():
 *
 *	o)  Initialize the hardware (RAM, devices, CPUs, ...) which
 *	    will be emulated.
 *
 *	o)  Load ROM code and/or other programs into emulated memory.
 *
 *	o)  Special hacks needed after programs have been loaded.
 *
 *	o)  Start running instructions on the bootstrap cpu.
 */
void emul_start(struct emul *emul)
{
	struct memory *mem;
	int i;
	uint64_t addr, memory_amount;

	srandom(time(NULL));

	atexit(fix_console);

	/*  Initialize dynamic binary translation, if available:  */
	if (emul->bintrans_enable)
		bintrans_init();

	/*
	 *  Create the system's memory:
	 *
	 *  A special hack is used for some SGI models,
	 *  where memory is offset by 128MB to leave room for
	 *  EISA space and other things.
	 */
	debug("adding memory: %i MB", emul->physical_ram_in_mb);
	memory_amount = (uint64_t)emul->physical_ram_in_mb * 1048576;
	if (emul->emulation_type == EMULTYPE_SGI && (emul->machine == 20 ||
	    emul->machine == 22 || emul->machine == 24 ||
	    emul->machine == 26)) {
		debug(" (offset by 128MB, SGI hack)");
		memory_amount += 128 * 1048576;
	}
	if (emul->emulation_type == EMULTYPE_SGI && (emul->machine == 28 ||
	    emul->machine == 30)) {
		debug(" (offset by 512MB, SGI hack)");
		memory_amount += 0x20000000;
	}
	mem = memory_new(memory_amount);
	debug("\n");

	/*  Create CPUs:  */
	emul->cpus = malloc(sizeof(struct cpu *) * emul->ncpus);
	if (emul->cpus == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(emul->cpus, 0, sizeof(struct cpu *) * emul->ncpus);

	debug("adding cpu0");
	if (emul->ncpus > 1)
		debug(" .. cpu%i", emul->ncpus-1);
	debug(": %s", emul->emul_cpu_name);
	for (i=0; i<emul->ncpus; i++) {
		emul->cpus[i] = cpu_new(mem, emul, i, emul->emul_cpu_name);
		if (emul->bintrans_enable)
			bintrans_init_cpu(emul->cpus[i]);
	}
	debug("\n");

	if (emul->use_random_bootstrap_cpu)
		emul->bootstrap_cpu = random() % emul->ncpus;
	else
		emul->bootstrap_cpu = 0;

	diskimage_dump_info();

	if (emul->use_x11)
		x11_init(emul);

	/*  Fill memory with random bytes:  */
	if (emul->random_mem_contents) {
		for (i=0; i<emul->physical_ram_in_mb*1048576; i+=256) {
			unsigned char data[256];
			unsigned int j;
			for (j=0; j<sizeof(data); j++)
				data[j] = random() & 255;
			addr = 0x80000000 + i;
			memory_rw(emul->cpus[emul->bootstrap_cpu], mem,
			    addr, data, sizeof(data), MEM_WRITE,
			    CACHE_NONE | NO_EXCEPTIONS);
		}
	}

	if (emul->userland_emul) {
		/*
		 *  For userland only emulation, no machine emulation is
		 *  needed.
		 */
	} else {
		machine_init(emul, mem);
		net_init();
	}

	/*  Load files (ROM code, boot code, ...) into memory:  */
	if (extra_argc > 0)
		debug("loading files into emulation memory:\n");

	if (emul->booting_from_diskimage)
		load_bootblock(emul, emul->cpus[emul->bootstrap_cpu]);

	while (extra_argc > 0) {
		file_load(mem, extra_argv[0], emul->cpus[emul->bootstrap_cpu]);

		/*
		 *  For userland emulation, the remainding items
		 *  on the command line will be passed as parameters
		 *  to the emulated program, and will not be treated
		 *  as filenames to load into the emulator.
		 *  The program's name will be in argv[0], and the
		 *  rest of the parameters in argv[1] and up.
		 */
		if (emul->userland_emul)
			break;

		extra_argc --;  extra_argv ++;
	}

	if (file_n_executables_loaded() == 0 && !emul->booting_from_diskimage) {
		fprintf(stderr, "No executable file loaded, and we're not booting directly from a disk image.\nAborting.\n");
		exit(1);
	}

	if ((emul->cpus[emul->bootstrap_cpu]->pc >> 32) == 0 &&
	    (emul->cpus[emul->bootstrap_cpu]->pc & 0x80000000ULL))
		emul->cpus[emul->bootstrap_cpu]->pc |= 0xffffffff00000000ULL;

	if ((emul->cpus[emul->bootstrap_cpu]->gpr[GPR_GP] >> 32) == 0 &&
	    (emul->cpus[emul->bootstrap_cpu]->gpr[GPR_GP] & 0x80000000ULL))
		emul->cpus[emul->bootstrap_cpu]->gpr[GPR_GP] |= 0xffffffff00000000ULL;

	/*  Same byte order for all CPUs:  */
	for (i=0; i<emul->ncpus; i++)
		if (i != emul->bootstrap_cpu)
			emul->cpus[i]->byte_order =
			    emul->cpus[emul->bootstrap_cpu]->byte_order;

	if (emul->userland_emul)
		useremul_init(emul->cpus[emul->bootstrap_cpu],
		    extra_argc, extra_argv);

	/*  Startup the bootstrap CPU:  */
	emul->cpus[emul->bootstrap_cpu]->bootstrap_cpu_flag = 1;
	emul->cpus[emul->bootstrap_cpu]->running            = 1;

	/*  Add PC dump points:  */
	add_dump_points(emul);

	add_symbol_name(&emul->symbol_context,
	    0x9fff0000, 0x10000, "r2k3k_cache", 0);
	symbol_recalc_sizes(&emul->symbol_context);

	if (emul->max_random_cycles_per_chunk > 0)
		debug("using random cycle chunks (1 to %i cycles)\n",
		    emul->max_random_cycles_per_chunk);

	/*  Special hack for ARC emulation:  */
	if (emul->emulation_type == EMULTYPE_ARC && emul->prom_emulation) {
		uint64_t start = emul->cpus[emul->bootstrap_cpu]->pc & 0x1fffffff;
		uint64_t len = 0x800000 - start;
		/*  NOTE/TODO: magic 8MB end of load program area  */
		arcbios_add_memory_descriptor(emul->cpus[emul->bootstrap_cpu],
		    0x60000, start-0x60000, ARCBIOS_MEM_FreeMemory);
		arcbios_add_memory_descriptor(emul->cpus[emul->bootstrap_cpu],
		    start, len, ARCBIOS_MEM_LoadedProgram);
	}

	debug("starting emulation: cpu%i pc=0x%016llx gp=0x%016llx\n\n",
	    emul->bootstrap_cpu, emul->cpus[emul->bootstrap_cpu]->pc,
	    emul->cpus[emul->bootstrap_cpu]->gpr[GPR_GP]);

	/*
	 *  console_init() makes sure that the terminal is in a good state.
	 *
	 *  The SIGINT handler is for CTRL-C  (enter the interactive debugger).
	 *
	 *  The SIGCONT handler is invoked whenever the user presses CTRL-Z
	 *  (or sends SIGSTOP) and then continues. It makes sure that the
	 *  terminal is in an expected state.
	 */
	debugger_emul = emul;

	console_init(emul);
	signal(SIGINT, debugger_activate);
	signal(SIGCONT, console_sigcont);

	if (!emul->verbose)
		quiet_mode = 1;


	cpu_run(emul, emul->cpus, emul->ncpus);


	if (emul->use_x11) {
		printf("Press enter to quit.\n");
		while (!console_charavail()) {
			x11_check_event();
			usleep(1);
		}
		console_readchar();
	}

	console_deinit();
}

