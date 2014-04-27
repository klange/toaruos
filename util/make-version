#!/bin/bash

VERSION=`git rev-parse --short HEAD`

X=$(git status -s | grep -q '^.M')
if [ $? -eq 0 ]; then
    VERSION="$VERSION-dirty"
fi

echo -n $VERSION
