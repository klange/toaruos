#!/bin/sh

export-cmd DESKTOP cat /var/run/.wallpaper.pid

if not empty? "$DESKTOP" then kill -SIGUSR2 $DESKTOP
