#!/bin/bash

(
    while [ 1 == 1 ]; do
        echo "..."
        sleep 1m
    done
) &

watchdog_pid=$!

sudo apt-get update >/dev/null 2>/dev/null
sudo apt-get install expect exuberant-ctags >/dev/null 2>/dev/null

if [ ! -d "hdd/usr/lib" ]; then
    make toolchain >/dev/null 2>/dev/null
fi


kill $!

. toolchain/activate.sh

make

expect util/test-travis.exp
