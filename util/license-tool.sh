#!/bin/bash

for f in $(find kernel userspace modules | fgrep '.c'); do
	echo -n "Looking at '$f'... "

	if ! grep -q "Copyright" $f; then
		echo "needs headers... "

		authors=$(mktemp)

		echo "/* This file is part of ToaruOS and is released under the terms" >> $authors
		echo " * of the NCSA / University of Illinois License - see LICENSE.md" >> $authors

		last_user="X"
		last_sdate="X"
		last_edate="X"
		while read -r line; do
			auth_user=$(echo "$line" | cut -d"	" -f1)
			auth_date=$(echo "$line" | cut -d"	" -f2)

			if [ "$last_user" != "$auth_user" ]; then
				if [ "$last_user" != "X" ]; then
					if [ "$last_edate" == $last_sdate ]; then
						echo " * Copyright (C) $last_sdate $last_user" >> $authors
					else
						echo " * Copyright (C) $last_sdate-$last_edate $last_user" >> $authors
					fi
				fi
				last_sdate=$auth_date
				last_user=$auth_user
			fi

			last_edate=$auth_date
			last_user=$auth_user
		done < <(git log --date=short --pretty=format:"%an	%ad" "$f" | sed 's/-..-..//' | sort | uniq; echo "-")

		echo " */" >> $authors

		tmpfile=$(mktemp)
		cat "$authors" "$f" > "$tmpfile"
		mv "$tmpfile" "$f"

		rm $authors
	else
		echo "-- already has license headers"
	fi
done
