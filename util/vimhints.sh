#!/bin/bash

for f in $(find kernel modules | fgrep '.c'); do
	echo -n "Looking at '$f'... "

	if ! grep -q "vim:" $f; then
		echo "needs vimhints... "

		header=$(mktemp)
		echo "/* vim: tabstop=4 shiftwidth=4 noexpandtab" >> $header
		echo "*/" >> $header
		tmpfile=$(mktemp)
		cat "$header" "$f" > "$tmpfile"
		mv "$tmpfile" "$f"

		rm $header
	else
		echo "-- already has vimhints"
	fi
done
