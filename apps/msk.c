/* vim: ts=4 sw=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 *
 * msk - Package Management Utility for ToaruOS
 *
 * This is a not-quite-faithful reconstruction of the original
 * Python msk. The supported package format is a bit different,
 * to avoid the need to implement a full JSON parser.
 *
 * Packages can optionally be uncompressed, which is also
 * important for bootstrapping at the moment.
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include <toaru/confreader.h>
#include <toaru/list.h>
#include <toaru/hashmap.h>

#define MSK_VERSION "0.0.1"
#define VAR_PATH "/var/msk"

static confreader_t * msk_config = NULL;
static confreader_t * msk_manifest = NULL;
static hashmap_t *    msk_installed = NULL;
static char *         msk_remote = NULL;

static int verbose = 0;

/**
 * checks whether 'candidate' is newer than 'current'.
 *
 * Requires version strings to be of the form x.y.z
 *
 * > 0   candidate is newer
 * = 0   candidate is the same
 * < 0   candidate is older
 */
#if 0
static int compare_version_strings(char * current, char * candidate) {
	int current_x, current_y, current_z;
	int candidate_x, candidate_y, candidate_z;

	sscanf(current, "%d.%d.%d", &current_x, &current_y, &current_z);
	sscanf(candidate, "%d.%d.%d", &candidate_x, &candidate_y, &candidate_z);

	if (candidate_x >= current_x) {
		if (candidate_y >= current_y) {
			if (candidate_z > current_z) {
				return 1;
			} else if (candidate_z == current_z) {
				return 0;
			}
		}
	}

	return -1;
}
#endif

static void read_config(void) {
	confreader_t * conf = confreader_load("/etc/msk.conf");
	if (!conf) {
		fprintf(stderr, "failed to read configuration file\n");
		exit(1);
	}

	if (!strcmp(confreader_getd(conf, "", "verbose",""), "y")) {
		verbose = 1;
	}

	msk_remote = confreader_get(conf, "", "remote");

	msk_config = conf;
}

static void read_manifest(void) {
	confreader_t * conf = confreader_load(VAR_PATH "/manifest");
	if (!conf) {
		fprintf(stderr, "no manifest; try `msk update` first\n");
		exit(1);
	}

	msk_manifest = conf;
}

static void read_installed(void) {
	msk_installed = hashmap_create(10);

	FILE * installed = fopen("/var/msk/installed", "r");
	if (!installed) return;

	while (!feof(installed)) {
		char tmp[128] = {0};
		if (!fgets(tmp, 128, installed)) break;
		char * nl = strstr(tmp, "\n");
		if (nl) *nl = '\0';

		char * eqeq = strstr(tmp, "==");
		if (!eqeq) {
			fprintf(stderr, "Installation cache is malformed\n");
			fprintf(stderr, "line was: [%s]\n", tmp);
			exit(1);
		}

		*eqeq = '\0';
		char * version = eqeq+2;

		hashmap_set(msk_installed, tmp, strdup(version));
	}
}

static void make_var(void) {
	struct stat buf;
	if (stat(VAR_PATH, &buf)) {
		mkdir(VAR_PATH, 0755);
	}
}

static void needs_root(void) {
	if (geteuid() != 0) {
		fprintf(stderr, "only root can install packages; try `sudo`\n");
		exit(1);
	}
}

static int usage(int argc, char * argv[]) {
#define _IT "\033[3m"
#define _END "\033[0m\n"
	fprintf(stderr,
			"%s - package manager " MSK_VERSION "\n"
			"\n"
			"usage: %s update\n"
			"       %s install [PACKAGE...]\n"
			"\n"
			" update  " _IT "update local manifest from remote" _END
			" install " _IT "install packages" _END
			"\n", argv[0], argv[0], argv[0]);
	return 1;
}

static int update_stores(int argc, char * argv[]) {
	needs_root();
	if (argc > 2) {
		fprintf(stderr,"%s: %s: unexpected arguments in command\n", argv[0], argv[1]);
		return usage(argc,argv);
	}

	read_config();
	make_var();

	if (!msk_remote) {
		fprintf(stderr, "%s: no configured msk_remote\n", argv[0]);
		return 1;
	}

	if (msk_remote[0] == '/') {
		char cmd[512];
		sprintf(cmd, "cp %s/manifest " VAR_PATH "/manifest", msk_remote);
		return system(cmd);
	} else {
		char cmd[512];
		sprintf(cmd, "fetch -vo " VAR_PATH "/manifest %s/manifest", msk_remote);
		return system(cmd);
	}
}

static int list_contains(list_t * list, char * key) {
	foreach(node, list) {
		char * v = node->value;
		if (!strcmp(v,key)) return 1;
	}
	return 0;
}

static int process_package(list_t * pkgs, char * name) {
	if (hashmap_has(msk_installed, name)) return 0;
	if (list_contains(pkgs, name)) return 0;

	if (!hashmap_has(msk_manifest->sections, name)) {
		fprintf(stderr, "don't know how to install '%s'\n", name);
		return 1;
	}

	/* Gather dependencies */
	char * tmp  = confreader_get(msk_manifest, name, "dependencies");
	if (strlen(tmp)) {
		char * deps = strdup(tmp);
		char * save;
		char * tok = strtok_r(deps, " ", &save);
		do {
			process_package(pkgs, tok);
			tok = strtok_r(NULL, " ", &save);
		} while (tok);
		free(deps);
	}

	/* Insert */
	list_insert(pkgs, strdup(name));
	return 0;
}

static int install_package(char * pkg) {

	char * type = confreader_getd(msk_manifest, pkg, "type", "");

	if (strstr(msk_remote, "http:") == msk_remote) {
		char * source = confreader_get(msk_manifest, pkg, "source");
		if (source) {
			fprintf(stderr, "Download %s...\n", pkg);
			char cmd[1024];
			sprintf(cmd, "fetch -o /tmp/msk.file -v %s/%s", msk_remote,
					source);
			system(cmd);
			hashmap_set(hashmap_get(msk_manifest->sections, pkg), "source", "/tmp/msk.file");
		}
	}

	fprintf(stderr, "Install '%s'...\n", pkg);

	if (!strcmp(type, "file")) {
		/* Legacy single-file package, has a source and a destination */

		if (verbose) {
			fprintf(stderr, "  - Copy file '%s' to '%s' and set its mask to '%s'\n",
					confreader_get(msk_manifest, pkg, "source"),
					confreader_get(msk_manifest, pkg, "destination"),
					confreader_get(msk_manifest, pkg, "mask"));
		}

		char cmd[1024];
		sprintf(cmd, "cp %s %s; chmod 0%s %s",
				confreader_get(msk_manifest, pkg, "source"),
				confreader_get(msk_manifest, pkg, "destination"),
				confreader_get(msk_manifest, pkg, "mask"),
				confreader_get(msk_manifest, pkg, "destination"));

		int status;
		if ((status = system(cmd))) {
			fprintf(stderr, "installation command returned %d\n", status);
			return status;
		}

	} else if (!strcmp(type, "tar")) {
		/* Uncompressed archive */

		if (verbose) {
			fprintf(stderr, "  - Extract '%s' to '%s'\n",
					confreader_get(msk_manifest, pkg, "source"),
					confreader_get(msk_manifest, pkg, "destination"));
		}

		char cmd[1024];
		sprintf(cmd, "cd %s; tar -xf %s",
				confreader_get(msk_manifest, pkg, "destination"),
				confreader_get(msk_manifest, pkg, "source"));

		int status;
		if ((status = system(cmd))) {
			fprintf(stderr, "installation command returned %d\n", status);
			return status;
		}

	} else if (!strcmp(type, "tgz")) {
		/* Compressed archive */

		if (verbose) {
			fprintf(stderr, "  - Extract (compressed) '%s' to '%s'\n",
					confreader_get(msk_manifest, pkg, "source"),
					confreader_get(msk_manifest, pkg, "destination"));
		}

		char cmd[1024];
		sprintf(cmd, "cd %s; ungz %s /tmp/%s.tar; tar -xf /tmp/%s.tar; rm /tmp/%s.tar",
				confreader_get(msk_manifest, pkg, "destination"),
				confreader_get(msk_manifest, pkg, "source"),
				pkg, pkg, pkg);

		int status;
		if ((status = system(cmd))) {
			fprintf(stderr, "installation command returned %d\n", status);
			return status;
		}

	} else if (!strcmp(type, "meta")) {
		/* Do nothing */

	} else {
		fprintf(stderr, "Unknown package type: %s\n", type);
		return 1;
	}

	char * post = confreader_getd(msk_manifest, pkg, "post", "");
	if (strlen(post)) {
		int status;
		if ((status = system(post))) {
			fprintf(stderr, "post-installation command returned %d\n", status);
			return status;
		}
	}

	/* Mark as installed */
	FILE * installed = fopen("/var/msk/installed", "a");
	fprintf(installed, "%s==%s\n", pkg, confreader_get(msk_manifest, pkg, "version"));
	fclose(installed);

	return 0;
}

static int install_packages(int argc, char * argv[]) {
	needs_root();
	read_config();
	read_manifest();
	read_installed();

	/* Go through each package and find its dependencies */
	list_t * ordered = list_create();

	for (int i = 2; i < argc; ++i) {
		if (process_package(ordered, argv[i])) {
			return 1;
		}
	}

	/* Additional packages must be installed, let's ask. */
	if (ordered->length != (unsigned int)(argc - 2)) {
		fprintf(stderr, "The following packages will be installed:\n");
		fprintf(stderr, "    ");
		int notfirst = 0;
		foreach(node, ordered) {
			fprintf(stderr, "%s%s", notfirst ? " " : "", (char*)node->value);
			notfirst = 1;
		}
		fprintf(stderr, "\nContinue? [Y/n] ");
		fflush(stdout);
		char resp[4];
		fgets(resp, 4, stdin);
		if (!(!strcmp(resp,"\n") || !strcmp(resp,"y\n") || !strcmp(resp,"Y\n") || !strcmp(resp,"yes\n"))) {
			fprintf(stderr, "Aborting.\n");
			return 1;
		}
	}

	foreach(node, ordered) {
		if (install_package(node->value)) {
			return 1;
		}
	}

	return 0;
}

static int list_packages(int argc, char * argv[]) {
	read_config();
	read_manifest();
	read_installed();

	/* Go through sections */
	list_t * packages = hashmap_keys(msk_manifest->sections);
	foreach(node, packages) {
		char * name = node->value;
		if (!strlen(name)) continue; /* skip empty section */
		char * desc = confreader_get(msk_manifest, name, "description");
		fprintf(stderr, " %c %20s %s\n", hashmap_has(msk_installed, name) ? 'I' : ' ', name, desc);
		/* TODO: Installation status */
	}

	return 0;
}

static int version(void) {
	fprintf(stderr, "msk " MSK_VERSION "\n");
	return 0;
}

int main(int argc, char * argv[]) {

	if (argc < 2) {
		return usage(argc,argv);
	} else if (!strcmp(argv[1],"--version")) {
		return version();
	} else if (!strcmp(argv[1],"update")) {
		return update_stores(argc,argv);
	} else if (!strcmp(argv[1], "install")) {
		return install_packages(argc,argv);
	} else if (!strcmp(argv[1], "list")) {
		return list_packages(argc, argv);
	} else {
		fprintf(stderr, "%s: unknown command '%s'\n", argv[0], argv[1]);
		return usage(argc,argv);
	}

}

