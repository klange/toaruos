/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2016-2018 K. Lange
 *
 * ISO 9660 filesystem driver (for CDs)
 */
#include <kernel/system.h>
#include <kernel/types.h>
#include <kernel/fs.h>
#include <kernel/logging.h>
#include <kernel/module.h>
#include <kernel/args.h>
#include <kernel/printf.h>
#include <kernel/tokenize.h>

#include <toaru/list.h>
#include <toaru/hashmap.h>

#define ISO_SECTOR_SIZE 2048

#define FLAG_HIDDEN      0x01
#define FLAG_DIRECTORY   0x02
#define FLAG_ASSOCIATED  0x04
#define FLAG_EXTENDED    0x08
#define FLAG_PERMISSIONS 0x10
#define FLAG_CONTINUES   0x80

typedef struct {
	fs_node_t * block_device;
	uint32_t block_size;
	hashmap_t * cache;
	list_t * lru;
} iso_9660_fs_t;

typedef struct {
	char year[4];
	char month[2];
	char day[2];
	char hour[2];
	char minute[2];
	char second[2];
	char hundredths[2];
	int8_t timezone;
} __attribute__((packed)) iso_9660_datetime_t;

typedef struct {
	uint8_t year;
	uint8_t month;
	uint8_t day;
	uint8_t hour;
	uint8_t minute;
	uint8_t second;
	int8_t timezone;
} __attribute__((packed)) iso_9660_rec_date_t;

typedef struct {
	uint8_t length;
	uint8_t ext_length;

	uint32_t extent_start_LSB;
	uint32_t extent_start_MSB;

	uint32_t extent_length_LSB;
	uint32_t extent_length_MSB;

	iso_9660_rec_date_t record_date;

	uint8_t flags;
	uint8_t interleave_units;
	uint8_t interleave_gap;

	uint16_t volume_seq_LSB;
	uint16_t volume_seq_MSB;

	uint8_t name_len;
	char name[];
} __attribute__((packed)) iso_9660_directory_entry_t;

typedef struct {
	uint8_t type; /* 0x01 */
	char id[5]; /* CD001 */

	uint8_t version;
	uint8_t _unused0;

	char system_id[32];
	char volume_id[32];

	uint8_t _unused1[8];

	uint32_t volume_space_LSB;
	uint32_t volume_space_MSB;

	uint8_t _unused2[32];

	uint16_t volume_set_LSB;
	uint16_t volume_set_MSB;

	uint16_t volume_seq_LSB;
	uint16_t volume_seq_MSB;

	uint16_t logical_block_size_LSB;
	uint16_t logical_block_size_MSB;

	uint32_t path_table_size_LSB;
	uint32_t path_table_size_MSB;

	uint32_t path_table_LSB;
	uint32_t optional_path_table_LSB;

	uint32_t path_table_MSB;
	uint32_t optional_path_table_MSB;

	/* iso_9660_directory_entry_t */
	char root[34];

	char volume_set_id[128];
	char volume_publisher[128];
	char data_preparer[128];
	char application_id[128];

	char copyright_file[38];
	char abstract_file[36];
	char bibliographic_file[37];

	iso_9660_datetime_t creation;
	iso_9660_datetime_t modification;
	iso_9660_datetime_t expiration;
	iso_9660_datetime_t effective;

	uint8_t file_structure_version;
	uint8_t _unused_3;

	char application_use[];
} __attribute__((packed)) iso_9660_volume_descriptor_t;

static void file_from_dir_entry(iso_9660_fs_t * this, size_t sector, iso_9660_directory_entry_t * dir, size_t offset, fs_node_t * fs);

#define CACHE_SIZE 64

static void read_sector(iso_9660_fs_t * this, uint32_t sector_id, char * buffer) {
	if (this->cache) {
		void * sector_id_v = (void *)sector_id;
		if (hashmap_has(this->cache, sector_id_v)) {
			memcpy(buffer,hashmap_get(this->cache, sector_id_v), this->block_size);

			node_t * me = list_find(this->lru, sector_id_v);
			list_delete(this->lru, me);
			list_append(this->lru, me);

		} else {
			if (this->lru->length > CACHE_SIZE) {
				node_t * l = list_dequeue(this->lru);
				free(hashmap_get(this->cache, l->value));
				hashmap_remove(this->cache, l->value);
				free(l);
			}
			read_fs(this->block_device, sector_id * this->block_size, this->block_size, (uint8_t *)buffer);
			char * buf = malloc(this->block_size);
			memcpy(buf, buffer, this->block_size);
			hashmap_set(this->cache, sector_id_v, buf);
			list_insert(this->lru, sector_id_v);
		}
	} else {
		read_fs(this->block_device, sector_id * this->block_size, this->block_size, (uint8_t *)buffer);
	}
}

static void inplace_lower(char * string) {
	while (*string) {
		if (*string >= 'A' && *string <= 'Z') {
			*string += ('a' - 'A');
		}
		string++;
	}
}

static void open_iso(fs_node_t *node, unsigned int flags) {
	/* Nothing to do here */
}

static void close_iso(fs_node_t *node) {
	/* Nothing to do here */
}

static struct dirent * readdir_iso(fs_node_t *node, uint32_t index) {
	if (index == 0) {
		struct dirent * out = malloc(sizeof(struct dirent));
		memset(out, 0x00, sizeof(struct dirent));
		out->ino = 0;
		strcpy(out->name, ".");
		return out;
	}

	if (index == 1) {
		struct dirent * out = malloc(sizeof(struct dirent));
		memset(out, 0x00, sizeof(struct dirent));
		out->ino = 0;
		strcpy(out->name, "..");
		return out;
	}

	iso_9660_fs_t * this = node->device;
	char * buffer = malloc(this->block_size);
	read_sector(this, node->inode, buffer);
	iso_9660_directory_entry_t * root_entry = (iso_9660_directory_entry_t *)(buffer + node->impl);

	debug_print(INFO, "[iso] Reading directory for readdir; sector = %d, offset = %d", node->inode, node->impl);

	uint8_t * root_data = malloc(root_entry->extent_length_LSB);
	uint8_t * offset = root_data;
	size_t sector_offset = 0;
	size_t length_to_read = root_entry->extent_length_LSB;
	while (length_to_read) {
		read_sector(this, root_entry->extent_start_LSB + sector_offset, (char*)offset);
		if (length_to_read >= this->block_size) {
			offset += this->block_size;
			sector_offset += 1;
			length_to_read -= this->block_size;
		} else {
			break;
		}
	}

	debug_print(INFO, "[iso] Done, want index = %d", index);

	/* Examine directory */
	offset = root_data;

	unsigned int i = 0;
	struct dirent *dirent = malloc(sizeof(struct dirent));
	fs_node_t * out = malloc(sizeof(fs_node_t));
	memset(dirent, 0, sizeof(struct dirent));
	while (1) {
		iso_9660_directory_entry_t * dir = (iso_9660_directory_entry_t *)offset;
		if (dir->length == 0) {
			debug_print(INFO, "dir->length = %d", dir->length);
			if ((size_t)(offset - root_data) < root_entry->extent_length_LSB) {
				offset += 1; // this->block_size - ((uintptr_t)offset % this->block_size);
				goto try_again;
			}
			break;
		}
		if (!(dir->flags & FLAG_HIDDEN)) {
			debug_print(INFO, "[iso] Found file %d", i);
			if (i == index) {
				file_from_dir_entry(this, (root_entry->extent_start_LSB)+(offset - root_data)/this->block_size, dir, (offset - root_data) % this->block_size, out);
				memcpy(&dirent->name, out->name, strlen(out->name)+1);
				dirent->ino = out->inode;
				goto cleanup;
			}
			i += 1;
		}
		offset += dir->length;
try_again:
		if ((size_t)(offset - root_data) > root_entry->extent_length_LSB) break;
	}

	debug_print(INFO, "offset = %x; root_data = %x; extent = %x", offset, root_data, root_entry->extent_length_LSB);

	free(dirent);
	dirent = NULL;

cleanup:
	free(root_data);
	free(buffer);
	free(out);
	return dirent;
}

static uint32_t read_iso(fs_node_t * node, uint64_t offset, uint32_t size, uint8_t * buffer) {
	iso_9660_fs_t * this = node->device;
	char * tmp = malloc(this->block_size);
	read_sector(this, node->inode, tmp);
	iso_9660_directory_entry_t * root_entry = (iso_9660_directory_entry_t *)(tmp + node->impl);

	uint32_t end;
	/* We can do this in a single underlying read to the filesystem */
	if (offset + size > root_entry->extent_length_LSB) {
		end = root_entry->extent_length_LSB;
	} else {
		end = offset + size;
	}
	uint32_t size_to_read = end - offset;

	read_fs(this->block_device, root_entry->extent_start_LSB * this->block_size + offset, size_to_read, (uint8_t *)buffer);

	free(tmp);
	return size_to_read;
}

static fs_node_t * finddir_iso(fs_node_t *node, char *name) {
	iso_9660_fs_t * this = node->device;
	char * buffer = malloc(this->block_size);
	read_sector(this, node->inode, buffer);
	iso_9660_directory_entry_t * root_entry = (iso_9660_directory_entry_t *)(buffer + node->impl);

	uint8_t * root_data = malloc(root_entry->extent_length_LSB);
	uint8_t * offset = root_data;
	size_t sector_offset = 0;
	size_t length_to_read = root_entry->extent_length_LSB;
	while (length_to_read) {
		read_sector(this, root_entry->extent_start_LSB + sector_offset, (char*)offset);
		if (length_to_read >= this->block_size) {
			offset += this->block_size;
			sector_offset += 1;
			length_to_read -= this->block_size;
		} else {
			break;
		}
	}

	/* Examine directory */
	offset = root_data;

	fs_node_t * out = malloc(sizeof(fs_node_t));
	while (1) {
		iso_9660_directory_entry_t * dir = (iso_9660_directory_entry_t *)offset;
		if (dir->length == 0) {
			if ((size_t)(offset - root_data) < root_entry->extent_length_LSB) {
				offset += 1; // this->block_size - ((uintptr_t)offset % this->block_size);
				goto try_next_finddir;
			}
			break;
		}
		if (!(dir->flags & FLAG_HIDDEN)) {
			memset(out, 0, sizeof(fs_node_t));
			file_from_dir_entry(this, (root_entry->extent_start_LSB)+(offset - root_data)/this->block_size, dir, (offset - root_data) % this->block_size, out);

			if (!strcmp(out->name, name)) {
				goto cleanup; /* found it */
			}

		}
		offset += dir->length;
try_next_finddir:
		if ((size_t)(offset - root_data) > root_entry->extent_length_LSB) break;
	}

	free(out);
	out = NULL;

cleanup:
	free(root_data);
	free(buffer);
	return out;
}

static void file_from_dir_entry(iso_9660_fs_t * this, size_t sector, iso_9660_directory_entry_t * dir, size_t offset, fs_node_t * fs) {
	fs->device = this;
	fs->inode  = sector; /* Sector the file is in */
	fs->impl   = offset; /* Offset */

	char * file_name = malloc(dir->name_len + 1);
	memcpy(file_name, dir->name, dir->name_len);
	file_name[dir->name_len] = 0;
	inplace_lower(file_name);
	char * dot = strchr(file_name, '.');
	if (!dot) {
		/* It's a directory. */
	} else {
		char * ext = dot + 1;
		char * semi = strchr(ext, ';');
		if (semi) {
			*semi = 0;
		}
		if (strlen(ext) == 0) {
			*dot = 0;
		} else {
			char * derp = ext;
			while (*derp == '.') derp++;
			if (derp != ext) {
				memmove(ext, derp, strlen(derp)+1);
			}
		}
	}
	memcpy(fs->name, file_name, strlen(file_name)+1);
	free(file_name);

	fs->uid = 0;
	fs->gid = 0;
	fs->length = dir->extent_length_LSB;
	fs->mask = 0555;
	fs->nlink = 0; /* Unsupported */
	if (dir->flags & FLAG_DIRECTORY) {
		fs->flags = FS_DIRECTORY;
		fs->readdir = readdir_iso;
		fs->finddir = finddir_iso;
	} else {
		fs->flags = FS_FILE;
		fs->read = read_iso;
	}
	/* Other things not supported */
	/* TODO actually get these from the CD into Unix time */
	fs->atime = now();
	fs->mtime = now();
	fs->ctime = now();

	fs->open = open_iso;
	fs->close = close_iso;
}

static fs_node_t * iso_fs_mount(char * device, char * mount_path) {
	char * arg = strdup(device);
	char * argv[10];
	int argc = tokenize(arg, ",", argv);

	fs_node_t * dev = kopen(argv[0], 0);
	if (!dev) {
		debug_print(ERROR, "failed to open %s", device);
		return NULL;
	}

	int cache = 1;

	for (int i = 1; i < argc; ++i) {
		if (!strcmp(argv[i],"nocache")) {
			cache = 0;
		} else {
			debug_print(WARNING, "Unrecognized option to iso driver: %s", argv[i]);
		}
	}

	if (!dev) {
		debug_print(ERROR, "failed to open %s", argv[0]);
		free(arg);
		return NULL;
	}

	iso_9660_fs_t * this = malloc(sizeof(iso_9660_fs_t));
	this->block_device = dev;
	this->block_size = ISO_SECTOR_SIZE;
	if (cache) {
		this->cache = hashmap_create_int(10);
		this->lru = list_create();
	} else {
		this->cache = NULL;
	}

	/* Probably want to put a block cache on this like EXT2 driver does; or do that in the ATAPI layer... */

	debug_print(WARNING, "ISO 9660 file system driver mounting %s to %s", device, mount_path);

	/* Read the volume descriptors */
	uint8_t * tmp = malloc(ISO_SECTOR_SIZE);
	int i = 0x10;
	int found = 0;
	while (1) {
		read_sector(this,i,(char*)tmp);
		if (tmp[0] == 0x00) {
			debug_print(WARNING, " Boot Record");
		} else if (tmp[0] == 0x01) {
			debug_print(WARNING, " Primary Volume Descriptor");
			found = 1;
			break;
		} else if (tmp[0] == 0x02) {
			debug_print(WARNING, " Secondary Volume Descriptor");
		} else if (tmp[0] == 0x03) {
			debug_print(WARNING, " Volume Partition Descriptor");
		}
		if (tmp[0] == 0xFF) break;
		i++;
	}

	if (!found) {
		debug_print(WARNING, "No primary volume descriptor?");
		free(arg);
		return NULL;
	}

	iso_9660_volume_descriptor_t * root = (iso_9660_volume_descriptor_t *)tmp;

	debug_print(WARNING, " Volume space:    %d", root->volume_space_LSB);
	debug_print(WARNING, " Volume set:      %d", root->volume_set_LSB);
	debug_print(WARNING, " Volume seq:      %d", root->volume_seq_LSB);
	debug_print(WARNING, " Block size:      %d", root->logical_block_size_LSB);
	debug_print(WARNING, " Path table size: %d", root->path_table_size_LSB);
	debug_print(WARNING, " Path table loc:  %d", root->path_table_LSB);

	iso_9660_directory_entry_t * root_entry = (iso_9660_directory_entry_t *)&root->root;

	debug_print(WARNING, "ISO root info:");
	debug_print(WARNING, " Entry len:  %d", root_entry->length);
	debug_print(WARNING, " File start: %d", root_entry->extent_start_LSB);
	debug_print(WARNING, " File len:   %d", root_entry->extent_length_LSB);
	debug_print(WARNING, " Is a directory: %s", (root_entry->flags & FLAG_DIRECTORY) ? "yes" : "no?");
	debug_print(WARNING, " Interleave units: %d", root_entry->interleave_units);
	debug_print(WARNING, " Interleave gap:   %d", root_entry->interleave_gap);
	debug_print(WARNING, " Volume Seq:       %d", root_entry->volume_seq_LSB);

	fs_node_t * fs = malloc(sizeof(fs_node_t));
	memset(fs, 0, sizeof(fs_node_t));
	file_from_dir_entry(this, i, root_entry, 156, fs);

	free(arg);
	return fs;
}

static int init(void) {
	vfs_register("iso", iso_fs_mount);
	return 0;
}

static int fini(void) {
	return 0;
}

MODULE_DEF(iso9660, init, fini);
