/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 *
 * yutani-clipboard - Manipulate the Yutani clipboard
 *
 * Gets and sets clipboard values.
 */
#include <stdio.h>
#include <unistd.h>
#include <getopt.h>

#include <toaru/yutani.h>

void show_usage(int argc, char * argv[]) {
	printf(
			"yutani-clipboard - set and obtain clipboard contents\n"
			"\n"
			"usage: %s -g\n"
			"       %s -s TEXT...\n"
			"       %s -f FILE\n"
			"\n"
			" -s     \033[3mset the clipboard text to argument\033[0m\n"
			" -f     \033[3mset the clibboard text to file\033[0m\n"
			" -g     \033[3mprint clipboard contents to stdout\033[0m\n"
			" -n     \033[3mensure a linefeed is printed\033[0m\n"
			" -?     \033[3mshow this help text\033[0m\n"
			"\n", argv[0], argv[0], argv[0]);
}

yutani_t * yctx;
int force_linefeed = 0;

int set_clipboard_from_file(char * file) {
	FILE * f;

	f = fopen(file, "r");
	if (!f) return 1;

	fseek(f, 0, SEEK_END);
	size_t size = ftell(f);
	fseek(f, 0, SEEK_SET);

	char * tmp = malloc(size+1);
	fread(tmp, 1, size, f);
	tmp[size] = '\0';

	yutani_set_clipboard(yctx, tmp);

	free(tmp);

	return 0;
}

void get_clipboard(void) {
	yutani_special_request(yctx, NULL, YUTANI_SPECIAL_REQUEST_CLIPBOARD);
	yutani_msg_t * clipboard = yutani_wait_for(yctx, YUTANI_MSG_CLIPBOARD);
	struct yutani_msg_clipboard * cb = (void *)clipboard->data;

	if (*cb->content == '\002') {
		int size = atoi(&cb->content[2]);
		FILE * clipboard = yutani_open_clipboard(yctx);
		char * selection_text = malloc(size + 1);
		fread(selection_text, 1, size, clipboard);
		selection_text[size] = '\0';
		fclose(clipboard);
		fwrite(selection_text, 1, size, stdout);
		if (force_linefeed && size && selection_text[size-1] != '\n') {
			printf("\n");
		}
	} else {
		char * selection_text = malloc(cb->size+1);
		memcpy(selection_text, cb->content, cb->size);
		selection_text[cb->size] = '\0';
		fwrite(selection_text, 1, cb->size, stdout);
		if (force_linefeed && cb->size && selection_text[cb->size-1] != '\n') {
			printf("\n");
		}
	}

}

int main(int argc, char * argv[]) {
	yctx = yutani_init();
	if (!yctx) {
		fprintf(stderr, "%s: failed to connect to compositor\n", argv[0]);
		return 1;
	}
	int opt;
	while ((opt = getopt(argc, argv, "?s:f:gn")) != -1) {
		switch (opt) {
			case 's':
				yutani_set_clipboard(yctx, optarg);
				return 0;
			case 'f':
				return set_clipboard_from_file(optarg);
			case 'n':
				force_linefeed = 1;
				break;
			case 'g':
				get_clipboard();
				return 0;
			case '?':
				show_usage(argc,argv);
				return 1;
		}
	}

	show_usage(argc, argv);
	return 1;
}
