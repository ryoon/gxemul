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
 *  $Id: debugger.c,v 1.23 2004-12-26 18:16:51 debug Exp $
 *
 *  Single-step debugger.
 *
 *
 *  TODO:
 *
 *	This entire module is very much non-reentrant. :-/
 *
 *	Add more functionality that already exists elsewhere in the emulator.
 *
 *	Nicer looking output of register and TLB dumps, floating point
 *	registers too. Warn about weird/invalid register contents.
 *
 *	Many other TODOs.
 */

#include <ctype.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "misc.h"

#include "console.h"
#include "cop0.h"
#include "cpu_types.h"
#include "emul.h"
#include "memory.h"

#ifdef HACK_STRTOLL
#define strtoll strtol
#define strtoull strtoul
#endif


extern int extra_argc;
extern char **extra_argv;

extern int quiet_mode;


/*
 *  Global debugger variables:
 */

static struct emul *debugger_emul;

int old_instruction_trace = 0;
int old_quiet_mode = 0;
int old_show_trace_tree = 0;

static int exit_debugger;
static int n_steps_left_before_interaction = 0;

#define	MAX_CMD_LEN		63
#define	N_PREVIOUS_CMDS		50
static char *last_cmd[N_PREVIOUS_CMDS];
static int last_cmd_index;

static char repeat_cmd[MAX_CMD_LEN + 1];

static uint64_t last_dump_addr = 0xffffffff80000000ULL;
static uint64_t last_unasm_addr = 0xffffffff80000000ULL;


/*
 *  debugger_activate():
 *
 *  This is a signal handler for CTRL-C.  It shouldn't be called directly,
 *  but setup code in emul.c sets the CTRL-C signal handler to use this
 *  function.
 */
void debugger_activate(int x)
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
 *  debugger_parse_name():
 *
 *  This function reads a string, and tries to match it to a register name,
 *  a symbol, or treat it as a decimal numeric value.
 *
 *  Some examples:
 *
 *	"0x7fff1234"		==> numeric value (hex, in this case)
 *	"pc", "r5", "hi", "lo"	==> register
 *	"memcpy+64"		==> symbol (plus offset)
 *
 *  Register names can be preceeded by "x:" where x is the CPU number. (CPU
 *  0 is assumed by default.)
 *
 *  To force detection of different types, a character can be added in front of
 *  the name: "$" for numeric values, "%" for registers, and "@" for symbols.
 *
 *  Return value is:
 *
 *	NAME_PARSE_NOMATCH	no match
 *	NAME_PARSE_MULTIPLE	multiple matches
 *
 *  or one of these (and then *valuep is read or written, depending on
 *  the writeflag):
 *
 *	NAME_PARSE_REGISTER	a register
 *	NAME_PARSE_NUMBER	a hex number
 *	NAME_PARSE_SYMBOL	a symbol
 */
#define	NAME_PARSE_NOMATCH	0
#define	NAME_PARSE_MULTIPLE	1
#define	NAME_PARSE_REGISTER	2
#define	NAME_PARSE_NUMBER	3
#define	NAME_PARSE_SYMBOL	4
static int debugger_parse_name(struct emul *emul, char *name, int writeflag,
	uint64_t *valuep)
{
	int match_register = 0, match_symbol = 0, match_numeric = 0;
	int skip_register, skip_numeric, skip_symbol;
	int cpunr = 0;

	if (emul == NULL || name == NULL) {
		fprintf(stderr, "debugger_parse_name(): NULL ptr\n");
		exit(1);
	}

	skip_register = name[0] == '$' || name[0] == '@';
	skip_numeric  = name[0] == '%' || name[0] == '@';
	skip_symbol   = name[0] == '$' || name[0] == '%';

	/*  Check for a register match:  */
	if (!skip_register && strlen(name) >= 2) {
		/*  CPU number:  */

		/*  TODO  */

		/*  Register name:  */
		if (strcasecmp(name, "pc") == 0) {
			if (writeflag) {
				emul->cpus[cpunr]->pc = *valuep;
				if (emul->cpus[cpunr]->delay_slot) {
					printf("NOTE: Clearing the delay slot flag! (It was set before.)\n");
					emul->cpus[cpunr]->delay_slot = 0;
				}
				if (emul->cpus[cpunr]->nullify_next) {
					printf("NOTE: Clearing the nullify-next flag! (It was set before.)\n");
					emul->cpus[cpunr]->nullify_next = 0;
				}
			} else
				*valuep = emul->cpus[cpunr]->pc;
			match_register = 1;
		}
		if (strcasecmp(name, "hi") == 0) {
			if (writeflag)
				emul->cpus[cpunr]->hi = *valuep;
			else
				*valuep = emul->cpus[cpunr]->hi;
			match_register = 1;
		}
		if (strcasecmp(name, "lo") == 0) {
			if (writeflag)
				emul->cpus[cpunr]->lo = *valuep;
			else
				*valuep = emul->cpus[cpunr]->lo;
			match_register = 1;
		}
		if (name[0] == 'r' && isdigit((int)name[1])) {
			int nr = atoi(name + 1);
			if (nr >= 0 && nr < 32) {
				if (writeflag) {
					if (nr != 0)
						emul->cpus[cpunr]->gpr[nr] = *valuep;
					else
						printf("WARNING: Attempt to modify r0.\n");
				} else
					*valuep = emul->cpus[cpunr]->gpr[nr];
				match_register = 1;
			}
		}

		/*  TODO: Coprocessor registers.  */
	}

	/*  Check for a number match:  */
	if (!skip_numeric && isdigit((int)name[0])) {
		uint64_t x;
		x = strtoull(name, NULL, 0);
		if (writeflag) {
			printf("You cannot assign like that.\n");
		} else
			*valuep = x;
		match_numeric = 1;
	}

	/*  Check for a symbol match:  */
	if (!skip_symbol) {
		int res;
		char *p, *sn;
		uint64_t newaddr, ofs = 0;

		sn = malloc(strlen(name) + 1);
		if (sn == NULL) {
			fprintf(stderr, "out of memory in debugger\n");
			exit(1);
		}
		strcpy(sn, name);

		/*  Is there a '+' in there? Then treat that as an offset:  */
		p = strchr(sn, '+');
		if (p != NULL) {
			*p = '\0';
			ofs = strtoull(p+1, NULL, 0);
		}

		res = get_symbol_addr(&emul->symbol_context, sn, &newaddr);
		if (res) {
			if (writeflag) {
				printf("You cannot assign like that.\n");
			} else
				*valuep = newaddr + ofs;
			match_symbol = 1;
		}

		free(sn);
	}

	if (match_register + match_symbol + match_numeric > 1)
		return NAME_PARSE_MULTIPLE;

	if (match_register)
		return NAME_PARSE_REGISTER;
	if (match_numeric)
		return NAME_PARSE_NUMBER;
	if (match_symbol)
		return NAME_PARSE_SYMBOL;

	return NAME_PARSE_NOMATCH;
}


/*
 *  show_breakpoint():
 */
static void show_breakpoint(struct emul *emul, int i)
{
	printf("%3i: 0x%016llx", i,
	    (long long)emul->breakpoint_addr[i]);
	if (emul->breakpoint_string[i] != NULL)
		printf(" (%s)", emul->breakpoint_string[i]);
	if (emul->breakpoint_flags[i])
		printf(": flags=0x%x", emul->breakpoint_flags[i]);
	printf("\n");
}


/****************************************************************************/


/*
 *  debugger_cmd_breakpoint():
 *
 *  TODO: automagic "expansion" for the subcommand names (s => show).
 */
static void debugger_cmd_breakpoint(struct emul *emul, char *cmd_line)
{
	int i, res;

	while (cmd_line[0] != '\0' && cmd_line[0] == ' ')
		cmd_line ++;

	if (cmd_line[0] == '\0') {
		printf("usage: breakpoint subcmd [args...]\n");
		printf("Available subcmds (and args) are:\n");
		printf("  add addr      add a breakpoint for address addr\n");
		printf("  delete x      delete breakpoint nr x\n");
		printf("  show          show current breakpoints\n");
		return;
	}

	if (strcmp(cmd_line, "show") == 0) {
		if (emul->n_breakpoints == 0)
			printf("No breakpoints set.\n");
		for (i=0; i<emul->n_breakpoints; i++)
			show_breakpoint(emul, i);
		return;
	}

	if (strncmp(cmd_line, "delete ", 7) == 0) {
		int x = atoi(cmd_line + 7);

		if (emul->n_breakpoints == 0) {
			printf("No breakpoints set.\n");
			return;
		}
		if (x < 0 || x >= emul->n_breakpoints) {
			printf("Invalid breakpoint nr %i. Use 'breakpoint "
			    "show' to see the current breakpoints.\n", x);
			return;
		}

		free(emul->breakpoint_string[x]);

		for (i=x; i<emul->n_breakpoints-1; i++) {
			emul->breakpoint_addr[i]   = emul->breakpoint_addr[i+1];
			emul->breakpoint_string[i] = emul->breakpoint_string[i+1];
			emul->breakpoint_flags[i]  = emul->breakpoint_flags[i+1];
		}
		emul->n_breakpoints --;
		return;
	}

	if (strncmp(cmd_line, "add ", 4) == 0) {
		uint64_t tmp;

		if (emul->n_breakpoints >= MAX_BREAKPOINTS) {
			printf("Too many breakpoints. (You need to recompile"
			    " mips64emul to increase this. Max = %i.)\n",
			    MAX_BREAKPOINTS);
			return;
		}

		i = emul->n_breakpoints;

		res = debugger_parse_name(emul, cmd_line + 4, 0, &tmp);
		if (!res) {
			printf("Couldn't parse '%s'\n", cmd_line + 4);
			return;
		}

		emul->breakpoint_string[i] = malloc(strlen(cmd_line+4) + 1);
		if (emul->breakpoint_string[i] == NULL) {
			printf("out of memory in debugger_cmd_breakpoint()\n");
			exit(1);
		}
		strcpy(emul->breakpoint_string[i], cmd_line+4);
		emul->breakpoint_addr[i] = tmp;
		emul->breakpoint_flags[i] = 0;

		emul->n_breakpoints ++;
		show_breakpoint(emul, i);
		return;
	}

	printf("Unknown breakpoint subcommand.\n");
}


/*
 *  debugger_cmd_bintrans():
 */
static void debugger_cmd_bintrans(struct emul *emul, char *cmd_line)
{
	if (!emul->bintrans_enabled_from_start) {
		printf("You must have enabled bintrans from the start of the simulation.\n"
		    "It is not possible to turn on afterwards.\n");
		return;
	}

	emul->bintrans_enable = !emul->bintrans_enable;
	printf("bintrans_enable = %s\n", emul->bintrans_enable? "ON" : "OFF");
}


/*
 *  debugger_cmd_continue():
 */
static void debugger_cmd_continue(struct emul *emul, char *cmd_line)
{
	exit_debugger = 1;
}


/*
 *  debugger_cmd_devices():
 */
static void debugger_cmd_devices(struct emul *emul, char *cmd_line)
{
	int i;
	struct memory *m;
	struct cpu *c;

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

	if (m->n_mmapped_devices == 0)
		printf("No memory-mapped devices\n");

	for (i=0; i<m->n_mmapped_devices; i++) {
		printf("%2i: %25s @ 0x%011llx, len = 0x%llx",
		    i, m->dev_name[i],
		    (long long)m->dev_baseaddr[i],
		    (long long)m->dev_length[i]);
		if (m->dev_flags[i]) {
			printf(" (");
			if (m->dev_flags[i] & MEM_BINTRANS_OK)
				printf("BINTRANS R");
			if (m->dev_flags[i] & MEM_BINTRANS_WRITE_OK)
				printf("+W");
			printf(")");
		}
		printf("\n");
	}
}


/*
 *  debugger_cmd_devstate():
 */
static void debugger_cmd_devstate(struct emul *emul, char *cmd_line)
{
	int i, j;
	struct memory *m;
	struct cpu *c;

	if (cmd_line[0] == '\0') {
		printf("usage: devstate devnr\n");
		printf("Use 'devices' to get a list of current device numbers.\n");
		return;
	}

	i = strtoll(cmd_line, NULL, 0);

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

	if (i < 0 || i >= m->n_mmapped_devices) {
		printf("No devices with that id.\n");
		return;
	}

	if (m->dev_f_state[i] == NULL) {
		printf("No state function has been implemented yet for that device type.\n");
		return;
	}

	for (j=0; ; j++) {
		int type;
		char *name;
		void *data;
		size_t len;
		int res = m->dev_f_state[i](c, m, m->dev_extra[i], 0,
		    j, &type, &name, &data, &len);
		if (!res)
			break;
		printf("%2i:%30s = (", j, name);
		switch (type) {
		case DEVICE_STATE_TYPE_INT:
			printf("int) %i", *((int *)data));
			break;
		default:
			printf("unknown)");
		}
		printf("\n");
	}
}


/*
 *  debugger_cmd_dump():
 */
static void debugger_cmd_dump(struct emul *emul, char *cmd_line)
{
	uint64_t addr, addr_start, addr_end;
	struct cpu *c;
	struct memory *m;
	int x, r;

	if (cmd_line[0] != '\0') {
		uint64_t tmp;
		r = debugger_parse_name(emul, cmd_line, 0, &tmp);
		if (r)
			last_dump_addr = tmp;
		else {
			printf("Unparsable address: %s\n", cmd_line);
			return;
		}
	}

	addr_start = last_dump_addr;
	addr_end = addr_start + 256;

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

	addr = addr_start & ~0xf;

	while (addr < addr_end) {
		unsigned char buf[16];
		memset(buf, 0, sizeof(buf));
		r = memory_rw(c, m, addr, &buf[0], sizeof(buf), MEM_READ,
		    CACHE_NONE | NO_EXCEPTIONS);

		printf("0x%016llx  ", (long long)addr);

		if (r == MEMORY_ACCESS_FAILED)
			printf("(memory access failed)\n");
		else {
			for (x=0; x<16; x++) {
				if (addr + x >= addr_start &&
				    addr + x < addr_end)
					printf("%02x%s", buf[x],
					    (x&3)==3? " " : "");
				else
					printf("  %s", (x&3)==3? " " : "");
			}
			printf(" ");
			for (x=0; x<16; x++) {
				if (addr + x >= addr_start &&
				    addr + x < addr_end)
					printf("%c", (buf[x]>=' ' &&
					    buf[x]<127)? buf[x] : '.');
				else
					printf(" ");
			}
			printf("\n");
		}

		addr += sizeof(buf);
	}

	last_dump_addr = addr_end;

	strcpy(repeat_cmd, "dump");
}


/*  This is defined below.  */
static void debugger_cmd_help(struct emul *emul, char *cmd_line);


/*
 *  debugger_cmd_itrace():
 */
static void debugger_cmd_itrace(struct emul *emul, char *cmd_line)
{
	old_instruction_trace = 1 - old_instruction_trace;
	printf("instruction_trace = %s\n", old_instruction_trace? "ON":"OFF");
	/*  TODO: how to preserve quiet_mode?  */
	old_quiet_mode = 0;
	printf("quiet_mode = %s\n", old_quiet_mode? "ON" : "OFF");
}


/*
 *  debugger_cmd_lookup():
 */
static void debugger_cmd_lookup(struct emul *emul, char *cmd_line)
{
	uint64_t addr;
	int res;
	char *symbol;
	uint64_t offset;

	if (cmd_line[0] == '\0') {
		printf("usage: lookup name|addr\n");
		return;

	}

	/*  Addresses never need to be given in decimal form anyway,
	    so assuming hex here will be ok.  */
	addr = strtoull(cmd_line, NULL, 16);

	if (addr == 0) {
		uint64_t newaddr;
		res = get_symbol_addr(&emul->symbol_context,
		    cmd_line, &newaddr);
		if (!res) {
			printf("lookup for '%s' failed\n", cmd_line);
			return;
		}
		printf("%s = 0x%016llx\n", cmd_line, (long long)newaddr);
		return;
	}

	symbol = get_symbol_name(&emul->symbol_context, addr, &offset);

	if (symbol != NULL)
		printf("0x%016llx = %s\n", (long long)addr, symbol);
	else
		printf("lookup for '%s' failed\n", cmd_line);
}


/*
 *  debugger_cmd_machine():
 */
static void debugger_cmd_machine(struct emul *emul, char *cmd_line)
{
	int i;

	printf("ram: %i MB\n",
	    (int)(emul->cpus[0]->mem->physical_max / 1048576));

	for (i=0; i<emul->ncpus; i++) {
		struct cpu_type_def *ct = &emul->cpus[i]->cpu_type;

		printf("cpu%i: %s, %s",
		    i, ct->name, emul->cpus[i]->running? "running" : "stopped");

		printf(" (%i-bit, ",
		    (ct->isa_level < 3 || ct->isa_level == 32)? 32 : 64);

		printf("%s-endian, ", emul->cpus[i]->byte_order
		    == EMUL_BIG_ENDIAN? "big" : "little");

		printf("%i TLB entries", ct->nr_of_tlb_entries);

		if (ct->default_picache || ct->default_pdcache)
			printf(", I+D = %i+%i KB",
			    (1 << ct->default_picache) / 1024,
			    (1 << ct->default_pdcache) / 1024);

		if (ct->default_scache) {
			int kb = (1 << ct->default_scache) / 1024;
			printf(", L2 = %i %cB",
			    kb >= 1024? kb / 1024 : kb,
			    kb >= 1024? 'M' : 'K');
		}

		printf(")\n");
	}
}


/*
 *  debugger_cmd_print():
 */
static void debugger_cmd_print(struct emul *emul, char *cmd_line)
{
	int res;
	uint64_t tmp;

	while (cmd_line[0] != '\0' && cmd_line[0] == ' ')
		cmd_line ++;

	if (cmd_line[0] == '\0') {
		printf("usage: print expr\n");
		return;
	}

	res = debugger_parse_name(emul, cmd_line, 0, &tmp);
	switch (res) {
	case NAME_PARSE_NOMATCH:
		printf("No match.\n");
		break;
	case NAME_PARSE_MULTIPLE:
		printf("Multiple matches. Try prefixing with %%, $, or @.\n");
		break;
	case NAME_PARSE_REGISTER:
	case NAME_PARSE_SYMBOL:
		printf("%s = 0x%016llx\n", cmd_line, (long long)tmp);
		break;
	case NAME_PARSE_NUMBER:
		printf("0x%016llx\n", (long long)tmp);
		break;
	}
}


/*
 *  debugger_cmd_quiet():
 */
static void debugger_cmd_quiet(struct emul *emul, char *cmd_line)
{
	old_quiet_mode = 1 - old_quiet_mode;
	printf("quiet_mode = %s\n", old_quiet_mode? "ON" : "OFF");
}


/*
 *  debugger_cmd_quit():
 */
static void debugger_cmd_quit(struct emul *emul, char *cmd_line)
{
	int i;

	for (i=0; i<emul->ncpus; i++)
		emul->cpus[i]->running = 0;
	emul->exit_without_entering_debugger = 1;
	emul->single_step = 0;
	exit_debugger = 1;
}


/*
 *  debugger_cmd_reg():
 */
static void debugger_cmd_reg(struct emul *emul, char *cmd_line)
{
	int i, cpuid = -1;

	if (cmd_line[0] != '\0') {
		cpuid = strtoull(cmd_line, NULL, 0);
		if (cpuid < 0 || cpuid >= emul->ncpus) {
			printf("cpu%i doesn't exist.\n", cpuid);
			return;
		}
	}

	for (i=0; i<emul->ncpus; i++)
		if (cpuid == -1 || i == cpuid)
			cpu_register_dump(emul->cpus[i]);
}


/*
 *  debugger_cmd_step():
 */
static void debugger_cmd_step(struct emul *emul, char *cmd_line)
{
	int n = 1;

	if (cmd_line[0] != '\0') {
		n = strtoull(cmd_line, NULL, 0);
		if (n < 1) {
			printf("invalid nr of steps\n");
			return;
		}
	}

	n_steps_left_before_interaction = n - 1;

	/*  Special hack, see debugger() for more info.  */
	exit_debugger = -1;

	strcpy(repeat_cmd, "step");
}


/*
 *  debugger_cmd_tlbdump():
 *
 *  Dump each CPU's TLB contents.
 */
static void debugger_cmd_tlbdump(struct emul *emul, char *cmd_line)
{
	int i, j, x = -1;

	if (cmd_line[0] != '\0') {
		x = strtoull(cmd_line, NULL, 0);
		if (x < 0 || x >= emul->ncpus) {
			printf("cpu%i doesn't exist.\n", x);
			return;
		}
	}

	for (i=0; i<emul->ncpus; i++)
	    if (x == -1 || i == x) {
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
 *  debugger_cmd_trace():
 */
static void debugger_cmd_trace(struct emul *emul, char *cmd_line)
{
	old_show_trace_tree = 1 - old_show_trace_tree;
	printf("show_trace_tree = %s\n", old_show_trace_tree? "ON" : "OFF");

	if (emul->bintrans_enable && old_show_trace_tree)
		printf("NOTE: the trace tree functionality doesn't "
		    "work very well with bintrans!\n");

	/*  TODO: how to preserve quiet_mode?  */
	old_quiet_mode = 0;
	printf("quiet_mode = %s\n", old_quiet_mode? "ON" : "OFF");
}


/*
 *  debugger_cmd_unassemble():
 *
 *  Dump emulated memory as MIPS instructions.
 */
static void debugger_cmd_unassemble(struct emul *emul, char *cmd_line)
{
	uint64_t addr, addr_start, addr_end;
	struct cpu *c;
	struct memory *m;
	int r;

	if (cmd_line[0] != '\0') {
		uint64_t tmp;
		r = debugger_parse_name(emul, cmd_line, 0, &tmp);
		if (r)
			last_unasm_addr = tmp;
		else {
			printf("Unparsable address: %s\n", cmd_line);
			return;
		}
	}

	addr_start = last_unasm_addr;
	addr_end = addr_start + 4 * 16;

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

	addr = addr_start;

	if ((addr & 3) != 0)
		printf("WARNING! You entered an unaligned address.\n");

	while (addr < addr_end) {
		unsigned char buf[4];
		memset(buf, 0, sizeof(buf));
		r = memory_rw(c, m, addr, &buf[0], sizeof(buf), MEM_READ,
		    CACHE_NONE | NO_EXCEPTIONS);

		if (c->byte_order == EMUL_BIG_ENDIAN) {
			int tmp;
			tmp = buf[0]; buf[0] = buf[3]; buf[3] = tmp;
			tmp = buf[1]; buf[1] = buf[2]; buf[2] = tmp;
		}

		cpu_disassemble_instr(c, &buf[0], 0, addr, 0);

		addr += sizeof(buf);
	}

	last_unasm_addr = addr_end;

	strcpy(repeat_cmd, "unassemble");
}


/*
 *  debugger_cmd_version():
 */
static void debugger_cmd_version(struct emul *emul, char *cmd_line)
{
#ifdef VERSION
	printf("%s, %s\n", VERSION, COMPILE_DATE);
#else
	printf("(no version), %s\n", COMPILE_DATE);
#endif
}


struct cmd {
	char	*name;
	char	*args;
	int	tmp_flag;
	void	(*f)(struct emul *, char *cmd_line);
	char	*description;
};

static struct cmd cmds[] = {
	{ "breakpoint", "...", 0, debugger_cmd_breakpoint,
		"manipulate breakpoints" },

	{ "bintrans", "", 0, debugger_cmd_bintrans,
		"toggle bintrans on or off" },

	/*  NOTE: Try to keep 'c' down to only one command. Having 'continue'
	    available as a one-letter command is very convenient.  */

	{ "continue", "", 0, debugger_cmd_continue,
		"continue execution" },

	{ "devstate", "devnr", 0, debugger_cmd_devstate,
		"show current state of a device" },

	{ "devices", "", 0, debugger_cmd_devices,
		"print a list of memory-mapped devices" },

	{ "dump", "[vaddr]", 0, debugger_cmd_dump,
		"dump memory contents in hex and ASCII" },

	{ "help", "", 0, debugger_cmd_help,
		"print this help message" },

	{ "itrace", "", 0, debugger_cmd_itrace,
		"toggle instruction_trace on or off" },

	{ "lookup", "name|addr", 0, debugger_cmd_lookup,
		"lookup a symbol by name or address" },

	{ "machine", "", 0, debugger_cmd_machine,
		"print a summary of the machine being emulated" },

	{ "print", "expr", 0, debugger_cmd_print,
		"evaluate an expression without side-effects" },

	{ "quiet", "", 0, debugger_cmd_quiet,
		"toggle quiet_mode on or off" },

	{ "quit", "", 0, debugger_cmd_quit,
		"quit the emulator" },

	{ "reg", "[cpuid]", 0, debugger_cmd_reg,
		"show registers" },

	/*  NOTE: Try to keep 's' down to only one command. Having 'step'
	    available as a one-letter command is very convenient.  */

	{ "step", "[n]", 0, debugger_cmd_step,
		"single-step one instruction (or n instructions)" },

	{ "tlbdump", "[cpuid]", 0, debugger_cmd_tlbdump,
		"dump all CPU's TLB contents (or a specific one's)" },

	{ "trace", "", 0, debugger_cmd_trace,
		"toggle show_trace_tree on or off" },

	{ "unassemble", "[addr]", 0, debugger_cmd_unassemble,
		"dump memory contents as MIPS instructions" },

	{ "version", "", 0, debugger_cmd_version,
		"print version information" },

	{ NULL, NULL, 0, NULL, NULL }
};


/*
 *  debugger_cmd_help():
 *
 *  Print a list of available commands.
 *
 *  NOTE: This is placed after the cmds[] array, because it needs to
 *  access it.
 */
static void debugger_cmd_help(struct emul *emul, char *cmd_line)
{
	int i, j, max_name_len = 0;

	i = 0;
	while (cmds[i].name != NULL) {
		int a = strlen(cmds[i].name);
		if (cmds[i].args != NULL)
			a += 1 + strlen(cmds[i].args);
		if (a > max_name_len)
			max_name_len = a;
		i++;
	}

	printf("Available commands:\n");

	i = 0;
	while (cmds[i].name != NULL) {
		char buf[100];
		snprintf(buf, sizeof(buf), "%s", cmds[i].name);
		if (cmds[i].args != NULL)
			snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
			    " %s", cmds[i].args);

		printf("  ");
		for (j=0; j<max_name_len; j++)
			if (j < strlen(buf))
				printf("%c", buf[j]);
			else
				printf(" ");

		printf("    %s\n", cmds[i].description);
		i++;
	}

	printf("Generic assignments:   x = expr\n");
	printf("where x must be a register, and expr can be a register, a numeric value, or\n"
	    "a symbol name (+ an optional numeric offset). In case there are multiple\n"
	    "matches (ie a symbol that has the same name as a register), you may add a\n"
	    "prefix character as a hint: '%%' for registers, '@' for symbols, and\n"
	    "'$' for numeric values. Use 0x for hexadecimal values.\n");
}


/****************************************************************************/


/*
 *  debugger_assignment():
 *
 *  cmd contains something like "pc=0x80001000", or "r31=memcpy+0x40".
 */
void debugger_assignment(struct emul *emul, char *cmd)
{
	char *left, *right;
	int res_left, res_right;
	uint64_t tmp;

	left  = malloc(strlen(cmd) + 1);
	if (left == NULL) {
		fprintf(stderr, "out of memory in debugger_assignment()\n");
		exit(1);
	}
	strcpy(left, cmd);
	right = strchr(left, '=');
	if (right == NULL) {
		fprintf(stderr, "internal error in the debugger\n");
		exit(1);
	}
	*right = '\0';

	/*  Remove trailing spaces in left:  */
	while (strlen(left) >= 1 && left[strlen(left)-1] == ' ')
		left[strlen(left)-1] = '\0';

	/*  Remove leading spaces in right:  */
	right++;
	while (*right == ' ' && *right != '\0')
		right++;

	/*  printf("left  = '%s'\nright = '%s'\n", left, right);  */

	res_right = debugger_parse_name(emul, right, 0, &tmp);
	switch (res_right) {
	case NAME_PARSE_NOMATCH:
		printf("No match for the right-hand side of the assignment.\n");
		break;
	case NAME_PARSE_MULTIPLE:
		printf("Multiple matches for the right-hand side of the assignment.\n");
		break;
	default:
		res_left = debugger_parse_name(emul, left, 1, &tmp);
		switch (res_left) {
		case NAME_PARSE_NOMATCH:
			printf("No match for the left-hand side of the assignment.\n");
			break;
		case NAME_PARSE_MULTIPLE:
			printf("Multiple matches for the left-hand side of the assignment.\n");
			break;
		default:
			debugger_cmd_print(emul, left);
		}
	}

	free(left);
}


/*
 *  debugger_readline():
 *
 *  Read a line from the terminal.
 */
static char *debugger_readline(void)
{
	int ch, i, n, i_match, reallen, cmd_len, cursor_pos;
	int read_from_index = last_cmd_index;
	char *cmd = last_cmd[last_cmd_index];

	cmd_len = 0; cmd[0] = '\0';
	printf("mips64emul> ");
	fflush(stdout);

	ch = '\0';
	cmd_len = 0;
	cursor_pos = 0;

	while (ch != '\n') {
		/*
		 *  TODO: This uses up 100% CPU, maybe that isn't too good.
		 *  The usleep() call might make it a tiny bit nicer on other
		 *  running processes, but it is still very ugly.
		 */
		while ((ch = console_readchar()) < 0)
			usleep(1);

		if (ch == '\b' && cursor_pos > 0) {
			/*  Backspace.  */
			cursor_pos --;
			cmd_len --;
			memmove(cmd + cursor_pos, cmd + cursor_pos + 1,
			    cmd_len);
			cmd[cmd_len] = '\0';
			printf("\b");
			for (i=cursor_pos; i<cmd_len; i++)
				printf("%c", cmd[i]);
			printf(" \b");
			for (i=cursor_pos; i<cmd_len; i++)
				printf("\b");
		} else if (ch == 4 && cmd_len > 0 && cursor_pos < cmd_len) {
			/*  CTRL-D: Delete.  */
			cmd_len --;
			memmove(cmd + cursor_pos, cmd + cursor_pos + 1,
			    cmd_len);
			cmd[cmd_len] = '\0';
			for (i=cursor_pos; i<cmd_len; i++)
				printf("%c", cmd[i]);
			printf(" \b");
			for (i=cursor_pos; i<cmd_len; i++)
				printf("\b");
		} else if (ch == 1) {
			/*  CTRL-A: Start of line.  */
			while (cursor_pos > 0) {
				cursor_pos --;
				printf("\b");
			}
		} else if (ch == 5) {
			/*  CTRL-E: End of line.  */
			while (cursor_pos < cmd_len) {
				printf("%c", cmd[cursor_pos]);
				cursor_pos ++;
			}
		} else if (ch == 11) {
			/*  CTRL-K: Kill to end of line.  */
			for (i=0; i<MAX_CMD_LEN; i++)
				console_makeavail(4);	/*  :-)  */
		} else if (ch >= ' ' && cmd_len < MAX_CMD_LEN) {
			/*  Visible character:  */
			memmove(cmd + cursor_pos + 1, cmd + cursor_pos,
			    cmd_len - cursor_pos);
			cmd[cursor_pos] = ch;
			cmd_len ++;
			cursor_pos ++;
			cmd[cmd_len] = '\0';
			printf("%c", ch);
			for (i=cursor_pos; i<cmd_len; i++)
				printf("%c", cmd[i]);
			for (i=cursor_pos; i<cmd_len; i++)
				printf("\b");
		} else if (ch == '\r' || ch == '\n') {
			ch = '\n';
			printf("\n");
		} else if (ch == '\t') {
			/*  Super-simple tab-completion:  */
			i = 0;
			while (cmds[i].name != NULL)
				cmds[i++].tmp_flag = 0;

			/*  Check for a (partial) command match:  */
			n = i = i_match = 0;
			while (cmds[i].name != NULL) {
				if (strncasecmp(cmds[i].name, cmd,
				    cmd_len) == 0) {
					cmds[i].tmp_flag = 1;
					i_match = i;
					n++;
				}
				i++;
			}

			switch (n) {
			case 0:	/*  Beep.  */
				printf("\a");
				break;
			case 1:	/*  Add the rest of the command:  */
				reallen = strlen(cmds[i_match].name);
				for (i=cmd_len; i<reallen; i++)
					console_makeavail(
					    cmds[i_match].name[i]);
				break;
			default:
				/*  Show all possible commands:  */
				printf("\n  ");
				i = 0;
				while (cmds[i].name != NULL) {
					if (cmds[i].tmp_flag)
						printf("  %s",
						    cmds[i].name);
					i++;
				}
				printf("\nmips64emul> ");
				for (i=0; i<cmd_len; i++)
					printf("%c", cmd[i]);
			}
		} else if (ch == 27) {
			/*  Escape codes: (cursor keys etc)  */
			while ((ch = console_readchar()) < 0)
				usleep(1);
			if (ch == '[' || ch == 'O') {
				while ((ch = console_readchar()) < 0)
					usleep(1);
				switch (ch) {
				case 'A':	/*  Up.  */
				case 'B':	/*  Down.  */
					if (ch == 'B' &&
					    read_from_index == last_cmd_index)
						break;

					if (ch == 'A')
						i = read_from_index - 1;
					else
						i = read_from_index + 1;

					if (i < 0)
						i = N_PREVIOUS_CMDS - 1;
					if (i >= N_PREVIOUS_CMDS)
						i = 0;

					/*  Special case: pressing 'down'
					    to reach last_cmd_index:  */
					if (i == last_cmd_index) {
						read_from_index = i;
						for (i=cursor_pos; i<cmd_len;
						    i++)
							printf(" ");
						for (i=cmd_len-1; i>=0; i--)
							printf("\b \b");
						cmd[0] = '\0';
						cmd_len = cursor_pos = 0;
					} else if (last_cmd[i][0] != '\0') {
						/*  Copy from old line:  */
						read_from_index = i;
						for (i=cursor_pos; i<cmd_len;
						    i++)
							printf(" ");
						for (i=cmd_len-1; i>=0; i--)
							printf("\b \b");
						strcpy(cmd,
						    last_cmd[read_from_index]);
						cmd_len = strlen(cmd);
						printf("%s", cmd);
						cursor_pos = cmd_len;
					}
					break;
				case 'C':	/*  Right  */
					if (cursor_pos < cmd_len) {
						printf("%c",
						    cmd[cursor_pos]);
						cursor_pos ++;
					}
					break;
				case 'D':	/*  Left  */
					if (cursor_pos > 0) {
						printf("\b");
						cursor_pos --;
					}
					break;
				}
			}
		}

		fflush(stdout);
	}

	return cmd;
}


/*
 *  debugger():
 *
 *  This is a loop, which reads a command from the terminal, and executes it.
 */
void debugger(void)
{
	int i, n, i_match, matchlen, cmd_len;
	char *cmd;

	if (n_steps_left_before_interaction > 0) {
		n_steps_left_before_interaction --;
		return;
	}

	exit_debugger = 0;

	while (!exit_debugger) {
		/*  Read a line from the terminal:  */
		cmd = debugger_readline();
		cmd_len = strlen(cmd);

		/*  Remove spaces:  */
		while (cmd_len > 0 && cmd[0]==' ')
			memmove(cmd, cmd+1, cmd_len --);
		while (cmd_len > 0 && cmd[cmd_len-1] == ' ')
			cmd[(cmd_len--)-1] = '\0';

		/*  No command? Then try reading another line.  */
		if (cmd_len == 0) {
			/*  Special case for repeated commands:  */
			if (repeat_cmd[0] != '\0')
				strcpy(cmd, repeat_cmd);
			else
				continue;
		} else {
			last_cmd_index ++;
			if (last_cmd_index >= N_PREVIOUS_CMDS)
				last_cmd_index = 0;

			repeat_cmd[0] = '\0';
		}

		/*  Is there a '=' on the command line? Then try to
		    do an assignment:  */
		if (strchr(cmd, '=') != NULL) {
			debugger_assignment(debugger_emul, cmd);
			continue;
		}

		i = 0;
		while (cmds[i].name != NULL)
			cmds[i++].tmp_flag = 0;

		/*  How many chars in cmd to match against:  */
		matchlen = 0;
		while (isalpha((int)cmd[matchlen]))
			matchlen ++;

		/*  Check for a command name match:  */
		n = i = i_match = 0;
		while (cmds[i].name != NULL) {
			if (strncasecmp(cmds[i].name, cmd, matchlen) == 0) {
				cmds[i].tmp_flag = 1;
				i_match = i;
				n++;
			}
			i++;
		}

		/*  No match?  */
		if (n == 0) {
			printf("Unknown command '%s'. "
			    "Type 'help' for help.\n", cmd);
			continue;
		}

		/*  More than one match?  */
		if (n > 1) {
			printf("Ambiguous command '%s':  ", cmd);
			i = 0;
			while (cmds[i].name != NULL) {
				if (cmds[i].tmp_flag)
					printf("  %s", cmds[i].name);
				i++;
			}
			printf("\n");
			continue;
		}

		/*  Exactly one match:  */
		if (cmds[i_match].f != NULL) {
			char *p = cmd + matchlen;
			/*  Remove leading whitespace from the args...  */
			while (*p != '\0' && *p == ' ')
				p++;

			/*  ... and run the command:  */
			cmds[i_match].f(debugger_emul, p);
		} else
			printf("FATAL ERROR: internal error in debugger.c:"
			    " no handler for this command?\n");

		/*  Special hack for the "step" command:  */
		if (exit_debugger == -1)
			return;
	}

	debugger_emul->single_step = 0;
	debugger_emul->instruction_trace = old_instruction_trace;
	debugger_emul->show_trace_tree = old_show_trace_tree;
	quiet_mode = old_quiet_mode;
}


/*
 *  debugger_init():
 *
 *  Must be called before any other debugger function is used.
 */
void debugger_init(struct emul *emul)
{
	int i;

	debugger_emul = emul;

	for (i=0; i<N_PREVIOUS_CMDS; i++) {
		last_cmd[i] = malloc(MAX_CMD_LEN + 1);
		if (last_cmd[i] == NULL) {
			fprintf(stderr, "debugger_init(): out of memory\n");
			exit(1);
		}
		last_cmd[i][0] = '\0';
	}

	last_cmd_index = 0;
	repeat_cmd[0] = '\0';
}

