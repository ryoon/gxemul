/*  $Id: new_test_loadstore.h,v 1.2 2005-06-28 16:38:03 debug Exp $  */

struct cpu {
	int pc;
	unsigned char **table0[1024];
};

struct ic {
	int dummy;
	int *arg1;
	int arg2;
	int *arg3;
};

void general_store(struct cpu *cpu, struct ic *ic);
void x(struct cpu *cpu, struct ic *ic);
void y(struct cpu *cpu, struct ic *ic);

