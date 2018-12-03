/* vim: ts=4 sw=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2015-2017 K. Lange
 *
 * netinit
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

#define DEFAULT_URL "http://10.0.2.1:8080/netboot.img"

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

static int has_video = 0;
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

static unsigned short * textmemptr = (unsigned short *)0xB8000;
static void placech(unsigned char c, int x, int y, int attr) {
	unsigned short *where;
	unsigned att = attr << 8;
	where = textmemptr + (y * 80 + x);
	*where = c | att;
}


#define LEFT_PAD 40
static int x = LEFT_PAD;
static int y = 0;
static int vx = 0;
static int vy = 0;
static void print_string(char * msg) {
	if (!has_video)  {
		while (*msg) {
			placech(' ',vx,vy,0);
			switch (*msg) {
				case '\n':
					vx = 0;
					vy += 1;
					if (vy == 25) {
						/* scroll */
						memcpy(textmemptr,textmemptr + 80,sizeof(unsigned short) * 80 * 24);
						memset(textmemptr + 80 * 24, 0, sizeof(unsigned short) * 80);
						vy = 24;
					}
					break;
				case '\033':
					msg++;
					if (*msg == '[') {
						msg++;
						if (*msg == 'G') {
							vx = 0;
						}
						if (*msg == 'K') {
							int last_x = vx;
							while (vx < 80) {
								placech(' ',vx,vy,0);
								vx++;
							}
							vx = last_x;
						}
					}
					break;
				default:
					placech(*msg,vx,vy,0xF);
					vx++;
					break;
			}
			placech('_',vx,vy,0xF);
			msg++;
		}
	} else {
		while (*msg) {
			write_char(x,y,' ',BG_COLOR);
			switch (*msg) {
				case '\n':
					x = LEFT_PAD;
					y += char_height;
					if (y > height - 30) {
						y = 0;
					}
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
	TRACE("\n\n");
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
	struct timeval now;
	gettimeofday(&now, NULL);

	TRACE("\033[G%6dkB",(int)size/1024);
	if (content_length) {
		int percent = (size * BAR_WIDTH) / (content_length);
		TRACE(" / %6dkB [%.*s%.*s]", (int)content_length/1024, percent,bar_perc,BAR_WIDTH-percent,bar_spac);
	}
	double timediff = (double)(now.tv_sec - start.tv_sec) + (double)(now.tv_usec - start.tv_usec)/1000000.0;
	if (timediff > 0.0) {
		double rate = (double)(size) / timediff;
		double s = rate/(1024.0) * 8.0;
		if (s > 1024.0) {
			TRACE(" %.2f mbps", s/1024.0);
		} else {
			TRACE(" %.2f kbps", s);
		}

		if (content_length) {
			if (rate > 0.0) {
				double remaining = (double)(content_length - size) / rate;

				TRACE(" (%.2f sec remaining)", remaining);
			}
		}
	}
	TRACE("\033[K");
}

static uint32_t crctab[256] = {
    0x00000000, 0x09073096, 0x120e612c, 0x1b0951ba,
    0xff6dc419, 0xf66af48f, 0xed63a535, 0xe46495a3,
    0xfedb8832, 0xf7dcb8a4, 0xecd5e91e, 0xe5d2d988,
    0x01b64c2b, 0x08b17cbd, 0x13b82d07, 0x1abf1d91,
    0xfdb71064, 0xf4b020f2, 0xefb97148, 0xe6be41de,
    0x02dad47d, 0x0bdde4eb, 0x10d4b551, 0x19d385c7,
    0x036c9856, 0x0a6ba8c0, 0x1162f97a, 0x1865c9ec,
    0xfc015c4f, 0xf5066cd9, 0xee0f3d63, 0xe7080df5,
    0xfb6e20c8, 0xf269105e, 0xe96041e4, 0xe0677172,
    0x0403e4d1, 0x0d04d447, 0x160d85fd, 0x1f0ab56b,
    0x05b5a8fa, 0x0cb2986c, 0x17bbc9d6, 0x1ebcf940,
    0xfad86ce3, 0xf3df5c75, 0xe8d60dcf, 0xe1d13d59,
    0x06d930ac, 0x0fde003a, 0x14d75180, 0x1dd06116,
    0xf9b4f4b5, 0xf0b3c423, 0xebba9599, 0xe2bda50f,
    0xf802b89e, 0xf1058808, 0xea0cd9b2, 0xe30be924,
    0x076f7c87, 0x0e684c11, 0x15611dab, 0x1c662d3d,
    0xf6dc4190, 0xffdb7106, 0xe4d220bc, 0xedd5102a,
    0x09b18589, 0x00b6b51f, 0x1bbfe4a5, 0x12b8d433,
    0x0807c9a2, 0x0100f934, 0x1a09a88e, 0x130e9818,
    0xf76a0dbb, 0xfe6d3d2d, 0xe5646c97, 0xec635c01,
    0x0b6b51f4, 0x026c6162, 0x196530d8, 0x1062004e,
    0xf40695ed, 0xfd01a57b, 0xe608f4c1, 0xef0fc457,
    0xf5b0d9c6, 0xfcb7e950, 0xe7beb8ea, 0xeeb9887c,
    0x0add1ddf, 0x03da2d49, 0x18d37cf3, 0x11d44c65,
    0x0db26158, 0x04b551ce, 0x1fbc0074, 0x16bb30e2,
    0xf2dfa541, 0xfbd895d7, 0xe0d1c46d, 0xe9d6f4fb,
    0xf369e96a, 0xfa6ed9fc, 0xe1678846, 0xe860b8d0,
    0x0c042d73, 0x05031de5, 0x1e0a4c5f, 0x170d7cc9,
    0xf005713c, 0xf90241aa, 0xe20b1010, 0xeb0c2086,
    0x0f68b525, 0x066f85b3, 0x1d66d409, 0x1461e49f,
    0x0edef90e, 0x07d9c998, 0x1cd09822, 0x15d7a8b4,
    0xf1b33d17, 0xf8b40d81, 0xe3bd5c3b, 0xeaba6cad,
    0xedb88320, 0xe4bfb3b6, 0xffb6e20c, 0xf6b1d29a,
    0x12d54739, 0x1bd277af, 0x00db2615, 0x09dc1683,
    0x13630b12, 0x1a643b84, 0x016d6a3e, 0x086a5aa8,
    0xec0ecf0b, 0xe509ff9d, 0xfe00ae27, 0xf7079eb1,
    0x100f9344, 0x1908a3d2, 0x0201f268, 0x0b06c2fe,
    0xef62575d, 0xe66567cb, 0xfd6c3671, 0xf46b06e7,
    0xeed41b76, 0xe7d32be0, 0xfcda7a5a, 0xf5dd4acc,
    0x11b9df6f, 0x18beeff9, 0x03b7be43, 0x0ab08ed5,
    0x16d6a3e8, 0x1fd1937e, 0x04d8c2c4, 0x0ddff252,
    0xe9bb67f1, 0xe0bc5767, 0xfbb506dd, 0xf2b2364b,
    0xe80d2bda, 0xe10a1b4c, 0xfa034af6, 0xf3047a60,
    0x1760efc3, 0x1e67df55, 0x056e8eef, 0x0c69be79,
    0xeb61b38c, 0xe266831a, 0xf96fd2a0, 0xf068e236,
    0x140c7795, 0x1d0b4703, 0x060216b9, 0x0f05262f,
    0x15ba3bbe, 0x1cbd0b28, 0x07b45a92, 0x0eb36a04,
    0xead7ffa7, 0xe3d0cf31, 0xf8d99e8b, 0xf1deae1d,
    0x1b64c2b0, 0x1263f226, 0x096aa39c, 0x006d930a,
    0xe40906a9, 0xed0e363f, 0xf6076785, 0xff005713,
    0xe5bf4a82, 0xecb87a14, 0xf7b12bae, 0xfeb61b38,
    0x1ad28e9b, 0x13d5be0d, 0x08dcefb7, 0x01dbdf21,
    0xe6d3d2d4, 0xefd4e242, 0xf4ddb3f8, 0xfdda836e,
    0x19be16cd, 0x10b9265b, 0x0bb077e1, 0x02b74777,
    0x18085ae6, 0x110f6a70, 0x0a063bca, 0x03010b5c,
    0xe7659eff, 0xee62ae69, 0xf56bffd3, 0xfc6ccf45,
    0xe00ae278, 0xe90dd2ee, 0xf2048354, 0xfb03b3c2,
    0x1f672661, 0x166016f7, 0x0d69474d, 0x046e77db,
    0x1ed16a4a, 0x17d65adc, 0x0cdf0b66, 0x05d83bf0,
    0xe1bcae53, 0xe8bb9ec5, 0xf3b2cf7f, 0xfab5ffe9,
    0x1dbdf21c, 0x14bac28a, 0x0fb39330, 0x06b4a3a6,
    0xe2d03605, 0xebd70693, 0xf0de5729, 0xf9d967bf,
    0xe3667a2e, 0xea614ab8, 0xf1681b02, 0xf86f2b94,
    0x1c0bbe37, 0x150c8ea1, 0x0e05df1b, 0x0702ef8d,
  };

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

	mount("x", "/tmp", "tmpfs", 0, NULL);

	int tmpfd = open("/proc/framebuffer", O_RDONLY);
	if (tmpfd < 0) {
		has_video = 0;
		memset(textmemptr, 0, sizeof(unsigned short) * 80 * 25);
	} else {
		has_video = 1;
		framebuffer_fd = open("/dev/fb0", O_RDONLY);
		update_video(0);
		signal(SIGWINEVENT, update_video);
	}

	TRACE("\n\nToaruOS Netinit Host\n\n");

	TRACE("ToaruOS is free software under the NCSA / University of Illinois license.\n");
	TRACE("   https://toaruos.org/   https://git.toaruos.org/klange/toaruos\n\n");

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

	TRACE(" Netinit binary was built with: %s\n", COMPILER_VERSION);

	TRACE("\n");

	if (has_video) {
		TRACE("Display is %dx%d (%d bpp), framebuffer at 0x%x\n", width, height, depth, (unsigned int)framebuffer);
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
	/* TODO: Extract URL from kcmdline */
	if (argc < 2) {
		parse_url(DEFAULT_URL, &my_req);
	} else {
		parse_url(argv[1], &my_req);
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

#if 0
	char * vbuf = malloc(10240);
	setvbuf(f,vbuf,_IOLBF,10240);
#endif

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

#define RBUF_SIZE 10240
	char * buf = malloc(RBUF_SIZE);
	uint32_t crc32 = 0xffffffff;
	while (bytes_to_read > 0) {
		size_t r = fread(buf, 1, bytes_to_read < RBUF_SIZE ? bytes_to_read : RBUF_SIZE, f);
		fwrite(buf, 1, r, fetch_options.out);
		for (size_t i = 0; i < r; ++i) {
			int ind = (crc32 ^ buf[i]) & 0xFF;
			crc32 = (crc32 >> 8) ^ (crctab[ind]);
		}
		bytes_to_read -= r;
		bytes_read += r;
		draw_progress(bytes_total, bytes_read);
	}
	crc32 ^= 0xffffffff;
	free(buf);

	TRACE("\nDone: 0x%x\n", (unsigned int)crc32);

	fflush(fetch_options.out);
	fclose(fetch_options.out);

#if 0
	FILE * xtmp = fopen(img, "r");
	crc32 = 0xffffffff;
	int tab = 0;
	size_t bytesread = 0;
	while (!feof(xtmp)) {
		uint8_t buf[1024];
		size_t r = fread(buf, 1, 1024, xtmp);
		for (size_t i = 0; i < r; ++i) {
			if (tab & 0x01) {
				TRACE("%2x ", (unsigned char)buf[i]);
			} else {
				TRACE("%2x", (unsigned char)buf[i]);
			}
			tab++;
			if (tab == 32){
				tab = 0;
				TRACE("\n");
			}
			int ind = (crc32 ^ buf[i]) & 0xFF;
			crc32 = (crc32 >> 8) ^ (crctab[ind]);
		}
		bytesread += r;
	}
	crc32 ^= 0xffffffff;
	TRACE("\nDisk crc32: 0x%x (%d)\n", (unsigned int)crc32, bytesread);
#endif

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
		NULL,
	};
	execve("/bin/init",_argv,NULL);

	TRACE("ERROR: If you are seeing this, there was a problem\n");
	TRACE("       executing the init binary from the downloaded\n");
	TRACE("       filesystem. This may indicate a corrupted\n");
	TRACE("       download. Please try again.\n");

	return 0;
}
