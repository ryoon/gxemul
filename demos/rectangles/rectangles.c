/*
 *  $Id: rectangles.c,v 1.2 2006-05-04 17:16:38 debug Exp $
 *
 *  GXemul demo:  Random rectangles
 *
 *  This file is in the Public Domain.
 */

#include "dev_fb.h"


#ifdef MIPS
/*  Note: The ugly cast to a signed int (32-bit) causes the address to be
	sign-extended correctly on MIPS when compiled in 64-bit mode  */ 
#define PHYSADDR_OFFSET         ((signed int)0x80000000)
#else
#define PHYSADDR_OFFSET         0
#endif


/*  Framebuffer base address:  */
#define FB_BASE			(PHYSADDR_OFFSET + DEV_FB_ADDRESS)


void my_memset(unsigned char *a, int x, int len)
{
	while (len-- > 0)
		*a++ = x;
}


void draw_rectangle(int x1, int y1, int x2, int y2, int c)
{
	int y, len;

	for (y=y1; y<=y2; y++) {
		len = 3 * (x2-x1+1);
		if (len > 0)
			my_memset((unsigned char *)FB_BASE + 3*(640*y+x1),
			    c, len);
	}
}


unsigned int my_random()
{
	static int a = 0x124879b;
	static int b = 0xb7856fa2;
	int c = a ^ (b * 51);
	a = 17 * c - (b * 171);
	return c;
}


void f(void) {
	/*  Draw random rectangles forever:  */
	for (;;)  {
		draw_rectangle(my_random() % 640, my_random() % 480,
		    my_random() % 640, my_random() % 480, my_random());
	}
}

