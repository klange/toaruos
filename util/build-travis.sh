#!/bin/bash

make toolchain

. toolchain/activate.sh

make

expect util/test.exp
