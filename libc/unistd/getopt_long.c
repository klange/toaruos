#include <unistd.h>
#include <getopt.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/**
 * getopt / getopt_long
 */

int getopt_long(int argc, char * const argv[], const char *optstring, const struct option *longopts, int *longindex) {
	static char * nextchar = NULL;
	static int optind_expected = 1;

	if (optind != optind_expected) nextchar = NULL; /* If optind changed, ensure we process this argument from the start */
	if (optind == 0) optind = 1; /* If optind was set to 0, accept this as forcing a reset and starting parsing again from 1. */
	optind_expected = optind;

	/* Argument parsing has ended. */
	if (optind >= argc) return -1;

	/* POSIX.1-2024 says ignoring leading + with no change in behavior */
	if (*optstring == '+') optstring++;

	/* Print errors unless opterr was unset, or if start of opstring (after a possible +) was : */
	int print_errors = !!opterr;
	int was_colon = 0;

	if (*optstring == ':') {
		print_errors = 0;
		optstring++;
		was_colon = 1;
	}

	do {
		if (!nextchar) {
			nextchar = argv[optind];
			if (*nextchar != '-') {
				return -1;
			} else {
				nextchar++;

				if (*nextchar == '\0') {
					/* Special case, '-' is a non-option argument */
					return -1;
				}

				if (*nextchar == '-') {
					if (nextchar[1] == '\0') {
						/* End of arguments */
						optind++;
						return -1;
					} else if (longopts) {
						/* Scan through options */
						nextchar++;
						char tmp[strlen(nextchar)+1];
						strcpy(tmp, nextchar);
						char * eq = strchr(tmp, '=');
						if (eq) {
							*eq = '\0';
							optarg = nextchar + (eq - tmp + 1);
						} else {
							optarg = NULL;
						}

						int found = -1;
						for (int index = 0; longopts[index].name; ++index) {
							if (!strcmp(longopts[index].name, tmp)) {
								found = index;
							}
						}

						if (found == -1) {
							if (longindex) {
								*longindex = -1;
							}
							if (print_errors) {
								fprintf(stderr, "%s: Unknown long argument: %s\n", argv[0], tmp);
							}
							nextchar = NULL;
							optind++;
							optopt = '\0';
							return '?';
						} else {
							if (longindex) {
								*longindex = found;
							}
							if (longopts[found].has_arg == required_argument) {
								if (!optarg) {
									optarg = argv[optind+1];
									optind++;
								}
							}
							nextchar = NULL;
							optind++;
							if (!longopts[found].flag) {
								return longopts[found].val;
							} else {
								*longopts[found].flag = longopts[found].val;
								return 0;
							}
						}
					}
					/* else: --foo but not long, see if -: is set, otherwise continue as if - was an option */
				}
			}
		}

		int optout = *nextchar;

		if (!optout) {
			/* Processing of the current argv has completed, advance to the next. */
			nextchar = NULL;
			optind++;
			continue;
		}

		char * opt = strchr(optstring, optout);

		if (!opt) {
			if (print_errors) fprintf(stderr, "%s: Invalid option character: %c\n", argv[0], optout);
			optopt = optout;
			nextchar++;
			return '?';
		}

		if (opt[1] == ':') {
			if (nextchar[1] != '\0') {
				/* Argument comes from the rest of this argv */
				optarg = &nextchar[1];
				nextchar = NULL;
				optind++;
			} else if (opt[2] == ':') {
				/* Double colon is a GNU extension: Argument is optional, and only to be read from the same argv element.
				 * If we didn't find an argument here, optarg is to be set to 0, rather than taking the next argv.  */
				optarg = 0;
				nextchar = NULL;
				optind++;
			} else {
				/* Argument is the next argv */
				if (optind + 1 == argc) {
					if (print_errors) fprintf(stderr, "%s: Option requires an argument: '%c'\n", argv[0], optout);
					optopt = optout;
					nextchar++;
					return was_colon ? ':' : '?';
				}
				optarg = argv[optind+1];
				optind += 2;
				nextchar = NULL;
			}
		} else {
			nextchar++;
		}

		return optout;

	} while (optind < argc);

	return -1;
}
