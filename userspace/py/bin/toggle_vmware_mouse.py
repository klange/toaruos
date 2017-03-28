#!/usr/bin/python3
import fcntl
import sys
import os

if not 'absolute' in sys.argv and not 'relative' in sys.argv:
    print("Expected 'absolute' or 'relative'")
    sys.exit(1)

if not os.path.exists('/dev/vmmouse'):
    if not os.path.exists('/dev/absmouse'):
        print("No absolute mouse pointer available.")
        sys.exit(1)
    else:
        # VirtualBox
        with open('/dev/absmouse','r') as f:
            fcntl.ioctl(f, 1 if 'relative' in sys.argv else 2)
else:
    # VMWare / QEMU
    with open('/dev/vmmouse','r') as f:
        fcntl.ioctl(f, 1 if 'relative' in sys.argv else 2)
