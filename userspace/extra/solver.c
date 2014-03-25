/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Brute-Force SAT Solver
 *
 * Copyright (c) 2012 Kevin Lange.  All rights reserved.
 *
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal with the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *   1. Redistributions of source code must retain the above copyright notice,
 *      this list of conditions and the following disclaimers.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimers in the
 *      documentation and/or other materials provided with the distribution.
 *   3. Neither the names of Kevin Lange, nor the names of its contributors
 *      may be used to endorse or promote products derived from this Software
 *      without specific prior written permission.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * CONTRIBUTORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * WITH THE SOFTWARE.
 *
 */

#define _XOPEN_SOURCE 500
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "lib/list.h"

/* Width of a line to read for a clause. Clauses should not be too terrible long. */
#define LINE_WIDTH 4096

/* This brute-force solution uses bitsets, one bit per variable */
#define BITS_IN_SET 8
uint8_t * bit_sets = NULL;
uint64_t bit_sets_n = 0;
uint64_t variables  = 0;
uint64_t clause_n   = 0;
uint64_t collected  = 0;

#define DEBUG 0

/* Clauses are stored as dynamic linked-lists of variable states */
list_t ** clauses;

/* Check a bit (variable) for the current state */
inline uint8_t checkbit(uint64_t bit) {
	uint64_t set_id = bit / BITS_IN_SET;
	uint8_t offset = bit - set_id * BITS_IN_SET;
	uint8_t tmp = ((bit_sets[set_id] & (1 << offset)) > 0);

#if DEBUG
	fprintf(stderr,"bit %ld [%ld:%d] = %d [%x, %d]\n", bit, set_id, offset, tmp, bit_sets[set_id], 1 << offset);
#endif

	return tmp;
}

/* Initialize the bitsets */
inline void setup_bitsets() {
	bit_sets_n = variables / BITS_IN_SET + 1;
	bit_sets = (uint8_t *)malloc(sizeof(uint8_t) * bit_sets_n);
	memset(bit_sets, 0x00, sizeof(uint8_t) * bit_sets_n);
}

/*
 * Increment the state.
 * The bit sets are like a very large integer, and we are
 * incrementing by one.
 */
inline void next_bitset(uint64_t i) {
	if (__builtin_expect(bit_sets[i] == 0xFF, 0)) {
		bit_sets[i] = 0;
		if (__builtin_expect(i + 1 == bit_sets_n, 0)) {
			/* If we have run out of state, there is no solution. */
			printf("UNSATISFIABLE\n");
			exit(0);
		}
		next_bitset(i + 1);
	} else {
		bit_sets[i]++;
		if (i + 1 == bit_sets_n) {
			if (__builtin_expect(bit_sets[i] == (1 << variables), 0)) {
				/* If we have run out of state, there is no solution. */
				printf("UNSATISFIABLE\n");
				exit(0);
			}
		}
	}
}

/*
 * We can determine if a clause is true by
 * running through each of the values in it and checking
 * if any of them are true (as a clause is a set of ORs)
 */
inline uint8_t is_clause_true(uint64_t i) {
	list_t * clause = clauses[i];

	foreach(node, clause) {
		intptr_t var = (intptr_t)node->value;
		if (var < 0) {
			if (!checkbit(-var - 1)) return 1;
		} else {
			if (checkbit(var - 1)) return 1;
		}
	}

	return 0;
}

inline uint8_t solved_with_bitset() {
	for (uint64_t i = 0; i < clause_n; ++i) {
		if (!is_clause_true(i)) {
			/* If any clause is not true, the entire statement
			 * is false for this state */
			return 0;
		}
	}
	/* If all of the clauses are true, we are done. */
	return 1;
}

/* Read a line of input and process it */
int read_line() {
	char c = fgetc(stdin);
	switch (c) {
		case 'c':
			/* Comment */
			while (fgetc(stdin) != '\n');
			break;
		case 'p':
			/* Problem definition */
			{ 
				fgetc(stdin);
				while (fgetc(stdin) != ' ');
				char num[30];
				int offset = 0;
				char input;
				while ((input = fgetc(stdin)) != ' ') {
					num[offset] = input;
					offset++;
				}
				num[offset] = '\0';
				variables = atoi(num);
				offset = 0;
				while ((input = fgetc(stdin)) != '\n') {
					num[offset] = input;
					offset++;
				}
				num[offset] = '\0';
				clause_n = atoi(num);
				clauses  = malloc(sizeof(list_t *) * clause_n);
				setup_bitsets();
#if DEBUG
				fprintf(stderr, "%ld variables, %ld clauses\n", variables, clause_n);
#endif
			}
			break;
		default:
			/* Clause */
			{
				assert(variables > 0);
				assert(clauses > 0);

				ungetc(c, stdin);

				clauses[collected] = list_create();
				char * line = malloc(LINE_WIDTH);
				fgets(line, LINE_WIDTH - 1, stdin);
				if (line[strlen(line) - 1] == '\n') {
					line[strlen(line) - 1] = '\0';
				}

#if DEBUG
				fprintf(stderr, "Preparing to add clause %ld: %s\n", collected, line);
#endif

				char *p, *last;
				for ((p = strtok_r(line, " ", &last)); ;
						(p = strtok_r(NULL, " ", &last))) {
					int var = atoi(p);
					if (var == 0) break;
					uintptr_t x = var;
					list_insert(clauses[collected], (void *)x);
				}
				free(line);

#if DEBUG
				fprintf(stderr, "Added clause.\n");
#endif

				collected++;
				if (collected == clause_n) return 0;
			}
			break;
	}
	return 1;
}

int main(int argc, char * argv[]) {
	/* Read the CNF file from standard in and process it into clauses */
	while (read_line());

#if DEBUG
	/* Debug: Print out the clauses */
	for (uint32_t i = 0; i < clause_n; ++i) {
		list_t * clause = clauses[i];
		fprintf(stderr, "[clause #%d] ", (i + 1));
		foreach(node, clause) {
			fprintf(stderr, "%ld ", (uintptr_t)node->value);
		}
		fprintf(stderr, "\n");
	}

	/* Print out variable information */
	fprintf(stderr, "%ld variables to check, which means %d combinations to bruteforce.\n", variables, 1 << variables);
#endif

	/* Brute force! */
	while (!solved_with_bitset()) {
		next_bitset(0);
	}

	/* If we get to this point, we have a solution. */
	for (uint64_t i = 0; i < variables; ++i) {
		/* Print the state of each variable. */
		if (checkbit(i)) {
			printf("%ld", i + 1);
		} else {
			printf("-%ld", i + 1);
		}
		if (i != variables - 1) {
			printf(" ");
		} else {
			printf("\n");
		}
	}

}
