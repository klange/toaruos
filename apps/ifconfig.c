/**
 * @file  apps/ifconfig.c
 * @brief Network interface configuration tool.
 *
 * Manipulates and enumerates network interfaces.
 *
 * All of the APIs used in this tool are temporary and subject to change.
 */
#include <stdio.h>
#include <stdint.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>

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

static int configure_interface(const char * if_name) {
	char if_path[100];
	snprintf(if_path, 100, "/dev/net/%s", if_name);
	int netdev = open(if_path, O_RDWR);

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

	/* TODO stats */

	fprintf(stdout,"\n");

	return 0;
}

int main(int argc, char * argv[]) {
	int retval = 0;

	if (argc > 1) {
		return configure_interface(argv[1]);
	} else {
		/* Read /dev/net for interfaces */
		DIR * d = opendir("/dev/net");
		if (!d) {
			fprintf(stderr, "%s: no network?\n", _argv_0);
			return 1;
		}

		struct dirent * ent;
		while ((ent = readdir(d))) {
			if (ent->d_name[0] == '.') continue;
			if (configure_interface(ent->d_name)) {
				retval = 1;
			}
		}

		closedir(d);
	}

	return retval;
}
