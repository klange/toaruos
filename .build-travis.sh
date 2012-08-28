#!/bin/bash

sudo apt-get install clang yasm genext2fs build-essential wget libmpfr-dev libmpc-dev libgmp-dev qemu autoconf automake texinfo
# Build the toolchain: (silently)
pushd toolchain
./prepare.sh  > /dev/null 2>/dev/null || exit 1
./install.sh  > /dev/null 2>/dev/null || exit 1
. activate.sh > /dev/null 2>/dev/null || exit 1
popd
# Build the userspace tools:
pushd userspace
make || exit 1
popd
# Build the kernel
make system || exit 1        # to build the kernel
# XXX: Attempt to boot the kernel with qemu automatically...
