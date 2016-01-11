/* vim: ts=4 sw=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2015 Kevin Lange
 *
 * irc - Curses IRC client.
 */

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <locale.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>

#include <ncurses.h>
#ifndef A_ITALIC
#define A_ITALIC A_REVERSE
#endif

#include "lib/pthread.h"
#include "lib/spinlock.h"

#define _ITALIC "\033[3m"
#define _END    "\033[0m\n"

#define VERSION_STRING "0.2.0"

static char * nick = "toaru-user";
static char * host = NULL;
static unsigned short port = 6667;
static pthread_t read_thread;

static char * channel = NULL;

static WINDOW * main_win;
static WINDOW * topic_win;
static WINDOW * body_win;
static WINDOW * status_win;
static WINDOW * input_win;

static FILE * sock;
static FILE * sockb;

#define TIME_FMT "%02d:%02d:%02d"
#define TIME_ARGS hr, min, sec

static volatile int c_lock;

void show_usage(int argc, char * argv[]) {
	fprintf(stderr,
			"irc - Curses IRC client.\n"
			"\n"
			"usage: %s [-h] [-p port] [-n nick] host\n"
			"\n"
			" -p port " _ITALIC "Specify port to connect to" _END
			" -n nick " _ITALIC "Specify a nick to use" _END
			" -h      " _ITALIC "Print this help message" _END
			"\n", argv[0]);
	exit(1);
}

int user_color(char * user) {
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

int irc_color_to_pair(int fg, int bg) {
	int _fg = 0;
	int _bg = 0;
	if (fg == -1) {
		if (bg == -1) {
			_fg = 0;
		} else {
			_fg = COLOR_WHITE;
		}
	} else {
		fg = fg % 16;
		switch (fg) {
			case 0: _fg = COLOR_WHITE + 8; break;
			case 1: _fg = COLOR_BLACK; break;
			case 2: _fg = COLOR_BLUE; break;
			case 3: _fg = COLOR_GREEN; break;
			case 4: _fg = COLOR_RED + 8; break;
			case 5: _fg = COLOR_RED; break;
			case 6: _fg = COLOR_MAGENTA; break;
			case 7: _fg = COLOR_YELLOW; break;
			case 8: _fg = COLOR_YELLOW + 8; break;
			case 9: _fg = COLOR_GREEN + 8; break;
			case 10: _fg = COLOR_CYAN; break;
			case 11: _fg = COLOR_CYAN + 8; break;
			case 12: _fg = COLOR_BLUE + 8; break;
			case 13: _fg = COLOR_MAGENTA + 8; break;
			case 14: _fg = COLOR_BLACK + 8; break;
			case 15: _fg = COLOR_WHITE; break;
		}
	}
	if (bg == -1) {
		_bg = 0;
	} else {
		bg = bg % 16;
		switch (bg) {
			case 0: _bg = COLOR_WHITE + 8; break;
			case 1: _bg = COLOR_BLACK; break;
			case 2: _bg = COLOR_BLUE; break;
			case 3: _bg = COLOR_GREEN; break;
			case 4: _bg = COLOR_RED + 8; break;
			case 5: _bg = COLOR_RED; break;
			case 6: _bg = COLOR_MAGENTA; break;
			case 7: _bg = COLOR_YELLOW; break;
			case 8: _bg = COLOR_YELLOW + 8; break;
			case 9: _bg = COLOR_GREEN + 8; break;
			case 10: _bg = COLOR_CYAN; break;
			case 11: _bg = COLOR_CYAN + 8; break;
			case 12: _bg = COLOR_BLUE + 8; break;
			case 13: _bg = COLOR_MAGENTA + 8; break;
			case 14: _bg = COLOR_BLACK + 8; break;
			case 15: _bg = COLOR_WHITE; break;
		}
	}
	return _fg + _bg * 16;
}

void WRITE(const char *fmt, ...) {

	static int line_feed_pending = 0;

	int last_color = 0xFFFFFFFF;
	int bold_on = 0;
	int italic_on = 0;

	va_list args;
	va_start(args, fmt);
	char * tmp;
	vasprintf(&tmp, fmt, args);
	va_end(args);

	spin_lock(&c_lock);
	char * c = tmp;
	while (*c) {
		if (*c == '\n') {
			if (line_feed_pending) {
				wprintw(body_win, "\n");
			}
			line_feed_pending = 1;
			c++;
			continue;
		} else {
			if (line_feed_pending) {
				line_feed_pending = 0;
				wprintw(body_win, "\n");
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
					j += (*c - '0');
					c++;
				}
			}
			int t = irc_color_to_pair(i,j);
			if (t != last_color && last_color != 0xFFFFFFFF) {
				wattroff(body_win, COLOR_PAIR(last_color));
			}
			if (i != -1) {
				wattron(body_win, COLOR_PAIR(t));
				last_color = t;
			}
			continue;
		}
		if (*c == 0x02) {
			if (bold_on) {
				wattroff(body_win, A_BOLD);
				bold_on = 0;
			} else {
				wattron(body_win, A_BOLD);
				bold_on = 1;
			}
			c++;
			continue;
		}
		if (*c == 0x16) {
			if (italic_on) {
				wattroff(body_win, A_ITALIC);
				italic_on = 0;
			} else {
				wattron(body_win, A_ITALIC);
				italic_on = 1;
			}
			c++;
			continue;
		}
		if (*c == 0x0f) {
			if (last_color != 0xFFFFFFFF) {
				wattroff(body_win, COLOR_PAIR(last_color));
				last_color = 0xFFFFFFFF;
			}
			bold_on = 0;
			wattroff(body_win, A_BOLD);
			c++;
			continue;
		}

		wprintw(body_win, "%c", *c);
		c++;
	}
	wattroff(body_win, COLOR_PAIR(last_color));
	wattroff(body_win, A_BOLD);
	wattroff(body_win, A_ITALIC);
	free(tmp);
	wrefresh(body_win);
	spin_unlock(&c_lock);
}

void redraw_status(void) {

	spin_lock(&c_lock);
	wclear(status_win);
	wmove(status_win, 0, 0);
	wprintw(status_win, "[%s] ", nick);
	spin_unlock(&c_lock);

}

void refresh_all(void) {

	wrefresh(topic_win);
	wrefresh(body_win);
	wrefresh(status_win);
	wrefresh(input_win);

}

void get_time(int * h, int * m, int * s) {
	time_t rawtime;
	time(&rawtime);
	struct tm *tm_struct = localtime(&rawtime);

	*h = tm_struct->tm_hour;
	*m = tm_struct->tm_min;
	*s = tm_struct->tm_sec;
}

void handle(char * line) {
	char * c = line;

	while (c < line + strlen(line)) {
		char * e = strstr(c, "\r\n");
		if (e > line + strlen(line)) {
			break;
		}

		if (!e) {
			WRITE(c);
			goto next;
		}

		*e = '\0';

		if (strstr(c, "PING") == c) {
			char tmp[100];
			char * t = strstr(c, ":");
			fprintf(sock, "PONG %s\r\n", t);
			fflush(sock);
			goto next;
		}

		char * user;
		char * command;
		char * channel;
		char * message;

		user = c;
		if (user[0] == ':') { user++; }

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
			if (message[0] == ':') { message++; }
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
				WRITE(TIME_FMT " * %d%s %s\n", TIME_ARGS, user_color(user), user, message);
			} else {
				WRITE(TIME_FMT " 14<%d%s14> %s\n", TIME_ARGS, user_color(user), user, message);
			}
		} else if (!strcmp(command, "332")) {
			if (!message) {
				continue;
			}
			spin_lock(&c_lock);
			wmove(topic_win, 0, 0);
			wprintw(topic_win, " %s", message);
			wrefresh(topic_win);
			spin_unlock(&c_lock);
		} else if (!strcmp(command, "JOIN")) {
			char * t = strstr(user, "!");
			if (t) { t[0] = '\0'; }
			t = strstr(user, "@");
			if (t) { t[0] = '\0'; }
			if (channel[0] == ':') { channel++; }

			WRITE(TIME_FMT " 12-!12-11 %s has joined %s\n", TIME_ARGS, user, channel);
		} else if (!strcmp(command, "PART")) {
			char * t = strstr(user, "!");
			if (t) { t[0] = '\0'; }
			t = strstr(user, "@");
			if (t) { t[0] = '\0'; }
			if (channel[0] == ':') { channel++; }

			WRITE(TIME_FMT " 12-!12-10 %s has left %s\n", TIME_ARGS, user, channel);
		} else if (!strcmp(command,"372")) {
			WRITE(TIME_FMT " 14%s %s\n", TIME_ARGS, user, message ? message : "");
		} else if (!strcmp(command,"376")) {
			WRITE(TIME_FMT " 14%s (end of MOTD)\n", TIME_ARGS, user);
		} else {
			WRITE(TIME_FMT " 10%s %s %s %s\n", TIME_ARGS, user, command, channel, message ? message : "");
		}


next:
		if (!e) break;
		c = e + 2;
	}
}

void * irc_read_thread(void * garbage) {

	char * line = malloc(1024);

	while (1) {
		memset(line, 0, 1024);
		fgets(line, 1024, sockb);
		handle(line);
	}

}

void do_thing(char * thing) {
	if (!strcmp(thing, "/help")) {
		WRITE("[help] Herp derp you asked for help, silly you, there is none!\n");
	} else if (!strcmp(thing, "/quit") || strstr(thing,"/quit ") == thing) {
		char * m = strstr(thing, " "); if (m) m++;
		endwin();
		fprintf(sock,"QUIT :%s\r\n", m ? m : "http://toaruos.org/");
		fflush(sock);
		exit(0);
	} else if (!strcmp(thing, "/part") || strstr(thing,"/part ") == thing) {
		char * m = strstr(thing, " "); if (m) m++;
		fprintf(sock,"PART %s%s%s\r\n",channel,m?" :":"",m?m:"");
		fflush(sock);
		free(channel);
	} else if (strstr(thing, "/join ") == thing) {
		char * m = strstr(thing, " ");
		m++;
		fprintf(sock, "JOIN %s\r\n", m);
		fflush(sock);
		channel = strdup(m);
	} else if (strstr(thing, "/nick ") == thing) {
		char * m = strstr(thing, " ");
		m++;
		fprintf(sock, "NICK %s\r\n", m);
		fflush(sock);
		/* We should totally free that, but whatever. */
		nick = strdup(m);
		redraw_status();
		refresh_all();
	} else if (strstr(thing, "/me ") == thing) {
		char * m = strstr(thing, " ");
		m++;
		int hr, min, sec;
		get_time(&hr, &min, &sec);
		WRITE("%02d:%02d:%02d * %s %s\n", hr, min, sec, nick, m);
		fprintf(sock, "PRIVMSG %s :\001ACTION %s\001\r\n", channel, m);
		fflush(sock);
	} else if (strstr(thing, "/quote ") == thing) {
		char * m = strstr(thing, " ");
		m++;
		fprintf(sock, "%s\r\n", m);
		fflush(sock);
	} else if (strlen(thing) > 0 && thing[0] == '/') {
		WRITE("[system] Unknown command: %s\n", thing);
	} else if (strlen(thing) > 0) {
		if (!channel) {
			WRITE("[system] Not in a channel.\n");
		} else {
			int hr, min, sec;
			get_time(&hr, &min, &sec);
			WRITE("%02d:%02d:%02d 14<%s14> %s\n", hr, min, sec, nick, thing);
			fprintf(sock, "PRIVMSG %s :%s\r\n", channel, thing);
			fflush(sock);
		}
	}
}

void SIGWINCH_handler(int sig) {
	(void)sig;

	spin_lock(&c_lock);

	endwin();

	refresh();
	clear();

	int w = COLS;
	int h = LINES;

	/* Move */
	mvwin(topic_win,0,0);
	mvwin(body_win,1,0);
	mvwin(status_win,h-2,0);
	mvwin(input_win,h-1,0);

	/* Resize */
	wresize(topic_win,1,w);
	wresize(body_win,h-3,w);
	wresize(status_win,1,w);
	wresize(input_win,1,w);

	refresh_all();

	spin_unlock(&c_lock);
}


int main(int argc, char * argv[]) {

	int c;

	while ((c = getopt(argc, argv, "hp:n:")) != -1) {
		switch (c) {

			case 'n':
				nick = optarg;
				break;

			case 'p':
				port = atoi(optarg);
				break;

			case 'h':
			default:
				show_usage(argc,argv);
		}
	}

	if (optind >= argc) {
		show_usage(argc,argv);
	}

	setlocale (LC_ALL, "");

	host = argv[optind];

	char tmphost[512];
	sprintf(tmphost, "/dev/net/%s:%d", host, port);
	int sockfd = open(tmphost, O_RDWR);
	sock = fdopen(sockfd, "w");
	sockb = fdopen(sockfd, "r");

	if (!sock) {
		fprintf(stderr, "%s: Connection failed or network not available.\n", argv[0]);
		return 1;
	}

	main_win = initscr();
	start_color();
	use_default_colors();
	assume_default_colors(-1,-1);

	for (int bg = 1; bg < 16; ++bg) {
		for (int fg = 0; fg < 16; ++fg) {
			init_pair(fg+bg*16, fg, bg);
		}
	}

	for (int fg = 1; fg < 16; ++fg) {
		init_pair(fg, fg, -1);
	}


	int w, h;
	getmaxyx(main_win, h, w);

	topic_win  = newwin(1, w, 0, 0);
	body_win   = newwin(h-3, w, 1, 0);
	status_win = newwin(1, w, h-2, 0);
	input_win  = newwin(1, w, h-1, 0);

	signal(SIGWINCH, SIGWINCH_handler);

	scrollok(body_win, TRUE);

	wbkgd(topic_win, COLOR_PAIR(COLOR_WHITE+COLOR_BLUE*16));
	wbkgd(body_win, COLOR_PAIR(0));
	wbkgd(status_win, COLOR_PAIR(COLOR_WHITE+COLOR_BLUE*16));
	wbkgd(input_win, COLOR_PAIR(0));

	/* Write the welcome thing to the body */
	wprintw(body_win, " - Toaru IRC v. %s - \n", VERSION_STRING);
	wprintw(body_win, " Copyright 2015 Kevin Lange\n");
	wprintw(body_win, " http://toaruos.org - http://github.com/klange/toaruos\n");
	wprintw(body_win, "\n");
	wprintw(body_win, " For help, type /help.\n");

	wmove(topic_win, 0, 0);
	wprintw(topic_win, " Toaru IRC v. %s", VERSION_STRING);

	/* Update status */
	wmove(status_win, 0, 0);
	wprintw(status_win, "[%s] ", nick);

	refresh_all();

	pthread_create(&read_thread, NULL, irc_read_thread, NULL);

	fprintf(sock, "NICK %s\r\nUSER %s * 0 :%s\r\n", nick, nick, nick);
	fflush(sock);

	char * buf = malloc(1024);

	while (1) {
		spin_lock(&c_lock);
		wmove(input_win, 0, 0);
		wprintw(input_win, "[%s] ", channel ? channel : "(none)");
		memset(buf, 0, sizeof(buf));
		spin_unlock(&c_lock);
		wgetstr(input_win, buf);

		do_thing(buf);

		spin_lock(&c_lock);
		wclear(input_win);
		wrefresh(input_win);
		spin_unlock(&c_lock);
	}

	endwin();

	return 0;
}

