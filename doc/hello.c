/*  Hello world for mips64emul  */

#define	PUTCHAR_ADDRESS		0xb0000000
#define	HALT_ADDRESS		0xb0000010

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
