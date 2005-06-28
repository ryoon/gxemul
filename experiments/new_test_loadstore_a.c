/*
 *  $Id: new_test_loadstore_a.c,v 1.1 2005-06-28 11:34:52 debug Exp $
 *
 *  Experimenting with dynamic-but-not-binary-translation load/store.
 *  See new_test_loadstore_b.c for the main() function.
 */

#include "new_test_loadstore.h"

void general_store(struct cpu *cpu, struct ic *ic)
{
	general_store(cpu, ic);
}

void x(struct cpu *cpu, struct ic *ic)
{
	int addr = *ic->arg1 + ic->arg2;
	unsigned char **table1, *page;

	table1 = cpu->table0[addr >> 22];
	page = table1[((addr >> 12) & 1023)*2 + 1];

	if (page != 0)
		page[addr & 4095] = *(ic->arg3);
	else
		general_store(cpu, ic);
}

void y(struct cpu *cpu, struct ic *ic)
{
	int addr = *ic->arg1 + ic->arg2;
	unsigned char **table1, *page;

	table1 = cpu->table0[addr >> 22];
	page = table1[((addr >> 12) & 1023)*2 + 0];

	if (page != 0)
		*(ic->arg3) = page[addr & 4095];
	else
		general_store(cpu, ic);
}

