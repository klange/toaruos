#!/usr/bin/python3
"""
Client for communicating with qemu-harness.
Reads events from serial and sets display resolution.
"""

import os

if os.getuid() != 0:
    # Need to be root, going to do fun stuff.
    os.execve('/bin/gsudo',['gsudo',__file__],os.environ)

import fcntl
import struct

with open('/dev/fb0','r') as fb:
    with open('/dev/ttyS0','r+') as f:
        while 1:
            data = f.readline().strip()
            if data.startswith('geometry-changed'):
                # geometry-changed width height
                _, width, height = data.split()
                data = bytearray(struct.pack("II",int(width),int(height)))
                try:
                    fcntl.ioctl(fb, 0x5006, data) # IO_VID_SET
                except:
                    pass
                f.write('X')
                f.flush()

