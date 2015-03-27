#!/bin/bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

# PREFIX is the cross-compiler tools prefix
PREFIX=$DIR/local

# TARGET is the platform triplet
TARGET=i686-pc-toaru

# TOARU_SYSROOT is the system root, which is equivalent to the hard disk
export TOARU_SYSROOT=`readlink -f $DIR/../hdd`

# VIRTPREFIX is where we put stuff from the perspective of the target system
# since most build scripts will default to /usr/local and we want just /usr...
VIRTPREFIX=/usr
