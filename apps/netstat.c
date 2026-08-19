/**
 * @brief netstat - print information on open sockets
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2026 K. Lange
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <arpa/inet.h>

static void ip_ntoa(const uint32_t src_addr, char * out) {
	snprintf(out, 16, "%d.%d.%d.%d",
		(src_addr & 0xFF000000) >> 24,
		(src_addr & 0xFF0000) >> 16,
		(src_addr & 0xFF00) >> 8,
		(src_addr & 0xFF));
}

static void parse_udp_tcp(char * which) {
	char *line = NULL;
	size_t avail = NULL;
	ssize_t len = 0;

	char path[100];
	snprintf(path, 100, "/proc/net_%s", which);

	FILE * f = fopen(path, "r");
	if (!f) return;

	while ((len = getline(&line, &avail, f)) != -1) {
		char * from = line;
		uint32_t local_addr = strtoul(from, NULL, 16);
		from = strchrnul(from,':');
		if (*from) from++;
		int local_port = strtoul(from, NULL, 16);
		from = strchrnul(from,' ');
		if (*from) from++;
		int remote_addr = strtoul(from, NULL, 16);
		from = strchrnul(from,':');
		if (*from) from++;
		int remote_port = strtoul(from, NULL, 16);

		char tmp[17];
		char local_addr_str[30];
		char remote_addr_str[30];

		ip_ntoa(ntohl(local_addr), tmp);
		snprintf(local_addr_str, 30, "%s:%d", tmp, local_port);

		ip_ntoa(ntohl(remote_addr), tmp);
		snprintf(remote_addr_str, 30, "%s:%d", tmp, remote_port);

		fprintf(stdout, "%-7s %-30s %-30s\n", which, local_addr_str, remote_addr_str);
	}

	free(line);
	fclose(f);
}

static void parse_icmp(void) {
	char *line = NULL;
	size_t avail = NULL;
	ssize_t len = 0;

	FILE * f = fopen("/proc/net_icmp", "r");
	if (!f) return;

	while ((len = getline(&line, &avail, f)) != -1) {
		char * from = line;
		uint32_t local_addr = strtoul(from, NULL, 16);
		from = strchrnul(from,':');
		if (*from) from++;
		int local_port = strtoul(from, NULL, 16);
		char tmp[17];
		char local_addr_str[30];
		ip_ntoa(ntohl(local_addr), tmp);
		snprintf(local_addr_str, 30, "%s:%d", tmp, local_port);
		fprintf(stdout, "%-7s %-30s\n", "icmp", local_addr_str);
	}

	free(line);
	fclose(f);
}

int main(int argc, char * argv[]) {
	fprintf(stdout, "%-7s %-30s %-30s\n", "Proto", "Local Address", "Remote Address");
	parse_udp_tcp("tcp");
	parse_udp_tcp("udp");
	parse_icmp();
	return 0;
}

