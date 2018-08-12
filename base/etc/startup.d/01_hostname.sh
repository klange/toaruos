#!/bin/sh

export-cmd HOSTNAME cat /etc/hostname

if empty? "$HOSTNAME" then exec hostname "localhost" else exec hostname "$HOSTNAME"
