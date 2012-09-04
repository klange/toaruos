#!/bin/bash

unset CC
sudo apt-get install clang yasm genext2fs build-essential wget libmpfr-dev libmpc-dev libgmp-dev qemu autoconf automake texinfo
sudo apt-get remove kvm-ipxe
mkdir ~/bin
ln -s `which qemu-system-i386` ~/bin/qemu
export PATH="~/bin:$PATH"

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
# Boot it up and run some tests
make test || exit 1
