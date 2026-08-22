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
#include <getopt.h>
#include <arpa/inet.h>
#include <toaru/hashmap.h>
#include <toaru/procfs.h>

static int usage(char * argv[]) {
	fprintf(stderr,
		"usage: %s [--tcp|-t] [--udp|-u] [--icmp|-I] [--pex]\n",
		argv[0]);
	return 1;
}

#define PROTO_UDP  (1 << 0)
#define PROTO_TCP  (1 << 1)
#define PROTO_ICMP (1 << 2)
#define PROTO_PEX  (1 << 3)

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

static void free_val(void *val) {
	hashmap_entry_t * _val = val;
	free(_val->value);
	free(_val);
}

static void parse_pex(void) {
	char *line = NULL;
	size_t avail = NULL;
	ssize_t len = 0;

	FILE * f = fopen("/proc/net_pex", "r");
	if (!f) return;

	hashmap_t * map = hashmap_create_int(10);
	map->hash_val_free = free_val;

	while ((len = getline(&line, &avail, f)) != -1) {
		char * from = line;

		char local_addr_str[30];
		char remote_addr_str[30];

		if (*from == 's') {
			from += 2;
			char * name = from;
			from = strchrnul(from, ' ');
			*from = '\0';
			uintptr_t local_addr = strtoul(from + 1, NULL, 10);

			snprintf(local_addr_str, 30, "%s(%zu)", name, local_addr);
			snprintf(remote_addr_str, 30, "-");

			hashmap_set(map, (void*)local_addr, strdup(name));
		} else if (*from == 'c') {
			from += 2;
			uintptr_t local_addr = strtoul(from, NULL, 10);
			from = strchrnul(from, ' ');
			if (from) from++;
			uintptr_t remote_addr = strtoul(from, NULL, 10);
			from = strchrnul(from, ' ');
			if (from) from++;
			//uintptr_t owner = strtoul(from, NULL, 10);
			from = strchrnul(from, ' ');
			if (from) from++;
			uintptr_t pid = strtoul(from, NULL, 10);

			struct process * proc = procfs_get_pid(pid, 0);
			if (proc) {
				snprintf(local_addr_str, 30, "%zu,%s(%zd)", local_addr, proc->name, pid);
				procfs_free(proc);
			} else {
				snprintf(local_addr_str, 30, "%zu,%zd", local_addr, pid);
			}

			char * maybe = hashmap_get(map, (void*)remote_addr);
			if (maybe) {
				snprintf(remote_addr_str, 30, "%s(%zu)", maybe, remote_addr);
			} else {
				snprintf(remote_addr_str, 30, "%zu", remote_addr);
			}
		} else {
			continue;
		}

		fprintf(stdout, "%-7s %-30s %-30s\n", "pex", local_addr_str, remote_addr_str);
	}

	hashmap_free(map);
	free(map);

	free(line);
	fclose(f);
}

int main(int argc, char * argv[]) {
	int show_protos = 0;

	struct option long_opts[] = {
		{"tcp",   no_argument, 0, 't'},
		{"udp",   no_argument, 0, 'u'},
		{"icmp",  no_argument, 0, 'I'},
		{"pex",   no_argument, 0, 1000},
		{"help",  no_argument, 0, 'h'},
		{0,0,0,0},
	};

	int opt;
	while ((opt = getopt_long(argc, argv, "tuIh", long_opts, NULL)) != -1) {
		switch (opt) {
			case 't':
				show_protos |= PROTO_TCP;
				break;
			case 'u':
				show_protos |= PROTO_UDP;
				break;
			case 'I':
				show_protos |= PROTO_ICMP;
				break;
			case 1000:
				show_protos |= PROTO_PEX;
				break;
			case 'h':
				return usage(argv), 0;
			default:
				return usage(argv);
		}
	}

	if (optind != argc) return usage(argv);

	if (!show_protos) show_protos = ~0;

	fprintf(stdout, "%-7s %-30s %-30s\n", "Proto", "Local Address", "Remote Address");
	if (show_protos & PROTO_TCP)  parse_udp_tcp("tcp");
	if (show_protos & PROTO_UDP)  parse_udp_tcp("udp");
	if (show_protos & PROTO_ICMP) parse_icmp();
	if (show_protos & PROTO_PEX)  parse_pex();

	return 0;
}

