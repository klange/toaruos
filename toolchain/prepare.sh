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
    $BEG "tar" "Un-archiving $1..."
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

function deleteUnusedGCC () {
    # These directories are not used and are primarily for support of unnecessarily libraries like Java and the testsuite.
    rm -r $1/boehm-gc $1/gcc/ada $1/gcc/go $1/gcc/java $1/gcc/objc $1/gcc/objcp $1/gcc/testsuite $1/gnattools $1/libada $1/libffi $1/libgo $1/libjava $1/libobjc 
}

function installNewlibStuff () {
    cp -r ../patches/newlib/toaru $1/newlib/libc/sys/toaru
}

pushd "$DIR" > /dev/null

    if [ ! -d tarballs ]; then
        mkdir tarballs
    fi
    pushd tarballs > /dev/null
        $INFO "wget" "Pulling source packages..."
        grab "gcc"  "http://gcc.petsads.us/releases/gcc-4.6.0/gcc-4.6.0.tar.gz"
        grab "mpc"  "http://www.multiprecision.org/mpc/download/mpc-0.9.tar.gz"
        grab "mpfr" "http://www.mpfr.org/mpfr-3.0.1/mpfr-3.0.1.tar.gz"
        grab "gmp"  "ftp://ftp.gmplib.org/pub/gmp-5.0.1/gmp-5.0.1.tar.gz"
        grab "binutils" "http://ftp.gnu.org/gnu/binutils/binutils-2.22.tar.gz"
        grab "newlib" "ftp://sources.redhat.com/pub/newlib/newlib-1.19.0.tar.gz"
        grab "freetype" "http://download.savannah.gnu.org/releases/freetype/freetype-2.4.9.tar.gz"
        $INFO "wget" "Pulled source packages."
        $INFO "tar"  "Decompressing..."
        deco "gcc"  "gcc-4.6.0.tar.gz"
        deco "mpc"  "mpc-0.9.tar.gz"
        deco "mpfr" "mpfr-3.0.1.tar.gz"
        deco "gmp"  "gmp-5.0.1.tar.gz"
        deco "binutils" "binutils-2.22.tar.gz"
        deco "newlib" "newlib-1.19.0.tar.gz"
        deco "freetype" "freetype-2.4.9.tar.gz"
        $INFO "tar"  "Decompressed source packages."
        $INFO "patch" "Patching..."
        patc "gcc"  "gcc-4.6.0"
        patc "mpc"  "mpc-0.9"
        patc "mpfr" "mpfr-3.0.1"
        patc "gmp"  "gmp-5.0.1"
        patc "binutils" "binutils-2.22"
        patc "newlib" "newlib-1.19.0"
        patc "freetype" "freetype-2.4.9"
        $INFO "patch" "Patched third-party software."
        $INFO "--" "Running additional bits..."
        deleteUnusedGCC "gcc-4.6.0"
        installNewlibStuff "newlib-1.19.0"
    popd > /dev/null

    mkdir build
    mkdir local

popd > /dev/null
