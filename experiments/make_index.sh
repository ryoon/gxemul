#!/bin/sh
#
#  $Id: make_index.sh,v 1.2 2007-11-21 12:22:29 debug Exp $
#
#  Updates the .index file.
#

rm -f .index
for a in *.c *.cc *.h; do
	B=`grep COMMENT $a`
	if [ z"$B" != z ]; then
		printf "$a " >> .index
		echo "$B"|cut -d : -f 2- >> .index
	fi
done

