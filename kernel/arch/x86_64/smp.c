/**
 * @file  kernel/arch/x86_64/smp.c
 * @brief Multi-processor Support for x86-64.
 *
 * Locates and bootstraps APs using ACPI MADT tables.
 */
#include <stdint.h>
#include <kernel/string.h>
#include <kernel/process.h>
#include <kernel/printf.h>
#include <kernel/misc.h>
#include <kernel/args.h>
#include <kernel/arch/x86_64/acpi.h>
#include <kernel/arch/x86_64/mmu.h>

__attribute__((used))
__attribute__((naked))
static void __ap_bootstrap(void) {
	asm volatile (
		".code16\n"
		".org 0x0\n"
		".global _ap_bootstrap_start\n"
		"_ap_bootstrap_start:\n"

		/* Enable PAE, paging */
		"mov $0xA0, %%eax\n"
		"mov %%eax, %%cr4\n"

		/* Kernel base PML4 */
		".global init_page_region\n"
		"mov $init_page_region, %%edx\n"
		"mov %%edx, %%cr3\n"

		/* Set LME */
		"mov $0xc0000080, %%ecx\n"
		"rdmsr\n"
		"or $0x100, %%eax\n"
		"wrmsr\n"

		/* Enable long mode */
		"mov $0x80000011, %%ebx\n"
		"mov  %%ebx, %%cr0\n"

		/* Set up basic GDT */
		"addr32 lgdtl %%cs:_ap_bootstrap_gdtp-_ap_bootstrap_start\n"

		/* Jump... */
		"data32 jmp $0x08,$ap_premain\n"

		".global _ap_bootstrap_gdtp\n"
		".align 16\n"
		"_ap_bootstrap_gdtp:\n"
		".word 0\n"
		".quad 0\n"

		".code64\n"
		".align 16\n"
		"ap_premain:\n"
		"mov $0x10, %%ax\n"
		"mov %%ax, %%ds\n"
		"mov %%ax, %%ss\n"
		"mov $0x2b, %%ax\n"
		"ltr %%ax\n"
		".extern _ap_stack_base\n"
		"mov _ap_stack_base,%%rsp\n"
		".extern ap_main\n"
		"callq ap_main\n"

		".global _ap_bootstrap_end\n"
		"_ap_bootstrap_end:\n"
		: : : "memory"
	);
}

extern char _ap_bootstrap_start[];
extern char _ap_bootstrap_end[];
extern char _ap_bootstrap_gdtp[];
extern size_t arch_cpu_mhz(void);
extern void gdt_copy_to_trampoline(int ap, char * trampoline);
extern void arch_set_core_base(uintptr_t base);
extern void fpu_initialize(void);
extern void idt_ap_install(void);
extern void pat_initialize(void);
extern process_t * spawn_kidle(int);
extern union PML init_page_region[];

uintptr_t _ap_stack_base = 0;
static volatile int _ap_startup_flag = 0;
void load_processor_info(void);

/* For timing delays on IPIs */
static inline uint64_t read_tsc(void) {
	uint32_t lo, hi;
	asm volatile ( "rdtsc" : "=a"(lo), "=d"(hi) );
	return ((uint64_t)hi << 32) | (uint64_t)lo;
}

static void short_delay(unsigned long amount) {
	uint64_t clock = read_tsc();
	while (read_tsc() < clock + amount * arch_cpu_mhz());
}

static volatile int _ap_current = 0;
uintptr_t lapic_final = 0;

#define cpuid(in,a,b,c,d) do { asm volatile ("cpuid" : "=a"(a),"=b"(b),"=c"(c),"=d"(d) : "a"(in)); } while(0)

/* C entrypoint for APs */
void ap_main(void) {
	arch_set_core_base((uintptr_t)&processor_local_data[_ap_current]);

	uint32_t ebx, _unused;
	cpuid(0x1,_unused,ebx,_unused,_unused);
	if (this_core->lapic_id != (int)(ebx >> 24)) {
		printf("smp: lapic id does not match\n");
	}

	/* Load the IDT */
	idt_ap_install();
	fpu_initialize();
	pat_initialize();

	/* Enable our spurious vector register */
	*((volatile uint32_t*)(lapic_final + 0x0F0)) = 0x127;

	/* Set our pml pointers */
	this_core->current_pml = &init_page_region[0];

	/* Spawn our kidle, make it our current process. */
	this_core->kernel_idle_task = spawn_kidle(0);
	this_core->current_process = this_core->kernel_idle_task;

	load_processor_info();

	/* Inform BSP it can continue. */
	_ap_startup_flag = 1;

	switch_next();
}

void load_processor_info(void) {
	unsigned long a, b, unused;
	cpuid(0,unused,b,unused,unused);

	this_core->cpu_manufacturer = "Unknown";

	if (b == 0x756e6547) {
		cpuid(1, a, b, unused, unused);
		this_core->cpu_manufacturer = "Intel";
		this_core->cpu_model        = (a >> 4) & 0x0F;
		this_core->cpu_family       = (a >> 8) & 0x0F;
	} else if (b == 0x68747541) {
		cpuid(1, a, unused, unused, unused);
		this_core->cpu_manufacturer = "AMD";
		this_core->cpu_model        = (a >> 4) & 0x0F;
		this_core->cpu_family       = (a >> 8) & 0x0F;
	}

	snprintf(processor_local_data[this_core->cpu_id].cpu_model_name, 20, "(unknown)");

	/* See if we can get a long manufacturer strings */
	cpuid(0x80000000, a, unused, unused, unused);
	if (a >= 0x80000004) {
		uint32_t brand[12];
		cpuid(0x80000002, brand[0], brand[1], brand[2], brand[3]);
		cpuid(0x80000003, brand[4], brand[5], brand[6], brand[7]);
		cpuid(0x80000004, brand[8], brand[9], brand[10], brand[11]);
		memcpy(processor_local_data[this_core->cpu_id].cpu_model_name, brand, 48);
	}
}

void lapic_write(size_t addr, uint32_t value) {
	*((volatile uint32_t*)(lapic_final + addr)) = value;
	asm volatile ("":::"memory");
}

uint32_t lapic_read(size_t addr) {
	return *((volatile uint32_t*)(lapic_final + addr));
}

void lapic_send_ipi(int i, uint32_t val) {
	lapic_write(0x310, i << 24);
	lapic_write(0x300, val);
	do { asm volatile ("pause" : : : "memory"); } while (lapic_read(0x300) & (1 << 12));
}

void smp_initialize(void) {
	/* Locate ACPI tables */
	uintptr_t scan;
	int good = 0;
	for (scan = 0x000E0000; scan < 0x00100000; scan += 16) {
		char * _scan = mmu_map_from_physical(scan);
		if (_scan[0] == 'R' &&
			_scan[1] == 'S' &&
			_scan[2] == 'D' &&
			_scan[3] == ' ' &&
			_scan[4] == 'P' &&
			_scan[5] == 'T' &&
			_scan[6] == 'R') {
			good = 1;
			break;
		}
	}

	load_processor_info();

	if (!good) {
		printf("smp: No RSD PTR found\n");
		return;
	}

	struct rsdp_descriptor * rsdp = (struct rsdp_descriptor *)scan;
	uint8_t check = 0;
	uint8_t * tmp;
	for (tmp = (uint8_t *)scan; (uintptr_t)tmp < scan + sizeof(struct rsdp_descriptor); tmp++) {
		check += *tmp;
	}
	if (check != 0) {
		printf("smp: Bad checksum on RSDP\n");
		return; /* bad checksum */
	}

	/* Load information for the current CPU. */

	if (args_present("nosmp")) return;

	struct rsdt * rsdt = mmu_map_from_physical(rsdp->rsdt_address);

	int cores = 0;
	uintptr_t lapic_base = 0x0;
	for (unsigned int i = 0; i < (rsdt->header.length - 36) / 4; ++i) {
		uint8_t * table = mmu_map_from_physical(rsdt->pointers[i]);
		if (table[0] == 'A' && table[1] == 'P' && table[2] == 'I' && table[3] == 'C') {
			/* APIC table! Let's find some CPUs! */
			struct madt * madt = (void*)table;
			lapic_base = madt->lapic_addr;
			for (uint8_t * entry = madt->entries; entry < table + madt->header.length; entry += entry[1]) {
				switch (entry[0]) {
					case 0:
						if (entry[4] & 0x01) {
							processor_local_data[cores].cpu_id = cores;
							processor_local_data[cores].lapic_id = entry[3];
							cores++;
							if (cores == 33) {
								printf("smp: too many cores\n");
								arch_fatal();
							}
						}
						break;
					/* TODO: Other entries */
				}
			}
		}
	}

	processor_count = cores;

	if (!lapic_base) return;

	/* Allocate a virtual address with which we can poke the lapic */
	lapic_final = (uintptr_t)mmu_map_mmio_region(lapic_base, 0x1000);

	if (cores <= 1) return;

	/* Map the bootstrap code */
	memcpy(mmu_map_from_physical(0x1000), &_ap_bootstrap_start, (uintptr_t)&_ap_bootstrap_end - (uintptr_t)&_ap_bootstrap_start);

	for (int i = 1; i < cores; ++i) {
		_ap_startup_flag = 0;

		/* Set gdt pointer value */
		gdt_copy_to_trampoline(i, (char*)mmu_map_from_physical(0x1000) + ((uintptr_t)&_ap_bootstrap_gdtp - (uintptr_t)&_ap_bootstrap_start));

		/* Make an initial stack for this AP */
		_ap_stack_base = (uintptr_t)valloc(KERNEL_STACK_SIZE)+ KERNEL_STACK_SIZE;

		_ap_current = i;

		/* Send INIT */
		lapic_send_ipi(processor_local_data[i].lapic_id, 0x4500);
		short_delay(5000UL);

		/* Send SIPI */
		lapic_send_ipi(processor_local_data[i].lapic_id, 0x4601);

		/* Wait for AP to signal it is ready before starting next AP */
		do { asm volatile ("pause" : : : "memory"); } while (!_ap_startup_flag);
	}
}

void arch_wakeup_others(void) {
	/* Never wake up BSP, don't hit ourselves, easy. */
	for (int i = 1; i < processor_count; ++i) {
		if (i == this_core->cpu_id) continue;
		if (processor_local_data[i].current_process == processor_local_data[i].kernel_idle_task) {
			lapic_send_ipi(processor_local_data[i].lapic_id, 0x407E);
		}
	}
}
