#!/usr/bin/python

import tarfile
import json

import os
import signal
import sys

with tarfile.open(sys.argv[1], 'r:gz') as package:
    try:
        manifest_inf = package.getmember('manifest.json')
        manifest_br  = package.extractfile(manifest_inf)
        manifest_str = manifest_br.read()
    except KeyError:
        print("Invalid MSK package: missing manifest")
        sys.exit(1)

    manifest = json.loads(manifest_str)
    print(manifest['package'])
    print('.'.join([str(x) for x in manifest['version']]))

    print("contents:")
    for member in package.getnames():
        if not member.startswith('./'):
            continue
        print(member.replace('./','/',1))

    print("Going to extract to /")
    members = [member for member in package.getmembers() if member.name.startswith('./')]
    package.extractall('/',members=members)

    if os.path.exists('/tmp/.wallpaper.pid'):
        with open('/tmp/.wallpaper.pid','r') as f:
            pid = int(f.read().strip())
    if pid:
        os.kill(pid, signal.SIGUSR1)
