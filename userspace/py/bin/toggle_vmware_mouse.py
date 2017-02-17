#!/usr/bin/python3
import fcntl
import sys

if not 'absolute' in sys.argv and not 'relative' in sys.argv:
    print("Expected 'absolute' or 'relative'")
    sys.exit(1)

with open('/dev/vmmouse','r') as f:
    fcntl.ioctl(f, 1 if 'relative' in sys.argv else 2)
