#pragma once

#include <stdint.h>

#define E1000_REG_CTRL       0x0000
#define E1000_REG_STATUS     0x0008
#define E1000_REG_EEPROM     0x0014
#define E1000_REG_CTRL_EXT   0x0018
#define E1000_REG_ICR        0x00C0
#define E1000_REG_ITR        0x00c4
#define E1000_REG_IMS        0x00d0
#define E1000_REG_IMC        0x00d8

#define E1000_REG_RCTRL      0x0100
#define E1000_REG_RXDESCLO   0x2800
#define E1000_REG_RXDESCHI   0x2804
#define E1000_REG_RXDESCLEN  0x2808
#define E1000_REG_RXDESCHEAD 0x2810
#define E1000_REG_RXDESCTAIL 0x2818
#define E1000_REG_RDTR       0x2820

#define E1000_REG_TCTRL      0x0400
#define E1000_REG_TXDESCLO   0x3800
#define E1000_REG_TXDESCHI   0x3804
#define E1000_REG_TXDESCLEN  0x3808
#define E1000_REG_TXDESCHEAD 0x3810
#define E1000_REG_TXDESCTAIL 0x3818

#define E1000_REG_RXADDR     0x5400

#define E1000_NUM_RX_DESC 512
#define E1000_NUM_TX_DESC 512

#define RCTL_EN                         (1 << 1)    /* Receiver Enable */
#define RCTL_SBP                        (1 << 2)    /* Store Bad Packets */
#define RCTL_UPE                        (1 << 3)    /* Unicast Promiscuous Enabled */
#define RCTL_MPE                        (1 << 4)    /* Multicast Promiscuous Enabled */
#define RCTL_LPE                        (1 << 5)    /* Long Packet Reception Enable */
#define RCTL_LBM_NONE                   (0 << 6)    /* No Loopback */
#define RCTL_LBM_PHY                    (3 << 6)    /* PHY or external SerDesc loopback */
#define RCTL_RDMTS_HALF                 (0 << 8)    /* Free Buffer Threshold is 1/2 of RDLEN */
#define RCTL_RDMTS_QUARTER              (1 << 8)    /* Free Buffer Threshold is 1/4 of RDLEN */
#define RCTL_RDMTS_EIGHTH               (2 << 8)    /* Free Buffer Threshold is 1/8 of RDLEN */
#define RCTL_MO_36                      (0 << 12)   /* Multicast Offset - bits 47:36 */
#define RCTL_MO_35                      (1 << 12)   /* Multicast Offset - bits 46:35 */
#define RCTL_MO_34                      (2 << 12)   /* Multicast Offset - bits 45:34 */
#define RCTL_MO_32                      (3 << 12)   /* Multicast Offset - bits 43:32 */
#define RCTL_BAM                        (1 << 15)   /* Broadcast Accept Mode */
#define RCTL_VFE                        (1 << 18)   /* VLAN Filter Enable */
#define RCTL_CFIEN                      (1 << 19)   /* Canonical Form Indicator Enable */
#define RCTL_CFI                        (1 << 20)   /* Canonical Form Indicator Bit Value */
#define RCTL_DPF                        (1 << 22)   /* Discard Pause Frames */
#define RCTL_PMCF                       (1 << 23)   /* Pass MAC Control Frames */
#define RCTL_SECRC                      (1 << 26)   /* Strip Ethernet CRC */

#define RCTL_BSIZE_256                  (3 << 16)
#define RCTL_BSIZE_512                  (2 << 16)
#define RCTL_BSIZE_1024                 (1 << 16)
#define RCTL_BSIZE_2048                 (0 << 16)
#define RCTL_BSIZE_4096                 ((3 << 16) | (1 << 25))
#define RCTL_BSIZE_8192                 ((2 << 16) | (1 << 25))
#define RCTL_BSIZE_16384                ((1 << 16) | (1 << 25))

#define TCTL_EN                         (1 << 1)    /* Transmit Enable */
#define TCTL_PSP                        (1 << 3)    /* Pad Short Packets */
#define TCTL_CT_SHIFT                   4           /* Collision Threshold */
#define TCTL_COLD_SHIFT                 12          /* Collision Distance */
#define TCTL_SWXOFF                     (1 << 22)   /* Software XOFF Transmission */
#define TCTL_RTLC                       (1 << 24)   /* Re-transmit on Late Collision */

#define CMD_EOP                         (1 << 0)    /* End of Packet */
#define CMD_IFCS                        (1 << 1)    /* Insert FCS */
#define CMD_IC                          (1 << 2)    /* Insert Checksum */
#define CMD_RS                          (1 << 3)    /* Report Status */
#define CMD_RPS                         (1 << 4)    /* Report Packet Sent */
#define CMD_VLE                         (1 << 6)    /* VLAN Packet Enable */
#define CMD_IDE                         (1 << 7)    /* Interrupt Delay Enable */

#define ICR_TXDW   (1 << 0)
#define ICR_TXQE   (1 << 1)  /* Transmit queue is empty */
#define ICR_LSC    (1 << 2)  /* Link status changed */
#define ICR_RXSEQ  (1 << 3)  /* Receive sequence count error */
#define ICR_RXDMT0 (1 << 4)  /* Receive descriptor minimum threshold */
/* what's 5 (0x20)? */
#define ICR_RXO    (1 << 6)  /* Receive overrun */
#define ICR_RXT0   (1 << 7)  /* Receive timer interrupt? */
#define ICR_ACK    (1 << 17)
#define ICR_SRPD   (1 << 16)

struct e1000_rx_desc {
	volatile uint64_t addr;
	volatile uint16_t length;
	volatile uint16_t checksum;
	volatile uint8_t  status;
	volatile uint8_t  errors;
	volatile uint16_t special;
} __attribute__((packed)); /* this looks like it should pack fine as-is */

struct e1000_tx_desc {
	volatile uint64_t addr;
	volatile uint16_t length;
	volatile uint8_t  cso;
	volatile uint8_t  cmd;
	volatile uint8_t  status;
	volatile uint8_t  css;
	volatile uint16_t special;
} __attribute__((packed));

