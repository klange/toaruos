#!/bin/bash

OUT=$1
IN=$2

OUTDIR=`dirname $1`

rm -f $OUT
fallocate -l 64M $OUT
mkfs.fat $OUT

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

