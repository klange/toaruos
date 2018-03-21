#!/bin/bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

echo -n "Toolchain has not been built. Would you like to build it now? (y/n) " >&2
read response
case $response in
    [yY]) bash $DIR/build-gcc.sh >&2 ;;
    [nN]) echo -n "n" && exit 1 ;;
    *)
esac

