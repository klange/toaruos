#include <stdio.h>
#include <unistd.h>

#include <toaru/yutani.h>

yutani_t * yctx;

void show_usage(int argc, char * argv[]) {
	printf(
			"yutani-query - show misc. information about the display system\n"
			"\n"
			"usage: %s [-r?]\n"
			"\n"
			" -r     \033[3mprint display resoluton\033[0m\n"
			" -?     \033[3mshow this help text\033[0m\n"
			"\n", argv[0]);
}

int show_resolution(void) {
	printf("%dx%d\n", (int)yctx->display_width, (int)yctx->display_height);
	return 0;
}

int main(int argc, char * argv[]) {
	yctx = yutani_init();
	if (!yctx) {
		printf("(not connected)\n");
		return 1;
	}
	int opt;
	while ((opt = getopt(argc, argv, "?r")) != -1) {
		switch (opt) {
			case 'r':
				return show_resolution();
			case '?':
				show_usage(argc,argv);
				return 0;
		}
	}

	return 0;
}
