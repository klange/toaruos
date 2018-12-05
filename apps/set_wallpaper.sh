#!/bin/sh

if empty? "$1" then exec sh -c "echo 'usage: $0 WALLPAPER'"
if not stat -Lq "$1" then exec sh -c "echo '$0: $1 does not exist'"

export-cmd DESKTOP cat /var/run/.wallpaper.pid
if empty? "$DESKTOP" then sh -c "echo '$0: No wallpaper running?'"

echo "wallpaper=$1" > $HOME/.wallpaper.conf

kill -SIGUSR1 $DESKTOP

