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
 *  $Id: debugger.c,v 1.3 2004-12-14 03:29:26 debug Exp $
 *
 *  Single-step debugger.
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
 *
 *  (TODO: How to make these non-global in a nice way?)
 */

struct emul *debugger_emul;

int old_instruction_trace = 0;
int old_quiet_mode = 0;
int old_show_trace_tree = 0;

static int exit_debugger;

#define	MAX_CMD_LEN	60
static char last_cmd[MAX_CMD_LEN];
static int last_cmd_len = 0;

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


/****************************************************************************/


/*
 *  debugger_cmd_continue():
 */
static void debugger_cmd_continue(struct emul *emul, char *cmd_line)
{
	exit_debugger = 1;
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

	if (cmd_line[0] != '\0')
		last_dump_addr = strtoll(cmd_line + 1, NULL, 16);

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

	/*  Repetition of this command should be done with no args:  */
	last_cmd_len = 4;
	strcpy(last_cmd, "dump");
}


/*  This is defined below.  */
static void debugger_cmd_help(struct emul *emul, char *cmd_line);


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
 *  debugger_cmd_registers():
 */
static void debugger_cmd_registers(struct emul *emul, char *cmd_line)
{
	int i;

	for (i=0; i<emul->ncpus; i++)
		cpu_register_dump(emul->cpus[i]);

	last_cmd_len = 0;
}


/*
 *  debugger_cmd_step():
 */
static void debugger_cmd_step(struct emul *emul, char *cmd_line)
{
	/*  Special hack, see debugger() for more info.  */
	exit_debugger = -1;
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
	last_cmd_len = 0;
}


struct cmd {
	char	*name;
	char	*args;
	int	tmp_flag;
	void	(*f)(struct emul *, char *cmd_line);
	char	*description;
};

static struct cmd cmds[] = {
	"continue", "",	0, debugger_cmd_continue,
		"continue execution",

	"dump", "[addr]", 0, debugger_cmd_dump,
		"dump memory contents in hex and ASCII",

	"help", "", 0, debugger_cmd_help,
		"print this help message",

	"quit",	"", 0, debugger_cmd_quit,
		"quit the emulator",

	"quiet", "", 0, NULL /*  TODO  */,
		"toggle quiet_mode on or off",

	"registers", "", 0, debugger_cmd_registers,
		"dumps all CPUs' register values",

	"step", "", 0, debugger_cmd_step,
		"single step one instruction",

	"version", "", 0, debugger_cmd_version,
		"print version information",

	NULL, NULL, 0, NULL, NULL
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

		printf("     %s\n", cmds[i].description);
		i++;
	}

	last_cmd_len = 0;
}


/****************************************************************************/


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

		cpu_disassemble_instr(c, &buf[0], 0, addr, 0);

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
 *  debugger_readline():
 *
 *  Read a line from the terminal.
 *
 *  TODO: Cursor keys for line editing?
 */
static void debugger_readline(char *cmd, int max_cmd_len, int *cmd_len)
{
	int ch;

	*cmd_len = 0; cmd[0] = '\0';
	printf("mips64emul> ");
	fflush(stdout);

	ch = '\0';
	while (ch != '\n') {
		/*
		 *  TODO: This uses up 100% CPU, maybe that isn't too good.
		 *  The usleep() call might make it a tiny bit nicer on other
		 *  running processes, but it is still very ugly.
		 */
		ch = console_readchar();
		usleep(1);

		if (ch == '\b' && *cmd_len > 0) {
			*cmd_len --;
			cmd[*cmd_len] = '\0';
			printf("\b \b");
			fflush(stdout);
		} else if (ch >= ' ' && *cmd_len < max_cmd_len - 1) {
			cmd[(*cmd_len) ++] = ch;
			cmd[*cmd_len] = '\0';
			printf("%c", ch);
			fflush(stdout);
		} else if (ch == '\r' || ch == '\n') {
			ch = '\n';
			printf("\n");
		}
	}
}


/*
 *  debugger():
 *
 *  This is a loop, which reads a command from the terminal, and executes it.
 */
void debugger(void)
{
	int i, n, i_match, cmd_len, matchlen;
	char cmd[MAX_CMD_LEN];

	exit_debugger = 0;

	while (!exit_debugger) {
		/*  Read a line from the terminal:  */
		debugger_readline(&cmd[0], MAX_CMD_LEN, &cmd_len);

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

		/*  Remember this cmd:  */
		if (cmd_len > 0) {
			memcpy(last_cmd, cmd, cmd_len + 1);
			last_cmd_len = cmd_len;
		}

		/*  No command? Then read a new line.  */
		if (cmd_len == 0)
			continue;

		i = 0;
		while (cmds[i].name != NULL)
			cmds[i++].tmp_flag = 0;

		/*  How many chars in cmd to match against:  */
		matchlen = 0;
		while (isalpha(cmd[matchlen]))
			matchlen ++;

		/*  Check for a command name match:  */
		n = i = 0;
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
			last_cmd[0] = cmd[0] = '\0';
			last_cmd_len = cmd_len = 0;
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
			last_cmd[0] = cmd[0] = '\0';
			last_cmd_len = cmd_len = 0;
			continue;
		}

		/*  Exactly one match:  */
		if (cmds[i_match].f != NULL)
			cmds[i_match].f(debugger_emul, cmd + matchlen);
		else
			printf("FATAL ERROR: internal error in debugger.c:"
			    " no handler for this command?\n");

		/*  Special hack for the "step" command:  */
		if (exit_debugger == -1)
			return;


#if 0
			printf("  itrace              toggles instruction_trace on or off (currently %s)\n",
			    old_instruction_trace? "ON" : "OFF");
			printf("  tlbdump             dumps each CPU's TLB contents\n");
			printf("  trace               toggles show_trace_tree on or off (currently %s)\n",
			    old_show_trace_tree? "ON" : "OFF");
			printf("  unassemble [addr]   dumps emulated memory contents as MIPS instructions\n");

		} else if (strcasecmp(cmd, "i") == 0 ||
		    strcasecmp(cmd, "itrace") == 0) {
			old_instruction_trace = 1 - old_instruction_trace;
			printf("instruction_trace = %s\n",
			    old_instruction_trace? "ON" : "OFF");
			/*  TODO: how to preserve quiet_mode?  */
			old_quiet_mode = 0;
			printf("quiet_mode = %s\n",
			    old_quiet_mode? "ON" : "OFF");
		} else if (strcasecmp(cmd, "quiet") == 0) {
			old_quiet_mode = 1 - old_quiet_mode;
			printf("quiet_mode = %s\n",
			    old_quiet_mode? "ON" : "OFF");

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
			printf("quiet_mode = %s\n",
			    old_quiet_mode? "ON" : "OFF");
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
#endif
	}

	debugger_emul->single_step = 0;
	debugger_emul->instruction_trace = old_instruction_trace;
	debugger_emul->show_trace_tree = old_show_trace_tree;
	quiet_mode = old_quiet_mode;
}

