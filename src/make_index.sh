#!/bin/sh
#
#  $Id: make_index.sh,v 1.1 2007-06-17 23:32:20 debug Exp $
#
#  Updates the .index file.
#

rm -f .index
for a in *.c; do
	B=`grep COMMENT $a`
	if [ z"$B" != z ]; then
		printf "$a " >> .index
		echo "$B"|cut -d : -f 2- >> .index
	fi
done

