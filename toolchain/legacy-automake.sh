#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

. $DIR/config.sh
. $DIR/util.sh
. $DIR/activate.sh

# Just in case
pushd $DIR

pushd tarballs || bail
    grab "automake" "http://ftp.gnu.org/gnu/automake" "automake-1.11.6.tar.gz" || bail
    deco "automake" "automake-1.11.6.tar.gz" || bail
popd

pushd build || bail

    if [ ! -d automake ]; then
        mkdir automake
    fi

    pushd automake || bail
        $DIR/tarballs/automake-1.11.6/configure --prefix=$PREFIX
        make
        make install
        pushd $PREFIX/share || bail
            ln -s automake-1.11 automake
            ln -s aclocal-1.11 aclocal
        popd
    popd

popd

