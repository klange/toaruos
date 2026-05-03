#!/bin/esh

export-cmd DESKTOP cat $HOME/.wallpaper.pid

if not empty? "$DESKTOP" then kill -SIGUSR2 $DESKTOP
