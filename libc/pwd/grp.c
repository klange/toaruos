#define _TOARU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <grp.h>

static FILE * grpdb = NULL;

static void open_it(void) {
	grpdb = fopen("/etc/group", "re");
}

#define LINE_LEN 2048

static struct group grp_ent;
static char * grp_blob = NULL;
static size_t grp_blob_avail = 0;
static char ** grp_mem = NULL;

static int grp_common(char * grp_blob, char ***grp_memp, struct group *grp_entp) {
#define _next() do { if (!(e = strchr(e, ':'))) return 0; *e++ = 0; } while (0)
	char * e = grp_blob;
	grp_entp->gr_name = e;
	_next();

	grp_entp->gr_passwd = e;
	_next();

	char *gid = e;
	_next();
	grp_entp->gr_gid = atoi(gid);

	/* Now let's do the members */
	char * m = e;

	size_t cnt = 0;
	if (*m) cnt++;
	for (;*m;m++) if (*m == ',') cnt++;

	free(*grp_memp);
	*grp_memp = calloc(cnt + 1, sizeof(char**));

	m = e;
	for (size_t i = 0; i < cnt; ++i) {
		(*grp_memp)[i] = m;

		while (*m && *m != ',') m++;
		if (*m) {
			*m = '\0';
			m++;
		}
	}

	grp_entp->gr_mem = *grp_memp;
	return 1;
}

int fgetgrent_t(FILE * stream, struct group *grpbuf, char ** _buf, size_t *_buflen, struct group **grpbufp) {
	ssize_t len;

	if (!stream) {
		*grpbufp = NULL;
		return EINVAL;
	}

	while (1) {
		if ((len = getline(_buf, _buflen, stream)) <= 0) {
			*grpbufp = NULL;
			return ENOENT;
		}

		char * grp_blob = *_buf;
		if (grp_blob[len-1] == '\n') grp_blob[len-1] = '\0';

		char ** grp_mem = NULL;
		if (grp_common(grp_blob, &grp_mem, grpbuf)) {
			*grpbufp = grpbuf;
			return 0;
		}
	}
}

struct group *fgetgrent(FILE *stream) {
	ssize_t len;
	if (!stream) return NULL;

	while (1) {
		if ((len = getline(&grp_blob, &grp_blob_avail, stream)) <= 0) return NULL;
		if (grp_blob[len-1] == '\n') grp_blob[len-1] = '\0';
		if (grp_common(grp_blob, &grp_mem, &grp_ent)) return &grp_ent;
	}
}

struct group * getgrent(void) {
	if (!grpdb) open_it();
	if (!grpdb) return NULL;
	return fgetgrent(grpdb);
}

void setgrent(void) {
	if (!grpdb) open_it();
	if (grpdb) rewind(grpdb);
}

void endgrent(void) {
	if (grpdb) {
		fclose(grpdb);
		grpdb = NULL;
	}
	free(grp_blob);
	memset(&grp_ent, 0, sizeof(struct group));
	grp_blob = NULL;
	grp_blob_avail = 0;
	free(grp_mem);
	grp_mem = NULL;
}


struct group *getgrnam(const char *name) {
	struct group * grp;
	setgrent();

	while ((grp = getgrent())) {
		if (!strcmp(grp->gr_name, name)) return grp;
	}

	return NULL;
}

struct group *getgrgid(gid_t gid) {
	struct group * grp;
	setgrent();

	while ((grp = getgrent())) {
		if (grp->gr_gid == gid) return grp;
	}

	return NULL;
}


