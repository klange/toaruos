/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms 
 * of the NCSA / University of Illinois License - see License.md
 * Copyright (C) 2017 Kevin Lange
 *
 * Module to mount and read+write Microsoft FAT Volumes
 * Note that this only supports FAT16 and FAT32. There is no real point in supporting FAT12 (for now at least).
 * File Author: Jesse Buhagiar
 */
#include <system.h>
#include <types.h>
#include <fs.h>
#include <fat.h>
#include <ata.h>
#include <logging.h>
#include <module.h>
#include <printf.h>

#define MBR_SECTOR_SIZE     512

#define FAT16_VOLUME        0x01
#define FAT32_VOLUME        0x02

#define FAT_ATTR_RO         0x01 /* This File entry is READ ONLY */
#define FAT_ATTR_HIDDEN     0x02 /* This File entry is HIDDEN and should not be displayed */
#define FAT_ATTR_SYS        0x04 /* This File entry belongs to the SYSTEM and must NOT be physically moved (i.e during DEFRAG) */
#define FAT_ATTR_VOLLABEL   0x08 /* This File entry is the Volume Label, and CANNOT be altered */
#define FAT_ATTR_SUBDIR     0x10 /* This File entry indicates that the cluster chain to be read is actually a directory. File Size is zero */
#define FAT_ATTR_ARCHIVE    0x20 /* For reset and backup software(??). Not important for us */
#define FAT_ATTR_DEVICE     0x40 /* This File entry is a character device (internally set for the device name) and MUST NOT be changed */
#define FAT_ATTR_RSVD       0x80 /* Reserved bit. MUST NOT be altered. */

#define SECTOR_SIZE         512  /* We always initially read 512 bytes */

/*
 * FAT file system data structure
 */
typedef struct {
    fat_bpb_t *     bpb;            /* Our FAT Volume BIOS Paramater Block */
    fs_node_t *     device;         /* Handle to the mount point */     
    uint16_t        block_size;     /* Size of one block for this device */
    uint8_t         fat_type;       /* What version of FAT is this volume (12, 16, or 32)? */
    uint8_t *       fat;            /* Our actual FAT Linked List (Cluster Chain). We translate to bytes by doing sectors_per_fat * bytes_per_sector */
    uint32_t        fat_sector;     /* Sector where our FAT is located */    

    uint32_t        offset;         /* The sector offset to our actual FAT volume */
    uint32_t        length;         /* The size of this volume in sectors */
    uint32_t        data_sector;    /* Sector where our data actually begins */
    char            volume_label[11];
} fat_fs_t;

static mbr_t        mbr;            /* msdos disk Master Boot Record */
static fat_fs_t *   fat_fs;         /* Our FAT File System handle */

static void read_sector(fat_fs_t * this, size_t sector, char * buffer) {
    read_fs(this->device, sector, SECTOR_SIZE, (uint8_t *)buffer);
}

static uint32_t read_fat(uint32_t cluster)
{
    uint32_t table_offset;  /* Offset into our FAT table */
    uint32_t sector;        /* LBA sector to read */

    if(fat_fs->fat_type == FAT32_VOLUME) {
        
    } else {
    
    }
}

static uint32_t read_fatfs(fat_fs_t * fat_fs) {
    

}

static fs_node_t * mount_volume(char * device, char * mount_path) {
    fs_node_t * dev = kopen(device, 0); /* Try to open a handle to the device with the Kernel */
    
    /* This device is unable to mounted (perhaps it already is?!) */
    if(!dev) {
        debug_print(ERROR, "failed to open device %s", device);
        return NULL;    
    }

    /* We assume that the whole of this disk/volume is formatted to FAT, so we mount the first partition we find */
    read_fs(dev, 0, SECTOR_SIZE, (uint8_t *)&mbr);

    debug_print(WARNING, "FATFS driver mounting FAT partition to %s", mount_path);    
    fat_fs = (fat_fs_t *)malloc(sizeof(fat_fs_t));
    fat_fs->device = dev;
    fat_fs->offset = mbr.partitions[0].lba_first_sector;
    fat_fs->length = mbr.partitions[0].sector_count;

    /* Now we read in the actual FAT data structures */
    fat_fs->bpb = (fat_bpb_t *)malloc(sizeof(fat_bpb_t));
    read_fs(dev, fat_fs->offset * SECTOR_SIZE, SECTOR_SIZE, (uint8_t *)fat_fs->bpb);

    fat_fs->fat_sector = fat_fs->bpb->num_rsvd_sectors;
    if(fat_fs->bpb->num_root_entries == 0) {
        fat_fs->fat_type = FAT32_VOLUME;
        fat_fs->data_sector = fat_fs->bpb->num_rsvd_sectors + (fat_fs->bpb->num_fats * fat_fs->bpb->ebr.ebr32.fat_size32);
        memcpy(fat_fs->volume_label, fat_fs->bpb->ebr.ebr32.volume_label, 11);
        debug_print(NOTICE, "mounted volume %s is formatted to FAT32", fat_fs->volume_label);
    } else {
        fat_fs->fat_type = FAT16_VOLUME;
        fat_fs->data_sector = fat_fs->bpb->num_rsvd_sectors + (fat_fs->bpb->num_fats * fat_fs->bpb->fat_size16);
        memcpy(fat_fs->volume_label, fat_fs->bpb->ebr.ebr16.volume_label, 11);
        debug_print(NOTICE, "mounted volume %s is formatted to FAT16", fat_fs->volume_label);
    }

        
    debug_print(NOTICE, "mounted volume %s successfully", fat_fs->volume_label);
    vfs_mount(fat_fs->volume_label, mount_path);
    return dev;
}

static int init(void) {
    // TODO: Pass the actual volume name to the vfs
    vfs_register("fat", mount_volume);
    return 0;
}

static int destroy(void) {
    return 0;
}


MODULE_DEF(fat, init, destroy);



