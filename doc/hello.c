/*  Hello world for mips64emul  */

/*  Note: The cast to an int causes the address to be sign-extended
    correctly to 0xffffffffb00000xx when compiled in 64-bit mode  */
#define	PUTCHAR_ADDRESS		((int)0xb0000000)
#define	HALT_ADDRESS		((int)0xb0000010)

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
