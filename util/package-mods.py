#!/usr/bin/env python3

import os
import struct

# This is ORDERED, so don't screw it up.
mods_to_pack = [
    'zero',
    'random',
    'serial',
    'procfs',
    'tmpfs',
    'ext2',
    'ps2kbd',
    'ps2mouse',
    'lfbvideo',
    'packetfs',
]


with open('modpack.kop','wb') as pack:
    for mod in mods_to_pack:
        with open('hdd/mod/{mod}.ko'.format(mod=mod),'rb') as m:
            pack.write(b"PACK")
            size = os.stat(m.name).st_size
            extra = 0
            while (size + extra) % 4096 != 0:
                extra += 1
            pack.write(struct.pack("I", size+extra))
            pack.write(b'\0' * (4096-8))
            pack.write(m.read(size))
            pack.write(b'\0' * extra)
    pack.write(b"PACK")
    pack.write(b'\0\0\0\0')


