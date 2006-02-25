#!/bin/sh

if [ ! z$1 = zYES ]; then
	echo "usage: $0 YES"
	echo This will create lots of files in the current directory.
	echo Make sure you are standing in the right directory before
	echo running this script.
	exit
fi

HEADER=../make_doc_header.html
FOOTER=../make_doc_header.html


echo Generating documentation...


DEVICES=`echo ../../src/devices/dev_*.c`

rm -f tmp_devices.html
echo "<b>Devices:</b>" >> tmp_devices.html
echo "<pre>" >> tmp_devices.html
for a in $DEVICES; do
	printf "<a href=dev_" >> tmp_devices.html
	X=`echo $a | cut -d _ -f 2-|cut -d . -f 1`
	printf $X >> tmp_devices.html
	printf ".html>$X</a>\n" >> tmp_devices.html
done
echo "</pre>" >> tmp_devices.html

cat $HEADER > index.html
printf "<td align=left valign=top>\n" >> index.html
cat tmp_devices.html >> index.html
printf "</td>\n" >> index.html
printf "<td align=left valign=top>\n" >> index.html
printf "Hello.\n" >> index.html
printf "</td>\n" >> index.html
cat $FOOTER >> index.html

echo Done.
