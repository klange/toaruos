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

class ArbitraryData(object):

    def __init__(self, path):

        with open(path,'rb') as f:
            tmp = f.read()
            self.data = array.array('b',tmp)

        self.size = len(self.data)
        while (self.size % 2048):
            self.size += 1

    def write(self, data, offset):
        struct.pack_into(str(self.size) + 's', data, offset, self.data.tobytes())
        return offset + self.size

class ISO9660(object):

    def __init__(self, from_file=None):
        self.primary_volume_descriptor = ISOPrimaryVolumeDescriptor()
        self.boot_record = ISOElToritoBootRecord()
        self.volume_descriptor_set_terminator = ISOVolumeDescriptorSetTerminator()
        self.el_torito_catalog = ElToritoCatalog()

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
            self.boot_payload = ArbitraryData('cdrom/boot.sys')
            self.boot_record.set_catalog(0x13)
            self.el_torito_catalog.initial_entry.data['sector_count'] = 24
            self.el_torito_catalog.initial_entry.data['load_rba'] = 0x14

    def write(self, file_name):
        with open(file_name, 'wb') as f:
            data = array.array('b',b'\0'*(2048*0x14 + self.boot_payload.size))
            print(len(data))
            self.primary_volume_descriptor.write(data,0x10 * 2048)
            self.boot_record.write(data,0x11 * 2048)
            self.volume_descriptor_set_terminator.write(data,0x12 * 2048)
            self.el_torito_catalog.write(data,0x13 * 2048)
            self.boot_payload.write(data,0x14 * 2048)
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

