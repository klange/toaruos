/**
 * @file  kernel/vfs/tarfs.c
 * @brief Read-only filesystem driver for ustar archives.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 */
#include <bits/errno.h>
#include <kernel/types.h>
#include <kernel/vfs.h>
#include <kernel/printf.h>
#include <kernel/tokenize.h>

#include <kernel/list.h>
#include <kernel/hashmap.h>

#define TARFS_LOG_LEVEL WARNING

struct tarfs {
	fs_node_t * device;
	unsigned int length;
};

struct ustar {
	char filename[100];
	char mode[8];
	char ownerid[8];
	char groupid[8];

	char size[12];
	char mtime[12];

	char checksum[8];
	char type[1];
	char link[100];

	char ustar[6];
	char version[2];

	char owner[32];
	char group[32];

	char dev_major[8];
	char dev_minor[8];

	char prefix[155];
};

static unsigned int interpret_uid(struct ustar * file) {
	return 
		((file->ownerid[0] - '0') << 18) |
		((file->ownerid[1] - '0') << 15) |
		((file->ownerid[2] - '0') << 12) |
		((file->ownerid[3] - '0') <<  9) |
		((file->ownerid[4] - '0') <<  6) |
		((file->ownerid[5] - '0') <<  3) |
		((file->ownerid[6] - '0') <<  0);
}

static unsigned int interpret_gid(struct ustar * file) {
	return 
		((file->groupid[0] - '0') << 18) |
		((file->groupid[1] - '0') << 15) |
		((file->groupid[2] - '0') << 12) |
		((file->groupid[3] - '0') <<  9) |
		((file->groupid[4] - '0') <<  6) |
		((file->groupid[5] - '0') <<  3) |
		((file->groupid[6] - '0') <<  0);
}

static unsigned int interpret_mode(struct ustar * file) {
	return 
		((file->mode[0] - '0') << 18) |
		((file->mode[1] - '0') << 15) |
		((file->mode[2] - '0') << 12) |
		((file->mode[3] - '0') <<  9) |
		((file->mode[4] - '0') <<  6) |
		((file->mode[5] - '0') <<  3) |
		((file->mode[6] - '0') <<  0);
}

static unsigned int interpret_size(struct ustar * file) {
	return
		((file->size[ 0] - '0') << 30) |
		((file->size[ 1] - '0') << 27) |
		((file->size[ 2] - '0') << 24) |
		((file->size[ 3] - '0') << 21) |
		((file->size[ 4] - '0') << 18) |
		((file->size[ 5] - '0') << 15) |
		((file->size[ 6] - '0') << 12) |
		((file->size[ 7] - '0') <<  9) |
		((file->size[ 8] - '0') <<  6) |
		((file->size[ 9] - '0') <<  3) |
		((file->size[10] - '0') <<  0);
}

static unsigned int round_to_512(unsigned int i) {
	unsigned int t = i % 512;

	if (!t) return i;
	return i + (512 - t);
}

static int ustar_from_offset(struct tarfs * self, unsigned int offset, struct ustar * out);
static fs_node_t * file_from_ustar(struct tarfs * self, struct ustar * file, unsigned int offset);

#ifndef strncat
/**
 * @brief Copy up to n characters from src to the end of dest.
 *
 * Since tar filename and prefix fields are not necessarily NUL-terminated,
 * we need to ensure we only copy up to the relevant field size from each
 * of those fields. If you tried to naively treat those fields as regular
 * strings, you could end up including the octal file mode after the
 * filename, or arbtirary header extension data after the prefix, if a
 * sufficient long file name is included in the archive. If you actually
 * want to replace this with an sprintf-family function, be sure to use %.*s
 * formatters for these fields so you can supply their lengths.
 */
static char * strncat(char *dest, const char *src, size_t n) {
	char * end = dest;
	while (*end != '\0') {
		++end;
	}
	size_t i = 0;
	while (*src && i < n) {
		*end = *src;
		end++;
		src++;
		i++;
	}
	*end = '\0';
	return dest;
}
#endif

static int count_slashes(char * string) {
	int i = 0;
	char * s = strstr(string, "/");
	while (s) {
		if (*(s+1) == '\0') return i;
		i++;
		s = strstr(s+1,"/");
	}
	return i;
}

static int readdir_tar_root(fs_node_t *node, unsigned long index, struct dirent * out) {
	if (index == 0) {
		memset(out, 0x00, sizeof(struct dirent));
		out->d_ino = 0;
		strcpy(out->d_name, ".");
		return 1;
	}

	if (index == 1) {
		memset(out, 0x00, sizeof(struct dirent));
		out->d_ino = 0;
		strcpy(out->d_name, "..");
		return 1;
	}

	index -= 2;

	struct tarfs * self = node->device;
	/* Go through each file and pick the ones are at the root */
	/* Root files will have no /, so this is easy */
	unsigned int offset = 0;
	struct ustar * file = malloc(sizeof(struct ustar));
	while (offset < self->length) {
		int status = ustar_from_offset(self, offset, file);

		if (!status) {
			free(file);
			return 0;
		}

		char filename_workspace[256];

		memset(filename_workspace, 0, 256);
		strncat(filename_workspace, file->prefix, 155);
		strncat(filename_workspace, file->filename, 100);

		if (!count_slashes(filename_workspace)) {
			char * slash = strstr(filename_workspace,"/");
			if (slash) *slash = '\0'; /* remove trailing slash */
			if (strlen(filename_workspace)) {
				if (index == 0) {
					memset(out, 0x00, sizeof(struct dirent));
					out->d_ino = offset;
					strcpy(out->d_name, filename_workspace);
					free(file);
					return 1;
				} else {
					index--;
				}
			}
		}

		offset += 512;
		offset += round_to_512(interpret_size(file));

	}

	free(file);
	return 0;
}

static ssize_t read_tarfs(fs_node_t * node, off_t offset, size_t size, uint8_t * buffer) {
	struct tarfs * self = node->device;
	struct ustar * file = malloc(sizeof(struct ustar));
	ustar_from_offset(self, node->inode, file);
	size_t file_size = interpret_size(file);

	if ((size_t)offset > file_size) return 0;
	if (offset + size > file_size) {
		size = file_size - offset;
	}

	free(file);

	return read_fs(self->device, offset + node->inode + 512, size, buffer);
}

static int readdir_tarfs(fs_node_t *node, unsigned long index, struct dirent *out) {
	if (index == 0) {
		memset(out, 0x00, sizeof(struct dirent));
		out->d_ino = 0;
		strcpy(out->d_name, ".");
		return 1;
	}

	if (index == 1) {
		memset(out, 0x00, sizeof(struct dirent));
		out->d_ino = 0;
		strcpy(out->d_name, "..");
		return 1;
	}

	index -= 2;

	struct tarfs * self = node->device;

	/* Go through each file and pick the ones are at the root */
	/* Root files will have no /, so this is easy */
	unsigned int offset = node->inode;

	/* Read myself */
	struct ustar * file = malloc(sizeof(struct ustar));
	int status = ustar_from_offset(self, node->inode, file);
	char my_filename[256];

	/* Figure out my own filename, with forward slash */
	memset(my_filename, 0, 256);
	strncat(my_filename, file->prefix, 155);
	strncat(my_filename, file->filename, 100);

	while (offset < self->length) {
		ustar_from_offset(self, offset, file);

		if (!status) {
			free(file);
			return 0;
		}

		char filename_workspace[256];
		memset(filename_workspace, 0, 256);
		strncat(filename_workspace, file->prefix, 155);
		strncat(filename_workspace, file->filename, 100);

		if (startswith(filename_workspace, my_filename)) {
			if (!count_slashes(filename_workspace + strlen(my_filename))) {
				if (strlen(filename_workspace + strlen(my_filename))) {
					if (index == 0) {
						char * slash = strstr(filename_workspace+strlen(my_filename),"/");
						if (slash) *slash = '\0'; /* remove trailing slash */
						memset(out, 0x00, sizeof(struct dirent));
						out->d_ino = offset;
						strcpy(out->d_name, filename_workspace+strlen(my_filename));
						free(file);
						return 1;
					} else {
						index--;
					}
				}
			}
		}

		offset += 512;
		offset += round_to_512(interpret_size(file));
	}

	free(file);
	return 0;
}

static fs_node_t * finddir_tarfs(fs_node_t *node, const char *name) {
	struct tarfs * self = node->device;

	/* find my own filename */
	struct ustar * file = malloc(sizeof(struct ustar));
	ustar_from_offset(self, node->inode, file);

	char my_filename[256];
	/* Figure out my own filename, with forward slash */
	memset(my_filename, 0, 256);
	strncat(my_filename, file->prefix, 155);
	strncat(my_filename, file->filename, 100);

	/* This tarfs driver only supports file names up to 255 characters,
	 * as it doesn't parse long name entries, so any attempt to
	 * find a file that would be longer than that is going to fail. */
	if (strlen(my_filename) + strlen(name) > 255) {
		free(file);
		return NULL;
	}

	/* Append name */
	strncat(my_filename, name, strlen(name));

	unsigned int offset = node->inode;
	while (offset < self->length) {
		int status = ustar_from_offset(self, offset, file);

		if (!status) {
			free(file);
			return NULL;
		}

		char filename_workspace[256];
		memset(filename_workspace, 0, 256);
		strncat(filename_workspace, file->prefix, 155);
		strncat(filename_workspace, file->filename, 100);

		if (filename_workspace[strlen(filename_workspace)-1] == '/') {
			filename_workspace[strlen(filename_workspace)-1] = '\0';
		}
		if (!strcmp(filename_workspace, my_filename)) {
			return file_from_ustar(self, file, offset);
		}

		offset += 512;
		offset += round_to_512(interpret_size(file));
	}


	free(file);
	return NULL;
}

static ssize_t readlink_tarfs(fs_node_t * node, char * buf, size_t size) {
	struct tarfs * self = node->device;
	struct ustar * file = malloc(sizeof(struct ustar));
	ustar_from_offset(self, node->inode, file);

	size_t len = strlen(file->link);
	if (size < len) len = size;
	memcpy(buf, file->link, len);
	return len;
}

static fs_node_t * file_from_ustar(struct tarfs * self, struct ustar * file, unsigned int offset) {
	fs_node_t * fs = malloc(sizeof(fs_node_t));
	memset(fs, 0, sizeof(fs_node_t));
	fs->device = self;
	fs->inode  = offset;
	fs->impl   = 0;
	char filename_workspace[256];
	memcpy(fs->name, filename_workspace, strlen(filename_workspace)+1);

	fs->uid = interpret_uid(file);
	fs->gid = interpret_gid(file);
	fs->length = interpret_size(file);
	fs->mask = interpret_mode(file);
	fs->nlink = 0; /* Unsupported */
	fs->flags = FS_FILE;
	if (file->type[0] == '5') {
		fs->flags = FS_DIRECTORY;
		fs->readdir = readdir_tarfs;
		fs->finddir = finddir_tarfs;
		fs->create  = NULL;
	} else if (file->type[0] == '1') {
		//debug_print(ERROR, "Hardlink detected");
		/* go through file and find target, reassign inode to point to that */
	} else if (file->type[0] == '2') {
		fs->flags = FS_SYMLINK;
		fs->readlink = readlink_tarfs;
	} else {
		fs->flags = FS_FILE;
		fs->read = read_tarfs;
	}
	free(file);
#if 0
	/* TODO times are also available from the file */
	fs->atime = now();
	fs->mtime = now();
	fs->ctime = now();
#endif
	return fs;
}

static fs_node_t * finddir_tar_root(fs_node_t *node, const char *name) {
	struct tarfs * self = node->device;

	unsigned int offset = 0;
	struct ustar * file = malloc(sizeof(struct ustar));
	while (offset < self->length) {
		int status = ustar_from_offset(self, offset, file);

		if (!status) {
			free(file);
			return NULL;
		}

		char filename_workspace[256];
		memset(filename_workspace, 0, 256);
		strncat(filename_workspace, file->prefix, 155);
		strncat(filename_workspace, file->filename, 100);

		if (count_slashes(filename_workspace)) {
			/* skip */
		} else {
			char * slash = strstr(filename_workspace,"/");
			if (slash) *slash = '\0';
			if (!strcmp(filename_workspace, name)) {
				return file_from_ustar(self, file, offset);
			}
		}

		offset += 512;
		offset += round_to_512(interpret_size(file));
	}

	free(file);
	return NULL;
}

static int ustar_from_offset(struct tarfs * self, unsigned int offset, struct ustar * out) {
	read_fs(self->device, offset, sizeof(struct ustar), (unsigned char*)out);
	if (out->ustar[0] != 'u' ||
		out->ustar[1] != 's' ||
		out->ustar[2] != 't' ||
		out->ustar[3] != 'a' ||
		out->ustar[4] != 'r') {
		return 0;
	}
	return 1;
}

static fs_node_t * tar_mount(const char * device, const char * mount_path) {
	char * arg = strdup(device);
	char * argv[10];
	int argc = tokenize(arg, ",", argv);

	if (argc > 1) {
		//debug_print(WARNING, "tarfs driver takes no options");
		printf("tarfs got unexpected mount arguments: %s\n", device);
	}

	fs_node_t * dev = kopen(argv[0], 0);
	free(arg); /* Shouldn't need the filename or args anymore */

	if (!dev) {
		//debug_print(ERROR, "failed to open %s", device);
		printf("tarfs could not open target device\n");
		return NULL;
	}

	/* Create a metadata struct for this mount */
	struct tarfs * self = malloc(sizeof(struct tarfs));

	self->device = dev;
	self->length = dev->length;

	fs_node_t * root = malloc(sizeof(fs_node_t));
	memset(root, 0, sizeof(fs_node_t));

	root->uid     = 0;
	root->gid     = 0;
	root->length  = 0;
	root->mask    = 0555;
	root->readdir = readdir_tar_root;
	root->finddir = finddir_tar_root;
	root->create  = NULL;
	root->flags   = FS_DIRECTORY;
	root->device  = self;

	return root;
}

int tarfs_register_init(void) {
	vfs_register("tar", tar_mount);
	return 0;
}

/**
 * Unpack a tarfile directly as if it were the root filesystem.
 *
 * This is an in-kernel implementation of the former 'migrate'
 * userspace utility; since it runs before 'init', it avoids
 * issues with freeing the ramdisk.
 */
int tarfs_unpack(char * from_file) {
	int error = 0;
	fs_node_t * dev = kopen_error(from_file, 0, &error);
	if (!dev) {
		dprintf("migrate: could not open '%s'\n", from_file);
		return error;
	}

	struct tarfs * self = calloc(1, sizeof(struct tarfs));
	self->device = dev;
	self->length = dev->length;

	struct ustar * file = calloc(1, sizeof(struct ustar));
	off_t offset = 0;
	while (offset < self->length) {
		int status = ustar_from_offset(self, offset, file);
		if (!status) break;

		size_t file_size = interpret_size(file);

		if (file->type[0] == 'x') goto _next; /* Pax header */
		if (file->type[0] == '1') goto _next; /* Unsupported hard link */

		char filename_workspace[257];
		filename_workspace[0] = '/';
		memset(filename_workspace + 1, 0, 257);
		strncat(filename_workspace, file->prefix, 155);
		strncat(filename_workspace, file->filename, 100);

		mode_t mode = interpret_mode(file);
		fs_node_t * node;
		int error = 0;

		if (filename_workspace[strlen(filename_workspace)-1] == '/') {
			filename_workspace[strlen(filename_workspace)-1] = 0;
		}

		switch (file->type[0]) {
			case '0': /* Regular file */
				if (!(error = create_file_fs(filename_workspace, mode, &node))) {
					chown_fs(node, interpret_uid(file), interpret_gid(file));
					close_fs(node);
				} else {
					dprintf("migrate: error from create: %d\n", error);
				}
				break;
			case '5': /* Directory */
				if (!(error = mkdir_fs(filename_workspace, mode, &node))) {
					chown_fs(node, interpret_uid(file), interpret_gid(file));
					close_fs(node);
				} else if (error != -EEXIST) {
					dprintf("migrate: error from mkdir: %d\n", error);
				}
				goto _next; /* No contents for directory */
			case '2':
				symlink_fs(file->link, filename_workspace);
				goto _next;

			default:
				dprintf("migrate: file type '%c' is unsupported.\n", file->type[0]);
				goto _next;
		}

		/* Read contents of file */
		size_t to_write = file_size;
		size_t written = 0;
		while (to_write) {
			uint8_t buf[512];
			ssize_t r = read_fs(self->device, offset + 512 + written, (to_write < 512) ? to_write : 512, buf);
			if (r <= 0) break;
			ssize_t w = write_fs(node, written, r, buf);
			if (w <= 0) {
				dprintf("migrate: write error\n");
				break;
			}
			to_write -= w;
			written += w;
		}

_next:
		offset += 512;
		offset += round_to_512(file_size);
	}
	free(file);
	free(self);

	/* Tell the ramdisk to zero itself. */
	ioctl_fs(dev, 0x4001, NULL);
	return 0;
}
