/*
 *  $Id: new_test_loadstore_b.c,v 1.1 2005-06-28 11:34:52 debug Exp $
 *
 *  Experimenting with dynamic-but-not-binary-translation load/store.
 */

#include <stdio.h>
#include <stdlib.h>

#include "new_test_loadstore.h"

/*  These are in new_test_loadstore_a.c:  */
void x(struct cpu *cpu, struct ic *ic);
void y(struct cpu *cpu, struct ic *ic);

int main(int argc, char *argv[])
{
	int x1 = 72, x2 = 1234;
	struct ic ic = { &x1, 5, &x2 };
	struct cpu cpu;
	int i;
	char *page = malloc(4096);

	cpu.table0 = malloc(sizeof(void *) * 1024);
	cpu.table0[0] = malloc(sizeof(void *) * 2 * 1024);
	for (i=0; i<1024; i++) {
		cpu.table0[0][i*2+0] = page;
		cpu.table0[0][i*2+1] = page;
	}

	printf("A: 100 Million loads\n");
	printf("y=%i\n", x2);
	for (i=0; i<100000000; i++)
		x(&cpu, &ic);
	printf("y=%i\n", x2);
	printf("B\n");

	return 0;
}
