CFLAGS=-DHOSTNATIVE -I../../src/include/testmachine

all:
	@echo Read the README file for instructions on how to build
	@echo the floatingpoint test program for various targets.
	@echo To build for the host, type \"make native\"

native: fptest.o fpconst.o
	$(CC) fptest.o fpconst.o -o fptest
	./fptest > fptest_hostnative.output
	cat fptest_hostnative.output

clean:
	rm -f *.o fptest fptest.output *core

