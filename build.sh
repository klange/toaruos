#!/bin/bash

sudo apt-get install clang yasm genext2fs build-essential wget libmpfr-dev libmpc-dev libgmp-dev qemu autoconf automake texinfo
# Build the toolchain:
pushd toolchain
./prepare.sh
./install.sh
. activate.sh
popd
# Build the userspace tools:
pushd userspace
make
popd
# Build the kernel
make system         # to build the kernel
