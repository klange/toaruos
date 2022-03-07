/**
 * @file  apps/ifconfig.c
 * @brief Network interface configuration tool.
 *
 * Manipulates and enumerates network interfaces.
 *
 * All of the APIs used in this tool are temporary and subject to change.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021 K. Lange
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <netinet/in.h>

extern char * _argv_0;

static void ip_ntoa(const uint32_t src_addr, char * out) {
	snprintf(out, 16, "%d.%d.%d.%d",
		(src_addr & 0xFF000000) >> 24,
		(src_addr & 0xFF0000) >> 16,
		(src_addr & 0xFF00) >> 8,
		(src_addr & 0xFF));
}

static char * flagsToStr(uint32_t flags) {
	static char out[1024] = {0};
	char * o = out;

#define FLAG(f) if (flags & IFF_ ## f) { \
	if (o != out) o += sprintf(o,",");          \
	o += sprintf(o,"%s",#f); }                  \

	FLAG(UP)
	FLAG(BROADCAST)
	FLAG(DEBUG)
	FLAG(LOOPBACK)
	FLAG(RUNNING)
	FLAG(MULTICAST)

	return out;
}

static int print_human_readable_size(char * _out, size_t s) {
	size_t count = 5;
	char * prefix = "PTGMK";
	for (; count > 0 && *prefix; count--, prefix++) {
		size_t base = 1UL << (count * 10);
		if (s >= base) {
			size_t t = s / base;
			return sprintf(_out, "%zu.%1zu %cB", t, (s - t * base) / (base / 10), *prefix);
		}
	}
	return sprintf(_out, "%d B", (int)s);
}

static int open_netdev(const char * if_name) {
	char if_path[100];
	snprintf(if_path, 100, "/dev/net/%s", if_name);
	return open(if_path, O_RDONLY);
}

static int print_interface(const char * if_name) {
	int netdev = open_netdev(if_name);

	if (netdev < 0) {
		perror(_argv_0);
		return 1;
	}

	uint32_t flags = 0;
	ioctl(netdev, SIOCGIFFLAGS, &flags);
	uint32_t mtu = 0;
	ioctl(netdev, SIOCGIFMTU, &mtu);

	fprintf(stdout,"%s: flags=%d<%s> mtu %d\n", if_name, flags, flagsToStr(flags), mtu);

	/* Get IPv4 address */
	uint32_t ip_addr = 0;
	if (!ioctl(netdev, SIOCGIFADDR, &ip_addr)) {
		char ip_str[16];
		ip_ntoa(ntohl(ip_addr), ip_str);
		fprintf(stdout,"        inet %s", ip_str);
		/* Netmask ? */
		uint32_t netmask = 0;
		if (!ioctl(netdev, SIOCGIFNETMASK, &netmask)) {
			ip_ntoa(ntohl(netmask), ip_str);
			fprintf(stdout, "  netmask %s", ip_str);

			uint32_t bcast = (ip_addr & netmask) | (~netmask);
			ip_ntoa(ntohl(bcast), ip_str);
			fprintf(stdout, "  broadcast %s", ip_str);
		}
		fprintf(stdout,"\n");
	}

	uint8_t ip6_addr[16];
	if (!ioctl(netdev, SIOCGIFADDR6, &ip6_addr)) {
		/* TODO inet6 address to nice string */
		fprintf(stdout,"        inet6 %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
			ip6_addr[0], ip6_addr[1], ip6_addr[2], ip6_addr[3],
			ip6_addr[4], ip6_addr[5], ip6_addr[6], ip6_addr[7],
			ip6_addr[8], ip6_addr[9], ip6_addr[10], ip6_addr[11],
			ip6_addr[12], ip6_addr[13], ip6_addr[14], ip6_addr[15]);
	}

	/* Get ethernet address */
	uint8_t mac_addr[6];
	if (!ioctl(netdev, SIOCGIFHWADDR, &mac_addr)) {
		fprintf(stdout,"        ether %02x:%02x:%02x:%02x:%02x:%02x\n",
			mac_addr[0], mac_addr[1], mac_addr[2],
			mac_addr[3], mac_addr[4], mac_addr[5]);
	}

	netif_counters_t counts;
	if (!ioctl(netdev, SIOCGIFCOUNTS, &counts)) {
		char _buf[100];
		print_human_readable_size(_buf, counts.rx_bytes);
		fprintf(stdout,"        RX packets %zu  bytes %zu (%s)\n", counts.rx_count, counts.rx_bytes, _buf);
		print_human_readable_size(_buf, counts.tx_bytes);
		fprintf(stdout,"        TX packets %zu  bytes %zu (%s)\n", counts.tx_count, counts.tx_bytes, _buf);
	}

	/* TODO stats */

	fprintf(stdout,"\n");

	return 0;
}

static int print_all_interfaces(void) {
	int retval = 0;

	/* Read /dev/net for interfaces */
	DIR * d = opendir("/dev/net");
	if (!d) {
		fprintf(stderr, "%s: no network?\n", _argv_0);
		return 1;
	}

	struct dirent * ent;
	while ((ent = readdir(d))) {
		if (ent->d_name[0] == '.') continue;

		/* Retrieve data for the interface and print the results. */
		if (print_interface(ent->d_name)) {
			retval = 1;
		}
	}

	closedir(d);
	return retval;
}

static int maybe_address(const char * s) {
	/* Our inet_addr is not great, let's help it a little... */
	int dots = 0;
	for (;*s;s++) {
		if (*s == '.') {
			dots++;
			if (dots > 3) return 0;
			continue;
		}
		if (!isdigit(*s)) return 0;
	}

	return dots == 3;
}

static int parse_address(const char * cmd, const char * addr, in_addr_t * out) {
	if (!addr) {
		fprintf(stderr, "%s: %s: expected argument\n", _argv_0, cmd);
		return 1;
	}
	if (!maybe_address(addr)) {
		fprintf(stderr, "%s: %s: '%s' doesn't look like a valid address\n", _argv_0, cmd, addr);
		return 1;
	}

	*out = inet_addr(addr);
	return 0;
}

static int _set_address(int netdev, const char * cmd, const char * arg, const char * ioctlstr, unsigned long ioctltype) {
	int status;
	in_addr_t ip;
	if ((status = parse_address(cmd, arg, &ip))) return status;
	if ((status = ioctl(netdev, ioctltype, &ip))) { perror(ioctlstr); return status; }
	return 0;
}

#define set_address(cmd, arg, itype) _set_address(netdev, cmd, arg, #itype, itype)
#define command_with_address(cmd, itype) if (!strcmp(argv[i], cmd)) { if (_set_address(netdev, argv[i], argv[i+1], #itype, itype)) { return 1; } continue; }

int main(int argc, char * argv[]) {
	/* Figure out what we're trying to do. */
	if (argc < 2) return print_all_interfaces();

	/* Handle (ignore) some common commands */
	if (!strcmp(argv[1], "up") || !strcmp(argv[1],"down")) {
		fprintf(stderr, "%s: 'up' and 'down' commands are unsupported\n", argv[0]);
		return 1;
	}

	/* If there is an interface name and nothing else, print and be done with it. */
	if (argc == 2) return print_interface(argv[1]);

	/* All other options here require a leading interface. */
	int netdev = open_netdev(argv[1]);

	if (netdev < 0) {
		perror(argv[0]);
		return 1;
	}

	/* Now let's figure out what we want to do with remaining options */
	int collected_address = 0;

	for (int i = 2; i < argc; ++i) {
		/* Is this argument an address? */
		if (maybe_address(argv[i])) {
			/* Try to set IPv4 address */
			if (collected_address) {
				fprintf(stderr, "%s: expected at most one bare address, but found a second\n", argv[0]);
				return 1;
			}
			if (set_address("inet", argv[i], SIOCSIFADDR)) return 1;
			collected_address = 1;
		} else {
			command_with_address("netmask", SIOCSIFNETMASK);
			command_with_address("gw", SIOCSIFGATEWAY);
			command_with_address("gateway", SIOCSIFGATEWAY);
			command_with_address("inet", SIOCSIFADDR);
			fprintf(stderr, "%s: '%s' is not an understood command\n", argv[0], argv[i]);
			return 1;
		}
	}

	return 0;
}
