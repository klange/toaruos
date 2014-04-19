CC = i686-pc-toaru-gcc
LD = i686-pc-toaru-ld

CFLAGS  = -O2 -std=c99
CFLAGS += -finline-functions -ffreestanding
CFLAGS += -Wall -Wextra -Wno-unused-function -Wno-unused-parameter
CFLAGS += -pedantic -fno-omit-frame-pointer
CFLAGS += -D_KERNEL_

LWIPDIR=./lwip-STABLE-1_4_1/src

LWIPARCH=.

KERNEL_HEADERS = -I../../kernel/include
LWIP_HEADERS  = -I$(LWIPDIR)/include
LWIP_HEADERS += -I$(LWIPARCH)/include
LWIP_HEADERS += -I$(LWIPARCH)/include/arch
LWIP_HEADERS += -I$(LWIPDIR)/include/ipv4

COREFILES=$(LWIPDIR)/core/mem.c             \
    $(LWIPDIR)/core/memp.c              \
    $(LWIPDIR)/core/netif.c             \
    $(LWIPDIR)/core/pbuf.c              \
    $(LWIPDIR)/core/raw.c               \
    $(LWIPDIR)/core/stats.c             \
    $(LWIPDIR)/core/sys.c               \
    $(LWIPDIR)/core/tcp.c               \
    $(LWIPDIR)/core/tcp_in.c            \
    $(LWIPDIR)/core/tcp_out.c           \
    $(LWIPDIR)/core/udp.c               \
    $(LWIPDIR)/core/dns.c               \
    $(LWIPDIR)/core/dhcp.c              \
    $(LWIPDIR)/core/timers.c            \
    $(LWIPDIR)/core/init.c

CORE4FILES=$(LWIPDIR)/core/ipv4/icmp.c \
    $(LWIPDIR)/core/ipv4/ip.c          \
    $(LWIPDIR)/core/ipv4/inet.c        \
    $(LWIPDIR)/core/ipv4/ip_addr.c     \
    $(LWIPDIR)/core/ipv4/ip_frag.c     \
    $(LWIPDIR)/core/ipv4/inet_chksum.c

APIFILES=$(LWIPDIR)/api/api_lib.c $(LWIPDIR)/api/api_msg.c $(LWIPDIR)/api/err.c \
    $(LWIPDIR)/api/netbuf.c $(LWIPDIR)/api/netdb.c $(LWIPDIR)/api/netifapi.c \
    $(LWIPDIR)/api/sockets.c $(LWIPDIR)/api/tcpip.c

NETIFFILES=$(LWIPDIR)/netif/etharp.c

LWIPFILES=$(COREFILES) $(CORE4FILES) $(APIFILES) $(NETIFFILES)

MODULELD = -T ../../modules/link.ld

.PHONY: all clean

all: netif.ko rtl8139.ko

LWIPFILESW=$(wildcard $(LWIPFILES))
LWIPOBJS=$(LWIPFILESW:.c=.o)

%.o: %.c
	${CC} ${MODULELD} ${KERNEL_HEADERS} ${LWIP_HEADERS} -nostdlib ${CFLAGS} -c -o $@ $(<:.o=.c)

netif.ko: ${LWIPOBJS} netif.o
	${LD} -r -o $@ $^

rtl8139.ko: rtl8139.o
	${LD} -r -o $@ $^

clean:
	rm -f netif.ko
	rm -f rtl8139.ko
	rm -f ${LWIPOBJS}
