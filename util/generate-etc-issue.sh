#!/bin/bash

MAJOR=$(grep __kernel_version_major kernel/sys/version.c | sed s'/.*= \(.*\);/\1/')
MINOR=$(grep __kernel_version_minor kernel/sys/version.c | sed s'/.*= \(.*\);/\1/')

cat << EOF
ToaruOS ${MAJOR}.${MINOR} \n \l

EOF
