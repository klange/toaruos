#pragma once

#include <_cheader.h>
#include <stdint.h>

_Begin_C_Header

/* I would love to fully implement the Linux API for these, but
 * for now these are just cleaner versions of the temporary API
 * we currently provide. */

#define SIOCGIFHWADDR   0x12340001 /* Get hardware address */
#define SIOCGIFADDR     0x12340002 /* Get IPv4 address */
#define SIOCSIFADDR     0x12340012 /* Set IPv4 address */
#define SIOCGIFNETMASK  0x12340004 /* Get IPv4 subnet mask */
#define SIOCSIFNETMASK  0x12340014 /* Set IPv4 subnet mask */
#define SIOCGIFADDR6    0x12340003 /* Get IPv6 address */
#define SIOCSIFADDR6    0x12340013 /* Set IPv6 address */
#define SIOCGIFFLAGS    0x12340005 /* Get interface flags */
#define SIOCGIFMTU      0x12340006 /* Get interface mtu */
#define SIOCGIFGATEWAY  0x12340007
#define SIOCSIFGATEWAY  0x12340017
#define SIOCGIFCOUNTS   0x12340018

/**
 * Flags for interface status
 */
#define IFF_UP            0x0001
#define IFF_BROADCAST     0x0002
#define IFF_DEBUG         0x0004
#define IFF_LOOPBACK      0x0008
#define IFF_RUNNING       0x0010
#define IFF_MULTICAST     0x0020

typedef struct {
	size_t tx_count;
	size_t tx_bytes;
	size_t rx_count;
	size_t rx_bytes;
} netif_counters_t;

_End_C_Header
