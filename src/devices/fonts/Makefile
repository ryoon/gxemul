#  $Id: Makefile,v 1.1 2004/08/03 00:31:59 debug Exp $

all: font8x8.c font8x16.c

font8x8.c: Xconv_raw_to_c
	./Xconv_raw_to_c vt220l.808 font8x8 > font8x8.c

font8x16.c: Xconv_raw_to_c
	./Xconv_raw_to_c vt220l.816 font8x16 > font8x16.c

clean:
	rm -f Xconv_raw_to_c font8x16.c font8x8.c

