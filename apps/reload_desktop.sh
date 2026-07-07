#!/bin/esh

export-cmd DESKTOP cat $HOME/.wallpaper.pid

if [ -n "$DESKTOP" ] then kill -SIGUSR2 $DESKTOP
