/**
 * @brief irc - Internet Relay Chat client
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 */
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <va_list.h>
#include <time.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/fswait.h>

#define _ITALIC "\033[3m"
#define _END    "\033[0m\n"

#define VERSION_STRING "0.3.0"

/* Theming */
#define TIME_FMT "%02d:%02d:%02d"
#define TIME_ARGS hr, min, sec

static char * nick = "toaru-user";
static char * host = NULL;
static char * pass = NULL;
static unsigned short port = 6667;

static char * channel = NULL;

static int sock_fd;
static FILE * sock_r;
static FILE * sock_w;

struct color_pair {
	int fg;
	int bg;
};

static void show_usage(int argc, char * argv[]) {
	fprintf(stderr,
			"irc - Terminal IRC client.\n"
			"\n"
			"usage: %s [-h] [-p port] [-n nick] host\n"
			"\n"
			" -p port " _ITALIC "Specify port to connect to" _END
			" -P pass " _ITALIC "Password for server connection" _END
			" -n nick " _ITALIC "Specify a nick to use" _END
			" -h      " _ITALIC "Print this help message" _END
			"\n", argv[0]);
	exit(1);
}

static struct termios old;

static void set_unbuffered() {
	tcgetattr(fileno(stdin), &old);
	struct termios new = old;
	new.c_lflag &= (~ICANON & ~ECHO);
	tcsetattr(fileno(stdin), TCSAFLUSH, &new);
}

static void set_buffered() {
	tcsetattr(fileno(stdin), TCSAFLUSH, &old);
}

static int user_color(char * user) {
	int i = 0;
	while (*user) {
		i += *user;
		user++;
	}
	i = i % 5;
	switch (i) {
		case 0: return 2;
		case 1: return 3;
		case 2: return 4;
		case 3: return 6;
		case 4: return 10;
	}
	return 0;
}

static int color_pairs[] = {
	15, 0, 4, 2, 9, 1, 5, 3, 11, 10, 6, 14, 12, 13, 8, 7
};

static struct color_pair irc_color_to_pair(int fg, int bg) {
	int _fg = 0;
	int _bg = 0;
	if (fg == -1) {
		_fg = -1;
	} else {
		_fg = color_pairs[fg % 16];
	}
	if (bg == -1) {
		_bg = -1;
	} else {
		_bg = color_pairs[bg % 16];
	}
	return (struct color_pair){_fg, _bg};
}

static void get_time(int * h, int * m, int * s) {
	time_t rawtime;
	time(&rawtime);
	struct tm *tm_struct = localtime(&rawtime);

	*h = tm_struct->tm_hour;
	*m = tm_struct->tm_min;
	*s = tm_struct->tm_sec;
}

static void print_color(struct color_pair t) {
	fprintf(stdout, "\033[");
	if (t.fg == -1) {
		fprintf(stdout,"39");
	} else if (t.fg > 15) {
		/* TODO */
	} else if (t.fg > 7) {
		fprintf(stdout,"9%d", t.fg - 8);
	} else {
		fprintf(stdout,"3%d", t.fg);
	}
	fprintf(stdout, ";");
	if (t.bg == -1) {
		fprintf(stdout, "49");
	} else if (t.bg > 15) {
		/* TODO */
	} else if (t.bg > 7) {
		fprintf(stdout,"10%d", t.bg - 8);
	} else {
		fprintf(stdout,"4%d", t.bg);
	}
	fprintf(stdout, "m");
	fflush(stdout);
}

static void WRITE(const char * fmt, ...) {

	int bold_on = 0;
	int italic_on = 0;

	va_list args;
	va_start(args, fmt);
	char * tmp;
	vasprintf(&tmp, fmt, args);
	va_end(args);

	struct winsize w;
	ioctl(0, TIOCGWINSZ, &w);
	fprintf(stdout,"\033[%d;1H\033[K", w.ws_row);

	int line_feed_pending = 0;
	char * c = tmp;

	while (*c) {
		if (*c == '\n') {
			if (line_feed_pending) {
				/* Print line feed */
				fprintf(stdout, "\n");
			}
			line_feed_pending = 1;
			c++;
			continue;
		} else {
			if (line_feed_pending) {
				line_feed_pending = 0;
				/* Print line feed */
				fprintf(stdout, "\n");
			}
		}
		if (*c == 0x03) {
			c++;
			int i = -1;
			int j = -1;
			if (*c >= '0' && *c <= '9') {
				i = (*c - '0');
				c++;
			}
			if (*c >= '0' && *c <= '9') {
				i *= 10;
				i += (*c - '0');
				c++;
			}
			if (*c == ',') {
				c++;
				if (*c >= '0' && *c <= '9') {
					j = (*c - '0');
					c++;
				}
				if (*c >= '0' && *c <= '9') {
					j *= 10;
					j = (*c - '0');
					c++;
				}
			}
			struct color_pair t = irc_color_to_pair(i, j);
			print_color(t);
			continue;
		}
		if (*c == 0x02) {
			if (bold_on) {
				fprintf(stdout,"\033[22m");
				bold_on = 0;
			} else {
				fprintf(stdout,"\033[1m");
				bold_on = 1;
			}
			c++;
			continue;
		}
		if (*c == 0x16) {
			if (italic_on) {
				fprintf(stdout,"\033[23m");
				italic_on = 0;
			} else {
				fprintf(stdout,"\033[3m");
				italic_on = 1;
			}
			c++;
			continue;
		}
		if (*c == 0x0f) {
			fprintf(stdout, "\033[0m");
			c++;
			bold_on = 0;
			italic_on = 0;
			continue;
		}

		fprintf(stdout, "%c", *c);
		c++;
	}
	if (line_feed_pending) {
		fprintf(stdout, "\033[0m\033[K\n");
	}
	fflush(stdout);
	free(tmp);
}

static void handle(char * line) {
	char * c = line;
	while (c < line + strlen(line)) {
		char * e = strstr(c, "\r\n");
		if (e > line + strlen(line)) {
			break;
		}

		if (!e) {
			/* Write c */
			WRITE(c);
			goto next;
		}

		*e = '\0';

		if (strstr(c, "PING") == c) {
			char * t = strstr(c, ":");
			fprintf(sock_w, "PONG %s\r\n", t);
			fflush(sock_w);
			goto next;
		}

		char * user, * command, * channel, * message;

		user = c;
		if (user[0] == ':') {
			user++;
		}

		command = strstr(user, " ");
		if (!command) {
			WRITE("%s\n", user);
			goto next;
		}
		command[0] = '\0';
		command++;

		channel = strstr(command, " ");
		if (!channel) {
			WRITE("%s %s\n", user, command);
			goto next;
		}
		channel[0] = '\0';
		channel++;

		message = strstr(channel, " ");
		if (message) {
			message[0] = '\0';
			message++;
			if (message[0] == ':') {
				message++;
			}
		}

		int hr, min, sec;
		get_time(&hr, &min, &sec);

		if (!strcmp(command, "PRIVMSG")) {
			if (!message) continue;
			char * t = strstr(user, "!");
			if (t) { t[0] = '\0'; }
			t = strstr(user, "@");
			if (t) { t[0] = '\0'; }

			if (strstr(message, "\001ACTION ") == message) {
				message = message + 8;
				char * x = strstr(message, "\001");
				if (x) *x = '\0';
				WRITE(TIME_FMT " \002* \003%d%s\003\002 %s\n", TIME_ARGS, user_color(user), user, message);
			} else {
				WRITE(TIME_FMT " \00314<\003%d%s\00314>\003 %s\n", TIME_ARGS, user_color(user), user, message);
			}
		} else if (!strcmp(command, "332")) {
			if (!message) {
				continue;
			}
			/* Topic */
		} else if (!strcmp(command, "JOIN")) {
			char * t = strstr(user, "!");
			if (t) { t[0] = '\0'; }
			t = strstr(user, "@");
			if (t) { t[0] = '\0'; }
			if (channel[0] == ':') { channel++; }

			WRITE(TIME_FMT " \00312-\003!\00312-\00311 %s\003 has joined \002%s\n", TIME_ARGS, user, channel);
		} else if (!strcmp(command, "PART")) {
			char * t = strstr(user, "!");
			if (t) { t[0] = '\0'; }
			t = strstr(user, "@");
			if (t) { t[0] = '\0'; }
			if (channel[0] == ':') { channel++; }

			WRITE(TIME_FMT " \00312-\003!\00312\003-\00310 %s\003 has left \002%s\n", TIME_ARGS, user, channel);
		} else if (!strcmp(command, "372")) {
			WRITE(TIME_FMT " \00314%s\003 %s\n", TIME_ARGS, user, message ? message : "");
		} else if (!strcmp(command, "376")) {
			/* End of MOTD */
			WRITE(TIME_FMT " \00314%s (end of MOTD)\n", TIME_ARGS, user);
		} else {
			WRITE(TIME_FMT " \00310%s %s %s %s\n", TIME_ARGS, user, command, channel, message ? message : "");
		}

next:
		if (!e) break;
		c = e + 2;
	}
}

static void redraw_buffer(char * buf) {
	struct winsize w;
	ioctl(0, TIOCGWINSZ, &w);

	char tmp[1024];
	size_t left = snprintf(tmp,1024," [%s] ", channel ? channel : "(status)");
	size_t avail = w.ws_col - left - 1;
	size_t buflen = strlen(buf);
	char * from = buf;

	if (buflen >= avail) {
		from = buf + (buflen - avail);
	}

	fprintf(stdout,"\033[%d;1H%s%s\033[K\033[?25h", w.ws_row, tmp, from);
	fflush(stdout);
}

void handle_input(char * buf) {
	fflush(stdout);
	if (strstr(buf, "/help") == buf) {
		WRITE("[help] help text goes here\n");
	} else if (strstr(buf, "/quit") == buf) {
		char * m = strstr(buf, " "); if (m) m++;
		fprintf(sock_w, "QUIT :%s\r\n", m ? m : "https://github.com/klange/toaruos");
		fflush(sock_w);
		fprintf(stderr,"\033[0m\n");
		set_buffered();
		exit(0);
	} else if (strstr(buf,"/part") == buf) {
		if (!channel) {
			fprintf(stderr, "Not in a channel.\n");
			return;
		}
		char * m = strstr(buf, " "); if (m) m++;
		fprintf(sock_w, "PART %s%s%s\r\n", channel, m ? " :" : "", m ? m : "");
		fflush(sock_w);
		free(channel);
		channel = NULL;
	} else if (strstr(buf,"/join ") == buf) {
		char * m = strstr(buf, " "); if (m) m++;
		fprintf(sock_w, "JOIN %s\r\n", m);
		fflush(sock_w);
		channel = strdup(m);
	} else if (strstr(buf, "/") == buf) {
		WRITE("[system] Unknown command: %s\n", buf);
	} else {
		int hr, min, sec;
		get_time(&hr, &min, &sec);
		WRITE("%02d:%02d:%02d \00314<\003\002%s\002\00314>\003 %s\n", hr, min, sec, nick, buf);
		fprintf(sock_w, "PRIVMSG %s :%s\r\n", channel, buf);
	}
	redraw_buffer("");
}

int main(int argc, char * argv[]) {

	/* Option parsing */
	int c;
	while ((c = getopt(argc, argv, "?hp:n:P:")) != -1) {
		switch (c) {
			case 'n':
				nick = optarg;
				break;
			case 'P':
				pass = optarg;
				break;
			case 'p':
				port = atoi(optarg);
				break;
			case 'h':
			case '?':
			default:
				show_usage(argc, argv);
				break;
		}
	}

	if (optind >= argc) {
		show_usage(argc, argv);
	}

	host = argv[optind];

	/* Connect */
	{
		sock_fd = socket(AF_INET, SOCK_STREAM, 0);
		fprintf(stderr, "Looking up host...\n");
		struct hostent * remote = gethostbyname(host);
		if (!remote) {
			perror("gethostbyname");
			return 1;
		}
		struct sockaddr_in addr;
		addr.sin_family = AF_INET;
		memcpy(&addr.sin_addr.s_addr, remote->h_addr, remote->h_length);
		addr.sin_port = htons(port);
		fprintf(stderr, "Connecting...\n");
		if (connect(sock_fd, (struct sockaddr*)&addr, sizeof(struct sockaddr_in)) < 0) {
			perror("connect");
			return 1;
		}
		sock_r = fdopen(sock_fd, "r");
		sock_w = fdopen(sock_fd, "w");
	}

	set_unbuffered();

	fprintf(stdout, " - Toaru IRC v %s - \n", VERSION_STRING);
	fprintf(stdout, " Copyright 2015-2018 K. Lange\n");
	fprintf(stdout, " https://toaruos.org - https://github.com/klange/toaruos\n");
	fprintf(stdout, " \n");
	fprintf(stdout, " For help, type /help\n");

	if (pass) {
		fprintf(sock_w, "PASS %s\r\n", pass);
	}
	fprintf(sock_w, "NICK %s\r\nUSER %s * 0 :%s\r\n", nick, nick, nick);
	fflush(sock_w);

	int fds[] = {sock_fd, STDIN_FILENO, sock_fd};

	char net_buf[2048];
	memset(net_buf, 0, 2048);
	int net_buf_p = 0;

	char buf[1024] = {0};
	int buf_p = 0;

	while (1) {
		int index = fswait2(2,fds,200);

		if (index == 1) {
			/* stdin */
			int c = fgetc(stdin);
			if (c < 0) {
				continue;
			}
			if (c == 0x08 || c == 0x7F) {
				/* Remove from buffer */
				if (buf_p) {
					buf[buf_p-1] = '\0';
					buf_p--;
					redraw_buffer(buf);
				}
			} else if (c == '\n') {
				/* Send buffer */
				handle_input(buf);
				memset(buf, 0, 1024);
				buf_p = 0;
			} else {
				/* Append buffer, or check special keys */
				buf[buf_p] = c;
				buf_p++;
				redraw_buffer(buf);
			}
		} else if (index == 0) {
			/* network */
			do {
				int c = fgetc(sock_r);
				if (c < 0) continue;
				net_buf[net_buf_p] = c;
				net_buf_p++;
				if (c == '\n' || net_buf_p == 2046) {
					handle(net_buf);
					net_buf_p = 0;
					memset(net_buf, 0, 2048);
					redraw_buffer(buf);
				}
			} while (!_fwouldblock(sock_r));
		} else {
			/* timer */
		}
	}

}
