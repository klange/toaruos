#!/bin/bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

SPACE_REQ=$(du -sb "$DIR/../base" | cut -f 1)

let "SIZE = $SPACE_REQ / 4096 + 2048"
echo $SIZE

#if [ -e "$DIR/../base/usr/share/fonts" ]; then
#  echo 20000
#elif [ -e "$DIR/../base/usr/python" ]; then
#  echo 11000
#else
#  echo 4096
#fi

