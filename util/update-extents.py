#!/usr/bin/env python3
import array
import struct

def read_struct(fmt,buf,offset):
    out, = struct.unpack_from(fmt,buf,offset)
    return out, offset + struct.calcsize(fmt)

class ISO(object):

    def __init__(self, path):
        with open(path, 'rb') as f:
            tmp = f.read()
            self.data = array.array('b', tmp)
        self.sector_size = 2048
        o = 0x10 * self.sector_size
        self.type,             o = read_struct('B',self.data,o)
        self.id,               o = read_struct('5s',self.data,o)
        self.version,          o = read_struct('B',self.data,o)
        _unused0,              o = read_struct('B',self.data,o)
        self.system_id,        o = read_struct('32s',self.data,o)
        self.volume_id,        o = read_struct('32s',self.data,o)
        _unused1,              o = read_struct('8s',self.data,o)
        self.volume_space_lsb, o = read_struct('<I',self.data,o)
        self.volume_space_msb, o = read_struct('>I',self.data,o)
        _unused2,              o = read_struct('32s',self.data,o)
        self.volume_set_lsb,   o = read_struct('<H',self.data,o)
        self.volume_set_msb,   o = read_struct('>H',self.data,o)
        self.volume_seq_lsb,   o = read_struct('<H',self.data,o)
        self.volume_seq_msb,   o = read_struct('>H',self.data,o)
        self.logical_block_size_lsb,   o = read_struct('<H',self.data,o)
        self.logical_block_size_msb,   o = read_struct('>H',self.data,o)
        self.path_table_size_lsb,   o = read_struct('<I',self.data,o)
        self.path_table_size_msb,   o = read_struct('>I',self.data,o)
        self.path_table_lsb,   o = read_struct('<I',self.data,o)
        self.optional_path_table_lsb,   o = read_struct('<I',self.data,o)
        self.path_table_msb,   o = read_struct('>I',self.data,o)
        self.optional_path_table_msb,   o = read_struct('>I',self.data,o)
        _offset = o
        self.root_dir_entry, o = read_struct('34s',self.data,o)

        self.root = ISOFile(self,_offset)
        self._cache = {}

    def get_file(self, path):
        if path == '/':
            return self.root
        else:
            if path in self._cache:
                return self._cache[path]
            units = path.split('/')
            units = units[1:] # remove root
            me = self.root
            for i in units:
                next_file = me.find(i)
                if not next_file:
                    me = None
                    break
                else:
                    me = next_file
            self._cache[path] = me
            return me

class ISOFile(object):

    def __init__(self, iso, offset):
        self.iso = iso
        self.offset = offset

        o = offset
        self.length,            o = read_struct('B', self.iso.data, o)
        if not self.length:
            return
        self.ext_length,        o = read_struct('B', self.iso.data, o)
        self.extent_start_lsb,  o = read_struct('<I',self.iso.data, o)
        self.extent_start_msb,  o = read_struct('>I',self.iso.data, o)
        self.extent_length_lsb, o = read_struct('<I',self.iso.data, o)
        self.extent_length_msb, o = read_struct('>I',self.iso.data, o)

        self.date_data, o = read_struct('7s', self.iso.data, o)

        self.flags, o = read_struct('b', self.iso.data, o)
        self.interleave_units, o = read_struct('b', self.iso.data, o)
        self.interleave_gap, o = read_struct('b', self.iso.data, o)

        self.volume_seq_lsb, o = read_struct('<H',self.iso.data, o)
        self.volume_seq_msb, o = read_struct('>H',self.iso.data, o)

        self.name_len, o = read_struct('b', self.iso.data, o)
        self.name, o = read_struct('{}s'.format(self.name_len), self.iso.data, o)
        self.name = self.name.decode('ascii')

    def write_extents(self):
        struct.pack_into('<I', self.iso.data, self.offset + 2, self.extent_start_lsb)
        struct.pack_into('>I', self.iso.data, self.offset + 6, self.extent_start_lsb)
        struct.pack_into('<I', self.iso.data, self.offset + 10, self.extent_length_lsb)
        struct.pack_into('>I', self.iso.data, self.offset + 14, self.extent_length_lsb)

    def readable_name(self):
        if not ';' in self.name:
            return self.name.lower()
        else:
            tmp, _ = self.name.split(';')
            return tmp.lower()


    def list(self):
        sectors = self.iso.data[self.extent_start_lsb * self.iso.sector_size: self.extent_start_lsb * self.iso.sector_size+ 3 * self.iso.sector_size]
        offset = 0

        while 1:
            f = ISOFile(self.iso, self.extent_start_lsb * self.iso.sector_size + offset)
            yield f
            offset += f.length
            if not f.length:
                break

    def find(self, name):
        sectors = self.iso.data[self.extent_start_lsb * self.iso.sector_size: self.extent_start_lsb * self.iso.sector_size+ 3 * self.iso.sector_size]
        offset = 0
        if '.' in name and len(name.split('.')[0]) > 8:
            a, b = name.split('.')
            name = a[:8] + '.' + b
        if '-' in name:
            name = name.replace('-','_')
        while 1:
            f = ISOFile(self.iso, self.extent_start_lsb * self.iso.sector_size + offset)
            if not f.length:
                if offset < self.extent_length_lsb:
                    offset += 1
                    continue
                else:
                    break
            if ';' in f.name:
                tmp, _ = f.name.split(';')
                if tmp.endswith('.'):
                    tmp = tmp[:-1]
                if tmp.lower() == name.lower():
                    return f
            elif f.name.lower() == name.lower():
                return f
            offset += f.length
        return None

class FAT(object):

    def __init__(self, iso, offset):
        self.iso = iso
        self.offset = offset

        self.bytespersector,    _ = read_struct('H', self.iso.data, offset + 11)
        self.sectorspercluster, _ = read_struct('B', self.iso.data, offset + 13)
        self.reservedsectors,   _ = read_struct('H', self.iso.data, offset + 14)
        self.numberoffats,      _ = read_struct('B', self.iso.data, offset + 16)
        self.numberofdirs,      _ = read_struct('H', self.iso.data, offset + 17)
        self.fatsize, _ = read_struct('H', self.iso.data, offset + 22)

        self.root_dir_sectors = (self.numberofdirs * 32 + (self.bytespersector - 1)) // self.bytespersector
        self.first_data_sector = self.reservedsectors + (self.numberoffats * self.fatsize) + self.root_dir_sectors
        self.root_sector= self.first_data_sector - self.root_dir_sectors
        self.root = FATDirectory(self, self.offset + self.root_sector * self.bytespersector)

    def get_offset(self, cluster):
        return self.offset + ((cluster - 2) * self.sectorspercluster + self.first_data_sector) * self.bytespersector

    def get_file(self, path):
        units = path.split('/')
        units = units[1:]

        me = self.root
        out = None
        for i in units:
            for fatfile in me.list():
                if fatfile.readable_name() == i:
                    me = fatfile.to_dir()
                    out = fatfile
                    break
            else:
                return None
        return out

class FATDirectory(object):

    def __init__(self, fat, offset):

        self.fat = fat
        self.offset = offset

    def list(self):

        o = self.offset
        while 1:
            out = FATFile(self.fat, o)
            if out.name != '\0\0\0\0\0\0\0\0':
                yield out
            else:
                break
            o += out.size


class FATFile(object):

    def __init__(self, fat, offset):

        self.fat = fat
        self.offset = offset
        self.magic_long = None
        self.size = 0
        self.long_name = ''

        o = self.offset
        self.actual_offset = o

        self.attrib,     _ = read_struct('B',self.fat.iso.data,o+11)

        while (self.attrib & 0x0F) == 0x0F:
            # Long file name entry
            tmp = read_struct('10s',self.fat.iso.data,o+1)[0]
            tmp += read_struct('12s',self.fat.iso.data,o+14)[0]
            tmp += read_struct('4s',self.fat.iso.data,o+28)[0]
            tmp = "".join([x for x in tmp[::2] if x != '\xFF']).strip('\x00')
            self.long_name = tmp + self.long_name
            self.size += 32
            o = self.offset + self.size
            self.actual_offset = o
            self.attrib,     _ = read_struct('B',self.fat.iso.data,o+11)

        o = self.offset + self.size

        self.name,       o = read_struct('8s',self.fat.iso.data,o)
        self.ext,        o = read_struct('3s',self.fat.iso.data,o)
        self.attrib,     o = read_struct('B',self.fat.iso.data,o)
        self.userattrib, o = read_struct('B',self.fat.iso.data,o)
        self.undelete,   o = read_struct('b',self.fat.iso.data,o)
        self.createtime, o = read_struct('H',self.fat.iso.data,o)
        self.createdate, o = read_struct('H',self.fat.iso.data,o)
        self.accessdate, o = read_struct('H',self.fat.iso.data,o)
        self.clusterhi,  o = read_struct('H',self.fat.iso.data,o)
        self.modifiedti, o = read_struct('H',self.fat.iso.data,o)
        self.modifiedda, o = read_struct('H',self.fat.iso.data,o)
        self.clusterlow, o = read_struct('H',self.fat.iso.data,o)
        self.filesize,   o = read_struct('I',self.fat.iso.data,o)

        self.name = self.name.decode('ascii')
        self.ext  = self.ext.decode('ascii')

        self.size += 32

        self.cluster = (self.clusterhi << 16) + self.clusterlow

    def is_dir(self):
        return bool(self.attrib & 0x10)

    def is_long(self):
        return bool((self.attrib & 0x0F) == 0x0F)

    def to_dir(self):
        return FATDirectory(self.fat, self.fat.get_offset(self.cluster))

    def get_offset(self):
        return self.fat.get_offset(self.cluster)

    def readable_name(self):
        if self.long_name:
            return self.long_name
        if self.ext.strip():
            return (self.name.strip() + '.' + self.ext.strip()).lower()
        else:
            return self.name.strip().lower()


image = ISO('image.iso')
fat = image.root.find('FAT.IMG')

fatfs = FAT(image, fat.extent_start_lsb * image.sector_size)

def process(fatfile, path):
    if fatfile.is_long():
        return
    if fatfile.readable_name() == '.':
        return
    if fatfile.readable_name() == '..':
        return
    if fatfile.is_dir():
        for i in fatfile.to_dir().list():
            process(i, path + fatfile.readable_name() + '/')
    else:
        cdfile = image.get_file(path + fatfile.readable_name())
        if not cdfile:
            if fatfile.readable_name() != 'bootia32.efi' and fatfile.readable_name() != 'bootx64.efi':
                print("Warning:", fatfile.readable_name(), "not found in ISO")
        else:
            cdfile.extent_start_lsb = fatfile.get_offset() // 2048
            cdfile.extent_length_lsb = fatfile.filesize
            cdfile.write_extents()


for i in fatfs.root.list():
    process(i,'/')

with open('image.iso','wb') as f:
    f.write(image.data)

