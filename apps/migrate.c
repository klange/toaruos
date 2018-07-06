#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <unistd.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include <toaru/trace.h>
#include <toaru/hashmap.h>
#define TRACE_APP_NAME "migrate"

#define TRACE_(...) do { if (_debug) { TRACE(__VA_ARGS__); } } while (0)

#define CHUNK_SIZE 4096

static int _debug = 0;

int tokenize(char * str, char * sep, char **buf) {
	char * pch_i;
	char * save_i;
	int    argc = 0;
	pch_i = strtok_r(str,sep,&save_i);
	if (!pch_i) { return 0; }
	while (pch_i != NULL) {
		buf[argc] = (char *)pch_i;
		++argc;
		pch_i = strtok_r(NULL,sep,&save_i);
	}
	buf[argc] = NULL;
	return argc;
}

void copy_link(char * source, char * dest) {
	//fprintf(stderr, "need to copy link %s to %s\n", source, dest);
	char tmp[1024];
	readlink(source, tmp, 1024);
	symlink(tmp, dest);
}

void copy_file(char * source, char * dest, int mode) {
	//fprintf(stderr, "need to copy file %s to %s %x\n", source, dest, mode);

	int d_fd = open(dest, O_WRONLY | O_CREAT, mode);
	int s_fd = open(source, O_RDONLY);

	ssize_t length;

	length = lseek(s_fd, 0, SEEK_END);
	lseek(s_fd, 0, SEEK_SET);

	//fprintf(stderr, "%d bytes to copy\n", length);

	char buf[CHUNK_SIZE];

	while (length > 0) {
		size_t r = read(s_fd, buf, length < CHUNK_SIZE ? length : CHUNK_SIZE);
		//fprintf(stderr, "copying %d bytes from %s to %s\n", r, source, dest);
		write(d_fd, buf, r);
		length -= r;
		//fprintf(stderr, "%d bytes remaining\n", length);
	}

	close(s_fd);
	close(d_fd);

}

void copy_directory(char * source, char * dest, int mode) {
	DIR * dirp = opendir(source);
	if (dirp == NULL) {
		fprintf(stderr, "Failed to copy directory %s\n", source);
		return;
	}

	//fprintf(stderr, "Creating %s\n", dest);
	if (!strcmp(dest, "/")) {
		dest = "";
	} else {
		mkdir(dest, mode);
	}

	struct dirent * ent = readdir(dirp);
	while (ent != NULL) {
		if (!strcmp(ent->d_name,".") || !strcmp(ent->d_name,"..")) {
			//fprintf(stderr, "Skipping %s\n", ent->d_name);
			ent = readdir(dirp);
			continue;
		}
		//fprintf(stderr, "not skipping %s/%s → %s/%s\n", source, ent->d_name, dest, ent->d_name);
		struct stat statbuf;
		char tmp[strlen(source)+strlen(ent->d_name)+2];
		sprintf(tmp, "%s/%s", source, ent->d_name);
		char tmp2[strlen(dest)+strlen(ent->d_name)+2];
		sprintf(tmp2, "%s/%s", dest, ent->d_name);
		//fprintf(stderr,"%s → %s\n", tmp, tmp2);
		lstat(tmp,&statbuf);
		if (S_ISLNK(statbuf.st_mode)) {
			copy_link(tmp, tmp2);
		} else if (S_ISDIR(statbuf.st_mode)) {
			copy_directory(tmp, tmp2, statbuf.st_mode & 07777);
		} else if (S_ISREG(statbuf.st_mode)) {
			copy_file(tmp, tmp2, statbuf.st_mode & 07777);
		} else {
			fprintf(stderr, " %s is not any of the required file types?\n", tmp);
		}
		ent = readdir(dirp);
	}
	closedir(dirp);
}

void free_ramdisk(char * path) {
	int fd = open(path, O_RDONLY);
	ioctl(fd, 0x4001, NULL);
	close(fd);
}

hashmap_t * get_cmdline(void) {
	int fd = open("/proc/cmdline", O_RDONLY);
	char * out = malloc(1024);
	size_t r = read(fd, out, 1024);
	out[r] = '\0';
	if (out[r-1] == '\n') {
		out[r-1] = '\0';
	}

	char * arg = strdup(out);
	char * argv[1024];
	int argc = tokenize(arg, " ", argv);

	/* New let's parse the tokens into the arguments list so we can index by key */

	hashmap_t * args = hashmap_create(10);

	for (int i = 0; i < argc; ++i) {
		char * c = strdup(argv[i]);

		char * name;
		char * value;

		name = c;
		value = NULL;
		/* Find the first = and replace it with a null */
		char * v = c;
		while (*v) {
			if (*v == '=') {
				*v = '\0';
				v++;
				value = v;
				goto _break;
			}
			v++;
		}

_break:
		hashmap_set(args, name, value);
	}

	free(arg);
	free(out);

	return args;
}

int main(int argc, char * argv[]) {

	hashmap_t * cmdline = get_cmdline();

	if (hashmap_has(cmdline, "logtoserial")) {
		_debug = 1;
	}

	if (hashmap_has(cmdline, "root")) {
		TRACE_("Original root was %s", hashmap_get(cmdline, "root"));
	} else if (hashmap_has(cmdline,"init") && !strcmp(hashmap_get(cmdline,"init"),"/dev/ram0")) {
		TRACE_("Init is ram0, so this is probably a netboot image, going to assume root is /tmp/netboot.img");
		hashmap_set(cmdline,"root","/tmp/netboot.img");
	} else {
		TRACE_("Fatal: Don't know how to boot this. No root set.\n");
		return 1;
	}

	char * root = hashmap_get(cmdline,"root");

	char * start = hashmap_get(cmdline,"_start");
	if (!start) {
		start = "";
	}
	char * root_type = hashmap_get(cmdline,"root_type");
	if (!root_type) {
		root_type = "ext2";
	}

	char tmp[1024];

	TRACE_("Remounting root to /dev/base");
	sprintf(tmp, "mount %s %s /dev/base", root_type, root);
	system(tmp);

	TRACE_("Mounting tmpfs to /");
	system("mount tmpfs x /");

	TRACE_("Migrating root...");
	copy_directory("/dev/base","/",0660);
	system("mount tmpfs x /dev/base");

	if (strstr(root, "/dev/ram") != NULL) {
		char * tmp = strdup(root);
		char * c = strchr(tmp, ',');
		if (c) {
			*c = '\0';
		}
		TRACE_("Freeing ramdisk at %s", tmp);
		free_ramdisk(tmp);
		free(tmp);
	}

	TRACE_("Launching intended startup app...");
	if (!strcmp(start, "--vga")) {
		execvp("/bin/terminal-vga", (char *[]){"terminal-vga","-l",NULL});
	} else if (!strcmp(start, "--headless")) {
		execvp("/bin/getty", (char *[]){"getty",NULL});
	} else if (start) {
		execvp("/bin/compositor", (char *[]){"compositor","--",start,NULL});
	} else {
		execvp("/bin/compositor", (char *[]){"compositor",NULL});
	}

	return 0;
}
