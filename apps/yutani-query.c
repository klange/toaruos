#include <stdio.h>
#include <getopt.h>

#include <toaru/yutani.h>

yutani_t * yctx;

void show_usage(int argc, char * argv[]) {
	printf(
			"yutani-query - show misc. information about the display system\n"
			"\n"
			"usage: %s [-rfm?]\n"
			"\n"
			" -r     \033[3mprint display resoluton\033[0m\n"
			" -f     \033[3mprint the name of the default font\033[0m\n"
			" -m     \033[3mprint the name of the monospace font\033[0m\n"
			" -?     \033[3mshow this help text\033[0m\n"
			"\n", argv[0]);
}

int show_resolution(void) {
	printf("%dx%d\n", yctx->display_width, yctx->display_height);
	return 0;
}

#if 0
int show_fontname(int font) {
	init_shmemfonts();
	printf("%s\n", shmem_font_name(font));
	return 0;
}
#endif

int main(int argc, char * argv[]) {
	yctx = yutani_init();
	if (!yctx) {
		printf("(not connected)\n");
		return 1;
	}
	if (argc > 1) {
		for (int i = 1; i < argc; ++i) {
			if (argv[i][0] == '-') {
				char *c = &argv[i][1];
				while (*c) {
					switch (*c) {
						case 'r':
							return show_resolution();
#if 0
						case 'f':
							return show_fontname(FONT_SANS_SERIF);
						case 'm':
							return show_fontname(FONT_MONOSPACE);
#endif
						case '?':
							show_usage(argc, argv);
							return 0;
					}
					c++;
				}
			}
		}
	}

	return 0;
}
