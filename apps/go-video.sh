#!/bin/sh

# Start font server if freetype is available
if stat -Lq /usr/lib/libfreetype.so then font-server

if empty? "$1" then export WIDTH=1024 else export WIDTH="$1"
if empty? "$2" then export HEIGHT=768 else export HEIGHT="$2"

# Switch to graphics mode
if not set-resolution --initialize auto $WIDTH $HEIGHT then exec sh -c "echo 'Failed to set video mode, bailing.'; exit 1"

# Tell the terminal to pause input
killall -s USR2 terminal-vga

# Start the compositor
compositor
