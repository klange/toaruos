#!/bin/bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

OUT=$1
IN=$2

OUTDIR=`dirname $1`

# Calculate required space
SPACE_REQ=$(du -s -B 2048 "$DIR/../fatbase" | cut -f 1)
let "SIZE = (($SPACE_REQ + 25) * 2048)"
SPC=1


# Create empty FAT image
rm -f $OUT
mkdir -p cdrom
fallocate -l ${SIZE} $OUT || dd if=/dev/zero bs=1 count=${SIZE} of=$OUT
mkfs.fat -s $SPC -S 2048 $OUT

#echo "Turning $IN into $OUT"

# Add files
for i in $(find $IN)
do
    if [[ $i == $IN ]]; then
        continue
    fi
    OUT_FILE=`echo $i | sed s"/^$IN/$OUTDIR/"`
    IN_FILE=`echo $i | sed s"/^$IN//"`
    #echo $IN_FILE  $OUT_FILE
    if [ -d "$i" ]; then
        mmd -i $OUT $IN_FILE || exit 1
        mkdir -p $OUT_FILE || exit 1
    else
        mcopy -i $OUT $i '::'$IN_FILE || exit 1
        touch $OUT_FILE
    fi
done

rm -f cdrom/efi/boot/bootia32.efi # Otherwise virtualbox may erroneously try to load from this
rm -f cdrom/efi/boot/bootx64.efi # Same

