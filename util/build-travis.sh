#!/bin/bash

# Locale stuff
. /opt/build/base.sh

# Build toolchain
. /opt/toaruos/toolchain/activate.sh

# Print environment for reference
env

# Print cross GCC version for reference
i686-pc-toaru-gcc --version

# Clone the toolchain's Python checkout
git clone /opt/toaruos/toaru-python toaru-python

# Do the first build pass
make cdrom # Base pass to build libs

# Build the Python stuff
echo "travis_fold:start:Python"
toolchain/install-python.sh
echo "travis_fold:end:Python"

echo "travis_fold:start:PyCairo"
toolchain/install-pycairo.sh
echo "travis_fold:end:PyCairo"

# Do a final build
make cdrom # Build with python/pycairo

# We're done!
