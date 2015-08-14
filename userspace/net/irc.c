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
#include <string.h>
#include <time.h>
#include <fcntl.h>


#include <ncurses.h>

#include "lib/pthread.h"
#include "lib/spinlock.h"

#define _ITALIC "\033[3m"
#define _END    "\033[0m\n"

#define VERSION_STRING "0.1.0"

static char * nick = "toaru-user";
static char * host = NULL;
static unsigned short port = 6667;
static pthread_t read_thread;

static char * channel = NULL;

static WINDOW * main_win;
static WINDOW * body_win;
static WINDOW * status_win;
static WINDOW * input_win;

static FILE * sock;
static FILE * sockb;

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

#define WRITE(...) do { \
  spin_lock(&c_lock); \
  wprintw(body_win, __VA_ARGS__); \
  wrefresh(body_win); \
  spin_unlock(&c_lock); \
} while (0);

void refresh_all(void) {

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

		if (!strcmp(command, "PRIVMSG")) {
			message = strstr(channel, " ");
			if (!message) {
				WRITE("%s %s %s\n", user, command, channel);
				goto next;
			}
			message[0] = '\0';
			message++;
			if (message[0] == ':') { message++; }
			if (user[0] == ':') { user++; }
			char * t = strstr(user, "!");
			if (t) { t[0] = '\0'; }
			t = strstr(user, "@");
			if (t) { t[0] = '\0'; }
			int hr, min, sec;
			get_time(&hr, &min, &sec);

			if (strstr(message, "\001ACTION ") == message) {
				message = message + 8;
				char * x = strstr(message, "\001");
				if (x) *x = '\0';
				WRITE("%02d:%02d:%02d * %s: %s %s\n", hr, min, sec, user, channel, message);
			} else {
				WRITE("%02d:%02d:%02d <%s:%s> %s\n", hr, min, sec, user, channel, message);
			}
		} else {
			WRITE("%s %s %s\n", user, command, channel);
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
	} else if (!strcmp(thing, "/quit")) {
		endwin();
		exit(0);
	} else if (strstr(thing, "/join ") == thing) {
		char * m = strstr(thing, " ");
		m++;
		fprintf(sock, "JOIN %s\r\n", m);
		fflush(sock);
		channel = strdup(m);
	} else if (strlen(thing) > 0 && thing[0] == '/') {
		WRITE("[system] Unknown command: %s\n", thing);
	} else {
		if (!channel) {
			WRITE("[system] Not in a channel.\n");
		} else {
			int hr, min, sec;
			get_time(&hr, &min, &sec);
			WRITE("%02d:%02d:%02d <%s:%s> %s\n", hr, min, sec, nick, channel, thing);
			fprintf(sock, "PRIVMSG %s :%s\r\n", channel, thing);
			fflush(sock);
		}
	}
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
	assume_default_colors(-1,-1);
	start_color();
	init_pair(1, -1, -1);
	init_pair(2, COLOR_WHITE, COLOR_BLUE);
	init_pair(3, COLOR_WHITE, -1);

	int w, h;
	getmaxyx(main_win, h, w);

	body_win   = newwin(h-2, w, 0, 0);
	status_win = newwin(1, w, h-2, 0);
	input_win  = newwin(1, w, h-1, 0);

	scrollok(body_win, TRUE);

	wbkgd(status_win, COLOR_PAIR(1));
	wbkgd(status_win, COLOR_PAIR(2));
	wbkgd(input_win, COLOR_PAIR(3));

	/* Write the welcome thing to the body */
	wprintw(body_win, " - Toaru IRC v. %s - \n", VERSION_STRING);
	wprintw(body_win, " Copyright 2015 Kevin Lange\n");
	wprintw(body_win, " http://toaruos.org - http://github.com/klange/toaruos\n");
	wprintw(body_win, "\n");
	wprintw(body_win, " For help, type /help.\n");

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
		wclear(input_win);
		wrefresh(input_win);
	}

	endwin();

	return 0;
}

