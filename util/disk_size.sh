#!/bin/bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

if [ -f "$DIR/../.disk_size" ]; then
	cat "$DIR/../.disk_size"
else
	echo "131072"
fi
