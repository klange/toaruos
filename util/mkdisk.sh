#!/bin/bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

OUT=$1
IN=$2

OUTDIR=`dirname $1`

if [ -e "$DIR/../base/usr/share/fonts" ]; then
    SIZE=128
else
    SIZE=64
fi

rm -f $OUT
mkdir -p cdrom
fallocate -l ${SIZE}M $OUT || dd if=/dev/zero bs=1M count=${SIZE} of=$OUT
mkfs.fat -s 1 -S 2048 $OUT

#echo "Turning $IN into $OUT"

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

