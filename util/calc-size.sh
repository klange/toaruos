#!/bin/bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

SPACE_REQ=$(du -sb "$DIR/../base" | cut -f 1)

let "SIZE = ($SPACE_REQ / 3400)"
echo $SIZE

