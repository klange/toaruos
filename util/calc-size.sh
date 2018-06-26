#!/bin/bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

if [ -e "$DIR/../base/usr/python" ]; then
  echo 11000
else
  echo 4096
fi

