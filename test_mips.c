/*
 *  Copyright (C) 2003-2004 by Anders Gavare.  All rights reserved.
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
 *  $Id: test_mips.c,v 1.4 2004-01-10 11:37:50 debug Exp $
 *
 *  This is a test program, used to test the functionality of mips64emul
 *  (or other emulators, or possibly even actual hardware).
 *
 *  Compile this as a stand alone program.  If your compiler and linker are
 *  mips64unknown-elf-gcc and mips64-unknown-elf-ld, respectively, then the
 *  following should give you two ELF64 binaries, one normal and one using
 *  MIPS16 encoding:
 *
 *	mips64-unknown-elf-gcc -g -O3 -fno-builtin -fschedule-insns -mips4 -mabi=64  -c test_mips.c -o test_mips.o
 *	mips64-unknown-elf-ld -Ttext 0xffffffff80030000 -e entry_func test_mips.o -o test_mips --oformat=elf64-bigmips
 *
 *	mips64-unknown-elf-gcc -g -O3 -fno-builtin -fschedule-insns -mips16 -mabi=64  -c test_mips.c -o test_mips16.o
 *	mips64-unknown-elf-ld -Ttext 0xffffffff80030000 -e entry_func test_mips16.o -o test_mips16 --oformat=elf64-bigmips
 *
 *  Other choices of entry point, optimization settings, ABIs, and compiler
 *  might work too.
 */

#define	FRAMEBUFFER_ADDRESS		0xb2000000
#define	FRAMEBUFFER_XSIZE		640
#define	FRAMEBUFFER_YSIZE		480
#define	FRAMEBUFFER_BYTES_PER_PIXEL	3

#define	OK	1
#define	FAIL	0

/*  Change these if your compiler works differently than gcc on mips64  */
typedef	unsigned long long	uint64_t;
typedef	signed long long	int64_t;
typedef	unsigned int		uint32_t;
typedef	signed int		int32_t;
typedef	unsigned short		uint16_t;
typedef	signed short		int16_t;
typedef	unsigned char		uint8_t;
typedef	signed char		int8_t;

/*  Mandelbrot settings:  */
#define	MANDEL_WIDTH		72
#define	MANDEL_HEIGHT		40
#define	MANDEL_SCALE		4096
#define	MANDEL_MAXCOLOR		12
#define	MANDEL_COLORS		" ,.;:odxOQ@X#"


/*
 *  printchar():
 *
 *  This function should print a character to the (serial) console.
 *  In the emulator, it is as simple as storing a byte at a specific
 *  address.
 */
#define	PUTCHAR_ADDRESS		0xb0000000
void printchar(char ch)
{
	*((volatile unsigned char *) PUTCHAR_ADDRESS) = ch;
}


/*
 *  printstr():
 */
void printstr(char *s)
{
	while (*s)
		printchar(*s++);
}


/*
 *  printstatus():
 *
 *  Like printstr(), but adds "OK   " or "FAIL " in front.
 *  Every line of output from this program should be of this format,
 *  to simplify parsing.
 */
void printstatus(int status, char *s)
{
	if (status == OK)
		printstr("OK   ");
	else
		printstr("FAIL ");

	printstr(s);
}


/*
 *  printhex():
 *
 *  Print a number in hex. len is the number of characters to output.
 *  (This should be sizeof(value).)
 */
void printhex(uint64_t n, int len)
{
	char *hex = "0123456789abcdef";
	int i;

	printstr("0x");
	for (i=len*2-1; i>=0; i--)
		printchar(hex[(n >> (4*i)) & 15]);
}


/*
 *  printdec():
 *
 *  Print a number in decimal form.
 */
void printdec(int64_t x)
{
	if (x == 0)
		printchar('0');
	else if (x < 0) {
		printchar('-');
		printdec(-x);
	} else {
		int y = x / 10;
		if (y > 0)
			printdec(y);
		printchar('0' + (x % 10));
	}
}


/*
 *  read_cop0_reg():
 *
 *  Read a coprocessor 0 register.  This is probably GCC specific, and
 *  needs to be modified if it is to be used with other compilers.
 */
uint64_t read_cop0_reg(int reg)
{
	uint64_t x = 0;

	switch (reg) {
	case  0:asm ("mfc0 %0, $0; nop" : "=r"(x)); break;
	case  1:asm ("mfc0 %0, $1; nop" : "=r"(x)); break;
	case  2:asm ("mfc0 %0, $2; nop" : "=r"(x)); break;
	case  3:asm ("mfc0 %0, $3; nop" : "=r"(x)); break;
	case  4:asm ("mfc0 %0, $4; nop" : "=r"(x)); break;
	case  5:asm ("mfc0 %0, $5; nop" : "=r"(x)); break;
	case  6:asm ("mfc0 %0, $6; nop" : "=r"(x)); break;
	case  7:asm ("mfc0 %0, $7; nop" : "=r"(x)); break;
	case  8:asm ("mfc0 %0, $8; nop" : "=r"(x)); break;
	case  9:asm ("mfc0 %0, $9; nop" : "=r"(x)); break;
	case 10:asm ("mfc0 %0,$10; nop" : "=r"(x)); break;
	case 11:asm ("mfc0 %0,$11; nop" : "=r"(x)); break;
	case 12:asm ("mfc0 %0,$12; nop" : "=r"(x)); break;
	case 13:asm ("mfc0 %0,$13; nop" : "=r"(x)); break;
	case 14:asm ("mfc0 %0,$14; nop" : "=r"(x)); break;
	case 15:asm ("mfc0 %0,$15; nop" : "=r"(x)); break;
	default:
		printstr("\n");
		printstatus(FAIL, "unimplemented register ");
		printdec(reg);
		printstr("\n");
	}

	return x;
}


/*
 *  prime():
 *
 *  Tests a number for primality.  Returns 1 for a prime, 0 for a
 *  composite number.
 */
int prime(int64_t x)
{
	int64_t y = x-2;

	if ((x & 1) == 0)
		return 0;

	while (y > 1) {
		if ((x % y) == 0)
			return 0;
		y-=2;
	}

	return 1;
}


/*
 *  putpixel():
 *
 *  Write a rgb pixel to the framebuffer.
 */
void putpixel(int x, int y, int r, int g, int b)
{
	uint8_t *p = (uint8_t *) FRAMEBUFFER_ADDRESS;
	int offset = (FRAMEBUFFER_XSIZE * y + x) * FRAMEBUFFER_BYTES_PER_PIXEL;

	p[offset + 0] = r;
	p[offset + 1] = g;
	p[offset + 2] = b;
}


/*
 *  mandel_color():
 *
 *  Calculate the color of a mandelbrot "pixel".
 */
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

/****************************************************************************/


/*
 *  test_cop0():
 *
 */
void test_cop0(void)
{
	uint64_t r;
	int i;

	printstatus(OK, "Coprocessor 0 test:\n");

	r = read_cop0_reg(15);
	printstatus(OK, "reg 15 (PRid) = ");
	printhex(r, sizeof(r));
	printstr("\n");

	printstatus(OK, "\n");
}


/*
 *  test_primes():
 *
 *  List the first few primes.
 */
#define	N_PRIMES	9
#define	HIGHEST_PRIME	29
int correct[N_PRIMES]  = { 3, 5, 7, 11, 13, 17, 19, 23, 29 };
void test_primes(void)
{
	int x = 1, k, n_correct = 0;

	printstatus(OK, "Prime number test:\n");
	x = 3;  k = 0;

	while (x <= HIGHEST_PRIME) {
		if (prime(x)) {
			if (x == correct[k]) {
				printstatus(OK, "");
				printdec(x);
				n_correct ++;
			} else {
				printstatus(FAIL, "");
				printdec(x);
				printstr("; should be ");
				printdec(correct[k]);
			}
			printstr("\n");
			k++;
		}
		x += 2;
	}

	if (k == N_PRIMES && n_correct == N_PRIMES) {
		printstatus(OK, "All ");
		printdec(N_PRIMES);
		printstr(" primes found correctly.\n");
	} else {
		printstatus(FAIL, "");
		printdec(n_correct);
		printstr(" correct primes found, there should have been ");
		printdec(N_PRIMES);
		printstr(".\n");
	}

	printstatus(OK, "\n");
}


/*
 *  test_mandel():
 *
 *  Draw a mandelbrot. (This doesn't test anything specific, it's just for
 *  fun. The output is "OK" for every line.)
 */
void test_mandel(int use_framebuffer)
{
	int xsize = MANDEL_WIDTH, ysize = MANDEL_HEIGHT;
	long x, y, line, column;
	int color;
	long x1 = -2*MANDEL_SCALE, x2 = 2*MANDEL_SCALE;
	long y1 = -2*MANDEL_SCALE, y2 = 2*MANDEL_SCALE;

	if (use_framebuffer) {
		xsize = FRAMEBUFFER_XSIZE;
		ysize = FRAMEBUFFER_YSIZE;
	}

	printstatus(OK, "Mandelbrot test: xsize=");
	printdec(xsize); printstr(", ysize="); printdec(ysize); printstr("\n");

	if (!use_framebuffer) {
		printstatus(OK, "+");
		for (column=0; column<xsize; column++)
			printstr("-");
		printstr("+\n");
	}

	for (line=ysize-1; line>=0; line--) {
		if (!use_framebuffer)
			printstatus(OK, "|");
		for (column=0; column<xsize; column++) {
			x = (x2-x1) * column/xsize + x1;
			y = (y2-y1) * line/ysize + y1;
			color = mandel_color(x, y, MANDEL_MAXCOLOR);

			if (use_framebuffer) {
				if (color < MANDEL_MAXCOLOR / 2)
					putpixel(column, line, 255 * color /
					    (MANDEL_MAXCOLOR/2), 0, 0);
				else
					putpixel(column, line, 255,
					    255 * (color-MANDEL_MAXCOLOR/2) /
					    (MANDEL_MAXCOLOR/2), 0);
			} else
				printchar(MANDEL_COLORS[color]);
		}
		if (!use_framebuffer)
			printstr("|\n");
	}

	if (!use_framebuffer) {
		printstatus(OK, "+");
		for (column=0; column<xsize; column++)
			printstr("-");
		printstr("+\n");
	}

	printstatus(OK, "\n");
}


/****************************************************************************/


void entry_func(void)
{
	printstatus(OK, "test_mips start\n");
	printstatus(OK, "\n");

	/*  Do all tests.  */
	test_cop0();
	test_primes();
	test_mandel(0);
	test_mandel(1);

	printstatus(OK, "All tests done. Halting.\n");

	/*  Hang forever.  */
	for (;;)
		;
}

