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
 *  $Id: main.c,v 1.6 2003-11-24 04:27:23 debug Exp $
 *
 *  TODO:  Move out stuff into structures, separating things from main()
 *         completely.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "misc.h"
#include "diskimage.h"


extern int optind;
extern char *optarg;
int extra_argc;
char **extra_argv;


/*
 *  Global emulation variables:
 */

char emul_cpu_name[50];

int emulation_type = EMULTYPE_NONE;
int machine = MACHINE_NONE;
char *machine_name = NULL;

int random_mem_contents = 0;
int physical_ram_in_mb = 0;

/*  PC Dumppoints: if the PC value ever matches one of these, we set
	register_dump = instruction_trace = 1  */
int n_dumppoints = 0;
uint64_t dumppoint_pc[MAX_PC_DUMPPOINTS];
int dumppoint_flag_r[MAX_PC_DUMPPOINTS];

int register_dump = 0;
int instruction_trace = 0;
int trace_on_bad_address = 0;
int show_nr_of_instructions = 0;
int max_instructions = 0;
int emulated_ips = 0;
int show_opcode_statistics = 0;
int speed_tricks = 1;

int bootstrap_cpu;
int use_random_bootstrap_cpu = 0;
int ncpus = DEFAULT_NCPUS;
struct cpu **cpus = NULL;

int show_trace_tree = 0;
int tlb_dump = 0;

int use_x11 = 0;
int x11_scaledown = 1;
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
/*	printf(EMUL_DEBUG"%s"EMUL_DEBUG_END, buf);  */
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
	/*  printf(EMUL_DEBUG"%s"EMUL_DEBUG_END, buf);  */
}


/*
 *  usage():
 *
 *  Print program usage to stdout.
 */
void usage(char *progname)
{
	int i;
	struct cpu_type_def cpu_type_defs[] = CPU_TYPE_DEFS;

	printf("usage: %s [options] file [...]\n", progname);
	printf("  -A        try to emulate a generic ARC machine (default CPU = R4000)\n");
	printf("  -B        try to emulate a Playstation 2 machine (default CPU = R5900)\n");
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

	printf("  -d fname  add fname as a disk image\n");
	printf("  -D id     try to emulate a DECstation machine type 'id', where id may be:\n");
	printf("                1=PMAX(3100), 2=3MAX(5000), 3=3MIN(5000), 4=3MAX+(5000,5900),\n");
	printf("                5=5800, 6=5400, 7=MAXINE(5000), 11=5500, 12=5100(MIPSMATE)\n");
	printf("  -E        try to emulate a Cobalt machine (default CPU = RM5200)\n");
	printf("  -F        try to emulate a hpcmips machine\n");
	printf("  -G        try to emulate an SGI machine (Indy)\n");
	printf("  -H        initialize emulated RAM to random bytes, instead of zeroes\n");
	printf("  -h        display this help message\n");
	printf("  -I x      set emulation clock speed to x Hz (affects rtc devices only, not\n");
	printf("            actual emulation speed) (default depends on CPU and emulation mode)\n");
	printf("  -i        display each instruction as it is executed\n");
	printf("  -J        disable speed tricks\n");
	printf("  -M m      emulate m MBs of physical RAM  (default = %i)\n", DEFAULT_RAM_IN_MB);
	printf("  -m nr     run at most nr instructions (on any cpu)\n");
	printf("  -N        display nr of instructions/second average, at regular intervals\n");
	printf("  -n nr     set nr of CPUs  (default = %i)\n", DEFAULT_NCPUS);
	printf("  -P pc     add a PC dumppoint.  (if the PC register ever holds this value,\n");
	printf("            register dumping (-r) and instruction trace (-i) are enabled)\n");
	printf("  -p pc     same as -P, but only enables -i, not -r\n");
	printf("  -q        quiet mode (don't print debug messages)\n");
	printf("  -R        use random bootstrap cpu, instead of nr 0\n");
	printf("  -r        register dumps before every instruction\n");
	printf("  -s        show opcode usage statistics after simulation\n");
	printf("  -T        start -i and -r traces on accesses to invalid memory addresses\n");
	printf("  -t        show function trace tree\n");
	printf("  -U        dump TLB entries when the TLB is used for lookups\n");
#ifdef WITH_X11
	printf("  -X        use X11\n");
	printf("  -Y n      scale down framebuffer windows by n x n times  (default = %i)\n", x11_scaledown);
#endif /*  WITH_X11  */
}


/*
 *  get_cmd_args():
 *
 *  Reads command line arguments.
 */
int get_cmd_args(int argc, char *argv[])
{
	int ch, i;
	uint64_t new_pc_dumppoint;
	char *progname = argv[0];

	emul_cpu_name[0] = emul_cpu_name[sizeof(emul_cpu_name)-1] = '\0';

	symbol_init();

	while ((ch = getopt(argc, argv, "ABC:D:d:EFGHhI:iJM:m:Nn:P:p:qRrsTtUXY:")) != -1) {
		switch (ch) {
		case 'A':
			emulation_type = EMULTYPE_ARC;
			machine = 0;
			break;
		case 'B':
			emulation_type = EMULTYPE_PS2;
			machine = 0;
			break;
		case 'C':
			strncpy(emul_cpu_name, optarg, sizeof(emul_cpu_name)-1);
			break;
		case 'D':
			emulation_type = EMULTYPE_DEC;
			machine = atoi(optarg);
			break;
		case 'd':
			diskimage_add(optarg);
			break;
		case 'E':
			emulation_type = EMULTYPE_COBALT;
			machine = 0;
			break;
		case 'F':
			emulation_type = EMULTYPE_HPCMIPS;
			machine = 0;
			break;
		case 'G':
			emulation_type = EMULTYPE_SGI;
			machine = 0;
			break;
		case 'H':
			random_mem_contents = 1;
			break;
		case 'I':
			emulated_ips = atoi(optarg);
			break;
		case 'i':
			instruction_trace = 1;
			break;
		case 'J':
			speed_tricks = 0;
			break;
		case 'M':
			physical_ram_in_mb = atoi(optarg);
			break;
		case 'm':
			max_instructions = atoi(optarg);
			break;
		case 'N':
			show_nr_of_instructions = 1;
			break;
		case 'n':
			ncpus = atoi(optarg);
			break;
		case 'P':
		case 'p':
			new_pc_dumppoint = strtoll(optarg, NULL, 0);
			if ((new_pc_dumppoint >> 32) == 0 && (new_pc_dumppoint & 0x80000000))
				new_pc_dumppoint |= 0xffffffff00000000;
			if (n_dumppoints >= MAX_PC_DUMPPOINTS) {
				fprintf(stderr, "too many pc dumppoints\n");
				exit(1);
			}
			dumppoint_flag_r[n_dumppoints] = (ch == 'P') ? 1 : 0;
			dumppoint_pc[n_dumppoints++] = new_pc_dumppoint;
			break;
		case 'q':
			quiet_mode = 1;
			break;
		case 'R':
			use_random_bootstrap_cpu = 1;
			break;
		case 'r':
			register_dump = 1;
			break;
		case 's':
			show_opcode_statistics = 1;
			break;
		case 'T':
			trace_on_bad_address = 1;
			break;
		case 't':
			show_trace_tree = 1;
			break;
		case 'U':
			tlb_dump = 1;
			break;
		case 'X':
			use_x11 = 1;
			break;
		case 'Y':
			x11_scaledown = atoi(optarg);
			break;
		case 'h':
		default:
			usage(progname);
			exit(1);
		}
	}

	argc -= optind;
	argv += optind;

	extra_argc = argc;
	extra_argv = argv;

	/*  Default CPU type:  */

	if (emulation_type == EMULTYPE_PS2 && !emul_cpu_name[0])
		strcpy(emul_cpu_name, "R5900");

	if (emulation_type == EMULTYPE_DEC && !emul_cpu_name[0])
		strcpy(emul_cpu_name, "R2000");

	if (emulation_type == EMULTYPE_COBALT && !emul_cpu_name[0])
		strcpy(emul_cpu_name, "RM5200");

	if (emulation_type == EMULTYPE_ARC && !emul_cpu_name[0])
		strcpy(emul_cpu_name, "R4000");

	if (emulation_type == EMULTYPE_SGI && !emul_cpu_name[0])
		strcpy(emul_cpu_name, "R5000");

	if (!emul_cpu_name[0])
		strcpy(emul_cpu_name, CPU_DEFAULT);

	/*  Default memory size:  */

	if (emulation_type == EMULTYPE_PS2 && physical_ram_in_mb == 0)
		physical_ram_in_mb = 32;

	if (emulation_type == EMULTYPE_DEC && machine == MACHINE_PMAX_3100
	    && physical_ram_in_mb == 0)
		physical_ram_in_mb = 24;

	if (physical_ram_in_mb == 0)
		physical_ram_in_mb = DEFAULT_RAM_IN_MB;

	if (extra_argc == 0) {
		usage(progname);
		printf("You must specify one or more names of files that you wish to load into memory.\n");
		printf("Supported formats:  ELF a.out ecoff syms raw\n");
		printf("where syms is the text produced my running 'nm' (or 'nm -S') on a binary.\n");
		printf("To load a raw binary into memory, add \"address:\" in front of the filename,\n");
		printf("for example:    0xbfc00000:romimage.bin\n");
		exit(1);
	}

	if (ncpus < 1) {
		usage(progname);
		printf("Too few cpus.\n");
		exit(1);
	}

#ifndef WITH_X11
	if (use_x11) {
		fprintf(stderr, "Warning: %s was compiled without X11 support. Ignoring -X.\n", argv[0]);
		use_x11 = 0;
	}
#endif

	for (i=0; i<n_dumppoints; i++)
		debug("pc dumppoint %i: %016llx\n", i, (long long)dumppoint_pc[i]);

	return 0;
}


/*
 *  main():
 */
int main(int argc, char *argv[])
{
	get_cmd_args(argc, argv);

	emul();

	return 0;
}

