#!/bin/bash
# Make a wallpaper for grub from the input.

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

convert $1 \
	-resize 1024x768^ \
	-gravity center \
	-extent 1024x768 \
	-level 0,200% \
	${DIR}/cdrom/wallpaper.png
