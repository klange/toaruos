#!/usr/bin/env python3
"""
    Tool for creating ISO 9660 CD images.
"""
import array
import struct

class Structure(object):

    assert_size = -1

    def __init__(self):
        self.data = {}
        for field in self.fields:
            if len(field) > 2:
                f, s, d = field
                self.data[s] = d
            else:
                f, s = field
                if f.endswith('s'):
                    self.data[s] = b""
                else:
                    self.data[s] = 0
        if self.assert_size != -1:
            assert(len(self) == self.assert_size)

    def __len__(self):
        return sum([struct.calcsize(f[0]) for f in self.fields])

    def read(self, data, offset):

        def read_struct(fmt,buf,offset):
            out, = struct.unpack_from(fmt,buf,offset)
            return out, offset + struct.calcsize(fmt)

        o = offset
        for field in self.fields:
            if len(field) > 2:
                f, s, _ = field
            else:
                f, s = field
            self.data[s], o = read_struct(f, data, o)
        return o

    def write(self, data, offset):

        def write_struct(fmt, buf, offset, value):
            struct.pack_into(fmt, buf, offset, value)
            return offset + struct.calcsize(fmt)

        o = offset
        for field in self.fields:
            if len(field) > 2:
                f, s, _ = field
            else:
                f, s = field
            o = write_struct(f,data,o,self.data[s])
        return o

def read_struct(fmt,buf,offset):
    out, = struct.unpack_from(fmt,buf,offset)
    return out, offset + struct.calcsize(fmt)

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
            tmp = "".join([chr(x) for x in tmp[::2] if x != '\xFF']).strip('\x00')
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

def make_time():
    data = array.array('b',b'\0'*17)
    struct.pack_into(
        '4s2s2s2s2s2s2sb',
        data, 0,
        b'2018', b'11', b'14', # Year, Month, Day
        b'12', b'00', b'00', # Hour, Minute, Second
        b'00', # Hundreths
        0, # Offset
    )
    return bytes(data)

def make_date():
    data = array.array('b',b'\0'*7)
    struct.pack_into(
        'BBBBBBb',
        data, 0,
        118, 11, 14,
        12, 0, 0,
        0,
    )
    return bytes(data)

class ISOBootRecord(Structure):
    assert_size = 2048
    fields = (
        ('B', 'type_code', 0),
        ('5s', 'cd001', b'CD001'),
        ('B', 'version', 1),
        ('32s', 'boot_system_identifier'),
        ('32s', 'boot_identifier'),
        ('1977s', 'boot_record_data'),
    )

class ISOElToritoBootRecord(ISOBootRecord):
    assert_size = 2048
    fields = (
        ('B', 'type_code', 0),
        ('5s', 'cd001', b'CD001'),
        ('B', 'version', 1),
        ('32s', 'boot_system_identifier',b'EL TORITO SPECIFICATION'),
        ('32s', 'boot_identifier'),
        ('<I', 'catalog_lba'),
        ('1973s', 'boot_record_data'),
    )

    def set_catalog(self, catalog_lba):
        self.data['catalog_lba'] = catalog_lba

class ISOPrimaryVolumeDescriptor(Structure):
    assert_size = 2048
    fields = (
        ('B', 'type_code', 1),
        ('5s', 'cd001', b'CD001'),
        ('B', 'version', 1),
        ('B', 'unused_0', 0),
        ('32s', 'system_id', b' '*32),
        ('32s', 'volume_id', b'ToaruOS Boot CD'.ljust(32)),
        ('8s', 'unused_1', b'\0'*8),
        ('<I', 'volume_space_lsb'),
        ('>I', 'volume_space_msb'),
        ('32s', 'unused_2', b'\0'*32),
        ('<H', 'volume_set_size_lsb', 1),
        ('>H', 'volume_set_size_msb', 1),
        ('<H', 'volume_sequence_lsb', 1),
        ('>H', 'volume_sequence_msb', 1),
        ('<H', 'logical_block_size_lsb', 2048),
        ('>H', 'logical_block_size_msb', 2048),
        ('<I', 'path_table_size_lsb'),
        ('>I', 'path_table_size_msb'),
        ('<I', 'type_l_table_lsb'),
        ('<I', 'optional_type_l_table_lsb'),
        ('>I', 'type_m_table_msb'),
        ('>I', 'optional_type_m_table_msb'),
        ('34s', 'root_entry_data'),
        ('128s', 'volume_set_identifier', b' '*128),
        ('128s', 'publisher_identifier', b' '*128),
        ('128s', 'data_preparer_identifier', b' '*128),
        ('128s', 'application_identifier',b' '*128),
        ('38s', 'copyright_file_identifier',b' '*38),
        ('36s', 'abstract_file_identifier',b' '*36),
        ('37s', 'bibliographic_file_identifier',b' '*37),
        ('17s', 'volume_creation_time',make_time()),
        ('17s', 'volume_modification_time',make_time()),
        ('17s', 'volume_expiration_time',make_time()),
        ('17s', 'volume_effective_time',make_time()),
        ('B', 'file_structure_version'),
        ('B', 'unused_3', 0),
        ('512s', 'application_data'),
        ('653s', 'reserved', b'\0'*653),
    )

class ISOVolumeDescriptorSetTerminator(Structure):
    assert_size = 2048
    fields = (
        ('B', 'type_code', 0xFF),
        ('5s', 'cd001', b'CD001'),
        ('B', 'version', 1),
        ('2041s', 'unused', b'\0'*2041)
    )

class ISODirectoryEntry(Structure):
    assert_size = 33
    fields = (
        ('B', 'length'),
        ('B', 'ext_length'),
        ('<I', 'extent_start_lsb'),
        ('>I', 'extent_start_msb'),
        ('<I', 'extent_length_lsb'),
        ('>I', 'extent_length_msb'),
        ('7s', 'record_date', make_date()),
        ('B', 'flags'),
        ('B', 'interleave_units'),
        ('B', 'interleave_gap'),
        ('<H', 'volume_seq_lsb'),
        ('>H', 'volume_seq_msb'),
        ('B', 'name_len'),
    )

    def set_name(self, name):
        self.data['name_len'] = len(name)
        self.name = name
        self.data['length'] = self.assert_size + len(self.name)
        if self.data['length'] % 2:
            self.data['length'] += 1

    def set_extent(self, start, length):
        self.data['extent_start_lsb'] = start
        self.data['extent_start_msb'] = start
        self.data['extent_length_lsb'] = length
        self.data['extent_length_msb'] = length

    def write(self, data, offset):
        o = super(ISODirectoryEntry,self).write(data,offset)
        struct.pack_into(str(len(self.name))+'s', data, o, self.name.encode('utf-8'))
        return offset + self.data['length']

class ArbitraryData(object):

    def __init__(self, path=None, size=None):

        if path:
            with open(path,'rb') as f:
                tmp = f.read()
                self.data = array.array('b',tmp)
        elif size:
            self.data = array.array('b',b'\0'*size)
        else:
            raise ValueError("Expected one of path or size to be set.")

        self.size = len(self.data.tobytes())
        self.actual_size = self.size
        while (self.size % 2048):
            self.size += 1

    def write(self, data, offset):
        struct.pack_into(str(self.size) + 's', data, offset, self.data.tobytes())
        return offset + self.size

def make_entry():
    return b'\0'*34

class ISO9660(object):

    def __init__(self, from_file=None):
        self.primary_volume_descriptor = ISOPrimaryVolumeDescriptor()
        self.boot_record = ISOElToritoBootRecord()
        self.volume_descriptor_set_terminator = ISOVolumeDescriptorSetTerminator()
        self.el_torito_catalog = ElToritoCatalog()
        self.allocate = 0x13

        if from_file:
            # Only for a file we produced.
            with open(from_file, 'rb') as f:
                tmp = f.read()
                data = array.array('b', tmp)
            self.primary_volume_descriptor.read(data, 0x10 * 2048)
            self.boot_record.read(data, 0x11 * 2048)
            self.volume_descriptor_set_terminator.read(data, 0x12 * 2048)
            self.el_torito_catalog.read(data, self.boot_record.data['catalog_lba'] * 2048)
        else:
            # Root directory
            self.root = ISODirectoryEntry()
            self.root.data['flags'] = 0x02 # Directory
            self.root.set_name(' ')
            self.root_data = ArbitraryData(size=2048)
            self.root_data.sector_offset = self.allocate_space(1)
            self.root.set_extent(self.root_data.sector_offset,self.root_data.size)

            # Dummy entries
            t = ISODirectoryEntry()
            t.set_name('')
            o = t.write(self.root_data.data, 0)
            t = ISODirectoryEntry()
            t.set_name('\1')
            o = t.write(self.root_data.data, o)

            # Fat data
            self.fat_payload = ArbitraryData(path='cdrom/fat.img')
            self.fat_payload.sector_offset = self.allocate_space(self.fat_payload.size // 2048)
            self.fat = FAT(self.fat_payload, 0)
            self.fat_entry = ISODirectoryEntry()
            self.fat_entry.set_name('FAT.IMG')
            self.fat_entry.set_extent(self.fat_payload.sector_offset, self.fat_payload.actual_size)
            o = self.fat_entry.write(self.root_data.data, o)

            # Kernel
            self.kernel_entry = ISODirectoryEntry()
            self.kernel_entry.set_name('KERNEL.')
            f = self.fat.get_file('/kernel')
            self.kernel_entry.set_extent(f.get_offset() // 2048 + self.fat_payload.sector_offset, f.filesize)
            o = self.kernel_entry.write(self.root_data.data, o)

            # Ramdisk
            self.ramdisk_entry = ISODirectoryEntry()
            self.ramdisk_entry.set_name('RAMDISK.IMG')
            f = self.fat.get_file('/ramdisk.img')
            self.ramdisk_entry.set_extent(f.get_offset() // 2048 + self.fat_payload.sector_offset, f.filesize)
            o = self.ramdisk_entry.write(self.root_data.data, o)

            # Modules directory
            self.mods_data = ArbitraryData(size=(2048*2)) # Just in case
            self.mods_data.sector_offset = self.allocate_space(self.mods_data.size // 2048)
            self.mods_entry = ISODirectoryEntry()
            self.mods_entry.data['flags'] = 0x02
            self.mods_entry.set_name('MOD')
            self.mods_entry.set_extent(self.mods_data.sector_offset, self.mods_data.actual_size)
            o = self.mods_entry.write(self.root_data.data, o)

            # Modules themselves
            t = ISODirectoryEntry()
            t.set_name('')
            o = t.write(self.mods_data.data, 0)
            t = ISODirectoryEntry()
            t.set_name('\1')
            o = t.write(self.mods_data.data, o)
            for mod_file in [
                'fatbase/mod/ac97.ko',
                'fatbase/mod/ata.ko',
                'fatbase/mod/ataold.ko',
                'fatbase/mod/debug_sh.ko',
                'fatbase/mod/dospart.ko',
                'fatbase/mod/e1000.ko',
                'fatbase/mod/ext2.ko',
                'fatbase/mod/hda.ko',
                'fatbase/mod/iso9660.ko',
                'fatbase/mod/lfbvideo.ko',
                'fatbase/mod/net.ko',
                'fatbase/mod/packetfs.ko',
                'fatbase/mod/pcnet.ko',
                'fatbase/mod/pcspkr.ko',
                'fatbase/mod/portio.ko',
                'fatbase/mod/procfs.ko',
                'fatbase/mod/ps2kbd.ko',
                'fatbase/mod/ps2mouse.ko',
                'fatbase/mod/random.ko',
                'fatbase/mod/rtl.ko',
                'fatbase/mod/serial.ko',
                'fatbase/mod/snd.ko',
                'fatbase/mod/tarfs.ko',
                'fatbase/mod/tmpfs.ko',
                'fatbase/mod/usbuhci.ko',
                'fatbase/mod/vbox.ko',
                'fatbase/mod/vgadbg.ko',
                'fatbase/mod/vgalog.ko',
                'fatbase/mod/vidset.ko',
                'fatbase/mod/vmware.ko',
                'fatbase/mod/xtest.ko',
                'fatbase/mod/zero.ko',
            ]:
                entry = ISODirectoryEntry()
                entry.set_name(mod_file.replace('fatbase/mod/','').upper())
                f = self.fat.get_file('/'+mod_file.replace('fatbase/',''))
                entry.set_extent(f.get_offset() // 2048 + self.fat_payload.sector_offset, f.filesize)
                o = entry.write(self.mods_data.data, o)

            # Set up the boot catalog and records
            self.el_torito_catalog.sector_offset = self.allocate_space(1)
            self.boot_record.set_catalog(self.el_torito_catalog.sector_offset)
            self.boot_payload = ArbitraryData(path='cdrom/boot.sys')
            self.boot_payload.sector_offset = self.allocate_space(self.boot_payload.size // 2048)
            self.el_torito_catalog.initial_entry.data['sector_count'] = self.boot_payload.size // 512
            self.el_torito_catalog.initial_entry.data['load_rba'] = self.boot_payload.sector_offset
            self.el_torito_catalog.section.data['sector_count'] = 0 # Expected to be 0 or 1 for "until end of CD"
            self.el_torito_catalog.section.data['load_rba'] = self.fat_payload.sector_offset
            self.primary_volume_descriptor.data['root_entry_data'] = make_entry()

    def allocate_space(self, sectors):
        out = self.allocate
        self.allocate += sectors
        return out

    def write(self, file_name):
        with open(file_name, 'wb') as f:
            data = array.array('b',b'\0'*(2048*self.allocate))
            self.primary_volume_descriptor.write(data,0x10 * 2048)
            self.root.write(data,0x10*2048 + 156)
            self.boot_record.write(data,0x11 * 2048)
            self.mods_data.write(data, self.mods_data.sector_offset * 2048)
            self.root_data.write(data,self.root_data.sector_offset * 2048)
            self.volume_descriptor_set_terminator.write(data,0x12 * 2048)
            self.el_torito_catalog.write(data,self.el_torito_catalog.sector_offset * 2048)
            self.boot_payload.write(data,self.boot_payload.sector_offset * 2048)
            self.fat_payload.write(data,self.fat_payload.sector_offset * 2048)
            data.tofile(f)

class ElToritoValidationEntry(Structure):
    assert_size = 0x20
    fields = (
        ('B','header_id',1),
        ('B','platform_id',0),
        ('<H','reserved_0'),
        ('24s','id_str',b'\0'*24),
        ('<H','checksum',0x55aa),
        ('B','key_55',0x55),
        ('B','key_aa',0xaa),
    )

class ElToritoInitialEntry(Structure):
    assert_size = 0x20
    fields = (
        ('B','bootable',0x88),
        ('B','media_type'),
        ('<H','load_segment'),
        ('B','system_type'),
        ('B','unused_0'),
        ('<H','sector_count'),
        ('<I','load_rba'),
        ('20s','unused_1',b'\0'*20),
    )

class ElToritoSectionHeader(Structure):
    assert_size = 0x20
    fields = (
        ('B','header_id',0x91),
        ('B','platform_id',0xEF),
        ('<H','sections',1),
        ('28s','id_str',b'\0'*28)
    )

class ElToritoSectionEntry(Structure):
    assert_size = 0x20
    fields = (
        ('B','bootable',0x88),
        ('B','media_type'),
        ('<H','load_segment'),
        ('B','system_type'),
        ('B','unused_0'),
        ('<H','sector_count'),
        ('<I','load_rba'),
        ('B','selection_criteria'),
        ('19s','vendor'),
    )

class ElToritoCatalog(object):

    def __init__(self):
        self.validation_entry = ElToritoValidationEntry()
        self.initial_entry = ElToritoInitialEntry()
        self.section_header = ElToritoSectionHeader()
        self.section = ElToritoSectionEntry()

    def read(self, data, offset):
        o = offset
        o = self.validation_entry.read(data, o)
        o = self.initial_entry.read(data, o)
        o = self.section_header.read(data, o)
        o = self.section.read(data, o)

    def write(self, data, offset):
        o = offset
        o = self.validation_entry.write(data, o)
        o = self.initial_entry.write(data, o)
        o = self.section_header.write(data, o)
        o = self.section.write(data, o)


iso = ISO9660()
#print(iso.el_torito_catalog.validation_entry.data)
#print(iso.el_torito_catalog.initial_entry.data)
#print(iso.el_torito_catalog.section_header.data)
#print(iso.el_torito_catalog.section.data)
iso.write('test.iso')

