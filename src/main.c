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
 *  $Id: main.c,v 1.184 2005-01-29 09:22:17 debug Exp $
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "diskimage.h"
#include "emul.h"
#include "machine.h"
#include "memory.h"
#include "mips_cpu.h"
#include "mips_cpu_types.h"
#include "misc.h"


extern volatile int single_step;
extern int force_debugger_at_exit;

extern int optind;
extern char *optarg;
int extra_argc;
char **extra_argv;


/*****************************************************************************
 *
 *  NOTE:  debug(), fatal(), and debug_indentation() are not re-entrant.
 *         The global variable quiet_mode can be used to suppress the output
 *         of debug(), but not the output of fatal().
 *
 *****************************************************************************/

int verbose = 0;
int quiet_mode = 0;

static int debug_indent = 0;
static int debug_currently_at_start_of_line = 1;


/*
 *  va_debug():
 *
 *  Used internally by debug() and fatal().
 */
static void va_debug(va_list argp, char *fmt)
{
	char buf[DEBUG_BUFSIZE + 1];
	int i;

	buf[0] = buf[DEBUG_BUFSIZE] = 0;
	vsnprintf(buf, DEBUG_BUFSIZE, fmt, argp);

	if (debug_currently_at_start_of_line) {
		for (i=0; i<debug_indent; i++)
			printf(" ");
	}

	printf("%s", buf);
	debug_currently_at_start_of_line = 0;

	if (buf[strlen(buf) - 1] == '\n' ||
	    buf[strlen(buf) - 1] == '\r')
		debug_currently_at_start_of_line = 1;
}


/*
 *  debug_indentation():
 *
 *  Modify the debug indentation.
 */
void debug_indentation(int diff)
{
	debug_indent += diff;
	if (debug_indent < 0)
		fprintf(stderr, "WARNING: debug_indent less than 0!\n");
}


/*
 *  debug():
 *
 *  Debug output (ignored if quiet_mode is set).
 */
void debug(char *fmt, ...)
{
	va_list argp;

	if (quiet_mode)
		return;

	va_start(argp, fmt);
	va_debug(argp, fmt);
	va_end(argp);
}


/*
 *  fatal():
 *
 *  Fatal works like debug(), but doesn't care about the quiet_mode
 *  setting.
 */
void fatal(char *fmt, ...)
{
	va_list argp;

	va_start(argp, fmt);
	va_debug(argp, fmt);
	va_end(argp);
}


/*****************************************************************************/


/*
 *  usage():
 *
 *  Prints program usage to stdout.
 */
static void usage(char *progname, int longusage)
{
	printf("mips64emul");
#ifdef VERSION
	printf("-" VERSION);
#endif
	printf("   Copyright (C) 2003-2005  Anders Gavare\n");
	printf("Read the source code and/or documentation for "
	    "other Copyright messages.\n");

	printf("\nusage: %s [machine and general options] [ file [file ..] ]\n",
	    progname);
	printf("   or  %s [general options] @configfile [@configfile ..]\n",
	    progname);

	if (!longusage) {
		printf("\nRun  %s -h  for help on command line options.\n\n",
		    progname);
		goto ret;
	}

	printf("\nMachine selection options:\n");
	printf("  -A x      try to emulate an ARC machine (1=NEC-RD94, "
	    "2=PICA-61, 3=NEC-R94,\n");
	printf("            4=Deskstation Tyne, 5=MIPS Magnum, 6=NEC-R98, "
	    "7=Olivetti M700,\n");
	printf("            8=NEC-R96)\n");
	printf("  -a        emulate a generic test machine\n");
	printf("  -D id     try to emulate a DECstation machine type 'id', "
	    "where id may be:\n");
	printf("                1=PMAX(3100), 2=3MAX(5000), 3=3MIN(5000), "
	    "4=3MAX+(5000,5900),\n");
	printf("                5=5800, 6=5400, 7=MAXINE(5000), 11=5500, "
	    "12=5100(MIPSMATE)\n");
	printf("  -E t      try to emulate machine type t. (Use -H to get "
	    "a list of types.)\n");
	printf("  -e st     try to emulate machine subtype st. (Use this "
	    "with -E.)\n");
	printf("  -F xx     try to emulate an hpcmips machine, where 'xx'"
	    " may be:\n");
	printf("                1=Casio BE-300, 2=Casio E-105\n");
	printf("  -G xx     try to emulate an SGI machine, IPxx\n");

	printf("\nOther machine specific options:\n");
#ifdef BINTRANS
	printf("  -b        enable dynamic binary translation\n");
#endif
	printf("  -C x      try to emulate a specific CPU. (Use -H to get a "
	    "list of types.)\n");
	printf("  -d fname  add fname as a disk image. You can add \"xxx:\""
	    " as a prefix\n");
	printf("            where xxx is one or more of the following:\n");
	printf("                b     specifies that this is the boot"
	    " device\n");
	printf("                c     CD-ROM (instead of normal SCSI DISK)\n");
	printf("                d     SCSI DISK (this is the default)\n");
	printf("                r     read-only (don't allow changes to the"
	    " file)\n");
	printf("                t     SCSI tape\n");
	printf("                0-7   force a specific SCSI ID number\n");
	printf("  -I x      emulate clock interrupts at x Hz (affects"
	    " rtc devices only, not\n");
	printf("            actual runtime speed) (this disables automatic"
	    " clock adjustments)\n");
	printf("  -i        display each instruction as it is executed\n");
	printf("  -J        disable some speed tricks\n");
	printf("  -j name   set the name of the kernel, for example:\n");
	printf("                -j netbsd          for NetBSD/pmax\n");
	printf("                -j bsd             for OpenBSD/pmax\n");
	printf("                -j vmunix          for Ultrix/RISC\n");
	printf("  -M m      emulate m MBs of physical RAM\n");
	printf("  -m nr     run at most nr instructions (on any cpu)\n");
	printf("  -N        display nr of instructions/second average, at"
	    " regular intervals\n");
	printf("  -n nr     set nr of CPUs\n");
	printf("  -O        force netboot (tftp instead of disk), even when"
	    " a disk image is\n"
	    "            present (for DECstation, SGI, and ARC emulation)\n");
	printf("  -o arg    set the boot argument (for DEC, ARC, or SGI"
	    " emulation).\n");
	printf("            Default arg for DEC is '-a', for ARC '-aN'.\n");
	printf("  -p pc     add a breakpoint (remember to use the '0x' "
	    "prefix for hex!)\n");
	printf("  -Q        no built-in PROM emulation  (use this for "
	    "running ROM images)\n");
	printf("  -R        use random bootstrap cpu, instead of nr 0\n");
	printf("  -r        register dumps before every instruction\n");
	printf("  -S        initialize emulated RAM to random bytes, "
	    "instead of zeroes\n");
	printf("  -s        show opcode usage statistics after simulation\n");
	printf("  -T        enter the single-step debugger on "
	    "unimplemented memory accesses\n");
	printf("  -t        show function trace tree\n");
	printf("  -U        enable slow_serial_interrupts_hack_for_linux\n");
#ifdef ENABLE_USERLAND
	printf("  -u x      userland-only (syscall) emulation; 1=NetBSD/pmax,"
	    " 2=Ultrix/pmax\n");
#endif
#ifdef WITH_X11
	printf("  -X        use X11\n");
	printf("  -Y n      scale down framebuffer windows by n x n times\n");
#endif /*  WITH_X11  */
	printf("  -y x      set max_random_cycles_per_chunk to x"
	    " (experimental)\n");
	printf("  -Z n      set nr of graphics cards, for emulating a "
	    "dual-head or tripple-head\n"
	    "            environment (only for DECstation emulation)\n");
	printf("  -z disp   add disp as an X11 display to use for "
	    "framebuffers\n");

	printf("\nGeneral options:\n");
	printf("  -H        display a list of possible CPU and "
	    "machine types\n");
	printf("  -h        display this help message\n");
	printf("  -K        force the debugger to be entered at the end "
	    "of a simulation\n");
	printf("  -q        quiet mode (don't print startup messages)\n");
	printf("  -V        start up in the single-step debugger, paused\n");
	printf("  -v        verbose debug messages\n");
	printf("\n");
ret:
	printf("You must specify one or more names of files that you wish "
	    "to load into memory.\n"
	    "Supported formats:  ELF a.out ecoff srec syms raw\n"
	    "where syms is the text produced by running 'nm' (or 'nm -S') "
	    "on a binary.\n"
	    "To load a raw binary into memory, add \"address:\" in front "
	    "of the filename,\n"
	    "or \"address:skiplen:\".\n"
	    "Examples:  0xbfc00000:rom.bin         for a raw ROM dump image\n"
	    "           0xbfc00000:0x100:rom.bin   for an image with "
	    "0x100 bytes header\n");
}


/*
 *  show_cpus_and_machine_types():
 */
void show_cpus_and_machine_types(void)
{
	int i, iadd = 8;
	struct mips_cpu_type_def cpu_type_defs[] = CPU_TYPE_DEFS;

	printf("Available CPU names:\n");
	i = 0;
	while (cpu_type_defs[i].name != NULL) {
		printf("\t%s", cpu_type_defs[i].name);
		i++;
		if ((i % 7) == 0 || cpu_type_defs[i].name == NULL)
			printf("\n");
	}

	printf("Most of these are bogus, and not really implemented.\n");
	printf("\nAvailable machine types (with aliases) and "
	    "their subtypes:\n");

	debug_indentation(iadd);
	machine_list_available_types();
	debug_indentation(-iadd);

	printf("\n");
	printf("Use the alias when selecting a machine type or subtype,\n"
	    "not the real name.\n");
}


/*
 *  get_cmd_args():
 *
 *  Reads command line arguments.
 */
int get_cmd_args(int argc, char *argv[], struct emul *emul)
{
	int ch, res, using_switch_d = 0, using_switch_Z = 0;
	char *progname = argv[0];
	char *type = NULL, *subtype = NULL;
	int n_cpus_set = 0;
	int msopts = 0;		/*  Machine-specific options used  */
	struct machine *m = emul_add_machine(emul, "default");

	while ((ch = getopt(argc, argv, "A:aBbC:D:d:E:e:F:G:HhI:iJj:KM:m:"
	    "Nn:Oo:p:QqRrSsTtUu:VvXY:y:Z:z:")) != -1) {
		switch (ch) {
		case 'A':
			m->machine_type = MACHINE_ARC;
			m->machine_subtype = atoi(optarg);
			break;
		case 'a':
			m->machine_type = MACHINE_TEST;
			m->machine_subtype = 0;
			break;
		case 'B':
			m->machine_type = MACHINE_PS2;
			m->machine_subtype = 0;
			break;
		case 'b':
			m->bintrans_enable = 1;
			msopts = 1;
			break;
		case 'C':
			m->cpu_name = strdup(optarg);
			msopts = 1;
			break;
		case 'D':
			m->machine_type = MACHINE_DEC;
			m->machine_subtype = atoi(optarg);
			break;
		case 'd':
			diskimage_add(m, optarg);
			using_switch_d = 1;
			msopts = 1;
			break;
		case 'E':
			type = optarg;
			msopts = 1;
			break;
		case 'e':
			subtype = optarg;
			msopts = 1;
			break;
		case 'F':
			m->machine_type = MACHINE_HPCMIPS;
			m->machine_subtype = atoi(optarg);
			break;
		case 'G':
			m->machine_type = MACHINE_SGI;
			m->machine_subtype = atoi(optarg);
			break;
		case 'H':
			show_cpus_and_machine_types();
			exit(1);
		case 'h':
			usage(progname, 1);
			exit(1);
		case 'I':
			m->emulated_hz = atoi(optarg);
			m->automatic_clock_adjustment = 0;
			msopts = 1;
			break;
		case 'i':
			m->instruction_trace = 1;
			msopts = 1;
			break;
		case 'J':
			m->speed_tricks = 0;
			msopts = 1;
			break;
		case 'j':
			m->boot_kernel_filename = strdup(optarg);
			if (m->boot_kernel_filename == NULL) {
				fprintf(stderr, "out of memory\n");
				exit(1);
			}
			msopts = 1;
			break;
		case 'K':
			force_debugger_at_exit = 1;
			break;
		case 'M':
			m->physical_ram_in_mb = atoi(optarg);
			msopts = 1;
			break;
		case 'm':
			m->max_instructions = atoi(optarg);
			msopts = 1;
			break;
		case 'N':
			m->show_nr_of_instructions = 1;
			msopts = 1;
			break;
		case 'n':
			m->ncpus = atoi(optarg);
			n_cpus_set = 1;
			msopts = 1;
			break;
		case 'O':
			m->force_netboot = 1;
			msopts = 1;
			break;
		case 'o':
			m->boot_string_argument = strdup(optarg);
			if (m->boot_string_argument == NULL) {
				fprintf(stderr, "out of memory\n");
				exit(1);
			}
			msopts = 1;
			break;
		case 'p':
			if (m->n_breakpoints >= MAX_BREAKPOINTS) {
				fprintf(stderr, "too many breakpoints\n");
				exit(1);
			}
			m->breakpoint_string[m->n_breakpoints] = strdup(optarg);
			if (m->breakpoint_string[m->n_breakpoints] == NULL) {
				fprintf(stderr, "out of memory\n");
				exit(1);
			}
			m->breakpoint_flags[m->n_breakpoints] = 0;
			m->n_breakpoints ++;
			msopts = 1;
			break;
		case 'Q':
			m->prom_emulation = 0;
			msopts = 1;
			break;
		case 'q':
			quiet_mode = 1;
			break;
		case 'R':
			m->use_random_bootstrap_cpu = 1;
			msopts = 1;
			break;
		case 'r':
			m->register_dump = 1;
			msopts = 1;
			break;
		case 'S':
			m->random_mem_contents = 1;
			msopts = 1;
			break;
		case 's':
			m->show_opcode_statistics = 1;
			msopts = 1;
			break;
		case 'T':
			m->single_step_on_bad_addr = 1;
			msopts = 1;
			break;
		case 't':
			m->show_trace_tree = 1;
			msopts = 1;
			break;
		case 'U':
			m->slow_serial_interrupts_hack_for_linux = 1;
			msopts = 1;
			break;
		case 'u':
			m->userland_emul = atoi(optarg);
			msopts = 1;
			break;
		case 'V':
			single_step = 1;
			break;
		case 'v':
			verbose ++;
			break;
		case 'X':
			m->use_x11 = 1;
			msopts = 1;
			break;
		case 'Y':
			m->x11_scaledown = atoi(optarg);
			msopts = 1;
			break;
		case 'y':
			m->max_random_cycles_per_chunk = atoi(optarg);
			msopts = 1;
			break;
		case 'Z':
			m->n_gfx_cards = atoi(optarg);
			using_switch_Z = 1;
			msopts = 1;
			break;
		case 'z':
			m->x11_n_display_names ++;
			m->x11_display_names = realloc(
			    m->x11_display_names,
			    m->x11_n_display_names * sizeof(char *));
			if (m->x11_display_names == NULL) {
				printf("out of memory\n");
				exit(1);
			}
			m->x11_display_names[m->x11_n_display_names-1] =
			    strdup(optarg);
			if (m->x11_display_names
			    [m->x11_n_display_names-1] == NULL) {
				printf("out of memory\n");
				exit(1);
			}
			msopts = 1;
			break;
		default:
			printf("Invalid option.\n");
			usage(progname, 0);
			exit(1);
		}
	}

	if (type != NULL) {
		if (subtype == NULL)
			subtype = "";
		res = machine_name_to_type(type, subtype,
		    &m->machine_type, &m->machine_subtype);
		if (!res)
			exit(1);
	}

	argc -= optind;
	argv += optind;

	extra_argc = argc;
	extra_argv = argv;


	if (m->machine_type == MACHINE_NONE && msopts) {
		printf("Machine specific options used directly on the command "
		    "line, but no machine\nemulation specified?\n");
		exit(1);
	}


	/*  -i, -r, -t are pretty verbose:  */

	if (m->instruction_trace && !verbose) {
		printf("implicitly turning of -q and turning on -v, "
		    "because of -i\n");
		verbose = 1;
	}

	if (m->register_dump && !verbose) {
		printf("implicitly turning of -q and turning on -v, "
		    "because of -r\n");
		verbose = 1;
	}

	if (m->show_trace_tree && !verbose) {
		printf("implicitly turning of -q and turning on -v, "
		    "because of -t\n");
		verbose = 1;
	}


	/*
	 *  Usually, an executable filename must be supplied.
	 *
	 *  However, it is possible to boot directly from a harddisk image
	 *  file. If no kernel is supplied, and the emulation mode is set to
	 *  DECstation emulation, and there is a diskimage, then try to boot
	 *  from that.
	 */
	if (extra_argc == 0) {
		if (using_switch_d) {
			/*  Booting directly from a disk image...  */
		} else {
			usage(progname, 0);
			printf("\nNo filename given. Aborting.\n");
			exit(1);
		}
	} else if (m->boot_kernel_filename[0] == '\0') {
		/*
		 *  Default boot_kernel_filename is "", which can be overriden
		 *  by the -j command line option.  If it is still "" here,
		 *  and we're not booting directly from a disk image, then
		 *  try to set it to the last part of the last file name
		 *  given on the command line. (Last part = the stuff after
		 *  the last slash.)
		 */
		char *s = extra_argv[extra_argc - 1];
		char *s2;

		s2 = strrchr(s, '/');
		if (s2 == NULL)
			s2 = s;
		else
			s2 ++;

		m->boot_kernel_filename = strdup(s2);
		if (m->boot_kernel_filename == NULL) {
			fprintf(stderr, "out of memory\n");
			exit(1);
		}
	}

	if (m->n_gfx_cards < 0 || m->n_gfx_cards > 3) {
		fprintf(stderr, "Bad number of gfx cards (-Z).\n");
		exit(1);
	}

	if (m->bintrans_enable) {
		m->speed_tricks = 0;
		/*  TODO: Print a warning about this?  */
	}

	if (m->n_breakpoints > 0 && m->bintrans_enable) {
		fprintf(stderr, "Breakpoints and dynamic translation "
		    "don't work too well together right now.\n");
		exit(1);
	}

#ifndef BINTRANS
	if (m->bintrans_enable) {
		fprintf(stderr, "WARNING: %s was compiled without "
		    "bintrans support. Ignoring -b.\n", progname);
		m->bintrans_enable = 0;
	}
#endif

#ifndef ENABLE_USERLAND
	if (m->userland_emul) {
		fprintf(stderr, "FATAL: Userland emulation must be "
		    "enabled at configure time (--userland).\n");
		exit(1);
	}
#endif

#ifndef WITH_X11
	if (m->use_x11) {
		fprintf(stderr, "WARNING: %s was compiled without "
		    "X11 support. Ignoring -X.\n", progname);
		m->use_x11 = 0;
	}
#endif

	if (!using_switch_Z && !m->use_x11)
		m->n_gfx_cards = 0;

	m->bintrans_enabled_from_start = m->bintrans_enable;

	return 0;
}


/*
 *  main():
 *
 *  Two kinds of emulations are started from here:
 *
 *	o)  Simple emulations, using command line arguments, compatible with
 *	    earlier version of mips64emul.
 *
 *	o)  Emulations set up by parsing special config files. (0 or more.)
 */
int main(int argc, char *argv[])
{
	struct emul **emuls;
	int n_emuls;
	int i;

	machine_init();

	emuls = malloc(sizeof(struct emul *));
	if (emuls == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}

	/*  Allocate space for a simple emul setup:  */
	n_emuls = 1;
	emuls[0] = emul_new();
	if (emuls[0] == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}

	get_cmd_args(argc, argv, emuls[0]);

	/*  Print startup message:  */
	debug("mips64emul");
#ifdef VERSION
	debug("-" VERSION);
#endif
	debug("   Copyright (C) 2003-2005  Anders Gavare\n");
	debug("Read the source code and/or documentation for "
	    "other Copyright messages.\n\n");

	if (emuls[0]->machines[0]->machine_type == MACHINE_NONE)
		n_emuls --;

	/*  Simple initialization, from command line arguments:  */
	if (n_emuls > 0)
		emul_simple_init(emuls[0]);

	/*  Initialize emulations from config files:  */
	for (i=1; i<argc; i++) {
		if (argv[i][0] == '@') {
			n_emuls ++;
			emuls = realloc(emuls, sizeof(struct emul *) * n_emuls);
			if (emuls == NULL) {
				fprintf(stderr, "out of memory\n");
				exit(1);
			}
			emuls[n_emuls - 1] =
			    emul_create_from_configfile(argv[i] + 1);
		}
	}

	if (n_emuls == 0) {
		fprintf(stderr, "No emulations defined.\n");
		exit(1);
	}

	/*  Run all emulations:  */
	emul_run(emuls, n_emuls);

	return 0;
}

