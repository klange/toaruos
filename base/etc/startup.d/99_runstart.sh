#!/bin/sh

export-cmd START kcmdline -g start

# We haven't actually hit a login yet, so make sure these are set here...
export USER=root
export HOME=/home/root

export-cmd GETTY_ARGS qemu-fwcfg opt/org.toaruos.gettyargs

echo -n "Launching startup application..." > /dev/pex/splash
echo -n "!quit" > /dev/pex/splash

if equals? "$START" "--vga" then exec /bin/terminal-vga -l
if equals? "$START" "--headless" then exec /bin/getty ${GETTY_ARGS}
if empty? "$START" then exec /bin/compositor else exec /bin/compositor $START

