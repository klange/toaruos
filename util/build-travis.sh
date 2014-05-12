#!/bin/bash

sudo apt-get install expect exuberant-ctags

make toolchain >/dev/null 2>/dev/null

. toolchain/activate.sh

make

expect util/test.exp
