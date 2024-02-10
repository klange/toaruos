/**
 * @brief Send ICMP pings
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021 K. Lange
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <time.h>
#include <termios.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#define BYTES_TO_SEND 64

struct ICMP_Header {
	uint8_t type, code;
	uint16_t checksum;
	uint16_t identifier;
	uint16_t sequence_number;
	uint8_t payload[];
};

static unsigned long clocktime(void) {
	struct timeval tv;
	gettimeofday(&tv,NULL);
	return tv.tv_sec * 1000000 + tv.tv_usec;
}

static uint16_t icmp_checksum(char * payload, size_t len) {
	uint32_t sum = 0;
	uint16_t * s = (uint16_t *)payload;
	for (size_t i = 0; i < (len) / 2; ++i) {
		sum += ntohs(s[i]);
	}
	if (sum > 0xFFFF) {
		sum = (sum >> 16) + (sum & 0xFFFF);
	}
	return ~(sum & 0xFFFF) & 0xFFFF;
}

static int break_from_loop = 0;

static void sig_break_loop(int sig) {
	(void)sig;
	break_from_loop = 1;
}

int main(int argc, char * argv[]) {
	if (argc < 2) return 1;

	int pings_sent = 0;

	struct hostent * host = gethostbyname(argv[1]);

	if (!host) {
		fprintf(stderr, "%s: not found\n", argv[1]);
		return 1;
	}

	char * addr = inet_ntoa(*(struct in_addr*)host->h_addr_list[0]);

	int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);

	if (sock < 0) {
		fprintf(stderr, "%s: No socket: %s\n", argv[1], strerror(errno));
		return 1;
	}

	int yes = 1;
	setsockopt(sock, IPPROTO_IP, IP_RECVTTL, &yes, sizeof(int));

	signal(SIGINT, sig_break_loop);

	struct sockaddr_in dest;
	dest.sin_family = AF_INET;
	memcpy(&dest.sin_addr.s_addr, host->h_addr, host->h_length);

	printf("PING %s (%s) %d data bytes\n", argv[1], addr, BYTES_TO_SEND - 8);

	struct ICMP_Header * ping = malloc(BYTES_TO_SEND);
	ping->type = 8; /* request */
	ping->code = 0;
	ping->identifier = 0;
	ping->sequence_number = 0;
	/* Fill in data */
	for (int i = 0; i < BYTES_TO_SEND - 8; ++i) {
		ping->payload[i] = i;
	}

	int responses_received = 0;

	while (!break_from_loop) {
		ping->sequence_number = htons(pings_sent+1);
		ping->checksum = 0;
		ping->checksum = htons(icmp_checksum((void*)ping, BYTES_TO_SEND));

		/* Send it and wait */
		unsigned long sent_at = clocktime();
		if (sendto(sock, (void*)ping, BYTES_TO_SEND, 0, (struct sockaddr*)&dest, sizeof(struct sockaddr_in)) < 0) {
			fprintf(stderr, "sendto: %s\n", strerror(errno));
		}

		pings_sent++;

		struct pollfd fds[1];
		fds[0].fd = sock;
		fds[0].events = POLLIN;
		int ret = poll(fds,1,1000);

		if (ret > 0) {
			char data[4096];
			char control[4096];
			struct sockaddr_in source;
			socklen_t source_size = sizeof(struct sockaddr_in);
			struct iovec _iovec = {
				data, 4096
			};
			struct msghdr msg = {
				&source,
				source_size,
				&_iovec,
				1,
				control,
				4096,
				0
			};
			ssize_t len = recvmsg(sock, &msg, 0);
			unsigned long rcvd_at = clocktime();
			if (len > 0) {
				/* Is it actually a PING response ? */

				struct ICMP_Header * icmp = (void*)data;
				unsigned char ttl = 0;

				if (msg.msg_controllen) {
					char * control_msg = control;
					while (control_msg - control + sizeof(struct cmsghdr) <= msg.msg_controllen) {
						struct cmsghdr * cmsg = (void*)control_msg;
						if (cmsg->cmsg_level == IPPROTO_IP && (cmsg->cmsg_type == IP_RECVTTL || cmsg->cmsg_type == IP_TTL)) {
							memcpy(&ttl, CMSG_DATA(cmsg), 1);
							break;
						}
						control_msg += cmsg->cmsg_len;
					}
				}

				if (icmp->type == 0) {
					/* How much data, minus the header? */
					/* Get the address */
					char * from = inet_ntoa(source.sin_addr);
					int time_taken = (rcvd_at - sent_at);
					printf("%zd bytes from %s: icmp_seq=%d ttl=%d time=%d",
						len, from, ntohs(icmp->sequence_number), (unsigned char)ttl,
						time_taken / 1000);
					if (time_taken < 1000) {
						printf(".%03d", time_taken % 1000);
					} else if (time_taken < 10000) {
						printf(".%02d", (time_taken / 10) % 100);
					} else if (time_taken < 100000) {
						printf(".%01d", (time_taken / 100) % 10);
					}
					printf(" ms\n");
					responses_received++;
				}

			}
		}

		if (!break_from_loop) {
			sleep(1);
		}
	}

	printf("--- %s statistics ---\n", argv[1]);
	printf("%d packets transmitted, %d received, %d%% packet loss\n",
		pings_sent, responses_received, 100*(pings_sent-responses_received)/pings_sent);


	return 0;
}
