#include <unistd.h>
#include <getopt.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

char * optarg = NULL;
int optind = 1;
int opterr = 1;
int optopt = 0;

int getopt_long(int argc, char * const argv[], const char *optstring, const struct option *longopts, int *longindex) {
	static char * nextchar = NULL;
	static int optind_expected = 1;

	if (optind != optind_expected) nextchar = NULL; /* If optind changed, ensure we process this argument from the start */
	if (optind == 0) optind = 1; /* If optind was set to 0, accept this as forcing a reset and starting parsing again from 1. */
	optind_expected = optind;

	/* Argument parsing has ended. */
	if (optind >= argc) return -1;

	/* POSIX.1-2024 says ignore leading + with no change in behavior */
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

			/* No leading - means not-option argument, stop processing. */
			if (*nextchar != '-') return -1;
			nextchar++;

			/* Just a single dash is also a non-option argument, stop processing. */
			if (*nextchar == '\0') return -1;

			/* Just two dashes ends processing, consumes this argument. */
			if (*nextchar == '-' && !nextchar[1]) {
				optind++;
				return -1;
			}

			/* If long options are enabled, -- starts a long option. */
			if (*nextchar == '-' && longopts) {
				/* Scan through options */
				nextchar++;
				char * eq = strchrnul(nextchar, '=');
				optarg = *eq ? eq + 1 : NULL;

				int found = -1;
				int ambiguous = 0;
				int ret = '?';

				/* Try for a prefix match */
				for (int index = 0; longopts[index].name; ++index) {
					if (!strncmp(longopts[index].name, nextchar, eq - nextchar)) {
						if (strlen(longopts[index].name) == (size_t)(eq - nextchar)) {
							/* An exact match is an exact match, take it. */
							found = index;
							ambiguous = 0;
							break;
						}
						if (found != -1) ambiguous = 1;
						found = index;
					}
				}

				if (ambiguous) {
					if (print_errors) {
						fprintf(stderr, "%s: Ambiguous option: '--%.*s'; possibilities:", argv[0], (int)(eq - nextchar), nextchar);
						for (int index = 0; longopts[index].name; ++index) {
							if (!strncmp(longopts[index].name, nextchar, eq - nextchar)) {
								fprintf(stderr, " '--%s'", longopts[index].name);
							}
						}
						fprintf(stderr,"\n");
					}
					optopt = '\0';
					goto _erroneous_longopt;
				} else if (found == -1) {
					if (print_errors) fprintf(stderr, "%s: Unrecognized option: '--%.*s'\n", argv[0], (int)(eq - nextchar), nextchar);
					optopt = '\0';
					goto _erroneous_longopt;
				}

				if (found != -1) {
					if (longindex) *longindex = found;

					if (longopts[found].has_arg == required_argument) {
						if (!optarg) {
							if (optind + 1 == argc) {
								if (print_errors) fprintf(stderr, "%s: Option requires an argument: '%s'\n", argv[0], longopts[found].name);
								optopt = longopts[found].val;
								ret = was_colon ? ':' : '?'; /* Just for missing required argument, if caller asked, they get a colon. */
								goto _erroneous_longopt;
							}
							optarg = argv[optind+1];
							optind++;
						}
					} else if (longopts[found].has_arg == no_argument && optarg) {
						if (print_errors) fprintf(stderr, "%s: Option does not accept an argument: '%s'\n", argv[0], longopts[found].name);
						optopt = longopts[found].val;
						goto _erroneous_longopt;
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

_erroneous_longopt:
				if (longindex) *longindex = -1;
				nextchar = NULL;
				optind++;
				return ret;
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
			if (print_errors) fprintf(stderr, "%s: Unrecognized option character: '%c'\n", argv[0], optout);
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

int getopt(int argc, char * const argv[], const char * optstring) {
	return getopt_long(argc, argv, optstring, NULL, 0);
}
