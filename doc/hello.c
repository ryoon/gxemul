/*  Hello world for mips64emul  */

#define	PUTCHAR_ADDRESS		0xb0000000

void printchar(char ch)
{
	*((volatile unsigned char *) PUTCHAR_ADDRESS) = ch;
}

void printstr(char *s)
{
	while (*s)
		printchar(*s++);
}

void f(void)
{
	printstr("Hello world\n");
	for (;;)
		;
}
