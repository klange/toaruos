#!/bin/bash

# Toolchain Installer for Debian-like systems. If you're running
# something else, you're pretty much on your own.

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

BEG=$DIR/../util/mk-beg
END=$DIR/../util/mk-end
INFO=$DIR/../util/mk-info

function grab () {
    $BEG "wget" "Pulling $1..."
    wget -q "$2"
    $END "wget" "$1"
}

function deco () {
    $BEG "tar" "Unarchving $1..."
    tar -xf $2
    $END "tar" "$1"
}

function patc () {
    $BEG "patch" "Patching $1..."
    pushd "$2" > /dev/null
    patch -p1 < ../../patches/$2.patch > /dev/null
    popd > /dev/null
    $END "patch" "$1"
}

function deleteUnused_gcc_4_6_0 () {
    # These directories are not used and are primarily for support of unecessarily libraries like Java and the testsuite.
    rm -r gcc-4.6.0/boehm-gc gcc-4.6.0/gcc/ada gcc-4.6.0/gcc/go gcc-4.6.0/gcc/java gcc-4.6.0/gcc/objc gcc-4.6.0/gcc/objcp gcc-4.6.0/gcc/testsuite gcc-4.6.0/gnattools gcc-4.6.0/libada gcc-4.6.0/libffi gcc-4.6.0/libgo gcc-4.6.0/libjava gcc-4.6.0/libobjc 
}

pushd "$DIR" > /dev/null

    if [ ! -d tarballs ]; then
        mkdir tarballs
    fi
    pushd tarballs > /dev/null
        $INFO "wget" "Pulling source pakcages..."
        #grab "gcc"  "http://gcc.petsads.us/releases/gcc-4.6.0/gcc-4.6.0.tar.gz"
        #grab "mpc"  "http://www.multiprecision.org/mpc/download/mpc-0.9.tar.gz"
        #grab "mpfr" "http://www.mpfr.org/mpfr-3.0.1/mpfr-3.0.1.tar.gz"
        #grab "gmp"  "ftp://ftp.gmplib.org/pub/gmp-5.0.1/gmp-5.0.1.tar.gz"
        $INFO "wget" "Pulled source packages."
        $INFO "tar"  "Decompressing..."
        #deco "gcc"  "gcc-4.6.0.tar.gz"
        #deco "mpc"  "mpc-0.9.tar.gz"
        #deco "mpfr" "mpfr-3.0.1.tar.gz"
        #deco "gmp"  "gmp-5.0.1.tar.gz"
        $INFO "tar"  "Decompressed source packages."
        $INFO "patch" "Patching..."
        #patc "gcc"  "gcc-4.6.0"
        #patc "mpc"  "mpc-0.9"
        #patc "mpfr" "mpfr-3.0.1"
        #patc "gmp"  "gmp-5.0.1"
        $INFO "patch" "Patched third-party software."
    popd > /dev/null

popd > /dev/null
