/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014 Kevin Lange
 */
/*
 * Testing tool to write a file with the given contents.
 */
#include <stdio.h>
#include <string.h>

int main(int argc, char * argv[]) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s name \"content...\"\n", argv[0]);
        return 1;
    }
    FILE * f = fopen(argv[1], "w");

    if (!f) {
        fprintf(stderr, "%s: fopen: failed to open file\n", argv[0]);
        return 2;
    }

    size_t r = fwrite(argv[2], 1, strlen(argv[2]), f);

    fclose(f);

    fprintf(stderr, "%s: wrote %d bytes\n", argv[0], r);

    return 0;
}
