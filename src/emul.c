/*
 *  Copyright (C) 2003-2005  Anders Gavare.  All rights reserved.
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
 *  $Id: emul.c,v 1.128 2005-01-23 10:47:18 debug Exp $
 *
 *  Emulation startup and misc. routines.
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "bintrans.h"
#include "emul.h"
#include "console.h"
#include "diskimage.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"
#include "net.h"
#include "sgi_arcbios.h"

#ifdef HACK_STRTOLL
#define strtoll strtol
#define strtoull strtoul
#endif


extern int extra_argc;
extern char **extra_argv;

extern int quiet_mode;

extern struct emul *debugger_emul;
extern struct diskimage *diskimages[];


/*
 *  add_dump_points():
 *
 *  Take the strings breakpoint_string[] and convert to addresses
 *  (and store them in breakpoint_addr[]).
 *
 *  TODO: This function should be moved elsewhere.
 */
static void add_dump_points(struct machine *m)
{
	int i;
	int string_flag;
	uint64_t dp;

	for (i=0; i<m->n_breakpoints; i++) {
		string_flag = 0;
		dp = strtoull(m->breakpoint_string[i], NULL, 0);

		/*
		 *  If conversion resulted in 0, then perhaps it is a
		 *  symbol:
		 */
		if (dp == 0) {
			uint64_t addr;
			int res = get_symbol_addr(&m->symbol_context,
			    m->breakpoint_string[i], &addr);
			if (!res)
				fprintf(stderr,
				    "WARNING! Breakpoint '%s' could not be"
					" parsed\n",
				    m->breakpoint_string[i]);
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
		m->breakpoint_addr[i] = dp;

		debug("breakpoint %i: 0x%016llx", i, (long long)dp);
		if (string_flag)
			debug(" (%s)", m->breakpoint_string[i]);
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
static void load_bootblock(struct machine *m, struct cpu *cpu)
{
	int boot_disk_id;
	unsigned char minibuf[0x20];
	unsigned char *bootblock_buf;
	uint64_t bootblock_offset;
	uint64_t bootblock_loadaddr, bootblock_pc;
	int n_blocks, res, readofs;

	boot_disk_id = diskimage_bootdev(m);
	if (boot_disk_id < 0)
		return;

	switch (m->machine_type) {
	case MACHINE_DEC:
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
		res = diskimage_access(m, boot_disk_id, 0, 0,
		    minibuf, sizeof(minibuf));

		bootblock_loadaddr = minibuf[0x10] + (minibuf[0x11] << 8)
		  + (minibuf[0x12] << 16) + (minibuf[0x13] << 24);

		/*  Convert loadaddr to uncached:  */
		if ((bootblock_loadaddr & 0xf0000000ULL) != 0x80000000 &&
		    (bootblock_loadaddr & 0xf0000000ULL) != 0xa0000000)
			fatal("\nWARNING! Weird load address 0x%08x.\n\n",
			    (int)bootblock_loadaddr);
		bootblock_loadaddr &= 0x0fffffffULL;
		bootblock_loadaddr |= 0xffffffffa0000000ULL;

		bootblock_pc = minibuf[0x14] + (minibuf[0x15] << 8)
		  + (minibuf[0x16] << 16) + (minibuf[0x17] << 24);

		bootblock_pc &= 0x0fffffffULL;
		bootblock_pc |= 0xffffffffa0000000ULL;
		cpu->pc = bootblock_pc;

		readofs = 0x18;

		for (;;) {
			res = diskimage_access(m, boot_disk_id, 0, readofs,
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
				fatal("\nWARNING! Unusually large bootblock "
				    "(%i bytes)\n\n", n_blocks * 512);

			bootblock_buf = malloc(n_blocks * 512);
			if (bootblock_buf == NULL) {
				fprintf(stderr, "out of memory in "
				    "load_bootblock()\n");
				exit(1);
			}

			res = diskimage_access(m, boot_disk_id, 0,
			    bootblock_offset, bootblock_buf, n_blocks * 512);
			if (!res) {
				fatal("WARNING: could not load bootblocks from"
				    " disk offset 0x%llx\n",
				    (long long)bootblock_offset);
			}

			store_buf(cpu, bootblock_loadaddr,
			    (char *)bootblock_buf, n_blocks * 512);

			bootblock_loadaddr += 512*n_blocks;
			free(bootblock_buf);
			readofs += 8;
		}

		break;
	default:
		fatal("Booting from disk without a separate kernel "
		    "doesn't work in this emulation mode.\n");
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
	e->n_machines = 0;

	return e;
}


/*
 *  emul_add_machine():
 *
 *  Calls machine_new(), adds the new machine into the emul
 *  struct, and returns a pointer to the new machine.
 *
 *  This function should be used instead of manually calling
 *  machine_new().
 */
struct machine *emul_add_machine(struct emul *e, char *name)
{
	struct machine *m;

	m = machine_new(name, e);

	e->n_machines ++;
	e->machines = realloc(e->machines,
	    sizeof(struct machine *) * e->n_machines);
	if (e->machines == NULL) {
		fprintf(stderr, "emul_add_machine(): out of memory\n");
		exit(1);
	}

	e->machines[e->n_machines - 1] = m;
	return m;
}


/*
 *  add_arc_components():
 *
 *  This function adds ARCBIOS memory descriptors for the loaded program,
 *  and ARCBIOS components for SCSI devices.
 */
static void add_arc_components(struct machine *m)
{
	struct cpu *cpu = m->cpus[m->bootstrap_cpu];
	uint64_t start = cpu->pc & 0x1fffffff;
	uint64_t len = 0xc00000 - start;
	struct diskimage *d;
	uint64_t scsicontroller, scsidevice, scsidisk;

	if ((cpu->pc >> 60) != 0xf) {
		start = cpu->pc & 0xffffffffffULL;
		len = 0xc00000 - start;
	}

	len += 1048576 * m->memory_offset_in_mb;

	/*  NOTE/TODO: magic 12MB end of load program area  */
	arcbios_add_memory_descriptor(cpu,
	    0x60000 + m->memory_offset_in_mb * 1048576,
	    start-0x60000 - m->memory_offset_in_mb * 1048576,
	    ARCBIOS_MEM_FreeMemory);
	arcbios_add_memory_descriptor(cpu,
	    start, len, ARCBIOS_MEM_LoadedProgram);

	scsicontroller = arcbios_get_scsicontroller();
	if (scsicontroller == 0)
		return;

	/*  TODO: The device 'name' should defined be somewhere else.  */

	d = m->first_diskimage;
	while (d != NULL) {
		if (d->type == DISKIMAGE_SCSI) {
			int a, b, flags = COMPONENT_FLAG_Input;
			char component_string[100];
			char *name = "DEC     RZ58     (C) DEC2000";

			/*  Read-write, or read-only?  */
			if (d->writable)
				flags |= COMPONENT_FLAG_Output;
			else
				flags |= COMPONENT_FLAG_ReadOnly;

			a = COMPONENT_TYPE_DiskController;
			b = COMPONENT_TYPE_DiskPeripheral;

			if (d->is_a_cdrom) {
				flags |= COMPONENT_FLAG_Removable;
				a = COMPONENT_TYPE_CDROMController;
				b = COMPONENT_TYPE_FloppyDiskPeripheral;
				name = "NEC     CD-ROM CDR-210P 1.0 ";
			}

			scsidevice = arcbios_addchild_manual(cpu,
			    COMPONENT_CLASS_ControllerClass,
			    a, flags, 1, 2, d->id, 0xffffffff,
			    name, scsicontroller, NULL, 0);

			scsidisk = arcbios_addchild_manual(cpu,
			    COMPONENT_CLASS_PeripheralClass,
			    b, flags, 1, 2, 0, 0xffffffff, NULL,
			    scsidevice, NULL, 0);

			/*
			 *  Add device string to component address mappings:
			 *  "scsi(0)disk(0)rdisk(0)partition(0)"
			 */

			if (d->is_a_cdrom) {
				snprintf(component_string,
				    sizeof(component_string),
				    "scsi(0)cdrom(%i)", d->id);
				arcbios_add_string_to_component(
				    component_string, scsidevice);

				snprintf(component_string,
				    sizeof(component_string),
				    "scsi(0)cdrom(%i)fdisk(0)", d->id);
				arcbios_add_string_to_component(
				    component_string, scsidisk);
			} else {
				snprintf(component_string,
				    sizeof(component_string),
				    "scsi(0)disk(%i)", d->id);
				arcbios_add_string_to_component(
				    component_string, scsidevice);

				snprintf(component_string,
				    sizeof(component_string),
				    "scsi(0)disk(%i)rdisk(0)", d->id);
				arcbios_add_string_to_component(
				    component_string, scsidisk);
			}
		}

		d = d->next;
	}
}


/*
 *  emul_machine_setup():
 *
 *	o)  Initialize the hardware (RAM, devices, CPUs, ...) which
 *	    will be emulated in this machine.
 *
 *	o)  Load ROM code and/or other programs into emulated memory.
 *
 *	o)  Special hacks needed after programs have been loaded.
 */
static void emul_machine_setup(struct emul *emul, int machine_nr)
{
	struct machine *m;
	struct memory *mem;
	int i, iadd=4;
	uint64_t addr, memory_amount;

	m = emul->machines[machine_nr];

	debug("machine \"%s\":\n", m->name);
	debug_indentation(iadd);

	/*  Create the system's memory:  */
	debug("memory: %i MB", m->physical_ram_in_mb);
	memory_amount = (uint64_t)m->physical_ram_in_mb * 1048576;
	if (m->memory_offset_in_mb > 0) {
		/*
		 *  A special hack is used for some SGI models,
		 *  where memory is offset by 128MB to leave room for
		 *  EISA space and other things.
		 */
		debug(" (offset by %iMB)", m->memory_offset_in_mb);
		memory_amount += 1048576 * m->memory_offset_in_mb;
	}
	mem = memory_new(memory_amount);
	debug("\n");

	/*  Create CPUs:  */
	m->cpus = malloc(sizeof(struct cpu *) * m->ncpus);
	if (m->cpus == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(m->cpus, 0, sizeof(struct cpu *) * m->ncpus);

	/*  Initialize dynamic binary translation, if available:  */
	if (m->bintrans_enable)
		bintrans_init(mem);

	debug("adding cpu0");
	if (m->ncpus > 1)
		debug(" .. cpu%i", m->ncpus-1);
	debug(": %s", m->cpu_name);
	for (i=0; i<m->ncpus; i++) {
		m->cpus[i] = cpu_new(mem, m, i, m->cpu_name);
		if (m->bintrans_enable)
			bintrans_init_cpu(m->cpus[i]);
	}
	debug("\n");

	if (m->use_random_bootstrap_cpu)
		m->bootstrap_cpu = random() % m->ncpus;
	else
		m->bootstrap_cpu = 0;

	if (m->use_x11)
		x11_init(m);

	/*  Fill memory with random bytes:  */
	if (m->random_mem_contents) {
		for (i=0; i<m->physical_ram_in_mb*1048576; i+=256) {
			unsigned char data[256];
			unsigned int j;
			for (j=0; j<sizeof(data); j++)
				data[j] = random() & 255;
			addr = 0xffffffff80000000ULL + i;
			memory_rw(m->cpus[m->bootstrap_cpu], mem,
			    addr, data, sizeof(data), MEM_WRITE,
			    CACHE_NONE | NO_EXCEPTIONS);
		}
	}

	if ((m->machine_type == MACHINE_ARC ||
	    m->machine_type == MACHINE_SGI) && m->prom_emulation)
		arcbios_init();

	if (m->userland_emul) {
		/*
		 *  For userland-only emulation, no machine emulation
		 *  is needed.
		 */
	} else {
		machine_init(m);
	}

	diskimage_dump_info(m);

	/*  Load files (ROM code, boot code, ...) into memory:  */
	if (m->booting_from_diskimage)
		load_bootblock(m, m->cpus[m->bootstrap_cpu]);

	while (extra_argc > 0) {
		file_load(mem, extra_argv[0], m->cpus[m->bootstrap_cpu]);

		/*
		 *  For userland emulation, the remainding items
		 *  on the command line will be passed as parameters
		 *  to the emulated program, and will not be treated
		 *  as filenames to load into the emulator.
		 *  The program's name will be in argv[0], and the
		 *  rest of the parameters in argv[1] and up.
		 */
		if (m->userland_emul)
			break;

		extra_argc --;  extra_argv ++;
	}

	if (file_n_executables_loaded() == 0 && !m->booting_from_diskimage) {
		fprintf(stderr, "No executable file loaded, and we're not "
		    "booting directly from a disk image.\nAborting.\n");
		exit(1);
	}

	if ((m->cpus[m->bootstrap_cpu]->pc >> 32) == 0 &&
	    (m->cpus[m->bootstrap_cpu]->pc & 0x80000000ULL))
		m->cpus[m->bootstrap_cpu]->pc |= 0xffffffff00000000ULL;

	if ((m->cpus[m->bootstrap_cpu]->gpr[MIPS_GPR_GP] >> 32) == 0 &&
	    (m->cpus[m->bootstrap_cpu]->gpr[MIPS_GPR_GP] & 0x80000000ULL))
		m->cpus[m->bootstrap_cpu]->gpr[MIPS_GPR_GP] |= 0xffffffff00000000ULL;

	/*  Same byte order for all CPUs:  */
	for (i=0; i<m->ncpus; i++)
		if (i != m->bootstrap_cpu)
			m->cpus[i]->byte_order =
			    m->cpus[m->bootstrap_cpu]->byte_order;

	if (m->userland_emul)
		useremul_init(m->cpus[m->bootstrap_cpu],
		    extra_argc, extra_argv);

	/*  Startup the bootstrap CPU:  */
	m->cpus[m->bootstrap_cpu]->bootstrap_cpu_flag = 1;
	m->cpus[m->bootstrap_cpu]->running            = 1;

	/*  Add PC dump points:  */
	add_dump_points(m);

	add_symbol_name(&m->symbol_context,
	    0x9fff0000, 0x10000, "r2k3k_cache", 0);
	symbol_recalc_sizes(&m->symbol_context);

	if (m->max_random_cycles_per_chunk > 0)
		debug("using random cycle chunks (1 to %i cycles)\n",
		    m->max_random_cycles_per_chunk);

	/*  Special hack for ARC/SGI emulation:  */
	if ((m->machine_type == MACHINE_ARC ||
	    m->machine_type == MACHINE_SGI) && m->prom_emulation)
		add_arc_components(m);

	debug("starting cpu%i at ", m->bootstrap_cpu);
	if (m->cpus[m->bootstrap_cpu]->cpu_type.isa_level < 3 ||
	    m->cpus[m->bootstrap_cpu]->cpu_type.isa_level == 32) {
		debug("0x%08x", (int)m->cpus[m->bootstrap_cpu]->pc);
		if (m->cpus[m->bootstrap_cpu]->gpr[MIPS_GPR_GP] != 0)
			debug(" (gp=0x%08x)",
			    (int)m->cpus[m->bootstrap_cpu]->gpr[MIPS_GPR_GP]);
	} else {
		debug("0x%016llx", (long long)m->cpus[m->bootstrap_cpu]->pc);
		if (m->cpus[m->bootstrap_cpu]->gpr[MIPS_GPR_GP] != 0)
			debug(" (gp=0x%016llx)",
			    (long long)m->cpus[m->bootstrap_cpu]->gpr[MIPS_GPR_GP]);
	}
	debug("\n");

	debug_indentation(-iadd);
}


/*
 *  emul_simple_init():
 *
 *	o)  Initialize networks.
 *
 *	o)  Initialize one machine.
 */
void emul_simple_init(struct emul *emul)
{
	int i, iadd=4;

	if (emul->n_machines != 1) {
		fprintf(stderr, "emul_simple_init(): n_machines != 1\n");
		exit(1);
	}

	debug("Simple setup...\n");
	debug_indentation(iadd);

	/*  Create a network:  */
	emul->net = net_init(emul, NET_INIT_FLAG_GATEWAY, "10.0.0.0", 8);

	/*  Create the machine:  */
	for (i=0; i<emul->n_machines; i++)
		emul_machine_setup(emul, i);

	debug_indentation(-iadd);
}


/*
 *  emul_create_from_configfile():
 *
 *  Create an emul struct by reading settings from a configuration file.
 */
struct emul *emul_create_from_configfile(char *fname)
{
	int iadd = 4;
	struct emul *e = emul_new();

	debug("Creating emulation from configfile \"%s\":\n", fname);
	debug_indentation(iadd);

	/*  Create a network:  */
	/*  ..  */
	/*  Create the machine:  */
	/*  ..  */
	/*  emul_machine_setup(emul, 0);  */

	debug_indentation(-iadd);
	return e;
}


/*
 *  emul_run():
 *
 *	o)  Set up things needed before running emulations.
 *
 *	o)  Run emulations (one or more, in parallel).
 *
 *	o)  De-initialize things.
 */
void emul_run(struct emul **emuls, int n_emuls)
{
	struct emul *e;
	int i = 0, j, go = 1, n, anything;

	if (n_emuls < 1) {
		fprintf(stderr, "emul_run(): no thing to do\n");
		return;
	}

	srandom(time(NULL));
	atexit(fix_console);

	i = 79;
	while (i-- > 0)
		debug("-");
	debug("\n\n");

	/*  Initialize the interactive debugger:  */
	debugger_init(emuls, n_emuls);

	/*
	 *  console_init() makes sure that the terminal is in a good state.
	 *
	 *  The SIGINT handler is for CTRL-C  (enter the interactive debugger).
	 *
	 *  The SIGCONT handler is invoked whenever the user presses CTRL-Z
	 *  (or sends SIGSTOP) and then continues. It makes sure that the
	 *  terminal is in an expected state.
	 */
	console_init(emuls[0]);		/*  TODO: what is a good argument?  */
	signal(SIGINT, debugger_activate);
	signal(SIGCONT, console_sigcont);

	/*  No emulation in verbose mode? Then set quiet_mode.  */
	for (i=0; i<n_emuls; i++)
		if (!emuls[i]->verbose)
			quiet_mode = 1;

	/*  Initialize all CPUs in all machines in all emulations:  */
	for (i=0; i<n_emuls; i++) {
		e = emuls[i];
		if (e == NULL)
			continue;
		for (j=0; j<e->n_machines; j++)
			cpu_run_init(e, e->machines[j]);
	}

	/*
	 *  MAIN LOOP:
	 *
	 *  Run all emulations in parallel, running each machine in
	 *  each emulation.
	 */
	while (go) {
		go = 0;
		for (i=0; i<n_emuls; i++) {
			e = emuls[i];
			if (e == NULL)
				continue;

			for (j=0; j<e->n_machines; j++) {
				/*  TODO: cpu_run() is a strange name, since
				    there can be multiple cpus in a machine  */
				anything = cpu_run(e, e->machines[j]);
				if (anything)
					go = 1;
			}
		}
	}

	/*  Deinitialize all CPUs in all machines in all emulations:  */
	for (i=0; i<n_emuls; i++) {
		e = emuls[i];
		if (e == NULL)
			continue;
		for (j=0; j<e->n_machines; j++)
			cpu_run_deinit(e, e->machines[j]);
	}

	/*  Any machine using X11? Then we should wait before exiting:  */
	n = 0;
	for (i=0; i<n_emuls; i++)
		for (j=0; j<emuls[i]->n_machines; j++)
			if (emuls[i]->machines[j]->use_x11)
				n++;
	if (n > 0) {
		printf("Press enter to quit.\n");
		while (!console_charavail()) {
			x11_check_event();
			usleep(1);
		}
		console_readchar();
	}

	console_deinit();
}

