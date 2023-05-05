#!/bin/esh

if kcmdline -q no-startup-msk then exit 0

echo -n "Checking for package updates..." >> /dev/pex/splash
msk update &
