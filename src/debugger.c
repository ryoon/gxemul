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
 *  $Id: debugger.c,v 1.1 2004-12-14 02:21:21 debug Exp $
 *
 *  Single-step debugger.
 */

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

int old_instruction_trace = 0;
int old_quiet_mode = 0;
int old_show_trace_tree = 0;


#define	MAX_CMD_LEN	60


/*
 *  Global debugger variables:
 *
 *  (TODO: How to make these non-global in a nice way?)
 */

struct emul *debugger_emul;

static char last_cmd[MAX_CMD_LEN];
static int last_cmd_len = 0;


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
			printf("  quiet               toggles quiet_mode on or off (currently %s)\n",
			    old_quiet_mode? "ON" : "OFF");
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
			printf("quiet_mode = %s\n",
			    old_quiet_mode? "ON" : "OFF");
		} else if (strcasecmp(cmd, "quiet") == 0) {
			old_quiet_mode = 1 - old_quiet_mode;
			printf("quiet_mode = %s\n",
			    old_quiet_mode? "ON" : "OFF");
		} else if (strcasecmp(cmd, "quit") == 0) {
			for (i=0; i<debugger_emul->ncpus; i++)
				debugger_emul->cpus[i]->running = 0;
			debugger_emul->exit_without_entering_debugger = 1;
			debugger_emul->single_step = 0;
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


