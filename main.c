#include <system.h>
#include <multiboot.h>

/*
 * memcpy
 * Copy from source to destination. Assumes that
 * source and destination are not overlapping.
 */
void *
memcpy(
		void * restrict dest,
		const void * restrict src,
		size_t count
	  ) {
	size_t i;
	unsigned char *a = dest;
	const unsigned char *b = src;
	for ( i = 0; i < count; ++i ) {
		a[i] = b[i];
	}
	return dest;
}

/*
 * memset
 * Set `count` bytes to `val`.
 */
void *
memset(
		void *b,
		int val,
		size_t count
	  ) {
	size_t i;
	unsigned char * dest = b;
	for ( i = 0; i < count; ++i ) {
		dest[i] = (unsigned char)val;
	}
	return b;
}

/*
 * memsetw
 * Set `count` shorts to `val`.
 */
unsigned short *
memsetw(
		unsigned short *dest,
		unsigned short val,
		int count
	  ) {
	int i;
	i = 0;
	for ( ; i < count; ++i ) {
		dest[i] = val;
	}
	return dest;
}

/*
 * strlen
 * Returns the length of a given `str`.
 */
int
strlen(
		const char *str
	  ) {
	int i = 0;
	while (str[i] != (char)0) {
		++i;
	}
	return i;
}

/*
 * inportb
 * Read from an I/O port.
 */
unsigned char
inportb(
		unsigned short _port
	   ) {
	unsigned char rv;
	__asm__ __volatile__ ("inb %1, %0" : "=a" (rv) : "dN" (_port));
	return rv;
}

/*
 * outportb
 * Write to an I/O port.
 */
void
outportb(
		unsigned short _port,
		unsigned char _data
		) {
	__asm__ __volatile__ ("outb %1, %0" : : "dN" (_port), "a" (_data));
}

/*
 * 99 Bottles of Beer on the -Wall
 * Sample kernel mode program.
 */
void beer() {
	int i = 99;
	while (i > 0) {
		if (i == 1) {
			puts ("One bottle of beer on the wall, one bottle of beer. Take one down, pass it around, ");
		} else {
			kprintf("%d bottles of beer on the wall, %d bottles of beer...\n", i, i);
		}
		i--;
		if (i == 1) {
			puts("One bottle of beer on the wall.\n");
		} else {
			kprintf("%d bottles of beer on the wall.\n", i);
		}
		timer_wait(3);
	}
	puts("No more bottles of beer on the wall.\n");
}

/*
 * kernel entry point
 */
int
main(struct multiboot *mboot_ptr) {
	gdt_install();
	idt_install();
	isrs_install();
	irq_install();
	init_video();
	paging_install(mboot_ptr->mem_upper + 1024);
	timer_install();
	keyboard_install();
	/* Yes, yes, these are #define'd strings, consider this a nice test of kprintf */
	settextcolor(12,0);
	kprintf("[%s %s]\n", KERNEL_UNAME, KERNEL_VERSION_STRING);
	settextcolor(1,0);
	/* Multiboot Debug */
	kprintf("Received the following MULTIBOOT data:\n");
	settextcolor(7,0);
	kprintf("Flags : 0x%x ", mboot_ptr->flags);
	kprintf("Mem Lo: 0x%x ", mboot_ptr->mem_lower);
	kprintf("Mem Hi: 0x%x ", mboot_ptr->mem_upper);
	kprintf("Boot d: 0x%x\n", mboot_ptr->boot_device);
	kprintf("cmdlin: 0x%x ", mboot_ptr->cmdline);
	kprintf("Mods  : 0x%x ", mboot_ptr->mods_count);
	kprintf("Addr  : 0x%x ", mboot_ptr->mods_addr);
	kprintf("Syms  : 0x%x\n", mboot_ptr->num);
	kprintf("Syms  : 0x%x ", mboot_ptr->size);
	kprintf("Syms  : 0x%x ", mboot_ptr->addr);
	kprintf("Syms  : 0x%x ", mboot_ptr->shndx);
	kprintf("MMap  : 0x%x\n", mboot_ptr->mmap_length);
	kprintf("Addr  : 0x%x ", mboot_ptr->mmap_addr);
	kprintf("Drives: 0x%x ", mboot_ptr->drives_length);
	kprintf("Addr  : 0x%x ", mboot_ptr->drives_addr);
	kprintf("Config: 0x%x\n", mboot_ptr->config_table);
	kprintf("Loader: 0x%x ", mboot_ptr->boot_loader_name);
	kprintf("APM   : 0x%x ", mboot_ptr->apm_table);
	kprintf("VBE Co: 0x%x ", mboot_ptr->vbe_control_info);
	kprintf("VBE Mo: 0x%x\n", mboot_ptr->vbe_mode_info);
	kprintf("VBE In: 0x%x ", mboot_ptr->vbe_mode);
	kprintf("VBE se: 0x%x ", mboot_ptr->vbe_interface_seg);
	kprintf("VBE of: 0x%x ", mboot_ptr->vbe_interface_off);
	kprintf("VBE le: 0x%x\n", mboot_ptr->vbe_interface_len);
	resettextcolor();
	kprintf("(End multiboot raw data)\n");
	kprintf("Started with: %s\n", (char *)mboot_ptr->cmdline);
	kprintf("Booted from: %s\n", (char *)mboot_ptr->boot_loader_name);
	
	kprintf("%dkB lower memory\n", mboot_ptr->mem_lower);
	kprintf("%dkB higher memory ", mboot_ptr->mem_upper);
	int mem_mb = mboot_ptr->mem_upper / 1024;
	kprintf("(%dMB)\n", mem_mb);
	

	settextcolor(7,0);
	kprintf("Testing colors...\n");
	resettextcolor();
	int i;
	for (i = 0; i < 16; ++i) {
		settextcolor(i,i);
		putch(' ');
	}
	putch('\n');
	resettextcolor();

	settextcolor(9,0);
	kprintf(" = Roadmap = \n");
	kprintf("- Paging\n");
	kprintf("- Heap\n");
	kprintf("- VFS\n");
	kprintf("- Initial ramdisk\n");
	kprintf("- Task switching\n");
	kprintf("- User mode execution\n");
	kprintf("- EXT2\n");
	resettextcolor();

	kprintf("Kernel is finished booting. Press `q` to produce a page fault.\n");

	//for (;;);
	return 0;
}
