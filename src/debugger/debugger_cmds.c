/*
 *  Copyright (C) 2004-2006  Anders Gavare.  All rights reserved.
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
 *  $Id: debugger_cmds.c,v 1.1 2006-05-05 05:32:46 debug Exp $
 *
 *  Debugger commands. Included from debugger.c.
 */


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

		/*  Clear translations:  */
		for (i=0; i<m->ncpus; i++)
			if (m->cpus[i]->translation_cache != NULL)
				cpu_create_or_reset_tc(m->cpus[i]);
		return;
	}

	if (strncmp(cmd_line, "add ", 4) == 0) {
		uint64_t tmp;
		size_t breakpoint_buf_len;

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

		breakpoint_buf_len = strlen(cmd_line+4) + 1;
		m->breakpoint_string[i] = malloc(breakpoint_buf_len);
		if (m->breakpoint_string[i] == NULL) {
			printf("out of memory in debugger_cmd_breakpoint()\n");
			exit(1);
		}
		strlcpy(m->breakpoint_string[i], cmd_line+4,
		    breakpoint_buf_len);
		m->breakpoint_addr[i] = tmp;
		m->breakpoint_flags[i] = 0;

		m->n_breakpoints ++;
		show_breakpoint(m, i);

		/*  Clear translations:  */
		for (i=0; i<m->ncpus; i++)
			if (m->cpus[i]->translation_cache != NULL)
				cpu_create_or_reset_tc(m->cpus[i]);
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
	int i;
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
	} else if (strcmp(cmd_line, "consoles") == 0) {
		console_debug_dump(m);
	} else if (strncmp(cmd_line, "remove ", 7) == 0) {
		i = atoi(cmd_line + 7);
		if (i==0 && cmd_line[7]!='0') {
			printf("Weird device number. Use 'device list'.\n");
		} else
			memory_device_remove(m->memory, i);
	} else if (strcmp(cmd_line, "list") == 0) {
		if (mem->n_mmapped_devices == 0)
			printf("No memory-mapped devices in this machine.\n");

		for (i=0; i<mem->n_mmapped_devices; i++) {
			printf("%2i: %25s @ 0x%011"PRIx64", len = 0x%"PRIx64,
			    i, mem->dev_name[i],
			    (uint64_t) mem->dev_baseaddr[i],
			    (uint64_t) mem->dev_length[i]);

			if (mem->dev_flags[i]) {
				printf(" (");
				if (mem->dev_flags[i] & DM_DYNTRANS_OK)
					printf("DYNTRANS R");
				if (mem->dev_flags[i] & DM_DYNTRANS_WRITE_OK)
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
	printf("  consoles               list all slave consoles\n");
	printf("  list                   list memory-mapped devices in the"
	    " current machine\n");
	printf("  remove x               remove device nr x from the "
	    "current machine\n");
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

		if (c->is_32bit)
			printf("0x%08"PRIx32"  ", (uint32_t) addr);
		else
			printf("0x%016"PRIx64"  ", (uint64_t) addr);

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

	strlcpy(repeat_cmd, "dump", MAX_CMD_BUFLEN);
}


/*
 *  debugger_cmd_emuls():
 *
 *  Dump info about all current emuls.
 */
static void debugger_cmd_emuls(struct machine *m, char *cmd_line)
{
	int i, iadd = DEBUG_INDENTATION;

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
		printf("%s = 0x", cmd_line);
		if (m->cpus[0]->is_32bit)
			printf("%08"PRIx32"\n", (uint32_t) newaddr);
		else
			printf("%016"PRIx64"\n", (uint64_t) newaddr);
		return;
	}

	symbol = get_symbol_name(&m->symbol_context, addr, &offset);

	if (symbol != NULL) {
		if (m->cpus[0]->is_32bit)
			printf("0x%08"PRIx32, (uint32_t) addr);
		else
			printf("0x%016"PRIx64, (uint64_t) addr);
		printf(" = %s\n", symbol);
	} else
		printf("lookup for '%s' failed\n", cmd_line);
}


/*
 *  debugger_cmd_machine():
 *
 *  Dump info about the currently focused machine.
 */
static void debugger_cmd_machine(struct machine *m, char *cmd_line)
{
	int iadd = DEBUG_INDENTATION;

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
 *  debugger_cmd_ninstrs():
 */
static void debugger_cmd_ninstrs(struct machine *m, char *cmd_line)
{
	int toggle = 1;
	int previous_mode = m->show_nr_of_instructions;

	if (cmd_line[0] != '\0') {
		while (cmd_line[0] != '\0' && cmd_line[0] == ' ')
			cmd_line ++;
		switch (cmd_line[0]) {
		case '0':
			toggle = 0;
			m->show_nr_of_instructions = 0;
			break;
		case '1':
			toggle = 0;
			m->show_nr_of_instructions = 1;
			break;
		case 'o':
		case 'O':
			toggle = 0;
			switch (cmd_line[1]) {
			case 'n':
			case 'N':
				m->show_nr_of_instructions = 1;
				break;
			default:
				m->show_nr_of_instructions = 0;
			}
			break;
		default:
			printf("syntax: trace [on|off]\n");
			return;
		}
	}

	if (toggle)
		m->show_nr_of_instructions = !m->show_nr_of_instructions;

	printf("show_nr_of_instructions = %s",
	    m->show_nr_of_instructions? "ON" : "OFF");
	if (m->show_nr_of_instructions != previous_mode)
		printf("  (was: %s)", previous_mode? "ON" : "OFF");
	printf("\n");
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
		printf("%s = 0x%"PRIx64"\n", cmd_line, (uint64_t)tmp);
		break;
	case NAME_PARSE_SYMBOL:
		if (m->cpus[0]->is_32bit)
			printf("%s = 0x%08"PRIx32"\n", cmd_line, (uint32_t)tmp);
		else
			printf("%s = 0x%016"PRIx64"\n", cmd_line,(uint64_t)tmp);
		break;
	case NAME_PARSE_NUMBER:
		printf("0x%"PRIx64"\n", (uint64_t) tmp);
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
		if (m->cpus[0]->is_32bit)
			printf("0x%08"PRIx32, (uint32_t) addr);
		else
			printf("0x%016"PRIx64, (uint64_t) addr);
		printf(": %02x", a_byte);
		if (data > 255)
			printf(" (NOTE: truncating %0"PRIx64")",
			    (uint64_t) data);
		res = m->cpus[0]->memory_rw(m->cpus[0], m->cpus[0]->mem, addr,
		    &a_byte, 1, MEM_WRITE, CACHE_NONE | NO_EXCEPTIONS);
		if (!res)
			printf("  FAILED!\n");
		printf("\n");
		return;
	case 'h':
		if ((addr & 1) != 0)
			printf("WARNING: address isn't aligned\n");
		if (m->cpus[0]->is_32bit)
			printf("0x%08"PRIx32, (uint32_t) addr);
		else
			printf("0x%016"PRIx64, (uint64_t) addr);
		printf(": %04x", (int)data);
		if (data > 0xffff)
			printf(" (NOTE: truncating %0"PRIx64")",
			    (uint64_t) data);
		res = store_16bit_word(m->cpus[0], addr, data);
		if (!res)
			printf("  FAILED!\n");
		printf("\n");
		return;
	case 'w':
		if ((addr & 3) != 0)
			printf("WARNING: address isn't aligned\n");
		if (m->cpus[0]->is_32bit)
			printf("0x%08"PRIx32, (uint32_t) addr);
		else
			printf("0x%016"PRIx64, (uint64_t) addr);

		printf(": %08x", (int)data);

		if (data > 0xffffffff && (data >> 32) != 0
		    && (data >> 32) != 0xffffffff)
			printf(" (NOTE: truncating %0"PRIx64")",
			    (uint64_t) data);

		res = store_32bit_word(m->cpus[0], addr, data);
		if (!res)
			printf("  FAILED!\n");
		printf("\n");
		return;
	case 'd':
		if ((addr & 7) != 0)
			printf("WARNING: address isn't aligned\n");
		if (m->cpus[0]->is_32bit)
			printf("0x%08"PRIx32, (uint32_t) addr);
		else
			printf("0x%016"PRIx64, (uint64_t) addr);

		printf(": %016"PRIx64, (uint64_t) data);

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

	debugger_n_steps_left_before_interaction = n - 1;

	/*  Special hack, see debugger() for more info.  */
	exit_debugger = -1;

	strlcpy(repeat_cmd, "step", MAX_CMD_BUFLEN);
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
	int toggle = 1;
	int previous_mode = old_show_trace_tree;

	if (cmd_line[0] != '\0') {
		while (cmd_line[0] != '\0' && cmd_line[0] == ' ')
			cmd_line ++;
		switch (cmd_line[0]) {
		case '0':
			toggle = 0;
			old_show_trace_tree = 0;
			break;
		case '1':
			toggle = 0;
			old_show_trace_tree = 1;
			break;
		case 'o':
		case 'O':
			toggle = 0;
			switch (cmd_line[1]) {
			case 'n':
			case 'N':
				old_show_trace_tree = 1;
				break;
			default:
				old_show_trace_tree = 0;
			}
			break;
		default:
			printf("syntax: trace [on|off]\n");
			return;
		}
	}

	if (toggle)
		old_show_trace_tree = 1 - old_show_trace_tree;

	printf("show_trace_tree = %s", old_show_trace_tree? "ON" : "OFF");
	if (old_show_trace_tree != previous_mode)
		printf("  (was: %s)", previous_mode? "ON" : "OFF");
	printf("\n");

	if (m->bintrans_enable && old_show_trace_tree)
		printf("NOTE: the trace tree functionality doesn't "
		    "work very well with bintrans!\n");
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
		unsigned int i, len;
		unsigned char buf[17];	/*  TODO: How long can an
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

	strlcpy(repeat_cmd, "unassemble", MAX_CMD_BUFLEN);
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


/****************************************************************************/


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

	{ "ninstrs", "[on|off]", 0, debugger_cmd_ninstrs,
		"toggle (set or unset) show_nr_of_instructions" },

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

	/*  NOTE: Try to keep 'r' down to only one command. Having 'reg'
	    available as a one-letter command is very convenient.  */

	{ "reg", "[cpuid][,c]", 0, debugger_cmd_reg,
		"show GPRs (or coprocessor c's registers)" },

	/*  NOTE: Try to keep 's' down to only one command. Having 'step'
	    available as a one-letter command is very convenient.  */

	{ "step", "[n]", 0, debugger_cmd_step,
		"single-step one (or n) instruction(s)" },

	{ "tlbdump", "[cpuid][,r]", 0, debugger_cmd_tlbdump,
		"dump TLB contents (add ',r' for raw data)" },

	{ "trace", "[on|off]", 0, debugger_cmd_trace,
		"toggle show_trace_tree on or off" },

	{ "unassemble", "[addr [endaddr]]", 0, debugger_cmd_unassemble,
		"dump memory contents as instructions" },

	{ "version", "", 0, debugger_cmd_version,
		"print version information" },

	/*  Note: NULL handler.  */
	{ "x = expr", "", 0, NULL, "generic assignment" },

	{ NULL, NULL, 0, NULL, NULL }
};


/*
 *  debugger_cmd_help():
 *
 *  Print a list of available commands.
 *
 *  NOTE: This is placed after the cmds[] array, because it needs to
 *  access it.
 *
 *  TODO: Command completion (ie just type "help s" for "help step").
 */
static void debugger_cmd_help(struct machine *m, char *cmd_line)
{
	int only_one = 0, only_one_match = 0;
	char *nlines_env = getenv("LINES");
	int nlines = atoi(nlines_env != NULL? nlines_env : "999999"), curlines;
	size_t i, j, max_name_len = 0;

	if (cmd_line[0] != '\0') {
		only_one = 1;
	}

	i = 0;
	while (cmds[i].name != NULL) {
		size_t a = strlen(cmds[i].name);
		if (cmds[i].args != NULL)
			a += 1 + strlen(cmds[i].args);
		if (a > max_name_len)
			max_name_len = a;
		i++;
	}

	curlines = 0;
	if (!only_one) {
		printf("Available commands:\n");
		curlines++;
	}

	i = 0;
	while (cmds[i].name != NULL) {
		char buf[100];
		snprintf(buf, sizeof(buf), "%s", cmds[i].name);

		if (only_one) {
			if (strcmp(cmds[i].name, cmd_line) != 0) {
				i++;
				continue;
			}
			only_one_match = 1;
		}

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

		curlines ++;
		if (curlines >= nlines - 1) {
			char ch;
			printf("-- more --"); fflush(stdout);
			ch = debugger_readchar();
			printf("\n");
			if (ch == 'q' || ch == 'Q')
				return;
			curlines = 0;
		}
	}

	if (only_one) {
		if (!only_one_match)
			printf("%s: no such command\n", cmd_line);
		return;
	}

	/*  TODO: generalize/refactor  */
	curlines += 8;
	if (curlines > nlines - 1) {
		char ch;
		printf("-- more --"); fflush(stdout);
		ch = debugger_readchar();
		printf("\n");
		if (ch == 'q' || ch == 'Q')
			return;
		curlines = 0;
	}

	printf("\nIn generic assignments, x must be a register, and expr can be"
	    " a register, a\nnumeric value, or a symbol name (+ an optional "
	    "numeric offset). In case there\nare multiple matches (i.e. a "
	    "symbol that has the same name as a register), you\nmay add a "
	    "prefix character as a hint: '%%' for registers, '@' for symbols,"
	    " and\n'$' for numeric values. Use 0x for hexadecimal values.\n");
}

