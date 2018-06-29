/* vim: ts=4 sw=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2015-2017 Kevin Lange
 *
 * netboot-init
 *
 *   Download, decompress, and mount a root filesystem from the
 *   network and run the `/bin/init` contained therein.
 *
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <syscall.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <sys/time.h>
#include <getopt.h>
#include <syscall.h>

#define NETBOOT_URL "http://10.0.2.1:8080/netboot.img"

#include <pthread.h>
#include <kernel/video.h>
#include "../lib/list.c"
#include "../lib/hashmap.c"
#include "terminal-font.h"

extern int mount(char* src,char* tgt,char* typ,unsigned long,void*);

#define SIZE 512

struct http_req {
	char domain[SIZE];
	char path[SIZE];
	int port;
	int ssl;
};

struct {
	int show_headers;
	const char * output_file;
	const char * cookie;
	FILE * out;
} fetch_options = {0};

#define TRACE(msg,...) do { \
	char tmp[512]; \
	sprintf(tmp, msg, ##__VA_ARGS__); \
	fprintf(stderr, "%s", tmp); \
	fflush(stderr); \
	print_string(tmp); \
} while(0)

static int has_video = 1;
static int width, height, depth;
static char * framebuffer;
static struct timeval start;
static int framebuffer_fd;

#define char_height 20
#define char_width  9

#define BG_COLOR 0xFF050505
#define FG_COLOR 0xFFCCCCCC
#define EX_COLOR 0xFF999999

static void set_point(int x, int y, uint32_t value) {
	uint32_t * disp = (uint32_t *)framebuffer;
	uint32_t * cell = &disp[y * width + x];
	*cell = value;
}

static void write_char(int x, int y, int val, uint32_t color) {
	if (val > 128) {
		val = 4;
	}
#ifdef number_font
	uint8_t * c = number_font[val];
	for (uint8_t i = 0; i < char_height; ++i) {
		for (uint8_t j = 0; j < char_width; ++j) {
			if (c[i] & (1 << (8-j))) {
				set_point(x+j,y+i,color);
			} else {
				set_point(x+j,y+i,BG_COLOR);
			}
		}
	}
#else
	uint16_t * c = large_font[val];
	for (uint8_t i = 0; i < char_height; ++i) {
		for (uint8_t j = 0; j < char_width; ++j) {
			if (c[i] & (1 << (15-j))) {
				set_point(x+j,y+i,color);
			} else {
				set_point(x+j,y+i,BG_COLOR);
			}
		}
	}

#endif
}

#define BUF_SIZE 255
static void read_http_line(char * buf, FILE * f) {
	memset(buf, 0x00, BUF_SIZE);

	fgets(buf, BUF_SIZE, f);
	char * _r = strchr(buf, '\r');
	if (_r) {
		*_r = '\0';
	}
	if (!_r) {
		_r = strchr(buf, '\n'); /* that's not right, but, whatever */
		if (_r) {
			*_r = '\0';
		}
	}
}

#define LEFT_PAD 40
static int x = LEFT_PAD;
static int y = 0;
static void print_string(char * msg) {
	if (!has_video) return;
	while (*msg) {
		write_char(x,y,' ',BG_COLOR);
		switch (*msg) {
			case '\n':
				x = LEFT_PAD;
				y += char_height;
				break;
			case '\033':
				msg++;
				if (*msg == '[') {
					msg++;
					if (*msg == 'G') {
						x = LEFT_PAD;
					}
					if (*msg == 'K') {
						int last_x = x;
						while (x < width) {
							write_char(x,y,' ',FG_COLOR);
							x += char_width;
						}
						x = last_x;
					}
				}
				break;
			default:
				write_char(x,y,*msg,FG_COLOR);
				x += char_width;
				break;
		}
		write_char(x,y,'_',EX_COLOR);
		msg++;
	}
}

void parse_url(char * d, struct http_req * r) {
	if (strstr(d, "http://") == d) {

		d += strlen("http://");

		char * s = strstr(d, "/");
		if (!s) {
			strcpy(r->domain, d);
			strcpy(r->path, "");
		} else {
			*s = 0;
			s++;
			strcpy(r->domain, d);
			strcpy(r->path, s);
		}
		if (strstr(r->domain,":")) {
			char * port = strstr(r->domain,":");
			*port = '\0';
			port++;
			r->port = atoi(port);
		} else {
			r->port = 80;
		}
		r->ssl = 0;
	} else if (strstr(d, "https://") == d) {

		d += strlen("https://");

		char * s = strstr(d, "/");
		if (!s) {
			strcpy(r->domain, d);
			strcpy(r->path, "");
		} else {
			*s = 0;
			s++;
			strcpy(r->domain, d);
			strcpy(r->path, s);
		}
		if (strstr(r->domain,":")) {
			char * port = strstr(r->domain,":");
			*port = '\0';
			port++;
			r->port = atoi(port);
		} else {
			r->port = 443;
		}
		r->ssl = 1;
	} else {
		TRACE("sorry, can't parse %s\n", d);
		exit(1);
	}
}


static void bad_response(void) {
	TRACE("Bad response.\n");
	exit(1);
}

static char * img = "/tmp/netboot.img";

static void update_video(int sig) {
	(void)sig;
	ioctl(framebuffer_fd, IO_VID_WIDTH,  &width);
	ioctl(framebuffer_fd, IO_VID_HEIGHT, &height);
	ioctl(framebuffer_fd, IO_VID_DEPTH,  &depth);
	ioctl(framebuffer_fd, IO_VID_ADDR,   &framebuffer);
	ioctl(framebuffer_fd, IO_VID_SIGNAL, NULL);
	/* Clear the screen */
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			set_point(x,y,BG_COLOR);
		}
	}
	x = LEFT_PAD;
	y = 0;

	if (sig) {
		TRACE("(video display changed to %d x %d)\n", width, height);
	}
}

static volatile int watchdog_success = 0;

static void network_error(int is_thread) {
	TRACE("ERROR: Network does not seem to be available, or unable to reach host.\n");
	TRACE("       Please check your VM configuration.\n");
	if (is_thread) {
		pthread_exit(0);
	} else {
		exit(1);
	}
}

static void * watchdog_func(void * garbage) {
	(void)garbage;

	int i = 0;

	while (i < 5) {
		usleep(1000000);
		if (watchdog_success) {
			pthread_exit(0);
		}
		i++;
	}

	network_error(1);
	return NULL;
}

#define BAR_WIDTH 20
#define bar_perc "||||||||||||||||||||"
#define bar_spac "                    "
static void draw_progress(size_t content_length, size_t size) {
	TRACE("\033[G%6dkB",(int)size/1024);
	if (content_length) {
		int percent = (size * BAR_WIDTH) / (content_length);
		TRACE(" / %6dkB [%.*s%.*s]", (int)content_length/1024, percent,bar_perc,BAR_WIDTH-percent,bar_spac);
	}
	TRACE("\033[K");
}

/* This is taken from the kernel/sys/version.c */
#if (defined(__GNUC__) || defined(__GNUG__)) && !(defined(__clang__) || defined(__INTEL_COMPILER))
# define COMPILER_VERSION "gcc " __VERSION__
#elif (defined(__clang__))
# define COMPILER_VERSION "clang " __clang_version__
#else
# define COMPILER_VERSION "unknown-compiler how-did-you-do-that"
#endif

int main(int argc, char * argv[]) {
	int _stdin  = open("/dev/null", O_RDONLY);
	int _stdout = open("/dev/ttyS0", O_WRONLY);
	int _stderr = open("/dev/ttyS0", O_WRONLY);

	if (_stdout < 0) {
		_stdout = open("/dev/null", O_WRONLY);
		_stderr = open("/dev/null", O_WRONLY);
	}

	(void)_stdin;
	(void)_stdout;
	(void)_stderr;

	framebuffer_fd = open("/dev/fb0", O_RDONLY);
	if (framebuffer_fd < 0) {
		has_video = 0;
	} else {
		update_video(0);
		signal(SIGWINEVENT, update_video);
	}

	TRACE("\n\nToaruOS Netboot Host\n\n");

	TRACE("ToaruOS is free software under the NCSA / University of Illinois license.\n");
	TRACE("   http://toaruos.org/   https://github.com/klange/toaruos\n\n");

	struct utsname u;
	uname(&u);
	TRACE("%s %s %s %s\n", u.sysname, u.nodename, u.release, u.version);

	{
		char kernel_version[512] = {0};
		int fd = open("/proc/compiler", O_RDONLY);
		read(fd, kernel_version, 512);
		if (kernel_version[strlen(kernel_version)-1] == '\n') {
			kernel_version[strlen(kernel_version)-1] = '\0';
		}
		TRACE(" Kernel was built with: %s\n", kernel_version);
	}

	TRACE(" Netboot binary was built with: %s\n", COMPILER_VERSION);

	TRACE("\n");

	if (has_video) {
		TRACE("Display is %dx%d (%d bpp), framebuffer at 0x%x\n", width, height, depth, (unsigned int)framebuffer);
	} else {
		TRACE("No video? framebuffer_fd = %d\n", framebuffer_fd);
	}

	TRACE("\n");
	TRACE("Sleeping for a moment to let network initialize...\n");
	sleep(2);

#define LINE_LEN 100
	char line[LINE_LEN];

	FILE * f = fopen("/proc/netif", "r");

	while (fgets(line, LINE_LEN, f) != NULL) {
		if (strstr(line, "ip:") == line) {
			char * value = strchr(line,'\t')+1;
			*strchr(value,'\n') = '\0';
			TRACE("  IP address: %s\n", value);
		} else if (strstr(line, "device:") == line) {
			char * value = strchr(line,'\t')+1;
			*strchr(value,'\n') = '\0';
			TRACE("  Network Driver: %s\n", value);
		} else if (strstr(line, "mac:") == line) {
			char * value = strchr(line,'\t')+1;
			*strchr(value,'\n') = '\0';
			TRACE("  MAC address: %s\n", value);
		} else if (strstr(line, "dns:") == line) {
			char * value = strchr(line,'\t')+1;
			*strchr(value,'\n') = '\0';
			TRACE("  DNS server: %s\n", value);
		} else if (strstr(line, "gateway:") == line) {
			char * value = strchr(line,'\t')+1;
			*strchr(value,'\n') = '\0';
			TRACE("  Gateway: %s\n", value);
		} else if (strstr(line,"no network") == line){
			network_error(0);
		}
		memset(line, 0, LINE_LEN);
	}

	fclose(f);


	struct http_req my_req;
	if (argc > 1) {
		parse_url(argv[1], &my_req);
	} else {
		parse_url(NETBOOT_URL, &my_req);
	}

	char file[100];
	sprintf(file, "/dev/net/%s:%d", my_req.domain, my_req.port);

	TRACE("Fetching from %s... ", my_req.domain);

	fetch_options.out = fopen(img,"w+");

	pthread_t watchdog;

	pthread_create(&watchdog, NULL, watchdog_func, NULL);

	f = fopen(file,"r+");
	if (!f) {
		network_error(0);
	}

	watchdog_success = 1;

	TRACE("Connection established.\n");

	fprintf(f,
		"GET /%s HTTP/1.0\r\n"
		"User-Agent: curl/7.35.0\r\n"
		"Host: %s\r\n"
		"Accept: */*\r\n"
		"\r\n", my_req.path, my_req.domain);

	gettimeofday(&start, NULL);

	hashmap_t * headers = hashmap_create(10);

	/* Parse response */
	{
		char buf[BUF_SIZE];
		read_http_line(buf, f);
		TRACE("[%s]\n", buf);

		char * elements[3];

		elements[0] = buf;
		elements[1] = strchr(elements[0], ' ');
		if (!elements[1]) bad_response();
		*elements[1] = '\0';
		elements[1]++;

		elements[2] = strchr(elements[1], ' ');
		if (!elements[2]) bad_response();
		*elements[2] = '\0';
		elements[2]++;

		if (strcmp(elements[1], "200")) {
			TRACE("Bad response code: %s\n", elements[1]);
			return 1;
		}
	}

	while (1) {
		char buf[BUF_SIZE];
		read_http_line(buf, f);

		if (!*buf) {
			TRACE("(done with headers)\n");
			break;
		}

		/* Split */
		char * name = buf;
		char * value = strstr(buf, ": ");
		if (!value) bad_response();
		*value = '\0';
		value += 2;

		hashmap_set(headers, name, strdup(value));
	}

#if 1
	TRACE("Dumping headers.\n");
	list_t * hash_keys = hashmap_keys(headers);
	foreach(_key, hash_keys) {
		char * key = (char *)_key->value;
		TRACE("[%s] = %s\n", key, (char*)hashmap_get(headers, key));
	}
	list_free(hash_keys);
	free(hash_keys);
#endif

	/* determine how many bytes we should read now */
	if (!hashmap_has(headers, "Content-Length")) {
		TRACE("Don't know how much to read.\n");
		return 1;
	}

	int bytes_to_read = atoi(hashmap_get(headers, "Content-Length"));
	size_t bytes_total = (size_t)bytes_to_read;
	size_t bytes_read = 0;

#define RBUF_SIZE 1024
	while (bytes_to_read > 0) {
		char buf[RBUF_SIZE];
		size_t r = fread(buf, 1, bytes_to_read < RBUF_SIZE ? bytes_to_read : RBUF_SIZE, f);
		fwrite(buf, 1, r, fetch_options.out);
		bytes_to_read -= r;
		bytes_read += r;
		draw_progress(bytes_total, bytes_read);
	}

	TRACE("\nDone.\n");

	fflush(fetch_options.out);
	fclose(fetch_options.out);

	TRACE("Mounting filesystem... ");
	int err = 0;
	if ((err = mount(img, "/", "ext2", 0, NULL))) {
		TRACE("Mount error: %d; errno=%d\n", err, errno);
		return 0;
	} else {
		TRACE("Done.\n");
	}

	FILE * tmp = fopen("/bin/init","r");
	if (!tmp) {
		TRACE("/bin/init missing?\n");
	} else {
		TRACE("/bin/init exists, filesystem successfully mounted\n");
		fclose(tmp);
	}

	TRACE("Executing init...\n");
	char * const _argv[] = {
		"/bin/init",
		"--migrate",
		NULL,
	};
	execve("/bin/init",_argv,NULL);

	TRACE("ERROR: If you are seeing this, there was a problem\n");
	TRACE("       executing the init binary from the downloaded\n");
	TRACE("       filesystem. This may indicate a corrupted\n");
	TRACE("       download. Please try again.\n");

	return 0;
}
