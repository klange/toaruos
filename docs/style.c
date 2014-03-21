/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * This file is part of the ToAru Kernel and is released under
 * the terms of the NCSA License.
 *
 * This is an overview of the coding style for the ToAru Kernel.
 * Source files should include a header describing the file, but
 * this is not the 70s, you do not need to list the file name.
 * Having a set of vim: command hints at the top will ensure that
 * formatting remains correct.
 *
 * Tabs are assumed to be four spaces wide for the sake of line length.
 */

/**
 * Function
 *
 * Does things.
 *
 * @param argument Normal arguments should be in line with the rest
 *                 of the function, unless the line would be cumbersome
 *                 in length. Functions should, ideally, be commented
 *                 with a Doxygen-style header, like this one.
 * @param pointer  A pointer, to something; the * is separated from
 *                 both the type and the identifier, in order to equally
 *                 annoy both sides of that argument.
 * @param string_array An array of strings
 * @returns Stuff.
 */
int function(int argument, void * pointer, char * string_array[]) {
	/* Inline comments should use the classic C-style*/
	if (condition) {
		/*
		 * If a comment is sufficiently long, you should leave some
		 * space on the top line and the bottom line.
		 */
	} else {
		while ((argument & stuff) ||
		       (argument & other) ||
		       (argument & more)) {
			/*
			 * Multiline conditionals should be aligned with spaces
			 * (though this may annoy you in some editors)
			 */
		}
	}
}

/*
 * #define'd constants should be aligned on spaces and use
 * uppercase names, separated with underscores.
 */
#define IMPORTANT_CONSTANT       value
#define OTHER_IMPORTANT_CONSTANT other_value

/*
 * Similarly, variables should be aligned on spaces
 * and separated with underscores.
 */
int important_global       = 1;
int other_important_global = 2;

/*
 * Structs should be typedef'd to something_t
 * and may be either anonymous or have internal names.
 */
typedef struct {
	int x;
	int y;
	int z;
} new_type_t;

void foo() {
	/*
	 * NULL checks should be of the form !conditional()
	 * as should checks for comparison to 0
	 */
	if (!bar()) {
		if (!strcmp(str, "value")) {
			/* str is "value" */
		}
	}
	int a;      /* Comments like this */
	int b___;   /* Should be aligned on spaces */
	int c_____; /* And use C-style comments */
	int d___;   /* Values of the same type */
	int e;      /* Should be defined on separate lines */
}
