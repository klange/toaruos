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

static unsigned int crctab[256] = {
	0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f,
	0xe963a535, 0x9e6495a3,	0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
	0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2,
	0xf3b97148, 0x84be41de,	0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
	0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec,	0x14015c4f, 0x63066cd9,
	0xfa0f3d63, 0x8d080df5,	0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
	0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,	0x35b5a8fa, 0x42b2986c,
	0xdbbbc9d6, 0xacbcf940,	0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
	0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423,
	0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
	0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d,	0x76dc4190, 0x01db7106,
	0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
	0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d,
	0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
	0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
	0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
	0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7,
	0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
	0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa,
	0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
	0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81,
	0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
	0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84,
	0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
	0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
	0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
	0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e,
	0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
	0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55,
	0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
	0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28,
	0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
	0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f,
	0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
	0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
	0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
	0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69,
	0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
	0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc,
	0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
	0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693,
	0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
	0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
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
