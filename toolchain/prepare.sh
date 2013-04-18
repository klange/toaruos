#!/bin/bash

# Toolchain Installer for Debian-like systems. If you're running
# something else, you're pretty much on your own.

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

. $DIR/util.sh

function deleteUnusedGCC () {
    # If you are running from the non-core GCC, run this function delete the stuff we don't care about
    rm -r $1/boehm-gc $1/gcc/ada $1/gcc/go $1/gcc/java $1/gcc/objc $1/gcc/objcp $1/gcc/testsuite $1/gnattools $1/libada $1/libffi $1/libgo $1/libjava $1/libobjc 
}

pushd "$DIR" > /dev/null

    if [ ! -d tarballs ]; then
        mkdir tarballs
    fi
    pushd tarballs > /dev/null
        $INFO "wget" "Pulling source packages..."
        grab "gcc"  "http://www.netgull.com/gcc/releases/gcc-4.6.0" "gcc-core-4.6.0.tar.gz"
        grab "g++"  "http://www.netgull.com/gcc/releases/gcc-4.6.0" "gcc-g++-4.6.0.tar.gz"
        #grab "mpc"  "http://www.multiprecision.org/mpc/download" "mpc-0.9.tar.gz"
        #grab "mpfr" "http://www.mpfr.org/mpfr-3.0.1" "mpfr-3.0.1.tar.gz"
        #grab "gmp"  "ftp://ftp.gmplib.org/pub/gmp-5.0.1" "gmp-5.0.1.tar.gz"
        grab "binutils" "http://ftp.gnu.org/gnu/binutils" "binutils-2.22.tar.gz"
        grab "newlib" "https://github.com/downloads/klange/osdev" "newlib-1.19.0.tar.gz"
        grab "freetype" "http://download.savannah.gnu.org/releases/freetype" "freetype-2.4.9.tar.gz"
        grab "zlib" "http://zlib.net" "zlib-1.2.7.tar.gz"
        grab "libpng" "https://github.com/downloads/klange/osdev" "libpng-1.5.13.tar.gz"
        grab "pixman" "http://www.cairographics.org/releases" "pixman-0.26.2.tar.gz"
        grab "cairo" "http://www.cairographics.org/releases" "cairo-1.12.2.tar.xz"
        grab "mesa" "ftp://ftp.freedesktop.org/pub/mesa/7.5.2" "MesaLib-7.5.2.tar.gz"
        grab "ncurses" "ftp://invisible-island.net/ncurses" "ncurses-5.9.tar.gz"
        grab "vim" "ftp://ftp.vim.org/pub/vim/unix" "vim-7.3.tar.bz2"
        $INFO "wget" "Pulled source packages."
        rm -rf "binutils-2.22" "freetype-2.4.9" "gcc-4.6.0" "gmp-5.0.1" "libpng-1.5.13" "mpc-0.9" "mpfr-3.0.1" "newlib-1.19.0" "zlib-1.2.7" "pixman-0.28.2" "Mesa-7.5.2" "ncurses-5.9" "vim73"
        $INFO "tar"  "Decompressing..."
        deco "gcc"  "gcc-core-4.6.0.tar.gz"
        deco "g++"  "gcc-g++-4.6.0.tar.gz"
        #deco "mpc"  "mpc-0.9.tar.gz"
        #deco "mpfr" "mpfr-3.0.1.tar.gz"
        #deco "gmp"  "gmp-5.0.1.tar.gz"
        deco "binutils" "binutils-2.22.tar.gz"
        deco "newlib" "newlib-1.19.0.tar.gz"
        deco "freetype" "freetype-2.4.9.tar.gz"
        deco "zlib" "zlib-1.2.7.tar.gz"
        deco "libpng" "libpng-1.5.13.tar.gz"
        deco "pixman" "pixman-0.26.2.tar.gz"
        deco "cairo" "cairo-1.12.2.tar.xz"
        deco "mesa" "MesaLib-7.5.2.tar.gz"
        deco "ncurses" "ncurses-5.9.tar.gz"
        deco "vim" "vim-7.3.tar.bz2"
        $INFO "tar"  "Decompressed source packages."
        $INFO "patch" "Patching..."
        patc "gcc"  "gcc-4.6.0"
        #patc "mpc"  "mpc-0.9"
        #patc "mpfr" "mpfr-3.0.1"
        #patc "gmp"  "gmp-5.0.1"
        patc "binutils" "binutils-2.22"
        patc "newlib" "newlib-1.19.0"
        patc "freetype" "freetype-2.4.9"
        patc "libpng" "libpng-1.5.13"
        patc "pixman" "pixman-0.26.2"
        patc "cairo" "cairo-1.12.2"
        patc "mesa" "Mesa-7.5.2"
        patc "ncurses" "ncurses-5.9"
        patc "vim" "vim73"
        $INFO "patch" "Patched third-party software."
        $INFO "--" "Running additional bits..."
        #deleteUnusedGCC "gcc-4.6.0"
        installNewlibStuff "newlib-1.19.0"
    popd > /dev/null

    if [ ! -d build ]; then
        mkdir build
    fi
    if [ ! -d local ]; then
        mkdir local
    fi

popd > /dev/null
