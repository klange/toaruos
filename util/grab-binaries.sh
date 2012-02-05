#!/bin/bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
BINARIES="http://dl.dropbox.com/u/44305966/toaru-bin-current.tar.gz"

$DIR/mk-beg "wget" "Pulling binaries..."
wget --quiet -O /tmp/`whoami`-toaru-bin.tar.gz "$BINARIES"
$DIR/mk-end "wget" "Binaries retreived!"

$DIR/mk-beg "tar" "Extracting binaries..."
tar -xf /tmp/`whoami`-toaru-bin.tar.gz -C $DIR/../hdd/bin/
$DIR/mk-end "tar" "Binaries extracted."

$DIR/mk-beg "rm" "Removing hard disk image to ensure rebuild..."
rm -f $DIR/../toaruos-disk.img 2>/dev/null
$DIR/mk-end "rm" "Hard disk image removed."

$DIR/mk-beg "rm" "Cleaning up..."
rm /tmp/`whoami`-toaru-bin.tar.gz
$dir/mk-end "rm" "Cleaned up."

$DIR/mk-info "    ---  Done!"
