#!/bin/sh

export-cmd START kcmdline -g start

if equals? "$START" "--vga" then exec /bin/terminal-vga -l
if equals? "$START" "--headless" then exec /bin/getty
if empty? "$START" then exec /bin/compositor else exec /bin/compositor $START

