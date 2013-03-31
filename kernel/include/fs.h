/* vim: tabstop=4 shiftwidth=4 noexpandtab
 */
#ifndef FS_H
#define FS_H

#define PATH_SEPARATOR '/'
#define PATH_SEPARATOR_STRING "/"
#define PATH_UP  ".."
#define PATH_DOT "."

#define FS_FILE        0x01
#define FS_DIRECTORY   0x02
#define FS_CHARDEVICE  0x04
#define FS_BLOCKDEVICE 0x08
#define FS_PIPE        0x10
#define FS_SYMLINK     0x20
#define FS_MOUNTPOINT  0x40

#define	_IFMT		0170000	/* type of file */
#define		_IFDIR	0040000	/* directory */
#define		_IFCHR	0020000	/* character special */
#define		_IFBLK	0060000	/* block special */
#define		_IFREG	0100000	/* regular */
#define		_IFLNK	0120000	/* symbolic link */
#define		_IFSOCK	0140000	/* socket */
#define		_IFIFO	0010000	/* fifo */

struct fs_node;

typedef uint32_t (*read_type_t) (struct fs_node *, uint32_t, uint32_t, uint8_t *);
typedef uint32_t (*write_type_t) (struct fs_node *, uint32_t, uint32_t, uint8_t *);
typedef void (*open_type_t) (struct fs_node *, uint8_t read, uint8_t write);
typedef void (*close_type_t) (struct fs_node *);
typedef struct dirent *(*readdir_type_t) (struct fs_node *, uint32_t);
typedef struct fs_node *(*finddir_type_t) (struct fs_node *, char *name);
typedef void (*create_type_t) (struct fs_node *, char *name, uint16_t permission);
typedef void (*mkdir_type_t) (struct fs_node *, char *name, uint16_t permission);
typedef int (*ioctl_type_t) (struct fs_node *, int request, void * argp);
typedef int (*get_size_type_t) (struct fs_node *);

typedef struct fs_node {
	char name[256];         // The filename.
	void * device;          // Device object (optional)
	uint32_t mask;          // The permissions mask.
	uint32_t uid;           // The owning user.
	uint32_t gid;           // The owning group.
	uint32_t flags;         // Flags (node type, etc).
	uint32_t inode;         // Inode number.
	uint32_t length;        // Size of the file, in byte.
	uint32_t impl;          // Used to keep track which fs it belongs to.

	/* times */
	uint32_t atime;         // Accessed
	uint32_t mtime;         // Modified
	uint32_t ctime;         // Created

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

	struct fs_node *ptr;   // Alias pointer, for symlinks.
	uint32_t offset;       // Offset for read operations XXX move this to new "file descriptor" entry
	int32_t shared_with;   // File descriptor sharing XXX 
} fs_node_t;

struct dirent {
	uint32_t ino;           // Inode number.
	char name[256];         // The filename.
};

struct stat  {
	uint16_t  st_dev;
	uint16_t  st_ino;
	uint32_t  st_mode;
	uint16_t  st_nlink;
	uint16_t  st_uid;
	uint16_t  st_gid;
	uint16_t  st_rdev;
	uint32_t  st_size;
	uint32_t  st_atime;
	uint32_t  __unused1;
	uint32_t  st_mtime;
	uint32_t  __unused2;
	uint32_t  st_ctime;
	uint32_t  __unused3;
};

extern fs_node_t *fs_root;
extern fs_node_t * null_device_create();
extern fs_node_t * zero_device_create();
extern fs_node_t * serial_device_create(int device);
extern fs_node_t * procfs_create();
extern void serial_mount_devices();
extern int openpty(int * master, int * slave, char * name, void * _ign0, void * size);

extern fs_node_t * hello_device_create();
extern fs_node_t * random_device_create();

uint32_t read_fs(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
uint32_t write_fs(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
void open_fs(fs_node_t *node, uint8_t read, uint8_t write);
void close_fs(fs_node_t *node);
struct dirent *readdir_fs(fs_node_t *node, uint32_t index);
fs_node_t *finddir_fs(fs_node_t *node, char *name);
int mkdir_fs(char *name, uint16_t permission);
int create_file_fs(char *name, uint16_t permission);
fs_node_t *kopen(char *filename, uint32_t flags);
char *canonicalize_path(char *cwd, char *input);
fs_node_t *clone_fs(fs_node_t * source);
int ioctl_fs(fs_node_t *node, int request, void * argp);

void vfs_install();
int vfs_mount(char * path, fs_node_t * local_root);

/* Debug purposes only, please */
void debug_print_vfs_tree();

#endif
