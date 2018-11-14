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

            # Kernel
            self.kernel_data = ArbitraryData(path='fatbase/kernel')
            self.kernel_data.sector_offset = self.allocate_space(self.kernel_data.size // 2048)
            self.kernel_entry = ISODirectoryEntry()
            self.kernel_entry.set_name('KERNEL.')
            self.kernel_entry.set_extent(self.kernel_data.sector_offset, self.kernel_data.actual_size)
            o = self.kernel_entry.write(self.root_data.data, o)

            # Ramdisk
            self.ramdisk_data = ArbitraryData(path='fatbase/ramdisk.img')
            self.ramdisk_data.sector_offset = self.allocate_space(self.ramdisk_data.size // 2048)
            self.ramdisk_entry = ISODirectoryEntry()
            self.ramdisk_entry.set_name('RAMDISK.IMG')
            self.ramdisk_entry.set_extent(self.ramdisk_data.sector_offset, self.ramdisk_data.actual_size)
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
            self.mods = []
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
                mod = ArbitraryData(path=mod_file)
                mod.sector_offset = self.allocate_space(mod.size // 2048)
                entry = ISODirectoryEntry()
                entry.set_name(mod_file.replace('fatbase/mod/','').upper())
                entry.set_extent(mod.sector_offset, mod.actual_size)
                o = entry.write(self.mods_data.data, o)
                self.mods.append(mod)

            # Set up the boot catalog and records
            self.el_torito_catalog.sector_offset = self.allocate_space(1)
            self.boot_record.set_catalog(self.el_torito_catalog.sector_offset)
            self.boot_payload = ArbitraryData(path='cdrom/boot.sys')
            self.boot_payload.sector_offset = self.allocate_space(self.boot_payload.size // 2048)
            self.el_torito_catalog.initial_entry.data['sector_count'] = self.boot_payload.size // 512
            self.el_torito_catalog.initial_entry.data['load_rba'] = self.boot_payload.sector_offset
            self.fat_payload = ArbitraryData(path='cdrom/fat.img')
            self.fat_payload.sector_offset = self.allocate_space(self.fat_payload.size // 2048)
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
            self.kernel_data.write(data, self.kernel_data.sector_offset * 2048)
            self.ramdisk_data.write(data, self.ramdisk_data.sector_offset * 2048)
            self.mods_data.write(data, self.mods_data.sector_offset * 2048)
            for mod in self.mods:
                mod.write(data, mod.sector_offset * 2048)
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

