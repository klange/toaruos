#!/bin/esh

# Validate argument, make sure the image exists and can be loaded
if empty? "$1" then exec sh -c "echo 'usage: $0 WALLPAPER'"
export-cmd NEW_WALLPAPER realpath "$1"
if not stat -Lq "$NEW_WALLPAPER" then exec sh -c "echo '$0: $1: no such file or directory'"
if not check-image -q "$NEW_WALLPAPER" then exec sh -c "echo '$0: $1: not a valid image'"

# Write the wallpaper to the config
echo "wallpaper=$NEW_WALLPAPER" > $HOME/.wallpaper.conf

# Tell the desktop to reload it
export-cmd DESKTOP cat /var/run/.wallpaper.pid
if empty? "$DESKTOP" then exec sh -c "echo '$0: No wallpaper running?'"
kill -SIGUSR1 $DESKTOP
