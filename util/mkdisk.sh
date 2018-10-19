#!/bin/bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

OUT=$1
IN=$2

OUTDIR=`dirname $1`

# Calculate required space
SPACE_REQ=$(du -sb "$DIR/../fatbase" | cut -f 1)
let "SIZE = ($SPACE_REQ / 1040000)"

# Minimum size
if [ $SIZE -lt 32 ]; then
    SIZE=32
fi

# Use more sectors-per-cluster for larger disk sizes
if [ $SIZE -gt 128 ]; then
    SPC=4
else
    SPC=1
fi

# Create empty FAT image
rm -f $OUT
mkdir -p cdrom
fallocate -l ${SIZE}M $OUT || dd if=/dev/zero bs=1M count=${SIZE} of=$OUT
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
        mmd -i $OUT $IN_FILE
        mkdir -p $OUT_FILE
    else
        mcopy -i $OUT $i '::'$IN_FILE
        touch $OUT_FILE
    fi
done

rm -f cdrom/efi/boot/bootia32.efi # Otherwise virtualbox may erroneously try to load from this
rm -f cdrom/efi/boot/bootx64.efi # Same

