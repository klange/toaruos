#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>

#ifndef __toaru__
#undef realpath
#define realpath _realpath_toaru
#endif

#define SYMLINK_MAX 5

static void _append_dir(char *out, char *element) {
	strcat(out,"/");
	strcat(out,element);
}

static void _remove_last(char * out) {
	char * last = strrchr(out,'/');
	if (last) {
		*last = '\0';
	}
}

/**
 * This is accurate to how we handle paths in ToaruOS.
 * It's not correct for real symbolic link handling,
 * so it needs some work for that.
 */
char *realpath(const char *path, char *resolved_path) {
	/*
	 * Basically the same as what we do in the kernel for canonicalize_path
	 * but slightly more complicated because of the requirement to check
	 * symlinks... this is going to get interesting.
	 */
	if (!path) {
		errno = -EINVAL;
		return NULL;
	}

	if (!resolved_path) {
		resolved_path = malloc(PATH_MAX+1);
	}

	/* If we're lucky, we can do this with no allocations, so let's start here... */
	char working_path[PATH_MAX+1];
	memcpy(working_path, path, strlen(path)+1);

	*resolved_path = 0;

	if (working_path[0] != '/') {
		/* Begin by retreiving the current working directory */
		char cwd[PATH_MAX+1];
		if (!getcwd(cwd, PATH_MAX)) {
			/* Not actually sure if this is the right choice for this, but whatever. */
			errno = -ENOTDIR;
			return NULL;
		}

		char *save;
		char *tok = strtok_r(cwd,"/",&save);
		do {
			_append_dir(resolved_path, tok);
		} while ((tok = strtok_r(NULL,"/",&save)));
	}

	char *save;
	char *tok = strtok_r(working_path,"/",&save);
	do {
		if (!strcmp(tok,".")) continue;
		if (!strcmp(tok,"..")) {
			_remove_last(resolved_path);
			continue;
		} else {
			_append_dir(resolved_path, tok);
		}
	} while ((tok = strtok_r(NULL,"/",&save)));

	if (resolved_path[0] == '\0') {
		strcat(resolved_path,"/");
	}

	return resolved_path;
}

#ifndef __toaru__
int main(int argc, char * argv[]) {
	char tmp[PATH_MAX+1];

	if (!realpath(argv[1], tmp)) {
		fprintf(stderr, "invalid path, errno=%d\n", errno);
		return 1;
	}

	fprintf(stderr, "%s=%s\n", argv[1], tmp);
	return 0;
}
#endif
