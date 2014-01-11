#!/bin/bash

if [ -f /etc/debian_version ]; then
    sudo apt-get install yasm genext2fs build-essential wget libmpfr-dev libmpc-dev libgmp3-dev qemu autoconf automake texinfo pkg-config
elif [ -f /etc/fedora-release ]; then
    sudo yum groupinstall 'Development Tools'
    sudo yum groupinstall 'Development Libraries'
    sudo yum install yasm mpfr-devel libmpc-devel gmp-devel
    echo "Warning: Fedora is unsupported in this script. Be careful!"
    echo "For best results, follow the steps in the script manually."
    echo "(Script will continue in 5 seconds)"
    sleep 5
else
    echo "You are on an entirely unsupported system, please ensure you have the following packages:"
    echo "  - essential development packages for your platform (C headers, etc.)"
    echo "  - development headers for mpfr, mpc, and gmp"
    echo "  - clang / LLVM"
    echo "  - YASM"
    echo "  - genext2fs"
    echo "  - autoconf/automake"
    echo "  - wget"
    echo "  - qemu"
    echo "  - texinfo"
    echo "  - pkg-config"
    echo "(If you are on Arch, install: clang yasm genext2fs base-devel wget mpfr mpc gmp qemu autoconf automake texinfo pkg-config)"
    echo ""
    echo "... then comment out the 'exit' below this block of echos in 'build.sh'."
    exit 1
fi

# Really quick, let's check our genext2fs before we start
if [ -z "$(genext2fs --help 2>&1 | grep -- "block-size")" ]; then
    echo -e "\033[1;31mHold up!\033[0m"
    echo "Your genext2fs does not support the -B (--block-size) argument."
    echo "You need to build your own genext2fs with the patch described"
    echo "in this mailing list post:"
    echo
    echo "    http://bugs.debian.org/cgi-bin/bugreport.cgi?bug=562999"
    echo
    echo "You can get genext2fs sources from sourceforge:"
    echo
    echo "    http://genext2fs.sourceforge.net/"
    echo
    exit 1
fi

# Build the toolchain:
unset PKG_CONFIG_LIBDIR
pushd toolchain
./prepare.sh
./install.sh
. activate.sh
popd
# Build the kernel
make system         # to build the kernel
# XXX: Attempt to boot the kernel with qemu automatically...
