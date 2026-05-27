#!/bin/bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

OUT=$1
IN=$2

OUTDIR=`dirname $1`
SPACE_REQ=$(du -s -B 2048 "$DIR/../fatbase" | cut -f 1)
PAD=20
ATTEMPT=0

# Calculate required space
while true
do
    let "ATTEMPT = ATTEMPT + 1"
    if [[ $ATTEMPT -gt 10 ]]; then
        echo "Giving up."
        exit 1
    fi
    if [[ $ATTEMPT -gt 1 ]]; then
        echo "Trying again (attempt $ATTEMPT)"
    fi
    let "PAD = PAD + 5"
    let "SIZE = (($SPACE_REQ + $PAD) * 2048)"


    # Create empty FAT image
    rm -f $OUT
    mkdir -p cdrom
    fallocate -l ${SIZE} $OUT || dd if=/dev/zero bs=1 count=${SIZE} of=$OUT
    mkfs.fat -s 1 -S 2048 $OUT

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
            mmd -i $OUT $IN_FILE || continue 2
            mkdir -p $OUT_FILE || continue 2
        else
            mcopy -i $OUT $i '::'$IN_FILE || continue 2
            touch $OUT_FILE
        fi
    done

    break
done

rm -f cdrom/efi/boot/bootia32.efi # Otherwise virtualbox may erroneously try to load from this
rm -f cdrom/efi/boot/bootx64.efi # Same

