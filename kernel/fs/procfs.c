#include <system.h>
#include <fs.h>
#include <version.h>

#define PROCFS_STANDARD_ENTRIES 5

static fs_node_t * procfs_generic_create(char * name, read_type_t read_func) {
	fs_node_t * fnode = malloc(sizeof(fs_node_t));
	memset(fnode, 0x00, sizeof(fs_node_t));
	fnode->inode = 0;
	strcpy(fnode->name, name);
	fnode->uid = 0;
	fnode->gid = 0;
	fnode->flags   = FS_FILE;
	fnode->read    = read_func;
	fnode->write   = NULL;
	fnode->open    = NULL;
	fnode->close   = NULL;
	fnode->readdir = NULL;
	fnode->finddir = NULL;
	return fnode;
}

struct procfs_entry {
	int          id;
	char *       name;
	read_type_t  func;
};

uint32_t cpuinfo_func(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	return 0;
}

uint32_t meminfo_func(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	char buf[1024];
	unsigned int total = memory_total();
	unsigned int free  = total - memory_use();
	sprintf(buf, "MemTotal: %d kB\nMemFree: %d kB\n", total, free);

	size_t _bsize = strlen(buf);
	if (offset > _bsize) return 0;
	if (size > _bsize - offset) size = _bsize - offset;

	memcpy(buffer, buf + offset, size);
	return size;
}

uint32_t uptime_func(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	char buf[1024];
	sprintf(buf, "%d.%2d\n", timer_ticks, timer_subticks);

	size_t _bsize = strlen(buf);
	if (offset > _bsize) return 0;
	if (size > _bsize - offset) size = _bsize - offset;

	memcpy(buffer, buf + offset, size);
	return size;
}

uint32_t cmdline_func(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	char buf[1024];
	extern char * cmdline;
	sprintf(buf, "%s\n", cmdline ? cmdline : "");

	size_t _bsize = strlen(buf);
	if (offset > _bsize) return 0;
	if (size > _bsize - offset) size = _bsize - offset;

	memcpy(buffer, buf + offset, size);
	return size;
}

uint32_t version_func(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	char buf[1024];
	char version_number[512];
	sprintf(version_number, __kernel_version_format,
			__kernel_version_major,
			__kernel_version_minor,
			__kernel_version_lower,
			__kernel_version_suffix);
	sprintf(buf, "%s %s %s %s %s %s\n",
			__kernel_name,
			version_number,
			__kernel_version_codename,
			__kernel_build_date,
			__kernel_build_time,
			__kernel_arch);

	size_t _bsize = strlen(buf);
	if (offset > _bsize) return 0;
	if (size > _bsize - offset) size = _bsize - offset;

	memcpy(buffer, buf + offset, size);
	return size;
}


static struct procfs_entry std_entries[] = {
	{0, "cpuinfo", cpuinfo_func},
	{1, "meminfo", meminfo_func},
	{2, "uptime",  uptime_func},
	{3, "cmdline", cmdline_func},
	{4, "version", version_func},
};

static struct dirent * readdir_procfs_root(fs_node_t *node, uint32_t index) {
	if (index < PROCFS_STANDARD_ENTRIES) {
		struct dirent * out = malloc(sizeof(struct dirent));
		memset(out, 0x00, sizeof(struct dirent));
		out->ino = std_entries[index].id;
		strcpy(out->name, std_entries[index].name);
		return out;
	}
	/* XXX process entries */
	return NULL;
}

static fs_node_t * finddir_procfs_root(fs_node_t * node, char * name) {
	if (!name) return NULL;
	if (strlen(name) < 1) return NULL;

	if (name[0] >= '0' && name[0] <= '9') {
		/* XXX process entries */
		return NULL;
	}

	for (int i = 0; i < PROCFS_STANDARD_ENTRIES; ++i) {
		if (!strcmp(name, std_entries[i].name)) {
			fs_node_t * out = procfs_generic_create(std_entries[i].name, std_entries[i].func);
			return out;
		}
	}

	return NULL;
}


fs_node_t * procfs_create() {
	fs_node_t * fnode = malloc(sizeof(fs_node_t));
	memset(fnode, 0x00, sizeof(fs_node_t));
	fnode->inode = 0;
	strcpy(fnode->name, "proc");
	fnode->uid = 0;
	fnode->gid = 0;
	fnode->flags   = FS_DIRECTORY;
	fnode->read    = NULL;
	fnode->write   = NULL;
	fnode->open    = NULL;
	fnode->close   = NULL;
	fnode->readdir = readdir_procfs_root;
	fnode->finddir = finddir_procfs_root;
	return fnode;
}
