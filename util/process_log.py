import struct
import sys

addresses = {}

while 1:
    data = sys.stdin.read(13)
    t, = struct.unpack_from("c",data,0)
    addr, = struct.unpack_from("I",data,1)
    size, = struct.unpack_from("I",data,5)
    extra, = struct.unpack_from("I",data,9)

    if t == 'm':
        addresses[addr] = size
    elif t == 'v':
        addresses[addr] = size
    elif t == 'c':
        addresses[addr] = 'c'
    elif t == 'r':
        if addr not in addresses:
            print("Bad realloc: 0x%x" % addr)
        else:
            addresses[addr] = None
            addresses[extra]  = size
    elif t == 'f':
        if addr not in addresses:
            print("Bad free detected: 0x%x" % addr)
        elif addresses[addr] is None:
            print("Double free detected: 0x%x" % addr)
        elif addresses[addr] == 'c':
            print("freeing something that was calloced...")
            addresses[addr] = None
        elif addresses[addr] != size:
            print("Size on free is incorrect: 0x%x %d %d" % (addr,addresses[addr], size))
        elif extra != 0xDEADBEEF:
            print("Extremely large buffer or otherwise bad free (but size is right?): 0x%x" % addr)
        else:
            addresses[addr] = None
    else:
        print("Garbage data detected: ", t, addr, size, extra)
        break

