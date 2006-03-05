# $Id: Makefile,v 1.8 2006-03-05 16:52:36 debug Exp $

BINS=cp_removeblocks bintrans_eval try_runlen \
	sgiprom_to_bin decprom_dump_txt_to_bin hex_to_bin \
	new_test_1 new_test_2 new_test_x new_test_loadstore ic_statistics

all: $(BINS)

new_test_loadstore: new_test_loadstore_a.o new_test_loadstore_b.o
	$(CC) new_test_loadstore_a.o new_test_loadstore_b.o -o new_test_loadstore

clean:
	rm -f $(BINS) *.o *core

clean_all:
	$(MAKE) clean
