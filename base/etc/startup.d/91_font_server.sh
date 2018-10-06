#!/bin/sh

# Only start if we're likely to be running a GUI
export-cmd START kcmdline -g start
if equals? "$START" "--vga" then exit
if equals? "$START" "--headless" then exit

if stat -Lq /usr/share/fonts/DejaVuSansMono.ttf then font-server
