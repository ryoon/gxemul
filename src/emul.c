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
 *  $Id: emul.c,v 1.76 2004-10-10 14:07:49 debug Exp $
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
#include "emul.h"
#include "memory.h"
#include "net.h"

#ifdef HACK_STRTOLL
#define strtoll strtol
#define strtoull strtoul
#endif


extern int extra_argc;
extern char **extra_argv;

extern int quiet_mode;

int old_instruction_trace = 0;
int old_quiet_mode = 0;
int old_show_trace_tree = 0;


#define	MAX_CMD_LEN	60


/*
 *  add_pc_dump_points():
 *
 *  Take the strings dumppoint_string[] and convert to addresses
 *  (and store them in dumppoint_pc[]).
 */
static void add_pc_dump_points(struct emul *emul)
{
	int i;
	int string_flag;
	uint64_t dp;

	for (i=0; i<emul->n_dumppoints; i++) {
		string_flag = 0;
		dp = strtoull(emul->dumppoint_string[i], NULL, 0);

		/*
		 *  If conversion resulted in 0, then perhaps it is a
		 *  symbol:
		 */
		if (dp == 0) {
			uint64_t addr;
			int res = get_symbol_addr(&emul->symbol_context,
			    emul->dumppoint_string[i], &addr);
			if (!res)
				fprintf(stderr,
				    "WARNING! PC dumppoint '%s' could not be parsed\n",
				    emul->dumppoint_string[i]);
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
		emul->dumppoint_pc[i] = dp;

		debug("pc dumppoint %i: %016llx", i, (long long)dp);
		if (string_flag)
			debug(" (%s)", emul->dumppoint_string[i]);
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
 *  Global (static) debugger variables:
 *
 *  (TODO: How to make these non-global in a nice way?)
 */

static struct emul *debugger_emul;
static char last_cmd[MAX_CMD_LEN];
static int last_cmd_len = 0;


/*
 *  debugger_activate():
 *
 *  This is a signal handler for CTRL-C.
 */
static void debugger_activate(int x)
{
	if (debugger_emul->single_step) {
		/*  Already in the debugger. Do nothing.  */
		int i;
		for (i=0; i<MAX_CMD_LEN+1; i++)
			console_makeavail('\b');
		console_makeavail(' ');
		console_makeavail('\n');
		printf("^C");
		fflush(stdout);
	} else {
		/*  Enter the single step debugger.  */
		debugger_emul->single_step = 1;

		/*  Discard any chars in the input queue:  */
		while (console_charavail())
			console_readchar();
	}

	/*  Reactivate the signal handler:  */
	signal(SIGINT, debugger_activate);
}


/*
 *  debugger_dump():
 *
 *  Dump emulated memory in hex and ASCII.
 */
static void debugger_dump(struct emul *emul, uint64_t addr, int lines)
{
	struct cpu *c;
	struct memory *m;
	int x, r;

	if (emul->cpus == NULL) {
		printf("No cpus (?)\n");
		return;
	}
	c = emul->cpus[emul->bootstrap_cpu];
	if (c == NULL) {
		printf("emul->cpus[emul->bootstrap_cpu] = NULL\n");
		return;
	}
	m = emul->cpus[emul->bootstrap_cpu]->mem;

	while (lines -- > 0) {
		unsigned char buf[16];
		memset(buf, 0, sizeof(buf));
		r = memory_rw(c, m, addr, &buf[0], sizeof(buf), MEM_READ,
		    CACHE_NONE | NO_EXCEPTIONS);

		printf("0x%016llx  ", (long long)addr);

		if (r == MEMORY_ACCESS_FAILED)
			printf("(memory access failed)\n");
		else {
			for (x=0; x<16; x++)
				printf("%02x%s", buf[x],
				    (x&3)==3? " " : "");
			printf(" ");
			for (x=0; x<16; x++)
				printf("%c", (buf[x]>=' ' && buf[x]<127)?
				    buf[x] : '.');
			printf("\n");
		}

		addr += sizeof(buf);
	}
}


/*
 *  debugger_unasm():
 *
 *  Dump emulated memory as MIPS instructions.
 */
static void debugger_unasm(struct emul *emul, uint64_t addr, int lines)
{
	struct cpu *c;
	struct memory *m;
	int r;

	if (emul->cpus == NULL) {
		printf("No cpus (?)\n");
		return;
	}
	c = emul->cpus[emul->bootstrap_cpu];
	if (c == NULL) {
		printf("emul->cpus[emul->bootstrap_cpu] = NULL\n");
		return;
	}
	m = emul->cpus[emul->bootstrap_cpu]->mem;

	while (lines -- > 0) {
		unsigned char buf[4];
		memset(buf, 0, sizeof(buf));
		r = memory_rw(c, m, addr, &buf[0], sizeof(buf), MEM_READ,
		    CACHE_NONE | NO_EXCEPTIONS);

		if (c->byte_order == EMUL_BIG_ENDIAN) {
			int tmp;
			tmp = buf[0]; buf[0] = buf[3]; buf[3] = tmp;
			tmp = buf[1]; buf[1] = buf[2]; buf[2] = tmp;
		}

		cpu_disassemble_instr(c, &buf[0], 0, addr);

		addr += sizeof(buf);
	}
}


/*
 *  debugger_tlbdump():
 *
 *  Dump each CPU's TLB contents.
 */
static void debugger_tlbdump(struct emul *emul)
{
	int i, j;

	for (i=0; i<emul->ncpus; i++) {
		printf("cpu%i: (", i);
		if (emul->cpus[i]->cpu_type.isa_level < 3 ||
		    emul->cpus[i]->cpu_type.isa_level == 32)
			printf("index=0x%08x random=0x%08x wired=0x%08x",
			    (int)emul->cpus[i]->coproc[0]->reg[COP0_INDEX],
			    (int)emul->cpus[i]->coproc[0]->reg[COP0_RANDOM],
			    (int)emul->cpus[i]->coproc[0]->reg[COP0_WIRED]);
		else
			printf("index=0x%016llx random=0x%016llx wired=0x%016llx",
			    (long long)emul->cpus[i]->coproc[0]->reg[COP0_INDEX],
			    (long long)emul->cpus[i]->coproc[0]->reg[COP0_RANDOM],
			    (long long)emul->cpus[i]->coproc[0]->reg[COP0_WIRED]);
		printf(")\n");

		for (j=0; j<emul->cpus[i]->cpu_type.nr_of_tlb_entries; j++) {
			if (emul->cpus[i]->cpu_type.mmu_model == MMU3K)
				printf("%3i: hi=0x%08x lo=0x%08x\n",
				    j,
				    (int)emul->cpus[i]->coproc[0]->tlbs[j].hi,
				    (int)emul->cpus[i]->coproc[0]->tlbs[j].lo0);
			else if (emul->cpus[i]->cpu_type.isa_level < 3 ||
			    emul->cpus[i]->cpu_type.isa_level == 32)
				printf("%3i: hi=0x%08x mask=0x%08x lo0=0x%08x lo1=0x%08x\n",
				    j,
				    (int)emul->cpus[i]->coproc[0]->tlbs[j].hi,
				    (int)emul->cpus[i]->coproc[0]->tlbs[j].mask,
				    (int)emul->cpus[i]->coproc[0]->tlbs[j].lo0,
				    (int)emul->cpus[i]->coproc[0]->tlbs[j].lo1);
			else
				printf("%3i: hi=0x%016llx mask=0x%016llx lo0=0x%016llx lo1=0x%016llx\n",
				    j,
				    (long long)emul->cpus[i]->coproc[0]->tlbs[j].hi,
				    (long long)emul->cpus[i]->coproc[0]->tlbs[j].mask,
				    (long long)emul->cpus[i]->coproc[0]->tlbs[j].lo0,
				    (long long)emul->cpus[i]->coproc[0]->tlbs[j].lo1);
		}
	}
}


/*
 *  debugger():
 *
 *  An interractive debugger; reads a command from the terminal, and
 *  executes it.
 */
void debugger(void)
{
	int exit_debugger = 0;
	int ch, i;
	char cmd[MAX_CMD_LEN];
	int cmd_len;
	static uint64_t last_dump_addr = 0xffffffff80000000ULL;
	static uint64_t last_unasm_addr = 0xffffffff80000000ULL;

	cmd[0] = '\0'; cmd_len = 0;

	while (!exit_debugger) {
		/*  Read a line of input:  */
		cmd_len = 0; cmd[0] = '\0';
		printf("mips64emul> ");
		fflush(stdout);

		ch = '\0';
		while (ch != '\n') {
			/*
			 *  TODO: This uses up 100% CPU, maybe that isn't
			 *  very good.  The usleep() call might make it a
			 *  tiny bit nicer on other running processes, but
			 *  it is still very ugly.
			 */
			ch = console_readchar();
			usleep(1);

			if (ch == '\b' && cmd_len > 0) {
				cmd_len --;
				cmd[cmd_len] = '\0';
				printf("\b \b");
				fflush(stdout);
			} else if (ch >= ' ' && cmd_len < MAX_CMD_LEN-1) {
				cmd[cmd_len ++] = ch;
				cmd[cmd_len] = '\0';
				printf("%c", ch);
				fflush(stdout);
			} else if (ch == '\r' || ch == '\n') {
				ch = '\n';
				printf("\n");
			}
		}

		/*  Just pressing Enter will repeat the last cmd:  */
		if (cmd_len == 0 && last_cmd_len != 0) {
			cmd_len = last_cmd_len;
			memcpy(cmd, last_cmd, cmd_len + 1);
		}

		/*  Remove spaces:  */
		while (cmd_len > 0 && cmd[0]==' ')
			memmove(cmd, cmd+1, cmd_len --);
		while (cmd_len > 0 && cmd[cmd_len-1] == ' ')
			cmd[(cmd_len--)-1] = '\0';

		/*  printf("cmd = '%s'\n", cmd);  */

		/*  Remember this cmd:  */
		if (cmd_len > 0) {
			memcpy(last_cmd, cmd, cmd_len + 1);
			last_cmd_len = cmd_len;
		}

		if (strcasecmp(cmd, "c") == 0 ||
		    strcasecmp(cmd, "continue") == 0) {
			exit_debugger = 1;
		} else if (strcasecmp(cmd, "d") == 0 ||
		    strcasecmp(cmd, "dump") == 0) {
			debugger_dump(debugger_emul, last_dump_addr, 8);
			last_dump_addr += 8*16;
		} else if (strncasecmp(cmd, "d ", 2) == 0 ||
		    strncasecmp(cmd, "dump ", 5) == 0) {
			last_dump_addr = strtoll(cmd[1]==' '?
			    cmd + 2 : cmd + 5, NULL, 16);
			debugger_dump(debugger_emul, last_dump_addr, 8);
			last_dump_addr += 8*16;
			/*  Set last cmd to just 'd', so that just pressing
			    enter will cause dump to continue from the last
			    address:  */
			last_cmd_len = 1;
			strcpy(last_cmd, "d");
		} else if (strcasecmp(cmd, "h") == 0 ||
		    strcasecmp(cmd, "?") == 0 || strcasecmp(cmd, "help") == 0) {
			printf("  continue            continues emulation\n");
			printf("  dump [addr]         dumps emulated memory contents in hex and ASCII\n");
			printf("  help                prints this help message\n");
			printf("  itrace              toggles instruction_trace on or off (currently %s)\n",
			    old_instruction_trace? "ON" : "OFF");
			printf("  quit                quits mips64emul\n");
			printf("  registers           dumps all CPUs' register values\n");
			printf("  step                single steps one instruction\n");
			printf("  tlbdump             dumps each CPU's TLB contents\n");
			printf("  trace               toggles show_trace_tree on or off (currently %s)\n",
			    old_show_trace_tree? "ON" : "OFF");
			printf("  unassemble [addr]   dumps emulated memory contents as MIPS instructions\n");
			printf("  version             prints version info\n");
			last_cmd_len = 0;
		} else if (strcasecmp(cmd, "i") == 0 ||
		    strcasecmp(cmd, "itrace") == 0) {
			old_instruction_trace = 1 - old_instruction_trace;
			printf("instruction_trace = %s\n",
			    old_instruction_trace? "ON" : "OFF");
			/*  TODO: how to preserve quiet_mode?  */
			old_quiet_mode = 0;
		} else if (strcasecmp(cmd, "quit") == 0 ||
		    strcasecmp(cmd, "q") == 0) {
			for (i=0; i<debugger_emul->ncpus; i++)
				debugger_emul->cpus[i]->running = 0;
			exit_debugger = 1;
		} else if (strcasecmp(cmd, "r") == 0 ||
		    strcasecmp(cmd, "registers") == 0) {
			for (i=0; i<debugger_emul->ncpus; i++)
				cpu_register_dump(debugger_emul->cpus[i]);
			last_cmd_len = 0;
		} else if (strcasecmp(cmd, "s") == 0 ||
		    strcasecmp(cmd, "step") == 0) {
			return;
		} else if (strcasecmp(cmd, "tl") == 0 ||
		    strcasecmp(cmd, "tlbdump") == 0) {
			debugger_tlbdump(debugger_emul);
		} else if (strcasecmp(cmd, "tr") == 0 ||
		    strcasecmp(cmd, "trace") == 0) {
			old_show_trace_tree = 1 - old_show_trace_tree;
			printf("show_trace_tree = %s\n",
			    old_show_trace_tree? "ON" : "OFF");
			/*  TODO: how to preserve quiet_mode?  */
			old_quiet_mode = 0;
		} else if (strcasecmp(cmd, "u") == 0 ||
		    strcasecmp(cmd, "unassemble") == 0) {
			debugger_unasm(debugger_emul, last_unasm_addr, 16);
			last_unasm_addr += 16 * 4;
		} else if (strncasecmp(cmd, "u ", 2) == 0 ||
		    strncasecmp(cmd, "unassemble ", 11) == 0) {
			last_unasm_addr = strtoll(cmd[1]==' '?
			    cmd + 2 : cmd + 11, NULL, 16);
			debugger_unasm(debugger_emul, last_unasm_addr, 16);
			last_unasm_addr += 16 * 4;
			/*  Set last cmd to just 'u', so that just pressing
			    enter will continue from the last address:  */
			last_cmd_len = 1;
			strcpy(last_cmd, "u");
		} else if (strcasecmp(cmd, "v") == 0 ||
		    strcasecmp(cmd, "version") == 0) {
			printf("%s, %s\n",
#ifdef VERSION
			    VERSION,
#else
			    "(no version)",
#endif
			    COMPILE_DATE);
			last_cmd_len = 0;
		} else if (cmd[0] != '\0') {
			printf("Unknown command '%s'. Type 'help' for help.\n",
			    cmd);
			cmd[0] = '\0';
		}
	}

	debugger_emul->single_step = 0;
	debugger_emul->instruction_trace = old_instruction_trace;
	debugger_emul->show_trace_tree = old_show_trace_tree;
	quiet_mode = old_quiet_mode;
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
	unsigned char bootblock_buf[65536];
	uint64_t bootblock_offset;
	uint64_t bootblock_loadaddr, bootblock_pc;
	int n_blocks, res;

	if (boot_disk_id < 0)
		return;

	switch (emul->emulation_type) {
	case EMULTYPE_DEC:
		/*
		 *  The bootblock for DECstations is 8KB large.  We first read
		 *  the 32-bit word at offset 0x1c. This word tells us the
		 *  starting sector number of the bootblock.
		 *
		 *  The value at offset 0x10 is the load address, and the
		 *  value at 0x14 is the initial PC to use.  These two are
		 *  usually 0x80600000, or similar.
		 *
		 *  The value at offset 0x18 seems to be the number of
		 *  512-byte to read. (TODO: use this)
		 */
		res = diskimage_access(boot_disk_id, 0, 0,
		    minibuf, sizeof(minibuf));

		bootblock_offset = (minibuf[0x1c] + (minibuf[0x1d] << 8)
		  + (minibuf[0x1e] << 16) + (minibuf[0x1f] << 24)) * 512;

		bootblock_loadaddr = minibuf[0x10] + (minibuf[0x11] << 8)
		  + (minibuf[0x12] << 16) + (minibuf[0x13] << 24);

		bootblock_pc = minibuf[0x14] + (minibuf[0x15] << 8)
		  + (minibuf[0x16] << 16) + (minibuf[0x17] << 24);

		n_blocks = minibuf[0x18] + (minibuf[0x19] << 8)
		  + (minibuf[0x1a] << 16) + (minibuf[0x1b] << 24);

		if (n_blocks * 512 > sizeof(bootblock_buf))
			fatal("\nWARNING! Unusually large bootblock (%i bytes, more than 8KB)\n\n",
			    n_blocks * 512);

		res = diskimage_access(boot_disk_id, 0, bootblock_offset,
		    bootblock_buf, sizeof(bootblock_buf));
		if (!res) {
			fatal("ERROR: could not load bootblocks from disk offset 0x%16llx\n",
			    (long long)bootblock_offset);
		}

		/*  Convert loadaddr to uncached:  */
		if ((bootblock_loadaddr & 0xf0000000ULL) != 0x80000000 &&
		    (bootblock_loadaddr & 0xf0000000ULL) != 0xa0000000)
			fatal("\nWARNING! Weird load address 0x%08x.\n\n",
			    (int)bootblock_loadaddr);

		bootblock_loadaddr &= 0x0fffffff;
		bootblock_loadaddr |= 0xa0000000;

		store_buf(cpu, bootblock_loadaddr, (char *)bootblock_buf,
		    sizeof(bootblock_buf));

		bootblock_pc &= 0x0fffffff;
		bootblock_pc |= 0xa0000000;
		cpu->pc = bootblock_pc;

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
	e->emulation_type = EMULTYPE_TEST;
	e->machine = MACHINE_NONE;
	e->prom_emulation = 1;
	e->speed_tricks = 1;
	e->boot_kernel_filename = "";
	e->boot_string_argument = "-a";
	e->ncpus = DEFAULT_NCPUS;
	e->automatic_clock_adjustment = 1;
	e->x11_scaledown = 1;

	return e;
}


/*
 *  emul():
 *
 *	o) Initialize the hardware (RAM, devices, CPUs, ...) which
 *	   will be emulated.
 *	o) Load ROM code and/or other programs into emulated memory.
 *	o) Start running instructions on the bootstrap cpu.
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
	mem = memory_new(DEFAULT_BITS_PER_PAGETABLE, DEFAULT_BITS_PER_MEMBLOCK,
	    memory_amount, DEFAULT_MAX_BITS);
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
	debug(": %s\n", emul->emul_cpu_name);
	for (i=0; i<emul->ncpus; i++)
		emul->cpus[i] = cpu_new(mem, emul, i, emul->emul_cpu_name);

	if (emul->use_random_bootstrap_cpu)
		emul->bootstrap_cpu = random() % emul->ncpus;
	else
		emul->bootstrap_cpu = 0;

	diskimage_dump_info();

	if (emul->use_x11)
		x11_init();

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
	add_pc_dump_points(emul);

	add_symbol_name(&emul->symbol_context,
	    0x9fff0000, 0x10000, "r2k3k_cache", 0);
	symbol_recalc_sizes(&emul->symbol_context);

	if (emul->max_random_cycles_per_chunk > 0)
		debug("using random cycle chunks (1 to %i cycles)\n",
		    emul->max_random_cycles_per_chunk);

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

