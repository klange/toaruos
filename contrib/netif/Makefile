CC = i686-pc-toaru-gcc

CFLAGS  = -O2 -std=c99
CFLAGS += -finline-functions -ffreestanding
CFLAGS += -Wall -Wextra -Wno-unused-function -Wno-unused-parameter
CFLAGS += -Wstrict-prototypes -pedantic -fno-omit-frame-pointer
CFLAGS += -D_KERNEL_

LWIPDIR=./lwip-STABLE-1_4_1/src

KERNEL_HEADERS = -I../../kernel/include
LWIP_HEADERS  = -I$(LWIPDIR)/include
LWIP_HEADERS += -I$(LWIPARCH)/include
LWIP_HEADERS += -I$(LWIPARCH)/include/arch
LWIP_HEADERS += -I$(LWIPDIR)/include/ipv4

MODULELD = -T ../../modules/link.ld

.PHONY: all clean

al:: netif.ko

netif.ko: ${LWIP_SOURCES} netif.c
	${CC} ${MODULELD} ${KERNEL_HEADERS} ${LWIP_HEADERS} -nostdlib ${CFLAGS} -c -o $@ $< ${ERRORS}

clean:
	rm -f netif.ko
