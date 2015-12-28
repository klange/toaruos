#!/bin/bash

COOKIE=".2015-12-15-testing2.cookie"

unset CC

if [ ! -e "toolchain/local/$COOKIE" ]; then
    echo "=== Cleaning any preexisting stuff... ==="
    rm -fr toolchain/build
    rm -fr toolchain/local
    rm -fr toolchain/tarballs/*
    echo "=== Starting watchdog ==="
    (
        while [ 1 == 1 ]; do
            echo "..."
            sleep 1m
        done
    ) &
    watchdog_pid=$!
    echo "=== Begin Toolchain Build ==="
    pushd toolchain
        unset PKG_CONFIG_LIBDIR
        ./prepare.sh
        ./install.sh
        date > ./local/$COOKIE
    popd
    echo "=== End Toolchain Build ==="
    echo "=== Stopping watchdog ==="
    kill $watchdog_pid
else
    echo "=== Toolchain was cached. ==="
fi

. toolchain/activate.sh

make

echo "=== Running test suite. ==="

expect util/test-travis.exp

echo "=== Building Live CD ==="

git clone . cdrom
pushd cdrom
    make cdrom
    ls -lha toaruos.iso
    mkdir out
    cp toaruos.iso out/latest.iso
popd # cdrom
