import struct
import sys

addresses = {}
sources = {}
last_touched = {}

def find_nearby_allocations(addr,size):
    results = []
    for key in addresses.keys():
        if abs(addr - key) < size*2 and addresses[key]:
            results.append(key)
    results = sorted(results)
    for key in results:
        if key == addr:
            print("     self: 0x%x (size %d, allocated by 0x%x)" % (key, addresses[key], sources[key]))
        else:
            print("   nearby: 0x%x (size %d, allocated by 0x%x)" % (key, addresses[key], sources[key]))

count = 0
while 1:
    data = sys.stdin.read(17)
    t, = struct.unpack_from("c",data,0)
    addr, = struct.unpack_from("I",data,1)
    size, = struct.unpack_from("I",data,5)
    extra, = struct.unpack_from("I",data,9)
    fault, = struct.unpack_from("I",data,13)

    count += 1
    if count % 1000 == 0:
        print(count)

    if t == 'm':
        addresses[addr] = size
        sources[addr] = fault
        last_touched[addr] = t
    elif t == 'v':
        addresses[addr] = size
        sources[addr] = fault
        last_touched[addr] = t
    elif t == 'c':
        addresses[addr] = 'c'
        sources[addr] = 0
        last_touched[addr] = t
    elif t == 'r':
        if addr not in addresses:
            print("Bad realloc: 0x%x" % addr)
        else:
            addresses[addr] = None
            addresses[extra]  = size
            sources[extra] = fault
            last_touched[addr] = t
            last_touched[extra] = t
    elif t == 'f':
        if addr not in addresses:
            print("Bad free detected: 0x%x" % addr)
        elif addresses[addr] is None:
            print("Double free detected: 0x%x (allocated by 0x%x)" % (addr,sources[addr]))
        elif addresses[addr] == 'c':
            print("freeing something that was calloced...")
            addresses[addr] = None
        elif extra != 0xDEADBEEF:
            print("Large buffer has bad value: 0x%x (0x%x) expected size is %d, supposed is %d" % (addr, extra, addresses[addr], size))
        elif addresses[addr] != size:
            print("Size on free is incorrect: 0x%x %d %d 0x%x allocated by 0x%x last touched by %c" % (addr,addresses[addr], size, fault, sources[addr], last_touched[addr]))
            find_nearby_allocations(addr,addresses[addr])
        else:
            addresses[addr] = None
    elif t == 'h':
        if addr not in addresses:
            print("Spurious halt: 0x%x" % (addr))
        else:
            print("Halting on suspected bug: 0x%x size was %d" % (addr, addresses[addr]))
    else:
        print("Garbage data detected: %c 0x%x %d 0xx" % (t, addr, size, extra))
        break

