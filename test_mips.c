/*
 *  Copyright (C) 1999 by Anders Gavare.  All rights reserved.
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
 *  $Id: test_mips.c,v 1.3 2003-11-07 05:10:22 debug Exp $
 *
 *  mipstest.c  --  a test program to see that mips64emul emulates
 *                  everything correctly.
 */

#define	N_MINIBUFS		20

#define	PUTCHAR_ADDRESS		0xb0000000

char *readonly_string = "Hello world.\n";


void printchar(char ch)
{
	*((volatile int *) PUTCHAR_ADDRESS) = ch;
}


void printstr(char *s)
{
	while (*s)
		printchar(*s++);
}


void printhex(long int n)
{
	char *hex = "0123456789abcdef";
	int i;

	for (i=15; i>=0; i--)
		printchar(hex[(n >> (4*i)) & 15]);
}


void printdec(long int x)
{
	if (x<=0)
		printchar('0');
	else {
		int y = x / 10;
		if (y > 0)
			printdec(y);
		printchar('0' + (x % 10));
	}
}


void primetest(long int x)
{
	long int y = x-2;

	while (y > 1) {
		if ((x % y) == 0)
			return;
		y-=2;
	}

	printstr(", "); printdec(x);
}


void my_memcpy(void *a, void *b, long len)
{
	char *p1 = (char *) a;
	char *p2 = (char *) b;

	while (len-- > 0)
		*p1++ = *p2++;
}


#define	MANDEL_SCALE		4096
#define	MANDEL_MAXCOLOR		8
#define	MANDEL_COLORS		" ,.;:oxOX"

int mandel_color(long x, long y, int maxn)
{
	long tx1 = 0, ty1 = 0, tx2, ty2;
	long n = 0;

	for (;;) {
		tx2 = tx1*tx1/MANDEL_SCALE - ty1*ty1/MANDEL_SCALE + x;
		ty2 = 2*tx1*ty1/MANDEL_SCALE + y;
		if (tx2*tx2 + ty2*ty2 > 4*MANDEL_SCALE*MANDEL_SCALE || n>=maxn)
			return n;
		tx1 = tx2;
		ty1 = ty2;
		n++;
	}
}


void load_store_test(void)
{
	char buf[100];
	char *cp;
	short *sp;
	int *ip;
	long *lp;

	cp = (void *) &buf[0];
	sp = (void *) &buf[0];
	ip = (void *) &buf[0];
	lp = (void *) &buf[0];

	printstr("\nload/store test:\n");
	*cp = *sp;
	*sp = *lp;
	*lp = *ip;
	*ip = *cp;
}


void unaligned_load_store_test(void)
{
	unsigned char buf[24];
	int *p;
	int i;

return;

	for (i=0; i<sizeof(buf); i++)
		buf[i] = i;

	printstr("\nunaligned load/store test:\n");

	for (i=0; i<sizeof(buf); i++) {
		printstr(" ");
		printdec(buf[i]);
	}
	printstr("\n");

	p = (int *) &buf[1];
	*p = 0x0a0b0c0d;

	for (i=0; i<sizeof(buf); i++) {
		printstr(" ");
		printdec(buf[i]);
	}
	printstr("\n");

}


void render_mandel(int xsize, int ysize)
{
	long x, y, line, column;
	int color;
	long x1 = -2*MANDEL_SCALE, x2 = 2*MANDEL_SCALE;
	long y1 = -2*MANDEL_SCALE, y2 = 2*MANDEL_SCALE;

	printstr("\nMandelbrot test: xsize="); printdec(xsize);
		printstr(", ysize="); printdec(ysize); printstr("\n");

	printstr("+");
	for (column=0; column<xsize; column++)
		printstr("-");
	printstr("+\n");

	for (line=ysize-1; line>=0; line--) {
		printstr("|");
		for (column=0; column<xsize; column++) {
			x = (x2-x1) * column/xsize + x1;
			y = (y2-y1) * line/ysize + y1;
			color = mandel_color(x, y, MANDEL_MAXCOLOR);
			printchar(MANDEL_COLORS[color]);
		}
		printstr("|\n");
	}

	printstr("+");
	for (column=0; column<xsize; column++)
		printstr("-");
	printstr("+\n");
}


void p_hex_add(long a, long b)  { printhex(a + b); }
void p_hex_sub(long a, long b)  { printhex(a - b); }
void p_hex_mul(long a, long b)  { printhex(a * b); }
void p_hex_div(long a, long b)  { printhex(a / b); }
void p_hex_and(long a, long b)  { printhex(a & b); }
void p_hex_or(long a, long b)   { printhex(a | b); }
void p_hex_xor(long a, long b)  { printhex(a ^ b); }
void p_hex_mod(long a, long b)  { printhex(a % b); }


int entry_func(void)
{
	char *s = readonly_string;
	long int a = 0x123456789;
	long int b = 0xabcdef;
	long int x;
	char buf[N_MINIBUFS * 1024];

	printstr(readonly_string);
	printstr("\n");

	printstr("sizeof(char)      = "); printdec(sizeof(char)     ); printstr("\n");
	printstr("sizeof(short)     = "); printdec(sizeof(short)    ); printstr("\n");
	printstr("sizeof(int)       = "); printdec(sizeof(int)      ); printstr("\n");
	printstr("sizeof(long)      = "); printdec(sizeof(long)     ); printstr("\n");
	printstr("sizeof(long long) = "); printdec(sizeof(long long)); printstr("\n");
	printstr("sizeof(void *)    = "); printdec(sizeof(void *)   ); printstr("\n");

	printhex(a); printstr(" + "); printhex(b); printstr(" = "); p_hex_add(a,b); printstr("\n");
	printhex(a); printstr(" - "); printhex(b); printstr(" = "); p_hex_sub(a,b); printstr("\n");
	printhex(a); printstr(" * "); printhex(b); printstr(" = "); p_hex_mul(a,b); printstr("\n");
	printhex(a); printstr(" / "); printhex(b); printstr(" = "); p_hex_div(a,b); printstr("\n");
	printhex(a); printstr(" & "); printhex(b); printstr(" = "); p_hex_and(a,b); printstr("\n");
	printhex(a); printstr(" | "); printhex(b); printstr(" = "); p_hex_or(a,b); printstr("\n");
	printhex(a); printstr(" ^ "); printhex(b); printstr(" = "); p_hex_xor(a,b); printstr("\n");
	printhex(a); printstr(" % "); printhex(b); printstr(" = "); p_hex_mod(a,b); printstr("\n");

	printhex(a); printstr(" >> 4 = "); printhex(a >> 4); printstr("\n");
	printhex(a); printstr(" >> 10 = "); printhex(a >> 10); printstr("\n");

	load_store_test();

	unaligned_load_store_test();

	printstr("\nmemcpy test:\n");
	for (x=1; x<N_MINIBUFS; x++) {
		printstr(" "); printdec(x);
		my_memcpy(buf + 1024*(x-1), buf + 1024*x, 1024);
	}
	printstr("\n");


	/*  Prime number test:  */
	x = 1;
	printstr("\nPrime number test:\n");
	printstr("2");

	while (x < 100) {
		x += 2;
		primetest(x);
	}

	printstr("\n");


	render_mandel(77, 30);

	printstr("\nDone.\n");
	for (;;)
		;
}

