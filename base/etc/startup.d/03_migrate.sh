#!/bin/sh

if not kcmdline -q migrate then exit 0

echo -n "Migrating filesystem..." >> /dev/pex/splash
/bin/migrate
