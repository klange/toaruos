#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

TARGET=i686-pc-toaru
PREFIX="$DIR/local"
TOARU_SYSROOT="$DIR/../base"

cd "$DIR"

mkdir -p tarballs

pushd tarballs
    if [ ! -e "binutils-2.27.tar.gz" ]; then
        wget "http://ftp.gnu.org/gnu/binutils/binutils-2.27.tar.gz"
    fi
    if [ ! -e "gcc-6.4.0.tar.gz" ]; then
        wget "http://www.netgull.com/gcc/releases/gcc-6.4.0/gcc-6.4.0.tar.gz"
    fi

    if [ ! -d "binutils-2.27" ]; then
        tar -xf "binutils-2.27.tar.gz"
        pushd "binutils-2.27"
            patch -p1 < $DIR/patches/binutils.patch > /dev/null
        popd
    fi

    if [ ! -d "gcc-6.4.0" ]; then
        tar -xf "gcc-6.4.0.tar.gz"
        pushd "gcc-6.4.0"
            patch -p1 < $DIR/patches/gcc.patch > /dev/null
        popd
    fi
popd

mkdir -p local
mkdir -p build
mkdir -p build/binutils
mkdir -p build/gcc

pushd build

    unset PKG_CONFIG_LIBDIR # Just in case

    pushd binutils
        $DIR/tarballs/binutils-2.27/configure --target=$TARGET --prefix=$PREFIX --with-sysroot=$TOARU_SYSROOT --disable-werror || exit 1
        make -j4
        make install
    popd

    pushd gcc
        $DIR/tarballs/gcc-6.4.0/configure --target=i686-pc-toaru --prefix=$PREFIX --with-sysroot=$TOARU_SYSROOT --disable-nls --enable-languages=c --disable-libssp --with-newlib || baiol
        make all-gcc all-target-libgcc
        make install-gcc install-target-libgcc
    popd

popd



