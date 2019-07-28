/*
 *  GXemul floating point tests.
 *
 *  This file is in the Public Domain.
 */

#ifdef HOSTNATIVE
#include <stdio.h>
#include <stdlib.h>
#else
#include "dev_cons.h"
#endif


#ifdef MIPS
/*  Note: The ugly cast to a signed int (32-bit) causes the address to be
	sign-extended correctly on MIPS when compiled in 64-bit mode  */ 
#define PHYSADDR_OFFSET         ((signed int)0xa0000000)
#else
#define PHYSADDR_OFFSET         0
#endif


#define	PUTCHAR_ADDRESS		(PHYSADDR_OFFSET +		\
				DEV_CONS_ADDRESS + DEV_CONS_PUTGETCHAR)
#define	HALT_ADDRESS		(PHYSADDR_OFFSET +		\
				DEV_CONS_ADDRESS + DEV_CONS_HALT)


#include "fpconst.h"


void printchar(char ch)
{
#ifdef HOSTNATIVE
	printf("%c", ch);
#else
	*((volatile unsigned char *) PUTCHAR_ADDRESS) = ch;
#endif
}


void halt(void)
{
#ifdef HOSTNATIVE
	exit(0);
#else
	*((volatile unsigned char *) HALT_ADDRESS) = 0;
#endif
}


void str(char *s)
{
	while (*s)
		printchar(*s++);
}


void print_hex(unsigned long long value, int bits)
{
	unsigned long long head = value >> 4;
	unsigned long long tail = value & 15;
	char hexchar[16] = "0123456789abcdef";

	if (bits > 4)
		print_hex(head, bits - 4);

	printchar(hexchar[tail]);
}


void print_float(void* value)
{
	unsigned int v = *(unsigned int *) value;
	print_hex(v, 32);
}


void print_double(void* value)
{
	unsigned long long v = *(unsigned long long *) value;
	print_hex(v, 64);
}


#ifdef HOSTNATIVE
int main(int argc, char* argv[])
#else
int f(void)
#endif
{
	str("GXemul floating point tests\n\n");

	str("constant 0.0:\t");
	{
		float f;
		f = f_0_0();
		print_float(&f);
		str("\t");
	}
	{
		double d;
		d = d_0_0();
		print_double(&d);
		str("\n");
	}

	str("constant -0.0:\t");
	{
		float f;
		f = f_m0_0();
		print_float(&f);
		str("\t");
	}
	{
		double d;
		d = d_m0_0();
		print_double(&d);
		str("\n");
	}

	str("constant 0.17:\t");
	{
		float f;
		f = f_0_17();
		print_float(&f);
		str("\t");
	}
	{
		double d;
		d = d_0_17();
		print_double(&d);
		str("\n");
	}

	str("constant -0.17:\t");
	{
		float f;
		f = f_m0_17();
		print_float(&f);
		str("\t");
	}
	{
		double d;
		d = d_m0_17();
		print_double(&d);
		str("\n");
	}

	str("constant 1.0:\t");
	{
		float f;
		f = f_1_0();
		print_float(&f);
		str("\t");
	}
	{
		double d;
		d = d_1_0();
		print_double(&d);
		str("\n");
	}

	str("constant -1.0:\t");
	{
		float f;
		f = f_m1_0();
		print_float(&f);
		str("\t");
	}
	{
		double d;
		d = d_m1_0();
		print_double(&d);
		str("\n");
	}

	str("constant 1.7:\t");
	{
		float f;
		f = f_1_7();
		print_float(&f);
		str("\t");
	}
	{
		double d;
		d = d_1_7();
		print_double(&d);
		str("\n");
	}

	str("constant -1.7:\t");
	{
		float f;
		f = f_m1_7();
		print_float(&f);
		str("\t");
	}
	{
		double d;
		d = d_m1_7();
		print_double(&d);
		str("\n");
	}

	str("constant 42:\t");
	{
		float f;
		f = f_42();
		print_float(&f);
		str("\t");
	}
	{
		double d;
		d = d_42();
		print_double(&d);
		str("\n");
	}

	str("constant -42:\t");
	{
		float f;
		f = f_m42();
		print_float(&f);
		str("\t");
	}
	{
		double d;
		d = d_m42();
		print_double(&d);
		str("\n");
	}

	str("constant inf:\t");
	{
		float f;
		f = f_inf();
		print_float(&f);
		str("\t");
	}
	{
		double d;
		d = d_inf();
		print_double(&d);
		str("\n");
	}

	str("constant -inf:\t");
	{
		float f;
		f = f_m_inf();
		print_float(&f);
		str("\t");
	}
	{
		double d;
		d = d_m_inf();
		print_double(&d);
		str("\n");
	}

	str("constant nan:\t");
	{
		float f;
		f = f_nan();
		print_float(&f);
		str("\t");
	}
	{
		double d;
		d = d_nan();
		print_double(&d);
		str("\n");
	}

	str("constant nan_x:\t");
	{
		float f;
		f = f_nan_x();
		print_float(&f);
		str("\t");
	}
	{
		double d;
		d = d_nan_x();
		print_double(&d);
		str("\n");
	}

	// TODO: Denormalized subnormal values
	// TODO: Min/max values?

	str("\n");

	str("0.0 + 0.0:\t");
	{
		float x1, x2, x;
		x1 = f_0_0();
		x2 = f_0_0();
		x = x1 + x2;
		print_float(&x);
		str("\t");
	}
	{
		double x1, x2, x;
		x1 = d_0_0();
		x2 = d_0_0();
		x = x1 + x2;
		print_double(&x);
		str("\n");
	}

	str("0.0 + -0.0:\t");
	{
		float x1, x2, x;
		x1 = f_0_0();
		x2 = f_m0_0();
		x = x1 + x2;
		print_float(&x);
		str("\t");
	}
	{
		double x1, x2, x;
		x1 = d_0_0();
		x2 = d_m0_0();
		x = x1 + x2;
		print_double(&x);
		str("\n");
	}

	str("-0.0 + 0.0:\t");
	{
		float x1, x2, x;
		x1 = f_m0_0();
		x2 = f_0_0();
		x = x1 + x2;
		print_float(&x);
		str("\t");
	}
	{
		double x1, x2, x;
		x1 = d_m0_0();
		x2 = d_0_0();
		x = x1 + x2;
		print_double(&x);
		str("\n");
	}

	str("0.0 + 1.0:\t");
	{
		float x1, x2, x;
		x1 = f_0_0();
		x2 = f_1_0();
		x = x1 + x2;
		print_float(&x);
		str("\t");
	}
	{
		double x1, x2, x;
		x1 = d_0_0();
		x2 = d_1_0();
		x = x1 + x2;
		print_double(&x);
		str("\n");
	}

	str("0.0 + -1.0:\t");
	{
		float x1, x2, x;
		x1 = f_0_0();
		x2 = f_m1_0();
		x = x1 + x2;
		print_float(&x);
		str("\t");
	}
	{
		double x1, x2, x;
		x1 = d_0_0();
		x2 = d_m1_0();
		x = x1 + x2;
		print_double(&x);
		str("\n");
	}

	str("1.0 + 0.0:\t");
	{
		float x1, x2, x;
		x1 = f_1_0();
		x2 = f_0_0();
		x = x1 + x2;
		print_float(&x);
		str("\t");
	}
	{
		double x1, x2, x;
		x1 = d_1_0();
		x2 = d_0_0();
		x = x1 + x2;
		print_double(&x);
		str("\n");
	}

	str("1.0 + 1.0:\t");
	{
		float x1, x2, x;
		x1 = f_1_0();
		x2 = f_1_0();
		x = x1 + x2;
		print_float(&x);
		str("\t");
	}
	{
		double x1, x2, x;
		x1 = d_1_0();
		x2 = d_1_0();
		x = x1 + x2;
		print_double(&x);
		str("\n");
	}

	str("1.0 + -1.0:\t");
	{
		float x1, x2, x;
		x1 = f_1_0();
		x2 = f_m1_0();
		x = x1 + x2;
		print_float(&x);
		str("\t");
	}
	{
		double x1, x2, x;
		x1 = d_1_0();
		x2 = d_m1_0();
		x = x1 + x2;
		print_double(&x);
		str("\n");
	}

	str("0.0 + 1.7:\t");
	{
		float x1, x2, x;
		x1 = f_0_0();
		x2 = f_1_7();
		x = x1 + x2;
		print_float(&x);
		str("\t");
	}
	{
		double x1, x2, x;
		x1 = d_0_0();
		x2 = d_1_7();
		x = x1 + x2;
		print_double(&x);
		str("\n");
	}

	str("1.7 + 0.0:\t");
	{
		float x1, x2, x;
		x1 = f_1_7();
		x2 = f_0_0();
		x = x1 + x2;
		print_float(&x);
		str("\t");
	}
	{
		double x1, x2, x;
		x1 = d_1_7();
		x2 = d_0_0();
		x = x1 + x2;
		print_double(&x);
		str("\n");
	}

	str("42 + 0.0:\t");
	{
		float x1, x2, x;
		x1 = f_42();
		x2 = f_0_0();
		x = x1 + x2;
		print_float(&x);
		str("\t");
	}
	{
		double x1, x2, x;
		x1 = d_42();
		x2 = d_0_0();
		x = x1 + x2;
		print_double(&x);
		str("\n");
	}

	str("42 + 42:\t");
	{
		float x1, x2, x;
		x1 = f_42();
		x2 = f_42();
		x = x1 + x2;
		print_float(&x);
		str("\t");
	}
	{
		double x1, x2, x;
		x1 = d_42();
		x2 = d_42();
		x = x1 + x2;
		print_double(&x);
		str("\n");
	}

	str("-42 + 42:\t");
	{
		float x1, x2, x;
		x1 = f_m42();
		x2 = f_42();
		x = x1 + x2;
		print_float(&x);
		str("\t");
	}
	{
		double x1, x2, x;
		x1 = d_m42();
		x2 = d_42();
		x = x1 + x2;
		print_double(&x);
		str("\n");
	}

	str("0.0 + inf:\t");
	{
		float x1, x2, x;
		x1 = f_0_0();
		x2 = f_inf();
		x = x1 + x2;
		print_float(&x);
		str("\t");
	}
	{
		double x1, x2, x;
		x1 = d_0_0();
		x2 = d_inf();
		x = x1 + x2;
		print_double(&x);
		str("\n");
	}

	str("0.0 + -inf:\t");
	{
		float x1, x2, x;
		x1 = f_0_0();
		x2 = f_m_inf();
		x = x1 + x2;
		print_float(&x);
		str("\t");
	}
	{
		double x1, x2, x;
		x1 = d_0_0();
		x2 = d_m_inf();
		x = x1 + x2;
		print_double(&x);
		str("\n");
	}

	str("inf + inf:\t");
	{
		float x1, x2, x;
		x1 = f_inf();
		x2 = f_inf();
		x = x1 + x2;
		print_float(&x);
		str("\t");
	}
	{
		double x1, x2, x;
		x1 = d_inf();
		x2 = d_inf();
		x = x1 + x2;
		print_double(&x);
		str("\n");
	}

	str("inf + -inf:\t");
	{
		float x1, x2, x;
		x1 = f_inf();
		x2 = f_m_inf();
		x = x1 + x2;
		print_float(&x);
		str("\t");
	}
	{
		double x1, x2, x;
		x1 = d_inf();
		x2 = d_m_inf();
		x = x1 + x2;
		print_double(&x);
		str("\n");
	}

	str("-inf + -inf:\t");
	{
		float x1, x2, x;
		x1 = f_m_inf();
		x2 = f_m_inf();
		x = x1 + x2;
		print_float(&x);
		str("\t");
	}
	{
		double x1, x2, x;
		x1 = d_m_inf();
		x2 = d_m_inf();
		x = x1 + x2;
		print_double(&x);
		str("\n");
	}

	str("inf + 1.7:\t");
	{
		float x1, x2, x;
		x1 = f_inf();
		x2 = f_1_7();
		x = x1 + x2;
		print_float(&x);
		str("\t");
	}
	{
		double x1, x2, x;
		x1 = d_inf();
		x2 = d_1_7();
		x = x1 + x2;
		print_double(&x);
		str("\n");
	}

	str("inf + nan:\t");
	{
		float x1, x2, x;
		x1 = f_inf();
		x2 = f_nan();
		x = x1 + x2;
		print_float(&x);
		str("\t");
	}
	{
		double x1, x2, x;
		x1 = d_inf();
		x2 = d_nan();
		x = x1 + x2;
		print_double(&x);
		str("\n");
	}

	str("nan + 0.0:\t");
	{
		float x1, x2, x;
		x1 = f_nan();
		x2 = f_0_0();
		x = x1 + x2;
		print_float(&x);
		str("\t");
	}
	{
		double x1, x2, x;
		x1 = d_nan();
		x2 = d_0_0();
		x = x1 + x2;
		print_double(&x);
		str("\n");
	}

	str("nan_x + 0.0:\t");
	{
		float x1, x2, x;
		x1 = f_nan_x();
		x2 = f_0_0();
		x = x1 + x2;
		print_float(&x);
		str("\t");
	}
	{
		double x1, x2, x;
		x1 = d_nan_x();
		x2 = d_0_0();
		x = x1 + x2;
		print_double(&x);
		str("\n");
	}

	str("nan + 1.7:\t");
	{
		float x1, x2, x;
		x1 = f_nan();
		x2 = f_1_7();
		x = x1 + x2;
		print_float(&x);
		str("\t");
	}
	{
		double x1, x2, x;
		x1 = d_nan();
		x2 = d_1_7();
		x = x1 + x2;
		print_double(&x);
		str("\n");
	}

	str("nan_x + 1.7:\t");
	{
		float x1, x2, x;
		x1 = f_nan_x();
		x2 = f_1_7();
		x = x1 + x2;
		print_float(&x);
		str("\t");
	}
	{
		double x1, x2, x;
		x1 = d_nan_x();
		x2 = d_1_7();
		x = x1 + x2;
		print_double(&x);
		str("\n");
	}

	str("nan + nan_x:\t");
	{
		float x1, x2, x;
		x1 = f_nan();
		x2 = f_nan_x();
		x = x1 + x2;
		print_float(&x);
		str("\t");
	}
	{
		double x1, x2, x;
		x1 = d_nan();
		x2 = d_nan_x();
		x = x1 + x2;
		print_double(&x);
		str("\n");
	}

	str("nan_x + nan:\t");
	{
		float x1, x2, x;
		x1 = f_nan_x();
		x2 = f_nan();
		x = x1 + x2;
		print_float(&x);
		str("\t");
	}
	{
		double x1, x2, x;
		x1 = d_nan_x();
		x2 = d_nan();
		x = x1 + x2;
		print_double(&x);
		str("\n");
	}

	// TODO: - * /
	// TODO: == < > <= >=
	// TODO: sqrt sin cos etc...

	str("\n");
	halt();
	return 0;
}

