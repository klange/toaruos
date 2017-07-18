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

# Build the CD
make cdrom

# We're done!
