#!/bin/bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd $DIR/..

HDD_PATH=`pwd`/hdd

if [ ! -d pycairo ]; then
	echo "No pycairo source checkout, cloning..."
	git clone https://github.com/klange/pycairo
fi

pushd pycairo/src || exit 1
    touch config.h
    make
    cp _cairo.so $HDD_PATH/usr/python/lib/python3.6/_cairo.so
    echo "from _cairo import *" > $HDD_PATH/usr/python/lib/python3.6/cairo.py
popd
