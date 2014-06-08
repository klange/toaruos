#!/bin/bash

for f in $(find kernel modules | fgrep '.c'); do
	echo -n "Looking at '$f'... "

	outfile=$(mktemp)
	sed -e 'N;s#\*/\n*/\*# *#g;P;D' $f > $outfile
	mv $outfile $f

	echo "done"
done
