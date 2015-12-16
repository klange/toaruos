#!/bin/bash

COOKIE=".2015-12-15-testing.cookie"

if [ ! -a "toolchain/local/$COOKIE" ]; then
    echo "=== Cleaning any preexisting stuff... ==="
    rm -f toolchain/build
    rm -f toolchain/local
    rm -f toolchain/tarballs/!(*.tar*)
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

expect util/test-travis.exp
