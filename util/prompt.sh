#!/bin/bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

if ! $DIR/check-reqs.sh >&2; then
    echo "A toolchain is not available and the above issues were found." >&2
    echo "Resolve the problems above and run \`make\` again." >&2
    echo -n "n" && exit 1
fi

echo -n "Toolchain has not been built. Would you like to build it now? (y/n) " >&2
read response
case $response in
    [yY]) bash $DIR/build-gcc.sh >&2 ;;
    [nN]) echo -n "n" && exit 1 ;;
    *)
esac

