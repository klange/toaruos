#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

for mod in $DIR/../modules/*.c; do
	fgrep -q "@package $1" "$mod" || continue
	echo -n $mod | sed s'#.*/\([^/]*\)\.c#\1 #'
done

echo
