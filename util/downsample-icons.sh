#!/bin/bash

pushd hdd/usr/share/icons/48
	for i in *.png; do
		echo "Downsampling $i..."
		convert "$i" -filter box -resize 24x24 -define png:color-type=6 ../24/$i
	done
popd
