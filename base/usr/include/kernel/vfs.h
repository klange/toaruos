#pragma once

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <bits/dirent.h>

#define PATH_SEPARATOR '/'
#define PATH_SEPARATOR_STRING "/"
#define PATH_UP  ".."
#define PATH_DOT "."

#include <fcntl.h>

#define FS_FILE        0x01
#define FS_DIRECTORY   0x02
#define FS_CHARDEVICE  0x04
#define FS_BLOCKDEVICE 0x08
#define FS_PIPE        0x10
#define FS_SYMLINK     0x20
#define FS_MOUNTPOINT  0x40
#define FS_SOCKET      0x80

#define _IFMT       0170000 /* type of file */
#define     _IFDIR  0040000 /* directory */
#define     _IFCHR  0020000 /* character special */
#define     _IFBLK  0060000 /* block special */
#define     _IFREG  0100000 /* regular */
#define     _IFLNK  0120000 /* symbolic link */
#define     _IFSOCK 0140000 /* socket */
#define     _IFIFO  0010000 /* fifo */

struct fs_node;

typedef ssize_t (*read_type_t) (struct fs_node *,  off_t, size_t, uint8_t *);
typedef ssize_t (*write_type_t) (struct fs_node *, off_t, size_t, uint8_t *);
typedef void (*open_type_t) (struct fs_node *, unsigned int flags);
typedef void (*close_type_t) (struct fs_node *);
typedef struct dirent *(*readdir_type_t) (struct fs_node *, unsigned long);
typedef struct fs_node *(*finddir_type_t) (struct fs_node *, char *name);
typedef int (*create_type_t) (struct fs_node *, char *name, mode_t permission);
typedef int (*unlink_type_t) (struct fs_node *, char *name);
typedef int (*mkdir_type_t) (struct fs_node *, char *name, mode_t permission);
typedef int (*ioctl_type_t) (struct fs_node *, unsigned long request, void * argp);
typedef int (*get_size_type_t) (struct fs_node *);
typedef int (*chmod_type_t) (struct fs_node *, mode_t mode);
typedef int (*symlink_type_t) (struct fs_node *, char * name, char * value);
typedef ssize_t (*readlink_type_t) (struct fs_node *, char * buf, size_t size);
typedef int (*selectcheck_type_t) (struct fs_node *);
typedef int (*selectwait_type_t) (struct fs_node *, void * process);
typedef int (*chown_type_t) (struct fs_node *, uid_t, gid_t);
typedef int (*truncate_type_t) (struct fs_node *);

typedef struct fs_node {
	char name[256];         /* The filename. */
	void * device;          /* Device object (optional) */
	mode_t mask;          /* The permissions mask. */
	uid_t uid;           /* The owning user. */
	uid_t gid;           /* The owning group. */
	uint64_t flags;         /* Flags (node type, etc). */
	uint64_t inode;         /* Inode number. */
	uint64_t length;        /* Size of the file, in byte. */
	uint64_t impl;          /* Used to keep track which fs it belongs to. */
	uint64_t open_flags;    /* Flags passed to open (read/write/append, etc.) */

	/* times */
	time_t atime;         /* Accessed */
	time_t mtime;         /* Modified */
	time_t ctime;         /* Created  */

	/* File operations */
	read_type_t read;
	write_type_t write;
	open_type_t open;
	close_type_t close;
	readdir_type_t readdir;
	finddir_type_t finddir;
	create_type_t create;
	mkdir_type_t mkdir;
	ioctl_type_t ioctl;
	get_size_type_t get_size;
	chmod_type_t chmod;
	unlink_type_t unlink;
	symlink_type_t symlink;
	readlink_type_t readlink;
	truncate_type_t truncate;

	struct fs_node *ptr;   /* Alias pointer, for symlinks. */
	int64_t refcount;
	uint64_t nlink;

	selectcheck_type_t selectcheck;
	selectwait_type_t selectwait;

	chown_type_t chown;
} fs_node_t;

struct vfs_entry {
	char * name;
	fs_node_t * file;
	char * device;
	char * fs_type;
};

extern fs_node_t *fs_root;
extern int pty_create(void *size, fs_node_t ** fs_master, fs_node_t ** fs_slave);

int has_permission(fs_node_t *node, int permission_bit);
ssize_t read_fs(fs_node_t *node,  off_t offset, size_t size, uint8_t *buffer);
ssize_t write_fs(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer);
void open_fs(fs_node_t *node, unsigned int flags);
void close_fs(fs_node_t *node);
struct dirent *readdir_fs(fs_node_t *node, unsigned long index);
fs_node_t *finddir_fs(fs_node_t *node, char *name);
int mkdir_fs(char *name, mode_t permission);
int create_file_fs(char *name, mode_t permission);
fs_node_t *kopen(const char *filename, unsigned int flags);
char *canonicalize_path(const char *cwd, const char *input);
fs_node_t *clone_fs(fs_node_t * source);
int ioctl_fs(fs_node_t *node, unsigned long request, void * argp);
int chmod_fs(fs_node_t *node, mode_t mode);
int chown_fs(fs_node_t *node, uid_t uid, gid_t gid);
int unlink_fs(char * name);
int symlink_fs(char * value, char * name);
ssize_t readlink_fs(fs_node_t * node, char * buf, size_t size);
int selectcheck_fs(fs_node_t * node);
int selectwait_fs(fs_node_t * node, void * process);
int truncate_fs(fs_node_t * node);

void vfs_install(void);
void * vfs_mount(const char * path, fs_node_t * local_root);
typedef fs_node_t * (*vfs_mount_callback)(const char * arg, const char * mount_point);
int vfs_register(const char * name, vfs_mount_callback callback);
int vfs_mount_type(const char * type, const char * arg, const char * mountpoint);
void vfs_lock(fs_node_t * node);

/* Debug purposes only, please */
void debug_print_vfs_tree(void);

void map_vfs_directory(const char *);

int make_unix_pipe(fs_node_t ** pipes);

int fprintf(fs_node_t * f, const char * fmt, ...);
