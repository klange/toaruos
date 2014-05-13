#!/bin/bash

(
    while [ 1 == 1 ]; do
        echo "..."
        sleep 1m
    done
) &

watchdog_pid=$!

sudo apt-get install expect exuberant-ctags >/dev/null 2>/dev/null
make toolchain >/dev/null 2>/dev/null

kill $!

. toolchain/activate.sh

make

expect util/test-travis.exp
