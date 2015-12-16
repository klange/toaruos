#!/bin/bash

if [ ! -a "toolchain/local/bin/i686-pc-toaru-gcc" ]; then
    echo "=== Begin Toolchain Build ==="
    pushd toolchain
        unset PKG_CONFIG_LIBDIR
        ./prepare.sh
        ./install.sh
        . activate.sh
    popd
    echo "=== End Toolchain Build ==="
else
    echo "=== Toolchain was cached. ==="
fi

. toolchain/activate.sh

make

expect util/test-travis.exp
