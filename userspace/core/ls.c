/*
 * ls
 *
 * Lists files in a directory, with nice color
 * output like any modern ls should have.
 */


#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <unistd.h>
#include <dirent.h>

#include "lib/list.h"

#define MIN_COL_SPACING 2

#define EXE_COLOR		"1;32"
#define DIR_COLOR		"1;34"
#define REG_COLOR		"0"
#define MEDIA_COLOR		""
#define SYM_COLOR		""
#define BROKEN_COLOR	"1;"

#define DEFAULT_TERM_WIDTH 80
#define DEFAULT_TERM_HEIGHT 24

#define MAX(a, b) (((a) > (b)) ? (a) : (b))

#define LINE_LEN 4096

/* The program */

int entcmp(const void * c1, const void * c2) {
	struct dirent * d1 = *(struct dirent **)c1;
	struct dirent * d2 = *(struct dirent **)c2;
	return strcmp(d1->d_name, d2->d_name);
}

void print_entry(const char * filename, const char * srcpath, int colwidth) {
	/* Figure out full relpath */
	char * relpath = malloc(strlen(srcpath) + strlen(filename) + 2);
	sprintf(relpath, "%s/%s", srcpath, filename);

	/* Classify file */
	struct stat statbuf;
	stat(relpath, &statbuf);
	free(relpath);

	const char * ansi_color_str;
	if (S_ISDIR(statbuf.st_mode)) {
		// Directory
		ansi_color_str = DIR_COLOR;
	} else if (statbuf.st_mode & 0111) {
		// Executable
		ansi_color_str = EXE_COLOR;
	} else {
		// Something else
		ansi_color_str = REG_COLOR;
	}


	/* Print the file name */
	printf("\033[%sm%s\033[0m", ansi_color_str, filename);

	/* Pad the rest of the column */
	for (int rem = colwidth - strlen(filename); rem > 0; rem--) {
		printf(" ");
	}
}

void print_username(int uid) {
	FILE * passwd = fopen("/etc/passwd", "r");
	char line[LINE_LEN];

	while (fgets(line, LINE_LEN, passwd) != NULL) {

		line[strlen(line)-1] = '\0';

		char *p, *tokens[10], *last;
		int i = 0;
		for ((p = strtok_r(line, ":", &last)); p;
				(p = strtok_r(NULL, ":", &last)), i++) {
			if (i < 511) tokens[i] = p;
		}
		tokens[i] = NULL;

		if (atoi(tokens[2]) == uid) {
			printf("%s", tokens[0]);
			fclose(passwd);
			return;
		}
	}
	printf("%d", uid);
	fclose(passwd);
}

void print_entry_long(const char * filename, const char * srcpath) {
	/* Figure out full relpath */
	char * relpath = malloc(strlen(srcpath) + strlen(filename) + 2);
	sprintf(relpath, "%s/%s", srcpath, filename);

	/* Classify file */
	struct stat statbuf;
	stat(relpath, &statbuf);
	free(relpath);

	const char * ansi_color_str;
	if (S_ISDIR(statbuf.st_mode)) {
		// Directory
		ansi_color_str = DIR_COLOR;
	} else if (statbuf.st_mode & 0111) {
		// Executable
		ansi_color_str = EXE_COLOR;
	} else {
		// Something else
		ansi_color_str = REG_COLOR;
	}

	/* file permissions */
	if (S_ISLNK(statbuf.st_mode)) {
		printf("l");
	} else {
		printf( (S_ISDIR(statbuf.st_mode))  ? "d" : "-");
	}
	printf( (statbuf.st_mode & S_IRUSR) ? "r" : "-");
	printf( (statbuf.st_mode & S_IWUSR) ? "w" : "-");
	printf( (statbuf.st_mode & S_IXUSR) ? "x" : "-");
	printf( (statbuf.st_mode & S_IRGRP) ? "r" : "-");
	printf( (statbuf.st_mode & S_IWGRP) ? "w" : "-");
	printf( (statbuf.st_mode & S_IXGRP) ? "x" : "-");
	printf( (statbuf.st_mode & S_IROTH) ? "r" : "-");
	printf( (statbuf.st_mode & S_IWOTH) ? "w" : "-");
	printf( (statbuf.st_mode & S_IXOTH) ? "x" : "-");

	printf( " - "); /* number of links, not supported */

	print_username(statbuf.st_uid);
	printf("\t");
	print_username(statbuf.st_gid);
	printf("\t");

	printf(" %8d ", statbuf.st_size);

	char time_buf[80];
	struct tm * timeinfo;
	timeinfo = localtime(&statbuf.st_mtime);
	strftime(time_buf, 80, "%b %d  %Y", timeinfo);
	printf("%s ", time_buf);

	/* Print the file name */
	printf("\033[%sm%s\033[0m\n", ansi_color_str, filename);

}

void show_usage(int argc, char * argv[]) {
	printf(
			"ls - list files\n"
			"\n"
			"usage: %s [-lha] [path]\n"
			"\n"
			" -a     \033[3mlist all files (including . files)\033[0m\n"
			" -l     \033[3muse a long listing format\033[0m\n"
			" -?     \033[3mshow this help text\033[0m\n"
			"\n", argv[0]);
}

int main (int argc, char * argv[]) {

	/* Parse arguments */
	char * p = ".";
	int explicit_path_set = 0;
	int show_hidden = 0;
	int long_mode   = 0;

	if (argc > 1) {
		int index, c;
		while ((c = getopt(argc, argv, "al?")) != -1) {
			switch (c) {
				case 'a':
					show_hidden = 1;
					break;
				case 'l':
					long_mode = 1;
					break;
				case '?':
					show_usage(argc, argv);
					return 0;
			}
		}

		if (optind < argc) {
			p = argv[optind];
		}
	}

	/* Open the directory */
	DIR * dirp = opendir(p);
	if (dirp == NULL) {
		printf("no such directory\n");
		return -1;
	}

	/* Read the entries in the directory */
	list_t * ents_list = list_create();

	struct dirent * ent = readdir(dirp);
	while (ent != NULL) {
		if (show_hidden || (ent->d_name[0] != '.')) {
			struct dirent * entcpy = malloc(sizeof(struct dirent));
			memcpy(entcpy, ent, sizeof(struct dirent));
			list_insert(ents_list, (void *)entcpy);
		}

		ent = readdir(dirp);
	}
	closedir(dirp);

	/* Now, copy those entries into an array (for sorting) */
	struct dirent ** ents_array = malloc(sizeof(struct dirent *) * ents_list->length);
	int index = 0;
	node_t * node;
	foreach(node, ents_list) {
		ents_array[index++] = (struct dirent *)node->value;
	}
	list_free(ents_list);
	int numents = index;

	qsort(ents_array, numents, sizeof(struct dirent *), entcmp);

	if (long_mode) {
		printf("printing listing\n");
		for (int i = 0; i < numents; i++) {
			print_entry_long(ents_array[i]->d_name, p);
		}
	} else {
		/* Determine the gridding dimensions */
		int ent_max_len = 0;
		for (int i = 0; i < numents; i++) {
			ent_max_len = MAX(ent_max_len, strlen(ents_array[i]->d_name));
		}

		int term_width = DEFAULT_TERM_WIDTH;
		int term_height = DEFAULT_TERM_HEIGHT;

		/* This is a hack to determine the terminal with in a toaru terminal */
		printf("\033[1003z");
		fflush(stdout);
		scanf("%d,%d", &term_width, &term_height);
		term_width -= 1; /* And this just helps clean up our math */

		int col_ext = ent_max_len + MIN_COL_SPACING;
		int cols = ((term_width - ent_max_len) / col_ext) + 1;

		/* Print the entries */

		for (int i = 0; i < numents;) {

			/* Columns */
			print_entry(ents_array[i++]->d_name, p, ent_max_len);

			for (int j = 0; (i < numents) && (j < (cols-1)); j++) {
				printf("  ");
				print_entry(ents_array[i++]->d_name, p, ent_max_len);
			}

			printf("\n");
		}
	}

	free(ents_array);

	return 0;
}

/*
 * vim: tabstop=4
 * vim: shiftwidth=4
 * vim: noexpandtab
 */
