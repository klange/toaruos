/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014 Kevin Lange
 */
#include <module.h>
#include <logging.h>
#include <hashmap.h>
#include <ipv4.h>
#include <printf.h>

static hashmap_t * dns_cache;

uint32_t ip_aton(const char * in) {
	char ip[16];
	char * c = ip;
	int out[4];
	char * i;
	memcpy(ip, in, strlen(in) < 15 ? strlen(in) + 1 : 15);
	ip[15] = '\0';

	i = (char *)lfind(c, '.');
	*i = '\0';
	out[0] = atoi(c);
	c += strlen(c) + 1;

	i = (char *)lfind(c, '.');
	*i = '\0';
	out[1] = atoi(c);
	c += strlen(c) + 1;

	i = (char *)lfind(c, '.');
	*i = '\0';
	out[2] = atoi(c);
	c += strlen(c) + 1;

	out[3] = atoi(c);

	return ((out[0] << 24) | (out[1] << 16) | (out[2] << 8) | (out[3]));
}

void ip_ntoa(uint32_t src_addr, char * out) {
	sprintf(out, "%d.%d.%d.%d",
		(src_addr & 0xFF000000) >> 24,
		(src_addr & 0xFF0000) >> 16,
		(src_addr & 0xFF00) >> 8,
		(src_addr & 0xFF));
}

uint16_t calculate_ipv4_checksum(struct ipv4_packet * p) {
	uint32_t sum = 0;
	uint16_t * s = (uint16_t *)p;

	/* TODO: Checksums for options? */
	for (int i = 0; i < 10; ++i) {
		sum += ntohs(s[i]);
	}

	if (sum > 0xFFFF) {
		sum = (sum >> 16) + (sum & 0xFFFF);
	}

	return ~(sum & 0xFFFF) & 0xFFFF;
}

uint16_t calculate_tcp_checksum(struct tcp_check_header * p, struct tcp_header * h, void * d, size_t payload_size) {
	uint32_t sum = 0;
	uint16_t * s = (uint16_t *)p;

	/* TODO: Checksums for options? */
	for (int i = 0; i < 6; ++i) {
		sum += ntohs(s[i]);
		if (sum > 0xFFFF) {
			sum = (sum >> 16) + (sum & 0xFFFF);
		}
	}

	s = (uint16_t *)h;
	for (int i = 0; i < 10; ++i) {
		sum += ntohs(s[i]);
		if (sum > 0xFFFF) {
			sum = (sum >> 16) + (sum & 0xFFFF);
		}
	}

	uint16_t d_words = payload_size / 2;

	s = (uint16_t *)d;
	for (unsigned int i = 0; i < d_words; ++i) {
		sum += ntohs(s[i]);
		if (sum > 0xFFFF) {
			sum = (sum >> 16) + (sum & 0xFFFF);
		}
	}

	if (d_words * 2 != payload_size) {
		uint8_t * t = (uint8_t *)d;
		uint8_t tmp[2];
		tmp[0] = t[d_words * sizeof(uint16_t)];
		tmp[1] = 0;

		uint16_t * f = (uint16_t *)tmp;

		sum += ntohs(f[0]);
		if (sum > 0xFFFF) {
			sum = (sum >> 16) + (sum & 0xFFFF);
		}
	}

	return ~(sum & 0xFFFF) & 0xFFFF;
}

static struct dirent * readdir_netfs(fs_node_t *node, uint32_t index) {
	if (index == 0) {
		struct dirent * out = malloc(sizeof(struct dirent));
		memset(out, 0x00, sizeof(struct dirent));
		out->ino = 0;
		strcpy(out->name, ".");
		return out;
	}

	if (index == 1) {
		struct dirent * out = malloc(sizeof(struct dirent));
		memset(out, 0x00, sizeof(struct dirent));
		out->ino = 0;
		strcpy(out->name, "..");
		return out;
	}

	index -= 2;
	return NULL;
}

size_t print_dns_name(fs_node_t * tty, struct dns_packet * dns, size_t offset) {
	uint8_t * bytes = (uint8_t *)dns;
	while (1) {
		uint8_t c = bytes[offset];
		if (c == 0) {
			offset++;
			return offset;
		} else if (c >= 0xC0) {
			uint16_t ref = ((c - 0xC0) << 8) + bytes[offset+1];
			print_dns_name(tty, dns, ref);
			offset++;
			offset++;
			return offset;
		} else {
			for (int i = 0; i < c; ++i) {
				fprintf(tty,"%c",bytes[offset+1+i]);
			}
			fprintf(tty,".");
			offset += c + 1;
		}
	}
}

static int is_ip(char * name) {

	unsigned int dot_count = 0;
	unsigned int t = 0;

	for (char * c = name; *c != '\0'; ++c) {
		if ((*c < '0' || *c > '9') && (*c != '.')) return 0;
		if (*c == '.') {
			if (t > 255) return 0;
			dot_count++;
			t = 0;
		} else {
			t *= 10;
			t += *c - '0';
		}
		if (dot_count == 4) return 0;
	}
	if (dot_count != 3) return 0;

	return 1;
}

static uint32_t socket_read(fs_node_t * node, uint32_t offset, uint32_t size, uint8_t * buffer) {
	/* Sleep until we have something to receive */
	return 0;
}
static uint32_t socket_write(fs_node_t * node, uint32_t offset, uint32_t size, uint8_t * buffer) {
	/* Add the packet to the appropriate interface queue and send it off. */

	/* What other things (routing) should we be doing here? Or do we do those somewhere else? */
	/* Whatever... */
	return 0;
}

uint16_t next_ephemeral_port(void) {
	static uint16_t next = 49152;

	if (next == 0) {
		assert(0 && "All out of ephemeral ports, halting this time.");
	}

	uint16_t out = next;
	next++;

	if (next == 0) {
		debug_print(WARNING, "Ran out of ephemeral ports - next time I'm going to bail.");
		debug_print(WARNING, "You really need to implement a bitmap here.");
	}

	return out;
}

fs_node_t * socket_ipv4_tcp_create(uint32_t dest, uint16_t target_port, uint16_t source_port) {

	/* Okay, first step is to get us added to the table so we can receive syns. */

	return NULL;

}


/* TODO: socket_close - TCP close; UDP... just clean us up */
/* TODO: socket_open - idk, whatever */

static fs_node_t * finddir_netfs(fs_node_t * node, char * name) {
	/* Should essentially find anything. */
	debug_print(WARNING, "Need to look up domain or check if is IP: %s", name);
	/* Block until lookup is complete */
	if (is_ip(name)) {
		debug_print(WARNING, "   IP: %x", ip_aton(name));
	} else {
		if (hashmap_has(dns_cache, name)) {
			uint32_t ip = ip_aton(hashmap_get(dns_cache, name));
			debug_print(WARNING, "   In Cache: %s → %x", name, ip);
		} else {
			debug_print(WARNING, "   Still needs look up.");
		}
	}

	return NULL;
}

static fs_node_t * netfs_create(void) {
	fs_node_t * fnode = malloc(sizeof(fs_node_t));
	memset(fnode, 0x00, sizeof(fs_node_t));
	fnode->inode = 0;
	strcpy(fnode->name, "net");
	fnode->mask = 0555;
	fnode->flags   = FS_DIRECTORY;
	fnode->readdir = readdir_netfs;
	fnode->finddir = finddir_netfs;
	fnode->nlink   = 1;
	return fnode;
}

static int init(void) {

	dns_cache = hashmap_create(10);

	hashmap_set(dns_cache, "dakko.us", strdup("104.131.140.26"));

	/* /dev/net/{domain|ip}/{protocol}/{port} */
	vfs_mount("/dev/net", netfs_create());

	return 0;
}

static int fini(void) {
	return 0;
}

MODULE_DEF(net, init, fini);
