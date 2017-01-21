/* vim: tabstop=4 shiftwidth=4 noexpandtab
 */
#ifndef FAT_H_
#define FAT_H_

#ifdef _KERNEL_
#   include <types.h>
#else
#   ifdef BOOTLOADER
#       include <types.h>
#   else
#       include <stdint.h>
#   endif
#endif

/*
 * FAT12/16 EBR. This is read in right after fat_bpb_t for FAT12/16 volumes
 */
typedef struct {
    uint8_t     drive_number;       /* int13h drive number */
    uint8_t     reserved;           /* Reserved for use with Windows NT. Should always be 0 for us (when formatting) */
    uint8_t     boot_sig;           /* Extended Boot signature. Usually 0x29?? */
    uint32_t    volume_serial;      /* 32-bit serial number for this volume */
    char        volume_label[11];   /* 11-character volume label. Matches with the one in the root directory. Setting with no label is "NO NAME    " */
    char        volume_fs_type[8];  /* Either "FAT12   ", "FAT16   " or "FAT     ". Not reliable and NOT used by Microsoft */
    uint8_t     boot_code[448];     /* 448 bytes of boot code */
    uint16_t    signature;          /* Boot signature (0xAA55) */
} __attribute__((packed)) fat16_ebr_t;

/*
 * FAT32 EBR
 */
typedef struct {
    uint32_t    fat_size32;         /* For FAT32, how many sectors is occupied by ONE FAT? */
    uint16_t    flags;              /* Flags */
    uint16_t    version;            /* High byte is major, low byte is minor. Should be 0.0 or (some?) Windows will be angry! */
    uint32_t    root_cluster;       /* First cluster where the root directory resides. Usually 2. */
    uint16_t    fsinfo_sector;      /* Sector number that FSINFO resides */
    uint16_t    bk_boot_sector;     /* Indicates sector number of a copy of the boot sector */
    uint8_t     reserved1[12];      /* 12 reserved bytes for further expansion. Should be set to 0 on format */
    uint8_t     drive_number;       /* in13h drive number... But FAT32 this time :^) */
    uint8_t     reserved2;          /* Reserved for use with Windows NT */
    uint8_t     boot_sig;           /* Extended Boot signature. Usually 0x29?? */
    uint32_t    volume_serial;      /* 32-bit serial number for this volume */
    char        volume_label[11];   /* 11-character volume label. Matches with the one in the root directory. Setting with no label is "NO NAME    " */
    char        volume_fs_type[8];  /* Either "FAT32   ". Not reliable and NOT used by Microsoft */
    uint8_t     boot_code[420];     /* 420 bytes of boot code */
    uint16_t    signature;          /* Boot signature (0xAA55) */  
} __attribute__((packed)) fat32_ebr_t;

/*
 * FAT BIOS Paramater Block. This is ALWAYS the first sector of the volume.
 * It gives us some basic information about the file system and disk.
 */
typedef struct {
    uint8_t     jmp_code[3];        /* x86 Assembly Jump code. Tells the BIOS where to jump to (so we don't actually execute the BPB!) */
    char        oem[8];             /* 8-character OEM name. Tells us which tool formatted this disk. For us, it is TOARU1.0  */
    uint16_t    bytes_per_sector;   /* The number of bytes in a logical sector. Either 512, 1024, 2048, 4096 */
    uint8_t     sectors_per_clust;  /* How many sectors there are in one FAT cluster */
    uint16_t    num_rsvd_sectors;   /* Number of reserved sectors in the 'reserved section' of the volume starting at sector 1. */
    uint8_t     num_fats;           /* Number of FAT data structures there are on this volume. This should ALWAYS be 2 */
    uint16_t    num_root_entries;   /* Number of 32-byte entries contained in the root directory. 0 for FAT32 */
    uint16_t    total_sectors16;    /* 16-bit number of sectors for smaller volumes. If this is zero, total_sectors32 CANNOT be */
    uint8_t     media_type;         /* What type of volume is this? 0xF8 for fixed disk, 0xF0 for removable (USB, floppy etc) */
    uint16_t    fat_size16;         /* For FAT12/16, how many sectors occupied by ONE FAT. On FAT32, this is zero, and fat_size32 contains the value */
    uint16_t    sectors_per_track;  /* Number of sectors per track (for oldschool int13h) */
    uint16_t    num_heads;          /* Number of heads (for oldschool int13h) */
    uint32_t    hidden;             /* Number of hidden sectors in this volume */
    uint32_t    total_sectors32;    /* 32-bit number of sectors. */  
    union fat_ebr{
        fat16_ebr_t ebr16;
        fat32_ebr_t ebr32;
    } ebr;                
} __attribute__((packed)) fat_bpb_t;

/*
 * FAT32 FSINFO Structure. This is located in the sector as specified in fat32_ebr_t
 */
typedef struct {
    char        signature1[4];      /* This is ALWAYS 0x52 0x52 0x61 0x41 ("RRaA") */
    uint8_t     reserved1[480];     /* 480 bytes of reserved space. Set to 0x00 on format */
    char        signature2[4];      /* This is ALWAYS 0x72 0x72 0x41 0x61 ("rrAa") */
    uint32_t    free_clusters;      /* Last known number of free clusters on this volume. 0xFFFFFFFF is unknown, also after format */
    uint32_t    taken_clusters;     /* Last known number of used clusters on this volume. 0xFFFFFFF after format. This should start at 0x2 afterwards */
    uint8_t     reserved2[12];      /* Also set to 0x00 on format */
    uint32_t    fsinfo_signature;   /* ALWAYS 0x00 0x00 0x55 0xAA. Thsese should be checked to verify if this structure is actually valid */
} __attribute__((packed)) fsinfo_t;

/*
 * FAT 8.3 directory entry structure
 */
typedef struct {
    char        filename[8];        /* 8 Character name of this file. First byte can be 0x00 (Free Entry), 0xE5 (Erased Entry) or 0x2E (Dot Entry) */
    char        extension[3];       /* 3 Character extension */
    uint8_t     attributes;         /* File attributes. See /modules/fat.c for explanations */
    uint8_t     ntresvd;            /* Reserved by Windows NT. Set to 0x00 on file creation */
    uint8_t     millisecond_time;   /* Millisecond stamp of file creation. Contains a count of tenths of a second. Ranges from 0-199 inclusive */
    uint16_t    creation_time;      /* Time File was created */
    uint16_t    creation_date;      /* Date File was created */
    uint16_t    last_access;        /* Last access date */
    uint16_t    cluster_hi;         /* High word of this entry's first cluster number (ALWAYS 0 for FAT12/16) */
    uint16_t    write_time;         /* Time of last write */
    uint16_t    write_date;         /* Date of last write */
    uint16_t    cluster_lo;         /* Low word of this entry's first cluster number */
    uint32_t    filesize;           /* The size of this file in bytes */   
} __attribute__((packed)) dirent_t;






















#endif
