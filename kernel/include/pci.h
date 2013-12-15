#ifndef _PCI_H
#define _PCI_H

#define PCI_VENDOR_ID            0x00 // 2
#define PCI_DEVICE_ID            0x02 // 2
#define PCI_COMMAND              0x04 // 2
#define PCI_STATUS               0x06 // 2
#define PCI_REVISION_ID          0x08 // 1

#define PCI_PROG_IF              0x09 // 1
#define PCI_SUBCLASS             0x0a // 1
#define PCI_CLASS                0x0b // 1
#define PCI_CACHE_LINE_SIZE      0x0c // 1
#define PCI_LATENCY_TIMER        0x0d // 1
#define PCI_HEADER_TYPE          0x0e // 1
#define PCI_BIST                 0x0f // 1
#define PCI_BAR0                 0x10 // 4
#define PCI_BAR1                 0x14 // 4
#define PCI_BAR2                 0x18 // 4
#define PCI_BAR3                 0x1C // 4
#define PCI_BAR4                 0x20 // 4
#define PCI_BAR5                 0x24 // 4

#define PCI_CAPABILITIES         0x34 // 1
#define PCI_INTERRUPT_LINE       0x3c // 1
#define PCI_INTERRUPT_PIN        0x3d // 1

// Type 0x1 (PCI Bridge)
#define PCI_PRIMARY_BUS          0x18 // 1
#define PCI_SECONDARY_BUS        0x19 // 1

// PCI Capabilities list
#define PCI_CAP_PM			0x1  // Power management
#define PCI_CAP_AGP			0x2  // AGP
#define PCI_CAP_VPD			0x3  // Vital Product Data
#define PCI_CAP_SLOT		0x4  // Slot identification
#define PCI_CAP_MSI			0x5  // Message Signalled Interrupt
#define PCI_CAP_CHSWP		0x6  // CompactPCI HotSwap
#define PCI_CAP_PCIX		0x7  // PCI-X
#define PCI_CAP_HT			0x8  // HyperTransport
#define PCI_CAP_VS			0x9  // Vendor specific
#define PCI_CAP_SHPC		0xC  // PCI Hot plug
#define PCI_CAP_PCIB		0xD  // PCI Bridge
#define PCI_CAP_ARI			0xE  // ARI
#define PCI_CAP_EXP			0x10 // PCI Express
#define PCI_CAP_MSIX		0x11 // MSI-X
#define PCI_CAP_SATA		0x12 // SATA
#define PCI_CAP_FLR			0x13 // Function Level Reset

// PCI BAR
#define PCI_BAR_IO			0x01
#define PCI_BAR_LOWMEM		0x02
#define PCI_BAR_64			0x04
#define PCI_BAR_PREFETCH    0x08

#define PCI_HEADER_TYPE_DEVICE    0x0
#define PCI_HEADER_TYPE_BRIDGE    0x1
#define PCI_HEADER_TYPE_CARDBUS   0x2
#define PCI_HEADER_TYPE_MULTIFUNC 0x80

#define PCI_HDR_TYPE(x) \
    ((x) & 0x7F)

#define PCI_TYPE_BRIDGE 0x0604
#define PCI_TYPE_SATA   0x0106

#define PCI_ADDRESS_PORT 0xCF8
#define PCI_VALUE_PORT   0xCFC

#define PCI_NONE 0xFFFF

typedef struct pci_bar {
	union {
		uint64_t address;
		uint16_t port;
	} u;
	uint64_t size;
	uint32_t flags;
} pci_bar;

typedef struct pci_dev {
	uint32_t dev;

	uint16_t vendorid;
	uint16_t deviceid;

	uint16_t status;
	uint16_t cmd;

	uint8_t classcode;
	uint8_t subclass;
	uint8_t prog_intf;
	uint8_t revision;
	uint8_t bist;
	uint8_t type;
	uint8_t latency;
	uint8_t cacheline;

	uint16_t irq;
	uint8_t pin;

	pci_bar bar[5];

	node_t dev_node;

} pci_dev;

typedef void (*pci_func_t)(uint32_t device, uint16_t vendor_id, uint16_t device_id);

static inline int pci_extract_bus(uint32_t device) {
	return (uint8_t)((device >> 16));
}
static inline int pci_extract_slot(uint32_t device) {
	return (uint8_t)((device >> 8));
}
static inline int pci_extract_func(uint32_t device) {
	return (uint8_t)(device);
}

static inline uint32_t pci_get_addr(uint32_t device, int field) {
	return 0x80000000 | (pci_extract_bus(device) << 16) | (pci_extract_slot(device) << 11) | (pci_extract_func(device) << 8) | ((field) & 0xFC);
}

static inline uint32_t pci_box_device(int bus, int slot, int func) {
	return (uint32_t)((bus << 16) | (slot << 8) | func);
}

uint32_t pci_read_field(uint32_t device, int field, int size);
void pci_write_field(uint32_t device, int field, int size, uint32_t value);
uint16_t pci_find_type(uint32_t dev);
const char * pci_vendor_lookup(unsigned short vendor_id);
const char * pci_device_lookup(unsigned short vendor_id, unsigned short device_id);
void pci_scan_hit(pci_func_t f, uint32_t dev);
void pci_scan_func(pci_func_t f, int type, int bus, int slot, int func);
void pci_scan_slot(pci_func_t f, int type, int bus, int slot);
void pci_scan_bus(pci_func_t f, int type, int bus);
void pci_scan(pci_func_t f, int type);
void pci_install(void);

#endif
