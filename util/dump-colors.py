#!/usr/bin/env python

for i in range(256):
    print str(i) + "\t\x1b[48;5;" + str(i) + "m   \x1b[0m"
