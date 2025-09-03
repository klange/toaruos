#!/bin/bash

MAJOR=$(grep __kernel_version_major kernel/sys/version.c | sed s'/.*= \(.*\);/\1/')
MINOR=$(grep __kernel_version_minor kernel/sys/version.c | sed s'/.*= \(.*\);/\1/')
LOWER=$(grep __kernel_version_lower kernel/sys/version.c | sed s'/.*= \(.*\);/\1/')

cat << EOF
PRETTY_NAME="ToaruOS ${MAJOR}.${MINOR}"
NAME="ToaruOS"
VERSION_ID="${MAJOR}.${MINOR}.${LOWER}"
VERSION="${MAJOR}.${MINOR}.${LOWER}"
ID=toaru
HOME_URL="https://toaruos.org/"
SUPPORT_URL="https://github.com/klange/toaruos"
BUG_REPORT_URL="https://github.com/klange/toaruos"
EOF
