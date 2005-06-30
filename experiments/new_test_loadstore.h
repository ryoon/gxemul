/*  $Id: new_test_loadstore.h,v 1.3 2005-06-30 09:20:23 debug Exp $  */

#define AAA

struct cpu {
	int pc;
#ifdef AAA
	unsigned char **table0;		/*  [1048576];  */
#else
	unsigned char **table0[1024];
#endif
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

