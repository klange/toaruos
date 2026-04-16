#include <unistd.h>
#include <getopt.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

char * optarg = NULL;
int optind = 1;
int opterr = 1;
int optopt = 0;

/* Reverse the elements in [left,right] */
static void reverse(char * const argv[], int left, int right) {
	/* left and right are inclusive and can be the same */
	if (left == right) return;
	while (left < right) {
		char * scratch = argv[left];
		/* We lied about being const (BSD and GNU do something like this, too). */
		((char**)argv)[left] = argv[right];
		((char**)argv)[right] = scratch;
		left++;
		right--;
	}
}

/* Swaps the items from [a,b] with the ones in (b,optind) */
static int permutate_args(char * const argv[], int first_nonopt, int last_nonopt, int optind) {
	reverse(argv, first_nonopt, last_nonopt);
	reverse(argv, last_nonopt + 1, optind - 1);
	reverse(argv, first_nonopt, optind - 1);
	return optind - last_nonopt - 1;
}

/* Macro to cleanly record that we expect to process the next argument. */
#define finish_arg() do { \
	nextchar = NULL; \
	optind++; \
	optind_expected = optind; \
} while (0)

int getopt_long(int argc, char * const argv[], const char *optstring, const struct option *longopts, int *longindex) {
	static char * nextchar = NULL;
	static int optind_expected = 1;
	static int first_nonopt = -1;
	static int last_nonopt = -1;

	/* If optind changed, ensure we process this argument from the start */
	if (optind != optind_expected) nextchar = NULL;

	/* If optind was set to 0, accept this as forcing a reset and starting parsing again from 1. */
	if (optind == 0) {
		first_nonopt = last_nonopt = -1;
		optind = 1;
	}

	optind_expected = optind;

	/* Argument parsing has ended. */
	if (optind >= argc) goto _finish_up;

	/* XXX Does this play right with `optind` moving? */
	if (first_nonopt != -1) {
		/* Swap the nonopts with the arguments we processed in the last go. */
		int ops = permutate_args(argv, first_nonopt, last_nonopt, optind);
		last_nonopt += ops;
		first_nonopt += ops;
	}

	int print_errors = !!opterr;
	int was_colon = 0;
	int no_permute = 0;

	/* POSIX.1-2024 says ignore leading + with no change in behavior */
	if (*optstring == '+') {
		no_permute = 1;
		optstring++;
	}

	/* Print errors unless opterr was unset, or if start of opstring (after a possible +) was : */
	if (*optstring == ':') {
		print_errors = 0;
		optstring++;
		was_colon = 1;
	}

	do {
		if (!nextchar) {
			nextchar = argv[optind];

			/* no leading - or just a - is a non-option argument. */
			if (*nextchar != '-' || nextchar[1] == '\0') {
				if (no_permute) return -1; /* We are done. */

				/* Extend current block of nonopts (or start a new one), and continue to next argument */
				if (first_nonopt == -1) first_nonopt = optind;
				last_nonopt = optind;
				finish_arg();
				continue;
			}

			nextchar++;

			/* Just two dashes ends processing, consumes this argument. */
			if (*nextchar == '-' && !nextchar[1]) {
				optind++;
				goto _finish_up;
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

				/* GNU does not set longindex when an option was identified but had an error;
				 * BSD does, and it seems more useful. */
				if (longindex) *longindex = found;

				if (longopts[found].has_arg == required_argument) {
					if (!optarg) {
						if (optind + 1 == argc) {
							if (print_errors) fprintf(stderr, "%s: Option requires an argument: '--%s'\n", argv[0], longopts[found].name);
							optopt = longopts[found].val;
							ret = was_colon ? ':' : '?'; /* Just for missing required argument, if caller asked, they get a colon. */
							goto _erroneous_longopt;
						}
						optarg = argv[optind+1];
						optind++;
					}
				} else if (longopts[found].has_arg == no_argument && optarg) {
					if (print_errors) fprintf(stderr, "%s: Option does not accept an argument: '--%s'\n", argv[0], longopts[found].name);
					optopt = longopts[found].val;
					goto _erroneous_longopt;
				}
				/* optional_argument was handled implicitly */

				finish_arg();

				if (!longopts[found].flag) return longopts[found].val;

				*longopts[found].flag = longopts[found].val;
				return 0;

_erroneous_longopt:
				finish_arg();
				return ret;
			}
		}

		int optout = *nextchar;
		char * opt = strchr(optstring, optout);

		if (!opt || *opt == ':') {
			if (print_errors) fprintf(stderr, "%s: Unrecognized option character: '%c'\n", argv[0], optout);
			optopt = optout;
			nextchar++;
			if (!*nextchar) finish_arg();
			return '?';
		}

		if (opt[1] == ':') {
			if (nextchar[1] != '\0') {
				/* Argument comes from the rest of this argv */
				optarg = &nextchar[1];
				finish_arg();
			} else if (opt[2] == ':') {
				/* Double colon is a GNU extension: Argument is optional, and only to be read from the same argv element.
				 * If we didn't find an argument here, optarg is to be set to 0, rather than taking the next argv.  */
				optarg = 0;
				finish_arg();
			} else {
				/* Argument is the next argv */
				if (optind + 1 == argc) {
					if (print_errors) fprintf(stderr, "%s: Option requires an argument: '%c'\n", argv[0], optout);
					optopt = optout;
					nextchar++;
					return was_colon ? ':' : '?';
				}
				optarg = argv[optind+1];
				optind++;
				finish_arg();
			}
		} else {
			nextchar++;
			if (!*nextchar) finish_arg();
		}

		return optout;

	} while (optind < argc);

_finish_up: (void)0;
	if (first_nonopt != -1) {
		optind = first_nonopt + permutate_args(argv, first_nonopt, last_nonopt, optind);
		first_nonopt = last_nonopt = -1;
		/* Don't use finish_arg() because we already set optind to the right thing. */
		nextchar = NULL;
		optind_expected = optind;
	}
	return -1;
}

int getopt(int argc, char * const argv[], const char * optstring) {
	return getopt_long(argc, argv, optstring, NULL, 0);
}
