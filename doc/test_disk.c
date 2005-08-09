/*
 *  $Id: test_disk.c,v 1.1 2005-08-09 18:29:20 debug Exp $
 *
 *  Test program for dev_disk.  This program reads the first two 512-byte
 *  sectors of a disk image, and dumps them in hex/ascii format.
 *
 *  Compile:
 *	mips64-unknown-elf-gcc -O2 test_disk.c -mips4 -mabi=64 -c
 *
 *  and link:
 *	mips64-unknown-elf-ld -Ttext 0xa800000000030000 -e f
 *		test_disk.o -o test_disk --oformat=elf64-bigmips
 *
 *  Run:
 *	gxemul -d disk.img -E testmips test_disk
 */

/*  Note: The cast to a signed int causes the address to be sign-extended
    correctly to 0xffffffffb00000xx when compiled in 64-bit mode  */
#define	PUTCHAR_ADDRESS		((signed int)0xb0000000)
#define	HALT_ADDRESS		((signed int)0xb0000010)

#define	DISK_ADDRESS		((signed int)0xb3000000)

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

void printhex2(int i)
{
	printchar("0123456789abcdef"[i >> 4]);
	printchar("0123456789abcdef"[i & 15]);
}

void printhex4(int i)
{
	printhex2(i >> 8);
	printhex2(i & 255);
}

void f(void)
{
	int ofs, scsi_id = 0, status, i;
	unsigned char ch;

	printstr("Testing dev_disk.\n");
	printstr("Assuming that SCSI ID 0 is available.\n");

	for (ofs = 0; ofs < 1024; ofs += 512) {
		printstr("\n");

		*((volatile int *) (DISK_ADDRESS + 0)) = ofs;
		*((volatile int *) (DISK_ADDRESS + 0x10)) = scsi_id;

		/*  0 = read, 1 = write:  */
		*((volatile int *) (DISK_ADDRESS + 0x20)) = 0;

		/*  Get status:  */
		status = *((volatile int *) (DISK_ADDRESS + 0x30));

		if (status == 0) {
			printstr("Read failed.\n");
			halt();
		}

		printstr("Sector dump:\n");
		for (i = 0; i < 512; i++) {
			if ((i % 16) == 0) {
				printhex4(i);
				printstr(" ");
			}
			printstr(" ");
			ch = *((volatile unsigned char *) DISK_ADDRESS
			    + i + 0x4000);
			printhex2(ch);
			if ((i % 16) == 15) {
				int j;
				printstr("  ");
				for (j = i-15; j <= i; j++) {
					ch = *((volatile unsigned char *)
					    DISK_ADDRESS + j + 0x4000);
					if (ch < 32 || ch >= 127)
						ch = '.';
					printchar(ch);
				}
				printstr("\n");
			}
		}
	}

	printstr("\nDone.\n");
	halt();
}

