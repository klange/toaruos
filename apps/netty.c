/**
 * @brief Provides a PTY over a reverse network socket.
 *
 * Pipes data into and out of a PTY from a TCP socket connected to a remote
 * server.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018-2021 K. Lange
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pty.h>
#include <pthread.h>
#include <sys/wait.h>
#include <sys/fswait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

int fd_master, fd_slave, fd_serial;
volatile int _stop = 0;

void * handle_in(void * unused) {
	while (!_stop) {
		int index = fswait2(1,&fd_serial,200);
		char buf[1];
		int r;
		switch (index) {
			case 0: /* fd_serial */
				r = read(fd_serial, buf, 1);
				if (r > 0) {
					write(fd_master, buf, r);
				}
				break;
		}
	}

	return NULL;
}

int main(int argc, char * argv[]) {
	char * user = NULL;

	if (getuid() != 0) {
		fprintf(stderr, "%s: only root can do that\n", argv[0]);
		return 1;
	}

	int opt;
	while ((opt = getopt(argc, argv, "a:")) != -1) {
		switch (opt) {
			case 'a':
				user = optarg;
				break;
		}
	}

	if (optind == argc) {
		fprintf(stderr, "usage: %s remote:port\n", argv[0]);
		return 1;
	}

	char * remotehost = argv[optind];
	char * colon = strstr(remotehost, ":");
	if (!colon) {
		fprintf(stderr, "usage: %s remote:port\n", argv[0]);
		return 1;
	}

	*colon = '\0'; colon++;
	int remoteport = atoi(colon);

	openpty(&fd_master, &fd_slave, NULL, NULL, NULL);

	int sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		perror("socket");
		return 1;
	}

	struct hostent * remote = gethostbyname(remotehost);

	if (!remote) {
		perror("gethostbyname");
		return 1;
	}

	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	memcpy(&addr.sin_addr.s_addr, remote->h_addr, remote->h_length);
	addr.sin_port = htons(remoteport);

	if (connect(sock, (struct sockaddr*)&addr, sizeof(struct sockaddr_in)) < 0) {
		perror("connect");
		return 1;
	}

	fd_serial = sock; //open(file, O_RDWR);
	pthread_t input_buffer_thread;
	pthread_create(&input_buffer_thread, NULL, handle_in, NULL);

	pid_t child = fork();

	if (!child) {
		setsid();
		dup2(fd_slave, 0);
		dup2(fd_slave, 1);
		dup2(fd_slave, 2);
		ioctl(STDIN_FILENO, TIOCSCTTY, &(int){1});
		tcsetpgrp(STDIN_FILENO, getpid());

		system("ttysize -q");

		char * tokens[] = {"/bin/login-loop",NULL,NULL,NULL};

		if (user) {
			tokens[1] = "-f";
			tokens[2] = user;
		}

		execvp(tokens[0], tokens);
		exit(1);
	} else {

		while (1) {
			int index = fswait2(1,&fd_master,200);
			char buf[1024];
			int r;
			switch (index) {
				case 0: /* fd_master */
					r = read(fd_master, buf, 1024);
					write(fd_serial, buf, r);
					break;
				default: /* timeout */
					{
						int result = waitpid(child, NULL, WNOHANG);
						if (result > 0) {
							/* Child login shell has returned (session ended) */
							_stop = 1;
							return 0;
						}
					}
					break;
			}

		}

	}

	return 0;
}

