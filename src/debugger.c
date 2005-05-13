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
 *  $Id: debugger.c,v 1.103 2005-05-13 14:26:29 debug Exp $
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
 *	More generic expression evaluator (for example + - * / between multiple
 *	terms), including _TAB COMPLETION_ of symbols and register names!
 *
 *	Nicer looking output of register dumps, floating point registers,
 *	etc. Warn about weird/invalid register contents.
 *
 *	Ctrl-C doesn't enter the debugger on some OSes (HP-UX?)...
 *
 *	Many other TODOs.
 */

#include <ctype.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "bintrans.h"
#include "console.h"
#include "cpu.h"
#include "device.h"
#include "debugger.h"
#include "diskimage.h"
#include "emul.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"
#include "net.h"
#include "x11.h"


extern int extra_argc;
extern char **extra_argv;

extern int quiet_mode;


/*
 *  Global debugger variables:
 */

volatile int single_step = 0;
int force_debugger_at_exit = 0;
int show_opcode_statistics = 0;

int old_instruction_trace = 0;
int old_quiet_mode = 0;
int old_show_trace_tree = 0;


/*
 *  Private (global) debugger variables:
 */

static volatile int ctrl_c;

static int debugger_n_emuls;
static struct emul **debugger_emuls;
static struct emul *debugger_emul;
static struct machine *debugger_machine;

static int exit_debugger;
static int n_steps_left_before_interaction = 0;

#define	MAX_CMD_LEN		70
#define	N_PREVIOUS_CMDS		150
static char *last_cmd[N_PREVIOUS_CMDS];
static int last_cmd_index;

static char repeat_cmd[MAX_CMD_LEN + 1];

#define	MAGIC_UNTOUCHED		0x98ca76c2ffcc0011ULL

static uint64_t last_dump_addr = MAGIC_UNTOUCHED;
static uint64_t last_unasm_addr = MAGIC_UNTOUCHED;


/*
 *  debugger_activate():
 *
 *  This is a signal handler for CTRL-C.  It shouldn't be called directly,
 *  but setup code in emul.c sets the CTRL-C signal handler to use this
 *  function.
 */
void debugger_activate(int x)
{
	ctrl_c = 1;

	if (single_step) {
		/*  Already in the debugger. Do nothing.  */
		int i;
		for (i=0; i<MAX_CMD_LEN+1; i++)
			console_makeavail(MAIN_CONSOLE, '\b');
		console_makeavail(MAIN_CONSOLE, ' ');
		console_makeavail(MAIN_CONSOLE, '\n');
		printf("^C");
		fflush(stdout);
	} else {
		/*  Enter the single step debugger.  */
		single_step = 1;

		/*  Discard any chars in the input queue:  */
		while (console_charavail(MAIN_CONSOLE))
			console_readchar(MAIN_CONSOLE);
	}

	/*  Clear the repeat-command buffer:  */
	repeat_cmd[0] = '\0';

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
 *	"pc", "r5", "hi", "t4"	==> register (CPU dependant)
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
static int debugger_parse_name(struct machine *m, char *name, int writeflag,
	uint64_t *valuep)
{
	int match_register = 0, match_symbol = 0, match_numeric = 0;
	int skip_register, skip_numeric, skip_symbol;

	if (m == NULL || name == NULL) {
		fprintf(stderr, "debugger_parse_name(): NULL ptr\n");
		exit(1);
	}

	/*  Warn about non-signextended values:  */
	if (writeflag &&
	    ((*valuep) >> 32) == 0 && (*valuep) & 0x80000000ULL)
		printf("WARNING: The value is not sign-extended. "
		    "Is this what you intended?\n");

	skip_register = name[0] == '$' || name[0] == '@';
	skip_numeric  = name[0] == '%' || name[0] == '@';
	skip_symbol   = name[0] == '$' || name[0] == '%';

	/*  Check for a register match:  */
	if (!skip_register && strlen(name) >= 1)
		cpu_register_match(m, name, writeflag, valuep,
		    &match_register);

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

		res = get_symbol_addr(&m->symbol_context, sn, &newaddr);
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
static void show_breakpoint(struct machine *m, int i)
{
	printf("%3i: 0x%016llx", i,
	    (long long)m->breakpoint_addr[i]);
	if (m->breakpoint_string[i] != NULL)
		printf(" (%s)", m->breakpoint_string[i]);
	if (m->breakpoint_flags[i])
		printf(": flags=0x%x", m->breakpoint_flags[i]);
	printf("\n");
}


/****************************************************************************/


/*
 *  debugger_cmd_breakpoint():
 *
 *  TODO: automagic "expansion" for the subcommand names (s => show).
 */
static void debugger_cmd_breakpoint(struct machine *m, char *cmd_line)
{
	int i, res;

	while (cmd_line[0] != '\0' && cmd_line[0] == ' ')
		cmd_line ++;

	if (cmd_line[0] == '\0') {
		printf("syntax: breakpoint subcmd [args...]\n");
		printf("Available subcmds (and args) are:\n");
		printf("  add addr      add a breakpoint for address addr\n");
		printf("  delete x      delete breakpoint nr x\n");
		printf("  show          show current breakpoints\n");
		return;
	}

	if (strcmp(cmd_line, "show") == 0) {
		if (m->n_breakpoints == 0)
			printf("No breakpoints set.\n");
		for (i=0; i<m->n_breakpoints; i++)
			show_breakpoint(m, i);
		return;
	}

	if (strncmp(cmd_line, "delete ", 7) == 0) {
		int x = atoi(cmd_line + 7);

		if (m->n_breakpoints == 0) {
			printf("No breakpoints set.\n");
			return;
		}
		if (x < 0 || x >= m->n_breakpoints) {
			printf("Invalid breakpoint nr %i. Use 'breakpoint "
			    "show' to see the current breakpoints.\n", x);
			return;
		}

		free(m->breakpoint_string[x]);

		for (i=x; i<m->n_breakpoints-1; i++) {
			m->breakpoint_addr[i]   = m->breakpoint_addr[i+1];
			m->breakpoint_string[i] = m->breakpoint_string[i+1];
			m->breakpoint_flags[i]  = m->breakpoint_flags[i+1];
		}
		m->n_breakpoints --;
		return;
	}

	if (strncmp(cmd_line, "add ", 4) == 0) {
		uint64_t tmp;

		if (m->n_breakpoints >= MAX_BREAKPOINTS) {
			printf("Too many breakpoints. (You need to recompile"
			    " gxemul to increase this. Max = %i.)\n",
			    MAX_BREAKPOINTS);
			return;
		}

		i = m->n_breakpoints;

		res = debugger_parse_name(m, cmd_line + 4, 0, &tmp);
		if (!res) {
			printf("Couldn't parse '%s'\n", cmd_line + 4);
			return;
		}

		m->breakpoint_string[i] = malloc(strlen(cmd_line+4) + 1);
		if (m->breakpoint_string[i] == NULL) {
			printf("out of memory in debugger_cmd_breakpoint()\n");
			exit(1);
		}
		strcpy(m->breakpoint_string[i], cmd_line+4);
		m->breakpoint_addr[i] = tmp;
		m->breakpoint_flags[i] = 0;

		m->n_breakpoints ++;
		show_breakpoint(m, i);
		return;
	}

	printf("Unknown breakpoint subcommand.\n");
}


/*
 *  debugger_cmd_bintrans():
 */
static void debugger_cmd_bintrans(struct machine *m, char *cmd_line)
{
	int i;

	if (*cmd_line == '\0')
		goto printstate;

	if (!m->bintrans_enabled_from_start) {
		printf("You must have enabled bintrans from the start of the "
		    "simulation.\nIt is not possible to turn on afterwards.\n");
		return;
	}

	while (*cmd_line == ' ')
		cmd_line++;

	/*  Note: len 3 and 4, to include the NUL char.  */
	if (strncasecmp(cmd_line, "on", 3) == 0) {
		m->bintrans_enable = 1;
		for (i=0; i<m->ncpus; i++)
			bintrans_restart(m->cpus[i]);
	} else if (strncasecmp(cmd_line, "off", 4) == 0)
		m->bintrans_enable = 0;
	else
		printf("syntax: bintrans [on|off]\n");

printstate:
	printf("bintrans is now %s%s\n",
	     m->bintrans_enable? "ENABLED" : "disabled",
	     m->old_bintrans_enable? " (using the OLD bintrans system)" : "");
}


/*
 *  debugger_cmd_continue():
 */
static void debugger_cmd_continue(struct machine *m, char *cmd_line)
{
	if (*cmd_line) {
		printf("syntax: continue\n");
		return;
	}

	exit_debugger = 1;
}


/*
 *  debugger_cmd_device():
 */
static void debugger_cmd_device(struct machine *m, char *cmd_line)
{
	int i, j;
	struct memory *mem;
	struct cpu *c;

	if (cmd_line[0] == '\0')
		goto return_help;

	if (m->cpus == NULL) {
		printf("No cpus (?)\n");
		return;
	}
	c = m->cpus[m->bootstrap_cpu];
	if (c == NULL) {
		printf("m->cpus[m->bootstrap_cpu] = NULL\n");
		return;
	}
	mem = m->cpus[m->bootstrap_cpu]->mem;

	if (m->cpus == NULL) {
		printf("No cpus (?)\n");
		return;
	}
	c = m->cpus[m->bootstrap_cpu];
	if (c == NULL) {
		printf("m->cpus[m->bootstrap_cpu] = NULL\n");
		return;
	}
	mem = m->cpus[m->bootstrap_cpu]->mem;

	if (strcmp(cmd_line, "all") == 0) {
		device_dumplist();
	} else if (strncmp(cmd_line, "add ", 4) == 0) {
		device_add(m, cmd_line+4);
	} else if (strncmp(cmd_line, "remove ", 7) == 0) {
		i = atoi(cmd_line + 7);
		if (i==0 && cmd_line[7]!='0') {
			printf("Weird device number. Use 'device list'.\n");
		} else
			memory_device_remove(m->memory, i);
	} else if (strncmp(cmd_line, "state ", 6) == 0) {
		i = atoi(cmd_line + 6);
		if (i < 0 || i >= mem->n_mmapped_devices) {
			printf("No devices with that id.\n");
			return;
		}

		if (mem->dev_f_state[i] == NULL) {
			printf("No state function has been implemented yet "
			    "for that device type.\n");
			return;
		}

		for (j=0; ; j++) {
			int type;
			char *name;
			void *data;
			size_t len;
			int res = mem->dev_f_state[i](c, mem,
			    mem->dev_extra[i], 0, j, &type, &name, &data, &len);
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
	} else if (strcmp(cmd_line, "list") == 0) {
		if (mem->n_mmapped_devices == 0)
			printf("No memory-mapped devices in this machine.\n");

		for (i=0; i<mem->n_mmapped_devices; i++) {
			printf("%2i: %25s @ 0x%011llx, len = 0x%llx",
			    i, mem->dev_name[i],
			    (long long)mem->dev_baseaddr[i],
			    (long long)mem->dev_length[i]);
			if (mem->dev_flags[i]) {
				printf(" (");
				if (mem->dev_flags[i] & MEM_BINTRANS_OK)
					printf("BINTRANS R");
				if (mem->dev_flags[i] & MEM_BINTRANS_WRITE_OK)
					printf("+W");
				printf(")");
			}
			printf("\n");
		}
	} else
		goto return_help;

	return;

return_help:
	printf("syntax: devices cmd [...]\n");
	printf("Available cmds are:\n");
	printf("  add name_and_params    add a device to the current "
	    "machine\n");
	printf("  all                    list all registered devices\n");
	printf("  list                   list memory-mapped devices in the"
	    " current machine\n");
	printf("  remove x               remove device nr x from the "
	    "current machine\n");
	printf("  state x                show state of device nr x in"
	    " the current machine\n");
}


/*
 *  debugger_cmd_dump():
 *
 *  Dump emulated memory in hex and ASCII.
 *
 *  syntax: dump [addr [endaddr]]
 */
static void debugger_cmd_dump(struct machine *m, char *cmd_line)
{
	uint64_t addr, addr_start, addr_end;
	struct cpu *c;
	struct memory *mem;
	char *p = NULL;
	int x, r;

	if (cmd_line[0] != '\0') {
		uint64_t tmp;
		char *tmps = strdup(cmd_line);

		/*  addr:  */
		p = strchr(tmps, ' ');
		if (p != NULL)
			*p = '\0';
		r = debugger_parse_name(m, tmps, 0, &tmp);
		free(tmps);

		if (r == NAME_PARSE_NOMATCH || r == NAME_PARSE_MULTIPLE) {
			printf("Unparsable address: %s\n", cmd_line);
			return;
		} else {
			last_dump_addr = tmp;
		}

		p = strchr(cmd_line, ' ');
	}

	addr_start = last_dump_addr;

	if (addr_start == MAGIC_UNTOUCHED) {
		uint64_t tmp;
		int match_register = 0;
		cpu_register_match(m, "pc", 0, &tmp, &match_register);
		if (match_register) {
			addr_start = tmp;
		} else {
			printf("No starting address.\n");
			return;
		}
	}

	addr_end = addr_start + 16 * 16;

	/*  endaddr:  */
	if (p != NULL) {
		while (*p == ' ' && *p)
			p++;
		r = debugger_parse_name(m, p, 0, &addr_end);
		if (r == NAME_PARSE_NOMATCH || r == NAME_PARSE_MULTIPLE) {
			printf("Unparsable address: %s\n", cmd_line);
			return;
		}
	}

	if (m->cpus == NULL) {
		printf("No cpus (?)\n");
		return;
	}
	c = m->cpus[m->bootstrap_cpu];
	if (c == NULL) {
		printf("m->cpus[m->bootstrap_cpu] = NULL\n");
		return;
	}
	mem = m->cpus[m->bootstrap_cpu]->mem;

	addr = addr_start & ~0xf;

	ctrl_c = 0;

	while (addr < addr_end) {
		unsigned char buf[16];
		memset(buf, 0, sizeof(buf));
		r = c->memory_rw(c, mem, addr, &buf[0], sizeof(buf),
		    MEM_READ, CACHE_NONE | NO_EXCEPTIONS);

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

		if (ctrl_c)
			return;

		addr += sizeof(buf);
	}

	last_dump_addr = addr_end;

	strcpy(repeat_cmd, "dump");
}


/*
 *  debugger_cmd_emuls():
 *
 *  Dump info about all current emuls.
 */
static void debugger_cmd_emuls(struct machine *m, char *cmd_line)
{
	int i, iadd = 4;

	if (*cmd_line) {
		printf("syntax: emuls\n");
		return;
	}

	for (i=0; i<debugger_n_emuls; i++) {
		struct emul *e = debugger_emuls[i];

		if (e == NULL)
			continue;

		debug("emulation %i: \"%s\"\n", i,
		    e->name == NULL? "(no name)" : e->name);
		debug_indentation(iadd);

		emul_dumpinfo(e);

		debug_indentation(-iadd);
	}
}


/*
 *  debugger_cmd_focus():
 *
 *  Changes focus to specific machine (in a specific emulation).
 */
static void debugger_cmd_focus(struct machine *m, char *cmd_line)
{
	int x = -1, y = -1;
	char *p;

	if (!cmd_line[0]) {
		printf("syntax: focus x[,y]\n");
		printf("where x and y are integers as reported by the"
		    " 'emuls' command\n");
		goto print_current_focus_and_return;
	}

	x = atoi(cmd_line);
	p = strchr(cmd_line, ',');
	if (p == cmd_line) {
		printf("No machine number specified?\n");
		printf("syntax: focus x[,y]\n");
		return;
	}

	if (p != NULL)
		y = atoi(p + 1);

	if (y != -1) {
		/*  Change emul:  */
		if (y < 0 || y >= debugger_n_emuls) {
			printf("Invalid emul number: %i\n", y);
			return;
		}

		debugger_emul = debugger_emuls[y];

		/*  This is just in case the machine change below fails...  */
		debugger_machine = debugger_emul->machines[0];
	}

	/*  Change machine:  */
	if (x < 0 || x >= debugger_emul->n_machines) {
		printf("Invalid machine number: %i\n", x);
		return;
	}

	debugger_machine = debugger_emul->machines[x];

print_current_focus_and_return:
	printf("current emul: \"%s\"\n", debugger_emul->name == NULL?
	    "(no name)" : debugger_emul->name);
	printf("current machine: \"%s\"\n", debugger_machine->name == NULL?
	    "(no name)" : debugger_machine->name);
}


/*  This is defined below.  */
static void debugger_cmd_help(struct machine *m, char *cmd_line);


/*
 *  debugger_cmd_itrace():
 */
static void debugger_cmd_itrace(struct machine *m, char *cmd_line)
{
	if (*cmd_line) {
		printf("syntax: itrace\n");
		return;
	}

	old_instruction_trace = 1 - old_instruction_trace;
	printf("instruction_trace = %s\n", old_instruction_trace? "ON":"OFF");
	/*  TODO: how to preserve quiet_mode?  */
	old_quiet_mode = 0;
	printf("quiet_mode = %s\n", old_quiet_mode? "ON" : "OFF");
}


/*
 *  debugger_cmd_lookup():
 */
static void debugger_cmd_lookup(struct machine *m, char *cmd_line)
{
	uint64_t addr;
	int res;
	char *symbol;
	uint64_t offset;

	if (cmd_line[0] == '\0') {
		printf("syntax: lookup name|addr\n");
		return;

	}

	/*  Addresses never need to be given in decimal form anyway,
	    so assuming hex here will be ok.  */
	addr = strtoull(cmd_line, NULL, 16);

	if (addr == 0) {
		uint64_t newaddr;
		res = get_symbol_addr(&m->symbol_context,
		    cmd_line, &newaddr);
		if (!res) {
			printf("lookup for '%s' failed\n", cmd_line);
			return;
		}
		printf("%s = 0x%016llx\n", cmd_line, (long long)newaddr);
		return;
	}

	symbol = get_symbol_name(&m->symbol_context, addr, &offset);

	if (symbol != NULL)
		printf("0x%016llx = %s\n", (long long)addr, symbol);
	else
		printf("lookup for '%s' failed\n", cmd_line);
}


/*
 *  debugger_cmd_machine():
 *
 *  Dump info about the currently focused machine.
 */
static void debugger_cmd_machine(struct machine *m, char *cmd_line)
{
	int iadd = 4;

	if (*cmd_line) {
		printf("syntax: machine\n");
		return;
	}

	debug("machine \"%s\":\n", m->name);
	debug_indentation(iadd);
	machine_dumpinfo(m);
	debug_indentation(-iadd);
}


/*
 *  debugger_cmd_opcodestats():
 */
static void debugger_cmd_opcodestats(struct machine *m, char *cmd_line)
{
	if (*cmd_line) {
		printf("syntax: opcodestats\n");
		return;
	}

	if (!show_opcode_statistics) {
		printf("You need to start the emulator "
		    "with -s, if you want to gather statistics.\n");
	} else
		cpu_show_full_statistics(m);
}


/*
 *  debugger_cmd_pause():
 */
static void debugger_cmd_pause(struct machine *m, char *cmd_line)
{
	int cpuid = -1;

	if (cmd_line[0] != '\0')
		cpuid = atoi(cmd_line);
	else {
		printf("syntax: pause cpuid\n");
		return;
	}

	if (cpuid < 0 || cpuid >= m->ncpus) {
		printf("cpu%i doesn't exist.\n", cpuid);
		return;
	}

	m->cpus[cpuid]->running ^= 1;

	printf("cpu%i (%s) in machine \"%s\" is now %s\n", cpuid,
	    m->cpus[cpuid]->name, m->name,
	    m->cpus[cpuid]->running? "RUNNING" : "STOPPED");
}


/*
 *  debugger_cmd_print():
 */
static void debugger_cmd_print(struct machine *m, char *cmd_line)
{
	int res;
	uint64_t tmp;

	while (cmd_line[0] != '\0' && cmd_line[0] == ' ')
		cmd_line ++;

	if (cmd_line[0] == '\0') {
		printf("syntax: print expr\n");
		return;
	}

	res = debugger_parse_name(m, cmd_line, 0, &tmp);
	switch (res) {
	case NAME_PARSE_NOMATCH:
		printf("No match.\n");
		break;
	case NAME_PARSE_MULTIPLE:
		printf("Multiple matches. Try prefixing with %%, $, or @.\n");
		break;
	case NAME_PARSE_REGISTER:
		printf("%s = 0x%llx\n", cmd_line, (long long)tmp);
		break;
	case NAME_PARSE_SYMBOL:
		printf("%s = 0x%016llx\n", cmd_line, (long long)tmp);
		break;
	case NAME_PARSE_NUMBER:
		printf("0x%llx\n", (long long)tmp);
		break;
	}
}


/*
 *  debugger_cmd_put():
 */
static void debugger_cmd_put(struct machine *m, char *cmd_line)
{
	static char put_type = ' ';  /*  Remembered across multiple calls.  */
	char copy[200];
	int res, syntax_ok = 0;
	char *p, *p2, *q = NULL;
	uint64_t addr, data;
	unsigned char a_byte;

	strncpy(copy, cmd_line, sizeof(copy));
	copy[sizeof(copy)-1] = '\0';

	/*  syntax: put [b|h|w|d|q] addr, data  */

	p = strchr(copy, ',');
	if (p != NULL) {
		*p++ = '\0';
		while (*p == ' ' && *p)
			p++;
		while (strlen(copy) >= 1 &&
		    copy[strlen(copy) - 1] == ' ')
			copy[strlen(copy) - 1] = '\0';

		/*  printf("L = '%s', R = '%s'\n", copy, p);  */

		q = copy;
		p2 = strchr(q, ' ');

		if (p2 != NULL) {
			*p2 = '\0';
			if (strlen(q) != 1) {
				printf("Invalid type '%s'\n", q);
				return;
			}
			put_type = *q;
			q = p2 + 1;
		}

		/*  printf("type '%c', L '%s', R '%s'\n", put_type, q, p);  */
		syntax_ok = 1;
	}

	if (!syntax_ok) {
		printf("syntax: put [b|h|w|d|q] addr, data\n");
		printf("   b    byte        (8 bits)\n");
		printf("   h    half-word   (16 bits)\n");
		printf("   w    word        (32 bits)\n");
		printf("   d    doubleword  (64 bits)\n");
		printf("   q    quad-word   (128 bits)\n");
		return;
	}

	if (put_type == ' ') {
		printf("No type specified.\n");
		return;
	}

	/*  here: q is the address, p is the data.  */

	res = debugger_parse_name(m, q, 0, &addr);
	switch (res) {
	case NAME_PARSE_NOMATCH:
		printf("Couldn't parse the address.\n");
		return;
	case NAME_PARSE_MULTIPLE:
		printf("Multiple matches for the address."
		    " Try prefixing with %%, $, or @.\n");
		return;
	case NAME_PARSE_REGISTER:
	case NAME_PARSE_SYMBOL:
	case NAME_PARSE_NUMBER:
		break;
	default:
		printf("INTERNAL ERROR in debugger.c.\n");
		return;
	}

	res = debugger_parse_name(m, p, 0, &data);
	switch (res) {
	case NAME_PARSE_NOMATCH:
		printf("Couldn't parse the data.\n");
		return;
	case NAME_PARSE_MULTIPLE:
		printf("Multiple matches for the data value."
		    " Try prefixing with %%, $, or @.\n");
		return;
	case NAME_PARSE_REGISTER:
	case NAME_PARSE_SYMBOL:
	case NAME_PARSE_NUMBER:
		break;
	default:
		printf("INTERNAL ERROR in debugger.c.\n");
		return;
	}

	/*  TODO: haha, maybe this should be refactored  */

	switch (put_type) {
	case 'b':
		a_byte = data;
		printf("0x%016llx: %02x", (long long)addr, a_byte);
		if (data > 255)
			printf(" (NOTE: truncating %0llx)", (long long)data);
		res = m->cpus[0]->memory_rw(m->cpus[0], m->cpus[0]->mem, addr,
		    &a_byte, 1, MEM_WRITE, CACHE_NONE | NO_EXCEPTIONS);
		if (!res)
			printf("  FAILED!\n");
		printf("\n");
		return;
	case 'h':
		if ((data & 1) != 0)
			printf("WARNING: address isn't aligned\n");
		printf("0x%016llx: %04x", (long long)addr, (int)data);
		if (data > 0xffff)
			printf(" (NOTE: truncating %0llx)", (long long)data);
		res = store_16bit_word(m->cpus[0], addr, data);
		if (!res)
			printf("  FAILED!\n");
		printf("\n");
		return;
	case 'w':
		if ((data & 3) != 0)
			printf("WARNING: address isn't aligned\n");
		printf("0x%016llx: %08x", (long long)addr, (int)data);
		if (data > 0xffffffff && (data >> 32) != 0
		    && (data >> 32) != 0xffffffff)
			printf(" (NOTE: truncating %0llx)", (long long)data);
		res = store_32bit_word(m->cpus[0], addr, data);
		if (!res)
			printf("  FAILED!\n");
		printf("\n");
		return;
	case 'd':
		if ((data & 7) != 0)
			printf("WARNING: address isn't aligned\n");
		printf("0x%016llx: %016llx", (long long)addr, (long long)data);
		res = store_64bit_word(m->cpus[0], addr, data);
		if (!res)
			printf("  FAILED!\n");
		printf("\n");
		return;
	case 'q':
		printf("quad-words: TODO\n");
		/*  TODO  */
		return;
	default:
		printf("Unimplemented type '%c'\n", put_type);
		return;
	}
}


/*
 *  debugger_cmd_quiet():
 */
static void debugger_cmd_quiet(struct machine *m, char *cmd_line)
{
	int toggle = 1;
	int previous_mode = old_quiet_mode;

	if (cmd_line[0] != '\0') {
		while (cmd_line[0] != '\0' && cmd_line[0] == ' ')
			cmd_line ++;
		switch (cmd_line[0]) {
		case '0':
			toggle = 0;
			old_quiet_mode = 0;
			break;
		case '1':
			toggle = 0;
			old_quiet_mode = 1;
			break;
		case 'o':
		case 'O':
			toggle = 0;
			switch (cmd_line[1]) {
			case 'n':
			case 'N':
				old_quiet_mode = 1;
				break;
			default:
				old_quiet_mode = 0;
			}
			break;
		default:
			printf("syntax: quiet [on|off]\n");
			return;
		}
	}

	if (toggle)
		old_quiet_mode = 1 - old_quiet_mode;

	printf("quiet_mode = %s", old_quiet_mode? "ON" : "OFF");
	if (old_quiet_mode != previous_mode)
		printf("  (was: %s)", previous_mode? "ON" : "OFF");
	printf("\n");
}


/*
 *  debugger_cmd_quit():
 */
static void debugger_cmd_quit(struct machine *m, char *cmd_line)
{
	int i, j, k;
	struct emul *e;

	if (*cmd_line) {
		printf("syntax: quit\n");
		return;
	}

	for (i=0; i<debugger_n_emuls; i++) {
		single_step = 0;

		e = debugger_emuls[i];
		force_debugger_at_exit = 0;

		for (j=0; j<e->n_machines; j++) {
			struct machine *m = e->machines[j];

			for (k=0; k<m->ncpus; k++)
				m->cpus[k]->running = 0;

			m->exit_without_entering_debugger = 1;
		}
	}

	exit_debugger = 1;
}


/*
 *  debugger_cmd_reg():
 */
static void debugger_cmd_reg(struct machine *m, char *cmd_line)
{
	int i, cpuid = -1, coprocnr = -1;
	int gprs, coprocs;
	char *p;

	/*  [cpuid][,c]  */
	if (cmd_line[0] != '\0') {
		if (cmd_line[0] != ',') {
			cpuid = strtoull(cmd_line, NULL, 0);
			if (cpuid < 0 || cpuid >= m->ncpus) {
				printf("cpu%i doesn't exist.\n", cpuid);
				return;
			}
		}
		p = strchr(cmd_line, ',');
		if (p != NULL) {
			coprocnr = atoi(p + 1);
			if (coprocnr < 0 || coprocnr >= 4) {
				printf("Invalid coprocessor number.\n");
				return;
			}
		}
	}

	gprs = (coprocnr == -1)? 1 : 0;
	coprocs = (coprocnr == -1)? 0x0 : (1 << coprocnr);

	for (i=0; i<m->ncpus; i++)
		if (cpuid == -1 || i == cpuid)
			cpu_register_dump(m, m->cpus[i], gprs, coprocs);
}


/*
 *  debugger_cmd_step():
 */
static void debugger_cmd_step(struct machine *m, char *cmd_line)
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
static void debugger_cmd_tlbdump(struct machine *m, char *cmd_line)
{
	int x = -1;
	int rawflag = 0;

	if (cmd_line[0] != '\0') {
		char *p;
		if (cmd_line[0] != ',') {
			x = strtoull(cmd_line, NULL, 0);
			if (x < 0 || x >= m->ncpus) {
				printf("cpu%i doesn't exist.\n", x);
				return;
			}
		}
		p = strchr(cmd_line, ',');
		if (p != NULL) {
			switch (p[1]) {
			case 'r':
			case 'R':
				rawflag = 1;
				break;
			default:
				printf("Unknown tlbdump flag.\n");
				printf("syntax: tlbdump [cpuid][,r]\n");
				return;
			}
		}
	}

	cpu_tlbdump(m, x, rawflag);
}


/*
 *  debugger_cmd_trace():
 */
static void debugger_cmd_trace(struct machine *m, char *cmd_line)
{
	if (*cmd_line) {
		printf("syntax: trace\n");
		return;
	}

	old_show_trace_tree = 1 - old_show_trace_tree;
	printf("show_trace_tree = %s\n", old_show_trace_tree? "ON" : "OFF");

	if (m->bintrans_enable && old_show_trace_tree)
		printf("NOTE: the trace tree functionality doesn't "
		    "work very well with bintrans!\n");

	/*  TODO: how to preserve quiet_mode?  */
	old_quiet_mode = 0;
	printf("quiet_mode = %s\n", old_quiet_mode? "ON" : "OFF");
}


/*
 *  debugger_cmd_unassemble():
 *
 *  Dump emulated memory as instructions.
 *
 *  syntax: unassemble [addr [endaddr]]
 */
static void debugger_cmd_unassemble(struct machine *m, char *cmd_line)
{
	uint64_t addr, addr_start, addr_end;
	struct cpu *c;
	struct memory *mem;
	char *p = NULL;
	int r, lines_left = -1;

	if (cmd_line[0] != '\0') {
		uint64_t tmp;
		char *tmps = strdup(cmd_line);

		/*  addr:  */
		p = strchr(tmps, ' ');
		if (p != NULL)
			*p = '\0';
		r = debugger_parse_name(m, tmps, 0, &tmp);
		free(tmps);

		if (r == NAME_PARSE_NOMATCH || r == NAME_PARSE_MULTIPLE) {
			printf("Unparsable address: %s\n", cmd_line);
			return;
		} else {
			last_unasm_addr = tmp;
		}

		p = strchr(cmd_line, ' ');
	}

	addr_start = last_unasm_addr;

	if (addr_start == MAGIC_UNTOUCHED) {
		uint64_t tmp;
		int match_register = 0;
		cpu_register_match(m, "pc", 0, &tmp, &match_register);
		if (match_register) {
			addr_start = tmp;
		} else {
			printf("No starting address.\n");
			return;
		}
	}

	addr_end = addr_start + 1000;

	/*  endaddr:  */
	if (p != NULL) {
		while (*p == ' ' && *p)
			p++;
		r = debugger_parse_name(m, p, 0, &addr_end);
		if (r == NAME_PARSE_NOMATCH || r == NAME_PARSE_MULTIPLE) {
			printf("Unparsable address: %s\n", cmd_line);
			return;
		}
	} else
		lines_left = 20;

	if (m->cpus == NULL) {
		printf("No cpus (?)\n");
		return;
	}
	c = m->cpus[m->bootstrap_cpu];
	if (c == NULL) {
		printf("m->cpus[m->bootstrap_cpu] = NULL\n");
		return;
	}
	mem = m->cpus[m->bootstrap_cpu]->mem;

	addr = addr_start;

	ctrl_c = 0;

	while (addr < addr_end) {
		int i, len;
		unsigned char buf[32];	/*  TODO: How long can an
					    instruction be, on weird archs?  */
		memset(buf, 0, sizeof(buf));

		for (i=0; i<sizeof(buf); i++)
			c->memory_rw(c, mem, addr+i, buf+i, 1, MEM_READ,
			    CACHE_NONE | NO_EXCEPTIONS);

		len = cpu_disassemble_instr(m, c, buf, 0, addr, 0);

		if (ctrl_c)
			return;
		if (len == 0)
			break;

		addr += len;

		if (lines_left != -1) {
			lines_left --;
			if (lines_left == 0)
				break;
		}
	}

	last_unasm_addr = addr;

	strcpy(repeat_cmd, "unassemble");
}


/*
 *  debugger_cmd_version():
 */
static void debugger_cmd_version(struct machine *m, char *cmd_line)
{
	if (*cmd_line) {
		printf("syntax: version\n");
		return;
	}

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
	void	(*f)(struct machine *, char *cmd_line);
	char	*description;
};

static struct cmd cmds[] = {
	{ "breakpoint", "...", 0, debugger_cmd_breakpoint,
		"manipulate breakpoints" },

	{ "bintrans", "[on|off]", 0, debugger_cmd_bintrans,
		"toggle bintrans on or off" },

	/*  NOTE: Try to keep 'c' down to only one command. Having 'continue'
	    available as a one-letter command is very convenient.  */

	{ "continue", "", 0, debugger_cmd_continue,
		"continue execution" },

	{ "device", "...", 0, debugger_cmd_device,
		"show info about (or manipulate) devices" },

	{ "dump", "[addr [endaddr]]", 0, debugger_cmd_dump,
		"dump memory contents in hex and ASCII" },

	{ "emuls", "", 0, debugger_cmd_emuls,
		"print a summary of all current emuls" },

	{ "focus", "x[,y]", 0, debugger_cmd_focus,
		"changes focus to machine x (in emul y)" },

	{ "help", "", 0, debugger_cmd_help,
		"print this help message" },

	{ "itrace", "", 0, debugger_cmd_itrace,
		"toggle instruction_trace on or off" },

	{ "lookup", "name|addr", 0, debugger_cmd_lookup,
		"lookup a symbol by name or address" },

	{ "machine", "", 0, debugger_cmd_machine,
		"print a summary of the current machine" },

	{ "opcodestats", "", 0, debugger_cmd_opcodestats,
		"show opcode statistics" },

	{ "pause", "cpuid", 0, debugger_cmd_pause,
		"pause (or unpause) a CPU" },

	{ "print", "expr", 0, debugger_cmd_print,
		"evaluate an expression without side-effects" },

	{ "put", "[b|h|w|d|q] addr, data", 0, debugger_cmd_put,
		"modify emulated memory contents" },

	{ "quiet", "[on|off]", 0, debugger_cmd_quiet,
		"toggle quiet_mode on or off" },

	{ "quit", "", 0, debugger_cmd_quit,
		"quit the emulator" },

	{ "reg", "[cpuid][,c]", 0, debugger_cmd_reg,
		"show GPRs (or coprocessor c's registers)" },

	/*  NOTE: Try to keep 's' down to only one command. Having 'step'
	    available as a one-letter command is very convenient.  */

	{ "step", "[n]", 0, debugger_cmd_step,
		"single-step one (or n) instruction(s)" },

	{ "tlbdump", "[cpuid][,r]", 0, debugger_cmd_tlbdump,
		"dump TLB contents (add ',r' for raw data)" },

	{ "trace", "", 0, debugger_cmd_trace,
		"toggle show_trace_tree on or off" },

	{ "unassemble", "[addr [endaddr]]", 0, debugger_cmd_unassemble,
		"dump memory contents as instructions" },

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
static void debugger_cmd_help(struct machine *m, char *cmd_line)
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

		printf("   %s\n", cmds[i].description);
		i++;
	}

	printf("Generic assignments:   x = expr\n");
	printf("where x must be a register, and expr can be a register, a "
	    "numeric value, or\na symbol name (+ an optional numeric offset)."
	    " In case there are multiple\nmatches (ie a symbol that has the "
	    "same name as a register), you may add a\nprefix character as a "
	    "hint: '%%' for registers, '@' for symbols, and\n'$' for numeric"
	    " values. Use 0x for hexadecimal values.\n");
}


/****************************************************************************/


/*
 *  debugger_assignment():
 *
 *  cmd contains something like "pc=0x80001000", or "r31=memcpy+0x40".
 */
void debugger_assignment(struct machine *m, char *cmd)
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

	res_right = debugger_parse_name(m, right, 0, &tmp);
	switch (res_right) {
	case NAME_PARSE_NOMATCH:
		printf("No match for the right-hand side of the assignment.\n");
		break;
	case NAME_PARSE_MULTIPLE:
		printf("Multiple matches for the right-hand side of the "
		    "assignment.\n");
		break;
	default:
		res_left = debugger_parse_name(m, left, 1, &tmp);
		switch (res_left) {
		case NAME_PARSE_NOMATCH:
			printf("No match for the left-hand side of the "
			    "assignment.\n");
			break;
		case NAME_PARSE_MULTIPLE:
			printf("Multiple matches for the left-hand side "
			    "of the assignment.\n");
			break;
		default:
			debugger_cmd_print(m, left);
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
	int ch, i, j, n, i_match, reallen, cmd_len, cursor_pos;
	int read_from_index = last_cmd_index;
	char *cmd = last_cmd[last_cmd_index];

	cmd_len = 0; cmd[0] = '\0';
	printf("GXemul> ");
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
		while ((ch = console_readchar(MAIN_CONSOLE)) < 0) {
			x11_check_event(debugger_emuls, debugger_n_emuls);
			usleep(2);
		}

		if ((ch == '\b' || ch == 127) && cursor_pos > 0) {
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
		} else if (ch == 2) {
			/*  CTRL-B: Backwards one character.  */
			if (cursor_pos > 0) {
				printf("\b");
				cursor_pos --;
			}
		} else if (ch == 5) {
			/*  CTRL-E: End of line.  */
			while (cursor_pos < cmd_len) {
				printf("%c", cmd[cursor_pos]);
				cursor_pos ++;
			}
		} else if (ch == 6) {
			/*  CTRL-F: Forward one character.  */
			if (cursor_pos < cmd_len) {
				printf("%c",
				    cmd[cursor_pos]);
				cursor_pos ++;
			}
		} else if (ch == 11) {
			/*  CTRL-K: Kill to end of line.  */
			for (i=0; i<MAX_CMD_LEN; i++)
				console_makeavail(MAIN_CONSOLE, 4); /*  :-)  */
		} else if (ch == 14 || ch == 16) {
			/*  CTRL-P: Previous line in the command history,
			    CTRL-N: next line  */
			do {
				if (ch == 14 &&
				    read_from_index == last_cmd_index)
					break;
				if (ch == 16)
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
			} while (0);
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
					console_makeavail(MAIN_CONSOLE,
					    cmds[i_match].name[i]);
				/*  ... and a space, if the command takes
				    any arguments:  */
				if (cmds[i_match].args != NULL &&
				    cmds[i_match].args[0] != '\0')
					console_makeavail(MAIN_CONSOLE, ' ');
				break;
			default:
				/*  Show all possible commands:  */
				printf("\a\n");	/*  Beep. :-)  */
				i = 0;		/*  i = cmds index  */
				j = 0;		/*  j = # of cmds printed  */
				while (cmds[i].name != NULL) {
					if (cmds[i].tmp_flag) {
						int q;
						if (j == 0)
							printf("  ");
						printf("%s",
						    cmds[i].name);
						j++;
						if (j != 6)
							for (q=0; q<13-strlen(
							    cmds[i].name); q++)
								printf(" ");
						if (j == 6) {
							printf("\n");
							j = 0;
						}
					}
					i++;
				}
				if (j != 0)
					printf("\n");
				printf("GXemul> ");
				for (i=0; i<cmd_len; i++)
					printf("%c", cmd[i]);
			}
		} else if (ch == 27) {
			/*  Escape codes: (cursor keys etc)  */
			while ((ch = console_readchar(MAIN_CONSOLE)) < 0)
				usleep(1);
			if (ch == '[' || ch == 'O') {
				while ((ch = console_readchar(MAIN_CONSOLE))
				    < 0)
					usleep(1);
				switch (ch) {
				case '2':	/*  2~ = ins  */
				case '5':	/*  5~ = pgup  */
				case '6':	/*  6~ = pgdn  */
					/*  TODO: Ugly hack, but might work.  */
					while ((ch = console_readchar(
					    MAIN_CONSOLE)) < 0)
						usleep(1);
					/*  Do nothing for these keys.  */
					break;
				case '3':	/*  3~ = delete  */
					/*  TODO: Ugly hack, but might work.  */
					while ((ch = console_readchar(
					    MAIN_CONSOLE)) < 0)
						usleep(1);
					console_makeavail(MAIN_CONSOLE, '\b');
					break;
				case 'A':	/*  Up.  */
					/*  Up cursor ==> CTRL-P  */
					console_makeavail(MAIN_CONSOLE, 16);
					break;
				case 'B':	/*  Down.  */
					/*  Down cursor ==> CTRL-N  */
					console_makeavail(MAIN_CONSOLE, 14);
					break;
				case 'C':
					/*  Right cursor ==> CTRL-F  */
					console_makeavail(MAIN_CONSOLE, 6);
					break;
				case 'D':	/*  Left  */
					/*  Left cursor ==> CTRL-B  */
					console_makeavail(MAIN_CONSOLE, 2);
					break;
				case 'F':
					/*  End ==> CTRL-E  */
					console_makeavail(MAIN_CONSOLE, 5);
					break;
				case 'H':
					/*  Home ==> CTRL-A  */
					console_makeavail(MAIN_CONSOLE, 1);
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

		/*
		 *  Is there a '=' on the command line? Then try to do an
		 *  assignment.  (Only if there is just one word, followed
		 *  by the '=' sign. This makes it possible to use commands
		 *  such as "device add name addr=xyz".)
		 */
		if (strchr(cmd, '=') != NULL) {
			/*  Count the nr of words:  */
			int nw = 0, inword = 0;
			char *p = cmd;
			while (*p) {
				if (*p == '=')
					break;
				if (*p != ' ') {
					if (!inword)
						nw ++;
					inword = 1;
				} else
					inword = 0;
				p++;
			}

			if (nw == 1) {
				debugger_assignment(debugger_machine, cmd);
				continue;
			}
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
			cmds[i_match].f(debugger_machine, p);
		} else
			printf("FATAL ERROR: internal error in debugger.c:"
			    " no handler for this command?\n");

		/*  Special hack for the "step" command:  */
		if (exit_debugger == -1)
			return;
	}

	single_step = 0;
	debugger_machine->instruction_trace = old_instruction_trace;
	debugger_machine->show_trace_tree = old_show_trace_tree;
	quiet_mode = old_quiet_mode;
}


/*
 *  debugger_reset():
 *
 *  This function should be called before calling debugger(), when it is
 *  absolutely necessary that debugger() is interactive. Otherwise, it might
 *  return without doing anything, such as when single-stepping multiple
 *  instructions at a time.
 */
void debugger_reset(void)
{
	n_steps_left_before_interaction = 0;
}


/*
 *  debugger_init():
 *
 *  Must be called before any other debugger function is used.
 */
void debugger_init(struct emul **emuls, int n_emuls)
{
	int i;

	debugger_n_emuls = n_emuls;
	debugger_emuls = emuls;

	if (n_emuls < 1) {
		fprintf(stderr, "\nERROR: No emuls (?)\n");
		exit(1);
	}

	debugger_emul = emuls[0];
	if (emuls[0]->n_machines < 1) {
		fprintf(stderr, "\nERROR: No machines in emuls[0], "
		    "cannot handle this situation yet.\n\n");
		exit(1);
	}

	debugger_machine = emuls[0]->machines[0];

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

