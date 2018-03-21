#!/bin/bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

if [ ! -e "$DIR/local/bin/i686-pc-toaru-gcc" ]; then
    echo -n "Toolchain has not been built. Would you like to build it now? (y/n) " >&2
    read response
    case $response in
        [yY]) bash $DIR/build-gcc.sh ;;
        [nN]) exit 1 ;;
        *) exit 1 ;;
    esac
fi

export PATH="$DIR/local/bin:$PATH"
export TOOLCHAIN="$DIR"
echo "$DIR/local/bin:$PATH"
