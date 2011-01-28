#include <system.h>
#include <multiboot.h>

/*
 * kernel entry point
 */
int
main(struct multiboot *mboot_ptr) {
	if (mboot_ptr->mods_count > 0) {
		uint32_t module_start = *((uint32_t*)mboot_ptr->mods_addr);
		uint32_t module_end   = *(uint32_t*)(mboot_ptr->mods_addr+4);
		kmalloc_startat(module_end);
	}
#if 0
	mboot_ptr = copy_multiboot(mboot_ptr);
#endif
	gdt_install();	/* Global descriptor table */
	idt_install();	/* IDT */
	isrs_install();	/* Interrupt service requests */
	irq_install();	/* Hardware interrupt requests */
	init_video();	/* VGA driver */
	timer_install();
	keyboard_install();
	paging_install(mboot_ptr->mem_upper);
	heap_install();

	settextcolor(12,0);
	kprintf("[%s %s]\n", KERNEL_UNAME, KERNEL_VERSION_STRING);
	dump_multiboot(mboot_ptr);

	kprintf("Will begin dumping from second kB of module 1 in a second.\n");
	timer_wait(100);
	kprintf("Dumping.\n");
	uint32_t i;
	uint32_t module_start = *((uint32_t*)mboot_ptr->mods_addr);
	uint32_t module_end   = *(uint32_t*)(mboot_ptr->mods_addr+4);
	for (i = module_start + 1024; i < module_end; ++i) {
		kprintf("%c ", *((char *)i));
		if (i % 35 == 0) { kprintf("\n"); }
		timer_wait(1);
	}


	return 0;
}
