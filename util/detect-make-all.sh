#!/bin/bash

if [ -f toaruos.iso ]; then
    echo "cdrom tags"
else
    echo "system tags userspace"
fi
