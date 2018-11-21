#!/bin/sh

export-cmd START kcmdline -g start

# We haven't actually hit a login yet, so make sure these are set here...
export USER=root
export HOME=/home/root

if equals? "$START" "--vga" then exec /bin/terminal-vga -l
if equals? "$START" "--headless" then exec /bin/getty
if empty? "$START" then exec /bin/compositor else exec /bin/compositor $START

