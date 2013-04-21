#!/bin/bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

. $DIR/util.sh

pushd "$DIR" > /dev/null

    if [ ! -d tarballs ]; then
        mkdir tarballs
    fi
    pushd tarballs > /dev/null
        grab "mpc"  "http://www.multiprecision.org/mpc/download" "mpc-0.9.tar.gz"
        grab "mpfr" "http://www.mpfr.org/mpfr-3.0.1" "mpfr-3.0.1.tar.gz"
        grab "gmp"  "ftp://ftp.gmplib.org/pub/gmp-5.0.1" "gmp-5.0.1.tar.bz2"
        rm -rf "gmp-5.0.1" "mpc-0.9" "mpfr-3.0.1"
        deco "mpc"  "mpc-0.9.tar.gz"
        deco "mpfr" "mpfr-3.0.1.tar.gz"
        deco "gmp"  "gmp-5.0.1.tar.bz2"
        patc "mpc"  "mpc-0.9"
        patc "mpfr" "mpfr-3.0.1"
        patc "gmp"  "gmp-5.0.1"
    popd > /dev/null
popd > /dev/null
