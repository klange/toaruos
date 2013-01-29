#!/bin/bash

if [ -f /etc/debian_version ]; then
    sudo apt-get install clang yasm genext2fs build-essential wget libmpfr-dev libmpc-dev libgmp-dev qemu autoconf automake texinfo pkg-config
elif [ -f /etc/fedora-release ]; then
    sudo yum groupinstall 'Development Tools'
    sudo yum groupinstall 'Development Libraries'
    sudo yum install clang yasm mpfr-devel libmpc-devel gmp-devel
    echo "Warning: Fedora is unsupported in this script. Be careful!"
    echo "For best results, follow the steps in the script manually."
fi
# Build the toolchain:
unset PKG_CONFIG_LIBDIR
pushd toolchain
./prepare.sh
./install.sh
./activate.sh
popd
# Build the userspace tools:
pushd userspace
make
popd
# Build the kernel
make system         # to build the kernel
# XXX: Attempt to boot the kernel with qemu automatically...
