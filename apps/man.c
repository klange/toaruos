/**
 * @brief Rudimentary manpage viewer.
 *
 * Searches for a manual page at /usr/share/man/manN/PAGE.N,
 * and if one is found it is pasesed to a 'roff|more' pipeline
 * for display.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2026 K. Lange
 */
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <dirent.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/stat.h>

#if !defined(TOARU_MAN_TOOL_PREFIX)
#define TOARU_MAN_TOOL_PREFIX ""
#endif

#if !defined(TOARU_MAN_PATH_PREFIX)
#define TOARU_MAN_PATH_PREFIX ""
#endif

#define MAN_DIR  TOARU_MAN_PATH_PREFIX "/usr/share/man/man%s"
#define MAN_FMT  MAN_DIR "/%s.%s"
#define ROFF_CMD TOARU_MAN_TOOL_PREFIX "roff"
#define MORE_CMD TOARU_MAN_TOOL_PREFIX "more -rP'%s(%s)' --stay --alt"

/* Don't bother prefixing this; I don't want to build our grep elsewhere. */
#define GREP_CMD "grep --color -i -- '%s' -"

static int where_is = 0;

static int usage(char * argv[]) {
#define X_S "\033[3m"
#define X_E "\033[0m"
	fprintf(stderr,
		"usage: %s [-a] [-S " X_S "sectionlist" X_E "] [" X_S "section" X_E "] " X_S "page..." X_E "\n"
		"       %s -k [-S " X_S "sectionlist" X_E "] " X_S "keyword..." X_E "\n"
		"\n"
		"Display a manual page.\n"
		"\n"
		"Options:\n"
		"\n"
		" -a       " X_S "Display all matching manuals." X_E "\n"
		" -w       " X_S "Print the locations of manual pages." X_E "\n"
		" -S " X_S "list  Limit search to colon-separated list of sections." X_E "\n"
		" -k       " X_S "Search manual page NAME sections for keywords." X_E "\n"
		" --help   " X_S "Show this help text." X_E "\n"
		"\n", argv[0], argv[0]);
	return 1;
}

/**
 * @brief Try to display a manual page.
 *
 * Checks for the existence of @p filename and, if found, possibly
 * decompresses it, passes it to @b roff, and pipes it to @b more.
 *
 * @param filename Full path to the file to check.
 * @param page Page name to display in the @b more prompt.
 * @param i Section number to display in the @b more prompt.
 * @returns 1 if the stat succeeded and we ran the pipeline (even if the pipeline fails)
 *          otherwise 0 if the stat failed.
 */
static int try_filename(char * filename, char * page, char * i) {
	struct stat st;
	if (!stat(filename, &st)) {
		if (where_is) {
			puts(filename);
			return 1;
		}
		int is_gz = strlen(filename) > 3 && !strcmp(filename + strlen(filename)-3,".gz");
		char * systemcmd;
		asprintf(&systemcmd,
			!is_gz ?
			(ROFF_CMD " '%s' | " MORE_CMD) :
			("gunzip -c '%s' | " ROFF_CMD " -- - | " MORE_CMD),
			filename, page, i);
		int result = system(systemcmd);
		if (result) exit(WEXITSTATUS(result));
		free(systemcmd);
		return 1;
	}
	return 0;
}

/**
 * @brief Look for a page in a section.
 *
 * Checks whether @p page in section @p i exists
 * at the configured location, with or without
 * a @b .gz suffix.
 *
 * @param i Section to look in.
 * @param page Page name to look for.
 * @returns 0 if neither filename worked, 1 if one did.
 */
static int try_section(char *i, char * page) {
	char * filename;
	asprintf(&filename, MAN_FMT, i, page, i);
	if (try_filename(filename, page, i)) {
		free(filename);
		return 1;
	}
	free(filename);
	asprintf(&filename, MAN_FMT ".gz", i, page, i);
	if (try_filename(filename, page, i)) {
		free(filename);
		return 1;
	}
	free(filename);
	return 0;
}

/**
 * @brief Search pages in a section for a keyword.
 *
 * Looks at all the manual pages in section @p i and processes
 * them through 'roff -S NAME -P | grep -i' to determine if
 * they contain a requested @p keyword. First, this is done
 * in a dry-run mode where the output is written to /dev/null.
 * Then, if the page matched, its name is printed and it is
 * processed again for actual output.
 *
 * This will only try to look at files in the section that
 * actually look like pages, so if you ask for section 1
 * and a file doesn't match '{page}.1' or '{page}.1.gz',
 * it will be ignored. This matches the behavior of our page
 * lookup elsewhere, as man will not find these pages if
 * ask for them by name.
 *
 * We try to pass @p keyword to grep as a single argument,
 * but nothing in this stack does input validation, so it's
 * up to the user to not shell inject themselves with a bad
 * keyword argument.
 *
 * Because we run 'roff' with the '-P' option, grep should
 * be able to match across formatting changes correctly.
 *
 * Also we enable --color in grep because it looks nice.
 *
 * @param i Section to search in
 * @param keyword Pattern to pass to grep
 * @returns 1 if a page matched or 0 otherwise.
 */
static int search_section(char * i, char * keyword) {
	size_t len_i = strlen(i);
	char * dirpath;
	asprintf(&dirpath, MAN_DIR, i);
	DIR * dir = opendir(dirpath);
	if (!dir) return 0; /* Not a valid section? */

	int found = 0;
	struct dirent * ent;
	while ((ent = readdir(dir))) {
		if (ent->d_name[0] == '.') continue;

		char * filename;
		asprintf(&filename,"%s/%s", dirpath, ent->d_name);
		int is_gz = strlen(filename) > 3 && !strcmp(filename + strlen(filename)-3,".gz");

		char * page = strdup(ent->d_name);
		size_t len_page = strlen(page);

		if (is_gz) {
			/* Exclude .gz suffix from printed name */
			page[len_page - 3] = '\0';
			len_page -= 3;
		}

		if (len_page > len_i + 1 && page[len_page - len_i - 1] == '.' &&
			!strcmp(page + len_page - len_i, i)) {
			/* Exclude section suffix from printed name */
			page[len_page - len_i - 1] = '\0';
			len_page -= len_i + 1;
		} else {
			/* And reject files that don't look like man pages for this section. */
			free(page);
			free(filename);
			continue;
		}

		int dry_run = 1;
		while (dry_run >= 0) {

			if (!dry_run) {
				/* If not in dry run, prefix the output without the page and section
				 * so the user can actually find it. Try to format this nicely. */
				int written = fprintf(stdout, "%s (%s)", page, i);
				while (written < 30) {
					fprintf(stdout, " ");
					written++;
				}
				fprintf(stdout, " - ");
				fflush(stdout);
			}

			char * systemcmd;
			asprintf(&systemcmd,
				!is_gz ?
				(ROFF_CMD " -S NAME -P '%s' | " GREP_CMD "%s") :
				("gunzip -c '%s' | " ROFF_CMD " -S NAME -- - | " GREP_CMD "%s"),
				filename, keyword, dry_run ? " >/dev/null" : "");
			int result = system(systemcmd);
			free(systemcmd);
			if (result != 0) break;

			found |= 1;
			dry_run--;
		}

		free(page);
		free(filename);
	}
	free(dirpath);
	closedir(dir);
	return found;
}

/**
 * @brief Convert a colon-separated list into an array.
 *
 * Parses a colon-separated list of strings into an array of those
 * strings. Used to parse the @c MANSECT environment variable or
 * the @b -S option argument.
 *
 * @param str String to convert.
 * @returns An allocated array of pointers to strings in a copy of @c str
 */
static char ** sections_from_string(char * str) {
	char * working_space = strdup(str); /* Beacuse we modifiy it and it may come from _environ */
	size_t count = 1;
	char * c = working_space;
	for (; *c; ++c) {
		if (*c == ':') count++;
	}
	char ** sects = calloc(sizeof(char*),count+1);
	size_t i = 0;
	c = working_space;
	while (*c) {
		sects[i] = c;
		while (*c && *c != ':') c++;
		if (*c == ':') {
			*c = '\0';
			i++;
			c++;
		}
	}

	return sects;
}

/**
 * @brief Check if a string looks like a filename.
 *
 * Just checks if a path contains a /
 *
 * @param arg Possible filename.
 * @returns 1 if it looks like a filename, else 0.
 */
static int is_file_name(char * arg) {
	while (*arg) {
		if (*arg == '/') return 1;
		arg++;
	}
	return 0;
}

/**
 * @brief Check if a string looks like a section number.
 *
 * A section number can be a single lowercase letter,
 * such as @b n or @b l or it can be one numeric digit
 * followed by any amount of lower case letters. Is that
 * technically correct? Probably not, but it's close enough
 * to reality.
 *
 * @param arg Possible section number.
 * @returns 1 if it looks like a section, else 0.
 */
static int is_section(char * arg) {
	if (*arg >= 'a' && *arg <= 'z' && !arg[1]) return 1;
	if (*arg >= '0' && *arg <= '9') {
		arg++;
		while (*arg) {
			if (*arg < 'a' || *arg > 'z') return 0;
			arg++;
		}
		return 1;
	}
	return 0;
}

int main(int argc, char * argv[]) {

	/* The default section list either comes from @b $MANSECT - but only
	 * if we're building for ToaruOS and it is set - or the default
	 * list that we stole from an Ubuntu configuration. */
	char * section_list =
#if defined(__toaru__)
		getenv("MANSECT");
	if (!section_list) section_list =
#endif
			"1:n:l:8:3:0:2:3krk:5:4:9:6:7";

	int show_all = 0;
	int search_names = 0;
	int opt;
	while ((opt = getopt(argc, argv, "?awS:k-:")) != -1) {
		switch (opt) {
			case 'a':
				show_all = 1;
				break;
			case 'w':
				where_is = 1;
				break;
			case 'S':
				section_list = optarg;
				break;
			case 'k':
				search_names = 1;
				break;
			case '-':
				if (!strcmp(optarg,"help")) {
					usage(argv);
					return 0;
				}
				fprintf(stderr, "%s: '--%s' is not a recognized long option.\n", argv[0], optarg);
				/* fallthrough */
			case '?':
				return usage(argv);
		}
	}

	/* At least one argument is required. */
	if (optind == argc) {
		return 0;
	}

	/* Convert the default sections list (from the envvar, compiled default, or argument)
	 * to an array we can iterate over more easily. */
	char ** sections = sections_from_string(section_list);
	char * section = NULL;
	int ret = 0;

	/* If there is more than one argument and it looks like it might be a section
	 * then treat it is as such and use it for all of the rest of the arguments. */
	if (!search_names && optind + 1 != argc && is_section(argv[optind])) {
		section = argv[optind];
		optind++;
	}

	for (; optind != argc; ++optind) {
		int found = 0;

		if (search_names) {
			for (char ** s = section ? (char*[]){section, NULL} : sections; *s; ++s) {
				found |= search_section(*s, argv[optind]);
			}
		} else if (is_file_name(argv[optind])) {
			/* Accept things that look like raw paths to roff sources */
			if (try_filename(argv[optind], argv[optind], "")) found = 1;
		} else if (section) {
			/* If a single section argument was given, look in that. */
			if (try_section(section, argv[optind])) found = 1;
		} else {
			/* Otherwise, search through the MANSECT list in order. */
			for (char ** s = sections; *s; ++s) {
				if (try_section(*s, argv[optind])) {
					found = 1;
					if (!show_all) break;
				}
			}
		}

		if (!found) {
			fprintf(stderr,
				search_names
					? "%s: nothing appropriate\n"
					: "No manual entry for %s\n",
				argv[optind]);
			ret |= 1;
		}
	}

	return ret;
}
