#!/bin/bash

INSTALL_PACKAGES=true

cd "$( dirname "${BASH_SOURCE[0]}" )"

while test $# -gt 0; do
    case "$1" in
        -q|--quick)
            INSTALL_PACKAGES=false
            break
            ;;
        *)
            break
            ;;
    esac
done

unset CC

if [[ "$INSTALL_PACKAGES" == "true" ]] ; then

    echo "I am going to install some system packages. I will probably need you to provide a password."
    echo "If you don't want to do this and you're sure you have all of the required system packages, then interrupt the password prompt and run this script again with -q."

    if [ -f /etc/debian_version ]; then
        sudo apt-get install yasm genext2fs build-essential wget libmpfr-dev libmpc-dev libgmp3-dev qemu autoconf automake texinfo pkg-config git ctags
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
        echo "  - git"
        echo "  - ctags"
        echo "(If you are on Arch, install: clang yasm genext2fs base-devel wget mpfr mpc gmp qemu autoconf automake texinfo pkg-config git ctags)"
        echo ""
        echo "... then run this script (toolchain/toolchain-build.sh) again with the -q flag."
        exit 1
    fi
fi

# Really quick, let's check our genext2fs before we start
if [ -z "$(genext2fs --help 2>&1 | grep -- "block-size")" ]; then
    echo -e "\033[1;31mHold up!\033[0m"
    echo "Your genext2fs does not support the -B (--block-size) argument."
    echo "You can build a copy from the CVS HEAD which supports this:"
    echo
    echo "    http://genext2fs.sourceforge.net/"
    echo
    exit 1
fi

# Build the toolchain:
unset PKG_CONFIG_LIBDIR
./prepare.sh
./install.sh
. activate.sh

echo "Toolchain build is complete."
echo "You should now activate the toolchain:"
echo ""
echo "    . toolchain/activate.sh"
echo ""
echo " NOTICE: The toolchain makes use of Bash-isms, if you are using"
echo "         a different shell, please switch to Bash or set up your"
echo "         environment appropriately."
echo ""
echo "And then use Make to build the kernel and userspace:"
echo ""
echo "    make"
echo ""
