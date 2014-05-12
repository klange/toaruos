#!/bin/bash

sudo apt-get install expect exuberant-ctags

make toolchain

. toolchain/activate.sh

make

expect util/test.exp
