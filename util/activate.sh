#!/bin/bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

if [ ! -e "$DIR/local/bin/i686-pc-toaru-gcc" ]; then
    echo "Toolchain has not been built. Please run \`bash util/build-gcc.sh\`." >&2
    exit 1
fi

export PATH="$DIR/local/bin:$PATH"
export TOOLCHAIN="$DIR"
echo "$DIR/local/bin:$PATH"
