/*
 *  $Id: hello_mips.c,v 1.3 2005-08-09 18:29:20 debug Exp $
 *
 *  MIPS Hello World for GXemul.
 *
 *  Compile:
 *      mips64-unknown-elf-gcc -O2 hello_mips.c -mips4 -mabi=64 -c
 *
 *  and link:
 *      mips64-unknown-elf-ld -Ttext 0xa800000000030000 -e f
 *              hello_mips.o -o hello_mips --oformat=elf64-bigmips
 *
 *  Run:
 *      gxemul -E testmips hello_mips
 */

/*  Note: The cast to a signed int causes the address to be sign-extended
    correctly to 0xffffffffb00000xx when compiled in 64-bit mode  */
#define	PUTCHAR_ADDRESS		((signed int)0xb0000000)
#define	HALT_ADDRESS		((signed int)0xb0000010)

void printchar(char ch)
{
	*((volatile unsigned char *) PUTCHAR_ADDRESS) = ch;
}

void halt(void)
{
	*((volatile unsigned char *) HALT_ADDRESS) = 0;
}

void printstr(char *s)
{
	while (*s)
		printchar(*s++);
}

void f(void)
{
	printstr("Hello world\n");
	halt();
}

