#!/bin/bash

# Locale stuff
. /opt/build/base.sh

# Build toolchain
. /opt/toaruos/toolchain/activate.sh

# Print environment for reference
env

# Print cross GCC version for reference
i686-pc-toaru-gcc --version

# Cheating and copying /usr/python from toolchain
cp -r /opt/toaruos/hdd/usr/python hdd/usr/python

pushd hdd/usr
    if [ ! -d bin ]; then
        mkdir bin
    fi

    if [ ! -d lib ]; then
        mkdir lib
    fi

    pushd bin

        # Can never be too careful.
        ln -s ../python/bin/python3.6 python3.6
        ln -s ../python/bin/python3.6 python3
        ln -s ../python/bin/python3.6 python

    popd

    pushd lib

        ln -s ../python/lib/libpython3.6m.so

    popd
popd

# Build the CD
make cdrom

# We're done!
