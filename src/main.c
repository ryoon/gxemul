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
 *  $Id: main.c,v 1.148 2005-01-05 23:43:58 debug Exp $
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "misc.h"

#include "cpu_types.h"
#include "diskimage.h"
#include "emul.h"


extern int optind;
extern char *optarg;
int extra_argc;
char **extra_argv;


int quiet_mode = 0;


/*
 *  debug():
 *
 *  This can be used instead of manually using printf() all the time,
 *  if fancy output, such as bold text, is needed.
 */
void debug(char *fmt, ...)
{
	va_list argp;
	char buf[DEBUG_BUFSIZE + 1];

	if (quiet_mode)
		return;

	buf[0] = buf[DEBUG_BUFSIZE] = 0;
	va_start(argp, fmt);
	vsnprintf(buf, DEBUG_BUFSIZE, fmt, argp);
	va_end(argp);

	printf("%s", buf);
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
	char buf[DEBUG_BUFSIZE + 1];

	buf[0] = buf[DEBUG_BUFSIZE] = 0;
	va_start(argp, fmt);
	vsnprintf(buf, DEBUG_BUFSIZE, fmt, argp);
	va_end(argp);

	printf("%s", buf);
}


/*
 *  usage():
 *
 *  Print program usage to stdout.
 */
static void usage(char *progname, int longusage)
{
	int i;
	struct cpu_type_def cpu_type_defs[] = CPU_TYPE_DEFS;

	printf("mips64emul-%s  Copyright (C) 2003-2005  Anders Gavare\n",
#ifdef VERSION
	    VERSION
#else
	    "(no version)"
#endif
	    );
	printf("Read the documentation and/or source code for other Copyright notices.\n");
	printf("\nusage: %s [options] file [...]\n", progname);

	if (!longusage) {
		printf("\nRun with  -h  for help on command line options.\n\n");
		goto ret;
	}

	printf("\n");
	printf("  -A x      try to emulate an ARC machine (1=NEC-RD94, 2=PICA-61, 3=NEC-R94,\n");
	printf("            4=Deskstation Tyne, 5=MIPS Magnum, 6=NEC-R98, 7=Olivetti M700,\n");
	printf("            8=NEC-R96)\n");
	printf("  -a        emulate a generic test machine\n");
	printf("  -B        try to emulate a Playstation 2 machine\n");
#ifdef BINTRANS
	printf("  -b        enable dynamic binary translation\n");
#endif
	printf("  -C x      try to emulate a specific CPU. x may be one of the following:\n");

	/*  List CPU names:  */
	i = 0;
	while (cpu_type_defs[i].name != NULL) {
		if ((i % 6) == 0)
			printf("\t");
		printf("\t%s", cpu_type_defs[i].name);
		i++;
		if ((i % 6) == 0 || cpu_type_defs[i].name == NULL)
			printf("\n");
	}

	printf("  -D id     try to emulate a DECstation machine type 'id', where id may be:\n");
	printf("                1=PMAX(3100), 2=3MAX(5000), 3=3MIN(5000), 4=3MAX+(5000,5900),\n");
	printf("                5=5800, 6=5400, 7=MAXINE(5000), 11=5500, 12=5100(MIPSMATE)\n");
	printf("  -d fname  add fname as a disk image. You can add \"xxx:\" as a prefix\n");
	printf("            where xxx is one or more of the following:\n");
	printf("                b     specifies that this is the boot device\n");
	printf("                c     CD-ROM (instead of normal SCSI DISK)\n");
	printf("                d     SCSI DISK (this is the default)\n");
	printf("                r     read-only (don't allow changes to the file)\n");
	printf("                t     SCSI tape\n");
	printf("                0-7   force a specific SCSI ID number\n");
	printf("  -E        try to emulate a Cobalt machine\n");
	printf("  -e        try to emulate a MeshCube\n");
	printf("  -F xx     try to emulate an hpcmips machine, where 'xx' may be:\n");
	printf("                1=Casio BE-300, 2=Casio E-105\n");
	printf("  -f        try to emulate a Sony NeWS MIPS machine\n");
	printf("  -G xx     try to emulate an SGI machine, IPxx\n");
	printf("  -g        try to emulate a NetGear box (WG602)\n");
	printf("  -H        try to emulate a Linksys WRT54G\n");
	printf("  -h        display this help message\n");
	printf("  -I x      emulate clock interrupts at x Hz (affects rtc devices only, not\n");
	printf("            actual runtime speed) (this disables automatic clock adjustments)\n");
	printf("  -i        display each instruction as it is executed\n");
	printf("  -J        disable some speed tricks\n");
	printf("  -j name   set the name of the kernel, for example:\n");
	printf("                -j netbsd          for NetBSD/pmax\n");
	printf("                -j bsd             for OpenBSD/pmax\n");
	printf("                -j vmunix          for Ultrix/RISC\n");
	printf("  -M m      emulate m MBs of physical RAM\n");
	printf("  -m nr     run at most nr instructions (on any cpu)\n");
	printf("  -N        display nr of instructions/second average, at regular intervals\n");
	printf("  -n nr     set nr of CPUs\n");
	printf("  -O        fake netboot (tftp instead of disk), even when a disk image is\n"
	       "            present (for DECstation, SGI, and ARC emulation)\n");
	printf("  -o arg    set the boot argument (for DEC, ARC, or SGI emulation).\n");
	printf("            Default arg for DEC is '-a', for ARC '-aN'.\n");
	printf("  -p pc     add a breakpoint (remember to use the '0x' prefix for hex!)\n");
	printf("  -Q        no built-in PROM emulation  (use this for running ROM images)\n");
	printf("  -q        quiet mode (don't print startup or debug messages)\n");
	printf("  -R        use random bootstrap cpu, instead of nr 0\n");
	printf("  -r        register dumps before every instruction\n");
	printf("  -S        initialize emulated RAM to random bytes, instead of zeroes\n");
	printf("  -s        show opcode usage statistics after simulation\n");
	printf("  -T        start -i and -r traces on accesses to invalid memory addresses\n");
	printf("  -t        show function trace tree\n");
	printf("  -U        enable slow_serial_interrupts_hack_for_linux\n");
#ifdef ENABLE_USERLAND
	printf("  -u x      userland-only (syscall) emulation; 1=NetBSD/pmax, 2=Ultrix/pmax\n");
#endif
	printf("  -V        start up in the single-step debugger, paused\n");
	printf("  -v        verbose debug messages\n");
#ifdef WITH_X11
	printf("  -X        use X11\n");
	printf("  -Y n      scale down framebuffer windows by n x n times\n");
#endif /*  WITH_X11  */
	printf("  -y x      set max_random_cycles_per_chunk to x (experimental)\n");
	printf("  -Z n      set nr of graphics cards, for emulating a dual-head or tripple-head\n"
	       "            environment (only for DECstation emulation)\n");
	printf("  -z disp   add disp as an X11 display to use for framebuffers\n");
	printf("\n");

ret:
	printf("You must specify one or more names of files that you wish to load into memory.\n");
	printf("Supported formats:  ELF a.out ecoff srec syms raw\n");
	printf("where syms is the text produced by running 'nm' (or 'nm -S') on a binary.\n");
	printf("To load a raw binary into memory, add \"address:\" in front of the filename,\n");
	printf("or \"address:skiplen:\".\n");
	printf("Examples:  0xbfc00000:rom.bin         for a raw ROM dump image\n");
	printf("           0xbfc00000:0x100:rom.bin   for an image with 0x100 bytes header\n");
}


/*
 *  get_cmd_args():
 *
 *  Reads command line arguments.
 */
int get_cmd_args(int argc, char *argv[], struct emul *emul)
{
	int ch, using_switch_d = 0, using_switch_o = 0, using_switch_Z = 0;
	char *progname = argv[0];
	int n_cpus_set = 0;

	emul->emul_cpu_name[0] =
	    emul->emul_cpu_name[CPU_NAME_MAXLEN-1] = '\0';

	symbol_init(&emul->symbol_context);

	while ((ch = getopt(argc, argv, "A:aBbC:D:d:EeF:fG:gHhI:iJj:M:m:Nn:Oo:p:QqRrSsTtUu:VvXY:y:Z:z:")) != -1) {
		switch (ch) {
		case 'A':
			emul->emulation_type = EMULTYPE_ARC;
			emul->machine = atoi(optarg);
			break;
		case 'a':
			emul->emulation_type = EMULTYPE_TEST;
			emul->machine = 0;
			break;
		case 'B':
			emul->emulation_type = EMULTYPE_PS2;
			emul->machine = 0;
			break;
		case 'b':
			emul->bintrans_enable = 1;
			break;
		case 'C':
			strncpy(emul->emul_cpu_name, optarg,
			    CPU_NAME_MAXLEN - 1);
			break;
		case 'D':
			emul->emulation_type = EMULTYPE_DEC;
			emul->machine = atoi(optarg);
			break;
		case 'd':
			diskimage_add(optarg);
			using_switch_d = 1;
			break;
		case 'E':
			emul->emulation_type = EMULTYPE_COBALT;
			emul->machine = 0;
			break;
		case 'e':
			emul->emulation_type = EMULTYPE_MESHCUBE;
			emul->machine = 0;
			break;
		case 'F':
			emul->emulation_type = EMULTYPE_HPCMIPS;
			emul->machine = atoi(optarg);
			break;
		case 'f':
			emul->emulation_type = EMULTYPE_SONYNEWS;
			emul->machine = 0;
			break;
		case 'G':
			emul->emulation_type = EMULTYPE_SGI;
			emul->machine = atoi(optarg);
			break;
		case 'g':
			emul->emulation_type = EMULTYPE_NETGEAR;
			emul->machine = 0;
			break;
		case 'H':
			emul->emulation_type = EMULTYPE_WRT54G;
			emul->machine = 0;
			break;
		case 'h':
			usage(progname, 1);
			exit(1);
		case 'I':
			emul->emulated_hz = atoi(optarg);
			emul->automatic_clock_adjustment = 0;
			break;
		case 'i':
			emul->instruction_trace = 1;
			break;
		case 'J':
			emul->speed_tricks = 0;
			break;
		case 'j':
			emul->boot_kernel_filename = malloc(strlen(optarg) + 1);
			if (emul->boot_kernel_filename == NULL) {
				fprintf(stderr, "out of memory\n");
				exit(1);
			}
			strcpy(emul->boot_kernel_filename, optarg);
			break;
		case 'M':
			emul->physical_ram_in_mb = atoi(optarg);
			break;
		case 'm':
			emul->max_instructions = atoi(optarg);
			break;
		case 'N':
			emul->show_nr_of_instructions = 1;
			break;
		case 'n':
			emul->ncpus = atoi(optarg);
			n_cpus_set = 1;
			break;
		case 'O':
			emul->force_netboot = 1;
			break;
		case 'o':
			emul->boot_string_argument = malloc(strlen(optarg) + 1);
			if (emul->boot_string_argument == NULL) {
				fprintf(stderr, "out of memory\n");
				exit(1);
			}
			strcpy(emul->boot_string_argument, optarg);
			using_switch_o = 1;
			break;
		case 'p':
			if (emul->n_breakpoints >= MAX_BREAKPOINTS) {
				fprintf(stderr, "too many breakpoints\n");
				exit(1);
			}
			emul->breakpoint_string[emul->n_breakpoints] =
			    malloc(strlen(optarg) + 1);
			if (emul->breakpoint_string[emul->n_breakpoints] == NULL) {
				fprintf(stderr, "out of memory\n");
				exit(1);
			}
			strcpy(emul->breakpoint_string[emul->n_breakpoints], optarg);
			emul->breakpoint_flags[emul->n_breakpoints] = 0;
			emul->n_breakpoints ++;
			break;
		case 'Q':
			emul->prom_emulation = 0;
			break;
		case 'q':
			quiet_mode = 1;
			break;
		case 'R':
			emul->use_random_bootstrap_cpu = 1;
			break;
		case 'r':
			emul->register_dump = 1;
			break;
		case 'S':
			emul->random_mem_contents = 1;
			break;
		case 's':
			emul->show_opcode_statistics = 1;
			break;
		case 'T':
			emul->trace_on_bad_address = 1;
			break;
		case 't':
			emul->show_trace_tree = 1;
			break;
		case 'U':
			emul->slow_serial_interrupts_hack_for_linux = 1;
			break;
		case 'u':
			emul->userland_emul = atoi(optarg);
			break;
		case 'V':
			emul->single_step = 1;
			break;
		case 'v':
			emul->verbose ++;
			break;
		case 'X':
			emul->use_x11 = 1;
			break;
		case 'Y':
			emul->x11_scaledown = atoi(optarg);
			break;
		case 'y':
			emul->max_random_cycles_per_chunk = atoi(optarg);
			break;
		case 'Z':
			emul->n_gfx_cards = atoi(optarg);
			using_switch_Z = 1;
			break;
		case 'z':
			emul->x11_n_display_names ++;
			emul->x11_display_names = realloc(
			    emul->x11_display_names,
			    emul->x11_n_display_names * sizeof(char *));
			if (emul->x11_display_names == NULL) {
				printf("out of memory\n");
				exit(1);
			}
			emul->x11_display_names[emul->x11_n_display_names-1] =
			    malloc(strlen(optarg) + 1);
			if (emul->x11_display_names
			    [emul->x11_n_display_names-1] == NULL) {
				printf("out of memory\n");
				exit(1);
			}
			strcpy(emul->x11_display_names
			    [emul->x11_n_display_names-1], optarg);
			break;
		default:
			printf("Invalid option.\n");
			usage(progname, 0);
			exit(1);
		}
	}

	argc -= optind;
	argv += optind;

	extra_argc = argc;
	extra_argv = argv;


	/*  -i, -r, -t are pretty verbose:  */

	if (emul->instruction_trace && !emul->verbose) {
		printf("implicitly turning of -q and turning on -v, because of -i\n");
		emul->verbose = 1;
	}

	if (emul->register_dump && !emul->verbose) {
		printf("implicitly turning of -q and turning on -v, because of -r\n");
		emul->verbose = 1;
	}

	if (emul->show_trace_tree && !emul->verbose) {
		printf("implicitly turning of -q and turning on -v, because of -t\n");
		emul->verbose = 1;
	}


	/*  Default nr of CPUs (for SMP systems):  */

	if (!n_cpus_set) {
		if (emul->emulation_type == EMULTYPE_ARC &&
		    emul->machine == MACHINE_ARC_NEC_R96)
			emul->ncpus = 2;

		if (emul->emulation_type == EMULTYPE_ARC &&
		    emul->machine == MACHINE_ARC_NEC_R98)
			emul->ncpus = 4;
	}


	/*  Default CPU type:  */

	if (emul->emulation_type == EMULTYPE_PS2 && !emul->emul_cpu_name[0])
		strcpy(emul->emul_cpu_name, "R5900");

	if (emul->emulation_type == EMULTYPE_DEC && emul->machine > 2 &&
	    !emul->emul_cpu_name[0])
		strcpy(emul->emul_cpu_name, "R3000A");

	if (emul->emulation_type == EMULTYPE_DEC && emul->machine > 1 &&
	    !emul->emul_cpu_name[0])
		strcpy(emul->emul_cpu_name, "R3000");

	if (emul->emulation_type == EMULTYPE_DEC && !emul->emul_cpu_name[0])
		strcpy(emul->emul_cpu_name, "R2000");

	if (emul->emulation_type == EMULTYPE_SONYNEWS
	    && !emul->emul_cpu_name[0])
		strcpy(emul->emul_cpu_name, "R3000");

	if (emul->emulation_type == EMULTYPE_HPCMIPS
	    && !emul->emul_cpu_name[0]) {
		if (emul->machine == HPCMIPS_CASIO_BE300)
			strcpy(emul->emul_cpu_name, "VR4131");
		else if (emul->machine == HPCMIPS_CASIO_E105)
			strcpy(emul->emul_cpu_name, "VR4121");
		else {
			printf("Unimplemented HPCMIPS model?\n");
			exit(1);
		}
	}

	if (emul->emulation_type == EMULTYPE_COBALT && !emul->emul_cpu_name[0])
		strcpy(emul->emul_cpu_name, "RM5200");

	if (emul->emulation_type == EMULTYPE_MESHCUBE &&
	    !emul->emul_cpu_name[0])
		strcpy(emul->emul_cpu_name, "R4400");
		/*  TODO:  Should be AU1500, but Linux doesn't like
			the absence of caches in the emulator  */

	if (emul->emulation_type == EMULTYPE_NETGEAR && !emul->emul_cpu_name[0])
		strcpy(emul->emul_cpu_name, "RC32334");

	if (emul->emulation_type == EMULTYPE_WRT54G && !emul->emul_cpu_name[0])
		strcpy(emul->emul_cpu_name, "BCM4712");

	if (emul->emulation_type == EMULTYPE_ARC &&
	    emul->machine == MACHINE_ARC_JAZZ_PICA && !emul->emul_cpu_name[0])
		strcpy(emul->emul_cpu_name, "R4000");

	if (emul->emulation_type == EMULTYPE_ARC && !emul->emul_cpu_name[0])
		strcpy(emul->emul_cpu_name, "R4400");

	if (emul->emulation_type == EMULTYPE_SGI && emul->machine <= 12 &&
	    !emul->emul_cpu_name[0])
		strcpy(emul->emul_cpu_name, "R3000");

	if (emul->emulation_type == EMULTYPE_SGI && emul->machine == 35 &&
	    !emul->emul_cpu_name[0])
		strcpy(emul->emul_cpu_name, "R12000");

	if (emul->emulation_type == EMULTYPE_SGI && (emul->machine == 25 ||
	    emul->machine == 27 || emul->machine == 28 || emul->machine == 30
	    || emul->machine == 32) && !emul->emul_cpu_name[0])
		strcpy(emul->emul_cpu_name, "R10000");

	if (emul->emulation_type == EMULTYPE_SGI && (emul->machine == 21 ||
	    emul->machine == 26) && !emul->emul_cpu_name[0])
		strcpy(emul->emul_cpu_name, "R8000");

	if (emul->emulation_type == EMULTYPE_SGI && emul->machine == 24 &&
	    !emul->emul_cpu_name[0])
		strcpy(emul->emul_cpu_name, "R5000");

	/*  SGIs should probably work with R4000, R4400 or R5000 or similar.  */
	if (emul->emulation_type == EMULTYPE_SGI && !emul->emul_cpu_name[0])
		strcpy(emul->emul_cpu_name, "R4400");

	if (!emul->emul_cpu_name[0])
		strcpy(emul->emul_cpu_name, CPU_DEFAULT);


	/*  Default memory size:  */

	if (emul->emulation_type == EMULTYPE_PS2 && emul->physical_ram_in_mb == 0)
		emul->physical_ram_in_mb = 32;

	if (emul->emulation_type == EMULTYPE_SGI && emul->physical_ram_in_mb == 0)
		emul->physical_ram_in_mb = 48;

	if (emul->emulation_type == EMULTYPE_HPCMIPS &&
	    emul->physical_ram_in_mb == 0) {
		switch (emul->machine) {
		case HPCMIPS_CASIO_BE300:
			emul->physical_ram_in_mb = 16;
			break;
		case HPCMIPS_CASIO_E105:
			emul->physical_ram_in_mb = 32;
			break;
		}
	}

	if (emul->emulation_type == EMULTYPE_MESHCUBE && emul->physical_ram_in_mb == 0)
		emul->physical_ram_in_mb = 64;

	if (emul->emulation_type == EMULTYPE_NETGEAR && emul->physical_ram_in_mb == 0)
		emul->physical_ram_in_mb = 16;

	if (emul->emulation_type == EMULTYPE_WRT54G) {
		emul->dbe_on_nonexistant_memaccess = 0;
		if (emul->physical_ram_in_mb == 0)
			emul->physical_ram_in_mb = 32;
	}

	if (emul->emulation_type == EMULTYPE_ARC &&
	    emul->machine == MACHINE_ARC_JAZZ_PICA &&
	    emul->physical_ram_in_mb == 0)
		emul->physical_ram_in_mb = 64;

	if (emul->emulation_type == EMULTYPE_ARC &&
	    emul->machine == MACHINE_ARC_JAZZ_M700 &&
	    emul->physical_ram_in_mb == 0)
		emul->physical_ram_in_mb = 64;

	if (emul->emulation_type == EMULTYPE_ARC && emul->physical_ram_in_mb == 0)
		emul->physical_ram_in_mb = 48;

	if (emul->emulation_type == EMULTYPE_DEC &&
	    emul->machine == MACHINE_PMAX_3100 && emul->physical_ram_in_mb == 0)
		emul->physical_ram_in_mb = 24;

	if (emul->physical_ram_in_mb == 0)
		emul->physical_ram_in_mb = DEFAULT_RAM_IN_MB;


	/*  Default Boot string arguments:  */

	if (emul->emulation_type == EMULTYPE_ARC &&
	    !using_switch_o) {
		emul->boot_string_argument = "-aN";
	}

	if (emul->emulation_type == EMULTYPE_DEC &&
	    !using_switch_o) {
		emul->boot_string_argument = "-a";
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
		if (emul->emulation_type == EMULTYPE_DEC && using_switch_d) {
			emul->booting_from_diskimage = 1;
		} else {
			usage(progname, 0);
			printf("\nNo filename given. Aborting.\n");
			exit(1);
		}
	} else if (emul->boot_kernel_filename[0] == '\0') {
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

		emul->boot_kernel_filename = malloc(strlen(s2) + 1);
		if (emul->boot_kernel_filename == NULL) {
			fprintf(stderr, "out of memory\n");
			exit(1);
		}
		strcpy(emul->boot_kernel_filename, s2);
	}

	if (emul->n_gfx_cards < 0 || emul->n_gfx_cards > 3) {
		fprintf(stderr, "Bad number of gfx cards (-Z).\n");
		exit(1);
	}

	if (emul->ncpus < 1) {
		fprintf(stderr, "Too few cpus (-n).\n");
		exit(1);
	}

#if 0
	if (emul->show_opcode_statistics && emul->bintrans_enable) {
		fprintf(stderr,
		    "Cannot do both dynamic binary translation and exact opcode statistics.\n"
		    "Aborting.\n");
		exit(1);
	}
#endif

	if (emul->bintrans_enable) {
		emul->speed_tricks = 0;
		/*  TODO: Print a warning about this?  */
	}

	if (emul->ncpus > 1 && emul->bintrans_enable) {
		fprintf(stderr, "FATAL: Cannot use bintrans with more than one cpu.\n");
		exit(1);
	}

	if (emul->n_breakpoints > 0 && emul->bintrans_enable) {
		fprintf(stderr, "Breakpoints and dynamic translation don't work too well together right now.\n");
		exit(1);
	}

#ifndef BINTRANS
	if (emul->bintrans_enable) {
		fprintf(stderr, "WARNING: %s was compiled without bintrans support. Ignoring -b.\n", progname);
		emul->bintrans_enable = 0;
	}
#endif

#ifndef ENABLE_USERLAND
	if (emul->userland_emul) {
		fprintf(stderr, "FATAL: Userland emulation must be enabled at configure time (--userland).\n");
		exit(1);
	}
#endif

#ifndef WITH_X11
	if (emul->use_x11) {
		fprintf(stderr, "WARNING: %s was compiled without X11 support. Ignoring -X.\n", progname);
		emul->use_x11 = 0;
	}
#endif

	if (!using_switch_Z && !emul->use_x11)
		emul->n_gfx_cards = 0;

	emul->bintrans_enabled_from_start = emul->bintrans_enable;

	return 0;
}


/*
 *  main():
 */
int main(int argc, char *argv[])
{
	struct emul *emul;

	emul = emul_new();
	if (emul == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}

	get_cmd_args(argc, argv, emul);

	emul_start(emul);

	return 0;
}

