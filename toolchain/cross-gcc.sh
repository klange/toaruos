#!/bin/bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

. $DIR/config.sh
. $DIR/util.sh
. $DIR/activate.sh

pushd $DIR

# Grab extras
./extras.sh

MPC=mpc-0.9
GMP=gmp-5.0.1
MPFR=mpfr-3.0.1
GCCV=4.6.0
GCC=gcc-$GCCV
BINUTILS=binutils-2.22

VIRTPREFIX=/usr
REALPREFIX=$DIR/../hdd

if [ ! -d tarballs/$GCC/mpfr ]; then
    mv tarballs/$MPFR tarballs/$GCC/mpfr
fi
if [ ! -d tarballs/$GCC/gmp  ]; then
    mv tarballs/$GMP  tarballs/$GCC/gmp
fi
if [ ! -d tarballs/$GCC/mpc  ]; then
    mv tarballs/$MPC  tarballs/$GCC/mpc
fi

# Actual build process

pushd build || bail
    if [ -d binutils-native ]; then
        rm -rf binutils-native
    fi
    mkdir binutils-native
    pushd binutils-native || bail
        $DIR/tarballs/$BINUTILS/configure --prefix=$VIRTPREFIX --host=$TARGET --target=$TARGET || bail
        make || bail
        make DESTDIR=$REALPREFIX install || bail
    popd
    if [ -d gcc-native ]; then
        rm -rf gcc-native
    fi
    mkdir gcc-native
    pushd gcc-native || bail
        make distclean
        $DIR/tarballs/$GCC/configure --prefix=$VIRTPREFIX --host=$TARGET --target=$TARGET --disable-nls --enable-languages=c,c++ --disable-libssp --with-newlib || bail
        make all-gcc || bail
        make DESTDIR=$REALPREFIX install-gcc || bail
        make all-target-libgcc || bail
        make DESTDIR=$REALPREFIX install-target-libgcc || bail
        touch $PREFIX/$TARGET/include/fenv.h
        make all-target-libstdc++-v3 || bail
        make DESTDIR=$REALPREFIX install-target-libstdc++-v3 || bail
    popd

    TMP_INCFIX=$REALPREFIX$VIRTPREFIX/lib/gcc/$TARGET/$GCCV/include-fixed

    if [ -d $TMP_INCFIX ]; then
        rm -r "$TMP_INCFIX"
    fi

    pushd $REALPREFIX$VIRTPREFIX/bin || bail
        $TARGET-strip *
    popd

    pushd $REALPREFIX$VIRTPREFIX/libexec/gcc/$TARGET/$GCCV || bail
        $TARGET-strip cc1 collect2
    popd

popd
