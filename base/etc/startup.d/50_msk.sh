#!/bin/esh

if kcmdline -q no-startup-msk then exit 0

echo "Checking for package updates..." > /dev/console
msk update &
