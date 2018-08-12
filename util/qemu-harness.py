#!/usr/bin/env python3
"""
Harness for running QEMU and communicating window sizes through serial.
"""

import subprocess
import asyncio
import time
import sys

from Xlib.display import Display
from Xlib.protocol.event import KeyPress, KeyRelease
from Xlib.XK import string_to_keysym
import Xlib

qemu_bin = 'qemu-system-i386'

qemu = subprocess.Popen([
    qemu_bin,
    '-enable-kvm',
    '-cdrom','image.iso',
    # 1GB of RAM
    '-m','1G',
    # Enable audio
    '-soundhw','ac97,pcspk',
    # The GTK interface does not play well, force SDL
    '-display', 'sdl',
    # /dev/ttyS0 is stdio multiplexed with monitor
    '-serial', 'mon:stdio',
    # /dev/ttyS1 is TCP connection to the harness
    '-serial','tcp::4444,server,nowait',
    # Add a VGA card with 32mb of video RAM
    '-device', 'VGA,id=video0,vgamem_mb=32',
    # Set the fwcfg flag so our userspace app recognizes us
    '-fw_cfg','name=opt/org.toaruos.displayharness,string=1'
])

# Give QEMU some time to start up and create a window.
time.sleep(1)

# Find the QEMU window...
def findQEMU(window):
    try:
        x = window.get_wm_name()
        if 'QEMU' in x:
            return window
    except:
        pass
    children = window.query_tree().children
    for w in children:
        x = findQEMU(w)
        if x: return x
    return None

display = Display()
root = display.screen().root
qemu_win = findQEMU(root)

def send_key(key, state, up=False):
    """Send a key press or release to the QEMU window."""
    time.sleep(0.1)
    t = KeyPress
    if up:
        t = KeyRelease

    sym = string_to_keysym(key)
    ke = t(
        time=int(time.time()),
        root=display.screen().root,
        window=qemu_win,
        same_screen=0,
        child=Xlib.X.NONE,
        root_x = 0, root_y = 0, event_x = 0, event_y = 0,
        state = 0xc,
        detail = display.keysym_to_keycode(sym)
    )
    qemu_win.send_event(ke)
    display.flush()

class Client(asyncio.Protocol):

    def connection_made(self, transport):
        asyncio.ensure_future(heartbeat(transport))

    def data_received(self, data):
        if 'X' in data.decode('utf-8'):
            # Send Ctrl-Alt-u
            send_key('Control_L',0x00)
            send_key('Alt_L',0x04)
            send_key('u',0x0c)
            send_key('u',0x0c,True)
            send_key('Alt_L',0x0c,True)
            send_key('Control_L',0x04,True)

async def heartbeat(transport):
    """Heartbeat process checks window size every second and sends update signal."""
    w = 0
    h = 0
    while 1:
        await asyncio.sleep(1)
        try:
            g = qemu_win.get_geometry()
        except Xlib.error.BadDrawable:
            print("QEMU window is gone, exiting.")
            asyncio.get_event_loop().call_soon(sys.exit, 0)
            return
        if g.width != w or g.height != h:
            transport.write(("geometry-changed %d %d\n" % (g.width,g.height)).encode('utf-8'))
        w = g.width
        h = g.height

loop = asyncio.get_event_loop()
coro = loop.create_connection(Client,'127.0.0.1',4444)
asyncio.ensure_future(coro)
loop.run_forever()
loop.close()

