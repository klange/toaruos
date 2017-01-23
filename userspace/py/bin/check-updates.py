#!/usr/bin/python3
import os
import subprocess
import sys

import yutani
from dialog import DialogWindow
import yutani_mainloop

def version():
    """Get a release from uname without a git short sha."""
    release = os.uname().release
    if '-' in release:
        return release[:release.index('-')]
    return release

def compare_version(left,right):
    if left[0] > right[0]: return True
    if left[0] == right[0] and left[1] > right[1]: return True
    if left[0] == right[0] and left[1] == right[1] and left[2] > right[2]: return True
    return False

if __name__ == '__main__':

    # Verify network is available first.
    with open('/proc/netif','r') as f:
        lines = f.readlines()
        if len(lines) < 4 or "no network" in lines[0]:
            print("No network available, can't check for updates.")
            sys.exit(1)

    # Check for updates...
    try:
        current = list(map(int,version().split(".")))
        latest_str = subprocess.check_output(['fetch','http://toaruos.org/latest']).decode('utf-8').strip()
        latest = list(map(int,latest_str.split(".")))
    except:
        print("Unable to parse latest version.")
        sys.exit(1)

    if compare_version(latest,current):
        yutani.Yutani()
        d = yutani.Decor()
        def derp():
            sys.exit(0)
        def updates():
            subprocess.Popen(['help-browser.py','http://toaruos.org/update.trt'])
            sys.exit(0)

        DialogWindow(d,"Update Available",f"A new release of ToaruOS (v{latest_str}) is available.\nPlease visit <link target=\"#\">https://github.com/klange/toaruos</link> to upgrade.",callback=derp,cancel_callback=updates,icon='star',cancel_label="What's New?",close_is_cancel=False)

        yutani_mainloop.mainloop()



