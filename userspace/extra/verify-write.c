/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013 Kevin Lange
 */
/*
 * verify-write
 *
 * A dangerous tool to write to a file and verified it worked.
 */
#include <stdio.h>
#include <unistd.h>

#define CHUNK_SIZE 1024

int main(int argc, char * argv[]) {
	if (argc < 3) {
		printf("Expected two arguments, the file to read, and the filename to write out to.\nTry again, maybe?\n");
		return -1;
	}
	FILE * input  = fopen(argv[1], "r");
	FILE * output = fopen(argv[2], "w");
	size_t length;

	fseek(input, 0, SEEK_END);
	length = ftell(input);
	fseek(input, 0, SEEK_SET);

	char buf[CHUNK_SIZE];
	while (length > CHUNK_SIZE) {
		fread( buf, 1, CHUNK_SIZE, input);
		fwrite(buf, 1, CHUNK_SIZE, output);
		fflush(output);
		length -= CHUNK_SIZE;
	}
	if (length > 0) {
		fread( buf, 1, length, input);
		fwrite(buf, 1, length, output);
		fflush(output);
	}

	fclose(output);
	fclose(input);

	char * args[] = {"/bin/compare", argv[1], argv[2], NULL };
	execvp(args[0], args);

	return 0;
}
