#!/bin/bash

# Toolchain Installer for Debian-like systems. If you're running
# something else, you're pretty much on your own.

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

. $DIR/util.sh

pushd "$DIR" > /dev/null

    if [ ! -d tarballs ]; then
        mkdir tarballs
    fi
    pushd tarballs > /dev/null
        rm -r newlib-1.19.0
        deco "newlib" "newlib-1.19.0.tar.gz" || bail
        patc "newlib" "newlib-1.19.0" || bail
        installNewlibStuff "newlib-1.19.0" || bail
    popd > /dev/null

    if [ ! -d build ]; then
        mkdir build
    fi
    if [ ! -d local ]; then
        mkdir local
    fi

popd > /dev/null
