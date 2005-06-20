/*
 *  Copyright (C) 2002,2005  Anders Gavare.  All rights reserved.
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
 *  $Id: rssb_as.c,v 1.3 2005-06-20 05:52:45 debug Exp $
 *
 *  A simple assembler for URISC ("reverse subtract and skip on borrow").
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>


#define	MAX_SYMBOL_LEN	64

struct symbol {
	char name[MAX_SYMBOL_LEN];
	uint32_t addr;
	struct symbol *next;
};


struct symbol *first_symbol = NULL;


int debug = 0;


uint32_t get_symbol(char *symbol)
{
	struct symbol *s;

	s = first_symbol;
	while (s != NULL) {
		if (strcmp(symbol, s->name) == 0)
			return s->addr;

		s = s->next;
	}

	fprintf(stderr, "undefined symbol '%s'\n", symbol);
	exit(1);
}


void add_symbol(char *symbol, uint32_t addr)
{
	struct symbol *s;

	if (strlen(symbol) > MAX_SYMBOL_LEN-1) {
		fprintf(stderr, "symbol '%s' too long\n", symbol);
		exit(1);
	}

	s = malloc(sizeof(struct symbol));
	if (s == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}

	strlcpy(s->name, symbol, MAX_SYMBOL_LEN);
	s->addr = addr;

	if (debug > 1)
		printf("  %08x = %s\n", addr, symbol);

	s->next = first_symbol;
	first_symbol = s;
}


uint32_t pass1(FILE *fin)
{
	uint32_t curaddr = 0;
	char line[200];
	int lineno = 0;

	if (debug > 0)
		printf("Pass 1\n");

	while (!feof(fin)) {
		line[0] = '\0';
		fgets(line, sizeof(line), fin);
		lineno++;
		if (line[0]) {
			if (line[strlen(line)-1]=='\n')
				line[strlen(line)-1]=0;

			if (debug > 2)
				printf("    addr=%08x line='%s'\n",
				    curaddr,line);

			switch (line[0]) {
			case '\0':
			case '#':
				break;
			case 'L':
				/*  Add a symbol:  */
				add_symbol(line+1, curaddr);
				break;
			case 'A':
			case 'D':
			case 'E':
				/*  Increase addr by 4:  */
				curaddr += sizeof(uint32_t);
				break;
			case 'O':
				/*  Set orig manually:  */
				curaddr = atoi(line+1);
				break;
			default:
				fprintf(stderr, "error on input line %i\n",
				    lineno);
				exit(1);
			}
		}
	}

	return curaddr;
}


void pass2(FILE *fin, FILE *fout, uint32_t first_diffaddr)
{
	uint32_t curaddr = 0;
	uint32_t curdiffaddr = first_diffaddr;
	uint32_t value, value2;
	char line[200];
	int lineno = 0;
	char *p;
	unsigned char b1;
	unsigned char b2;
	unsigned char b3;
	unsigned char b4;

	if (debug > 0)
		printf("Pass 2\n");

	while (!feof(fin)) {
		line[0] = '\0';
		fgets(line, sizeof(line), fin);
		lineno++;
		if (line[0]) {
			if (line[strlen(line)-1]=='\n')
				line[strlen(line)-1]=0;

			if (debug > 2)
				printf("    addr=%08x line='%s'\n",
				    curaddr,line);

			switch (line[0]) {
			case '\0':
			case '#':
			case 'L':
				break;
			case 'A':
				/*  Output value and increase addr by 4:  */
				fseek(fout, curaddr, SEEK_SET);
				if (line[1]>='0' && line[1]<='9')
					value = atoi(line+1);
				else
					value = get_symbol(line+1);
				b1 = value >> 24;
				b2 = value >> 16;
				b3 = value >> 8;
				b4 = value;
				fwrite(&b1, 1, 1, fout);
				fwrite(&b2, 1, 1, fout);
				fwrite(&b3, 1, 1, fout);
				fwrite(&b4, 1, 1, fout);

				curaddr += sizeof(uint32_t);
				break;
			case 'D':
			case 'E':
				/*  Address differance calculation:  */
				p = line+1;
				while (*p && *p!=' ')
					p++;
				if (!*p) {
					fprintf(stderr, "error on input line"
					    " %i, D syntax error\n", lineno);
					exit(1);
				}
				p[0]=0; p++;

				if (line[1]>='0' && line[1]<='9')
					value = atoi(line+1);
				else
					value = get_symbol(line+1);

				if (p[0]>='0' && p[0]<='9')
					value2 = atoi(p);
				else
					value2 = get_symbol(p);

				if (line[0]=='D') {
					if (value < value2)
						value = value - value2 + 4;
					else
						value = value - value2;
				} else {
					/*  In conditional jumps:  The -4
					    is because PC is already
					    updated when it is written
					    back to:  */
					value = value - value2 - 4;
				}

				/*  Output value in the diff section:  */
				fseek(fout, curdiffaddr, SEEK_SET);
				b1 = value >> 24;
				b2 = value >> 16;
				b3 = value >> 8;
				b4 = value;
				fwrite(&b1, 1, 1, fout);
				fwrite(&b2, 1, 1, fout);
				fwrite(&b3, 1, 1, fout);
				fwrite(&b4, 1, 1, fout);

				/*  Output the diff addr to the code section: */
				fseek(fout, curaddr, SEEK_SET);
				value = curdiffaddr;
				b1 = value >> 24;
				b2 = value >> 16;
				b3 = value >> 8;
				b4 = value;
				fwrite(&b1, 1, 1, fout);
				fwrite(&b2, 1, 1, fout);
				fwrite(&b3, 1, 1, fout);
				fwrite(&b4, 1, 1, fout);

				curaddr += sizeof(uint32_t);
				curdiffaddr += sizeof(uint32_t);
				break;
			case 'O':
				/*  Set orig manually:  */
				curaddr = atoi(line+1);
				break;
			default:
				fprintf(stderr, "error on input line %i\n",
				    lineno);
				exit(1);
			}
		}
	}
}


int main(int argc, char *argv[])
{
	FILE *fin, *fout;
	uint32_t first_diffaddr;

	if (argc != 3) {
		fprintf(stderr, "usage: %s asmsource binimage\n", argv[0]);
		fprintf(stderr, "Input is read from asmsource and "
		    "output is written to binimage\n");
		exit(1);
	}

	fin = fopen(argv[1], "r");
	if (fin == NULL) {
		perror(argv[1]);
		exit(2);
	}

	fout = fopen(argv[2], "w");
	if (fout = NULL) {
		perror(argv[2]);
		exit(2);
	}

	first_diffaddr = pass1(fin);
	fseek(fin, 0, SEEK_SET);
	pass2(fin, fout, first_diffaddr);

	fclose(fin);
	fclose(fout);

	return 0;
}

