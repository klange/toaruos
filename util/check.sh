#!/bin/bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

if [ ! -e "$DIR/local/bin/i686-pc-toaru-gcc" ]; then
    echo -n "n";
    exit 1;
else
    echo -n "y";
    exit 0;
fi
