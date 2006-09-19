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
 *  $Id: debugger_expr.c,v 1.6 2006-09-19 10:50:08 debug Exp $
 *
 *  Expression evaluator.
 *
 *
 *  TODO:
 *	General expressions, with operators, parentheses etc.
 *	Settings.
 *	Sign-extension only on MIPS?
 *	SPECIAL IMPORTANT CASE: Clear the delay_slot flag when writing
 *		to the pc register.
 *	TAB completion? :-)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "cpu.h"
#include "debugger.h"
#include "machine.h"
#include "misc.h"
#include "settings.h"


extern struct settings *global_settings;


/*
 *  debugger_parse_name():
 *
 *  This function reads a string, and tries to match it to a register name,
 *  a symbol, or treat it as a decimal numeric value.
 *
 *  Some examples:
 *
 *	"0x7fff1234"		==> numeric value (hex, in this case)
 *	"pc", "r5", "hi", "t4"	==> register (CPU dependent)
 *	"memcpy+64"		==> symbol (plus offset)
 *
 *  Register names can be preceeded by "x:" where x is the CPU number. (CPU
 *  0 is assumed by default.)
 *
 *  To force detection of different types, a character can be added in front of
 *  the name: "$" for numeric values, "%" for registers or other settings,
 *  and "@" for symbols.
 *
 *  Return value is:
 *
 *	NAME_PARSE_NOMATCH	no match
 *	NAME_PARSE_MULTIPLE	multiple matches
 *
 *  or one of these (and then *valuep is read or written, depending on
 *  the writeflag):
 *
 *	NAME_PARSE_SETTINGS	a setting (e.g. a register)
 *	NAME_PARSE_NUMBER	a hex number
 *	NAME_PARSE_SYMBOL	a symbol
 */
int debugger_parse_name(struct machine *m, char *name, int writeflag,
	uint64_t *valuep)
{
	int match_settings = 0, match_symbol = 0, match_numeric = 0;
	int skip_settings, skip_numeric, skip_symbol;

	/*  TODO!!!  */
	int cur_emul_nr = 0, cur_machine_nr = 0, cur_cpu_nr = 0;

	if (m == NULL || name == NULL) {
		fprintf(stderr, "debugger_parse_name(): NULL ptr\n");
		exit(1);
	}

	/*  Warn about non-signextended values:  */
	if (writeflag) {
		if (m->cpus[0]->is_32bit) {
			/*  Automagically sign-extend.  TODO: Is this good?  */
			if (((*valuep) >> 32) == 0 && (*valuep) & 0x80000000ULL)
				(*valuep) |= 0xffffffff00000000ULL;
		} else {
			if (((*valuep) >> 32) == 0 && (*valuep) & 0x80000000ULL)
				printf("WARNING: The value is not sign-extende"
				    "d. Is this what you intended?\n");
		}
	}

	skip_settings = name[0] == '$' || name[0] == '@';
	skip_numeric  = name[0] == '%' || name[0] == '@';
	skip_symbol   = name[0] == '$' || name[0] == '%';

	if (!skip_settings) {
		char setting_name[400];
		char valuebuf[50];
		int res;

		valuebuf[0] = '\0';

		if (writeflag)
			snprintf(valuebuf, sizeof(valuebuf),
			    "0x%"PRIx64, *valuep);

		res = settings_access(global_settings, name,
		    writeflag, valuebuf, sizeof(valuebuf));
		if (res == SETTINGS_OK)
			match_settings = 1;

		if (!match_settings) {
			snprintf(setting_name, sizeof(setting_name),
			    GLOBAL_SETTINGS_NAME".%s", name);
			res = settings_access(global_settings, setting_name,
			    writeflag, valuebuf, sizeof(valuebuf));
			if (res == SETTINGS_OK)
				match_settings = 1;
		}

		if (!match_settings) {
			snprintf(setting_name, sizeof(setting_name),
			    GLOBAL_SETTINGS_NAME".emul[%i].%s",
			    cur_emul_nr, name);
			res = settings_access(global_settings, setting_name,
			    writeflag, valuebuf, sizeof(valuebuf));
			if (res == SETTINGS_OK)
				match_settings = 1;
		}

		if (!match_settings) {
			snprintf(setting_name, sizeof(setting_name),
			    GLOBAL_SETTINGS_NAME".emul[%i].machine[%i].%s",
			    cur_emul_nr, cur_machine_nr, name);
			res = settings_access(global_settings, setting_name,
			    writeflag, valuebuf, sizeof(valuebuf));
			if (res == SETTINGS_OK)
				match_settings = 1;
		}

		if (!match_settings) {
			snprintf(setting_name, sizeof(setting_name),
			    GLOBAL_SETTINGS_NAME".emul[%i].machine[%i]."
			    "cpu[%i].%s", cur_emul_nr, cur_machine_nr,
			    cur_cpu_nr, name);
			res = settings_access(global_settings, setting_name,
			    writeflag, valuebuf, sizeof(valuebuf));
			if (res == SETTINGS_OK)
				match_settings = 1;
		}

		if (match_settings)
			*valuep = strtoull(valuebuf, NULL, 0);
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
		strlcpy(sn, name, strlen(name)+1);

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

	if (match_settings + match_symbol + match_numeric > 1)
		return NAME_PARSE_MULTIPLE;

	if (match_settings)
		return NAME_PARSE_SETTINGS;
	if (match_numeric)
		return NAME_PARSE_NUMBER;
	if (match_symbol)
		return NAME_PARSE_SYMBOL;

	return NAME_PARSE_NOMATCH;
}

