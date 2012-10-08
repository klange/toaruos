#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
BEG=$DIR/../util/mk-beg
END=$DIR/../util/mk-end
INFO=$DIR/../util/mk-info

function grab () {
    $BEG "wget" "Pulling $1... [$2/$3]"
    if [ ! -f "$3" ]; then
        wget -q "$2/$3"
        $END "wget" "$1"
    else
        $END "-" "Already have a $1"
    fi
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

function installNewlibStuff () {
    cp -r ../patches/newlib/toaru $1/newlib/libc/sys/toaru
    cp -r ../patches/newlib/syscall.h $1/newlib/libc/sys/toaru/syscall.h
    # dlmalloc
    cp -r ../patches/newlib/malloc.c $1/newlib/libc/stdlib/malloc.c
    cp -r ../patches/newlib/setjmp.S $1/newlib/libc/machine/i386/setjmp.S
}

function bail () {
    echo -e "\033[1;31mBuild failed. Please check the logs above to see what went wrong.\033[0m"
    exit 1
}

