#!/bin/bash

COOKIE=".2016-12-03-dynamic.cookie"

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

make || exit 1

echo "=== Running test suite. ==="

expect util/test-travis.exp || exit 1

echo "=== Building live CD ==="

git fetch --unshallow

git clone . _cdsource || exit 1

cd _cdsource

make cdrom || exit 1

echo "=== Done. ==="

