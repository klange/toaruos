/**
 * @file  kernel/arch/x86_64/smp.c
 * @brief Multi-processor Support for x86-64.
 *
 * Locates and bootstraps APs using ACPI MADT tables.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021 K. Lange
 */
#include <stdint.h>
#include <kernel/string.h>
#include <kernel/process.h>
#include <kernel/printf.h>
#include <kernel/misc.h>
#include <kernel/args.h>
#include <kernel/time.h>
#include <kernel/multiboot.h>
#include <kernel/mmu.h>
#include <kernel/arch/x86_64/acpi.h>

__attribute__((used))
__attribute__((naked))
static void __ap_bootstrap(void) {
	asm volatile (
		".section .shit\n"
		".code16\n"
		".org 0x0\n"
		".global _ap_bootstrap_start\n"
		"_ap_bootstrap_start:\n"

		/* Enable PAE, paging */
		"mov $0xA0, %%eax\n"
		"mov %%eax, %%cr4\n"

		/* Kernel base PML4 */
		"mov $0x77777777, %%edx\n"
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
		"data32 jmp $0x08,$0x5A5A5A5A\n"

		".global _ap_bootstrap_gdtp\n"
		".align 16\n"
		"_ap_bootstrap_gdtp:\n"
		".word 0\n"
		".quad 0\n"

		".global _ap_bootstrap_end\n"
		"_ap_bootstrap_end:\n"
		".section .text\n"
		: : : "memory"
	);
}

__attribute__((used))
__attribute__((naked))
static void __ap_bootstrap_landing(void) {
	asm volatile (
		".code64\n"
		".align 16\n"
		".global _ap_premain\n"
		"_ap_premain:\n"
		"mov $0x10, %%ax\n"
		"mov %%ax, %%ds\n"
		"mov %%ax, %%ss\n"
		"mov $0x33, %%ax\n" /* TSS offset in gdt */
		"ltr %%ax\n"
		".extern _ap_stack_base\n"
		"mov _ap_stack_base(%%rip),%%rsp\n"
		".extern ap_main\n"
		"callq ap_main\n"
		: : : "memory"
	);
}

extern char _ap_bootstrap_start[];
extern char _ap_bootstrap_end[];
extern char _ap_bootstrap_gdtp[];
extern char _ap_premain[];
extern size_t arch_cpu_mhz(void);
extern void gdt_copy_to_trampoline(int ap, char * trampoline);
extern void arch_set_core_base(uintptr_t base);
extern void fpu_initialize(void);
extern void idt_ap_install(void);
extern void pat_initialize(void);
extern process_t * spawn_kidle(int);
extern union PML init_page_region[];

/**
 * @brief Read the timestamp counter.
 *
 * This is duplicated in a couple of places as it's a quick
 * inline wrapper for 'rdtsc'.
 */
static inline uint64_t read_tsc(void) {
	uint32_t lo, hi;
	asm volatile ( "rdtsc" : "=a"(lo), "=d"(hi) );
	return ((uint64_t)hi << 32) | (uint64_t)lo;
}

/**
 * @brief Pause by looping on TSC.
 *
 * Used for AP startup.
 */
static void short_delay(unsigned long amount) {
	uint64_t clock = read_tsc();
	while (read_tsc() < clock + amount * arch_cpu_mhz());
}

static volatile int _ap_current = 0;       /**< The AP we're currently starting up; shared between @c ap_main and @c smp_initialize */
static volatile int _ap_startup_flag = 0;  /**< Simple lock, shared between @c ap_main and @c smp_initialize */
uintptr_t _ap_stack_base = 0;              /**< Stack address for this AP to use on startup; used by @c __ap_boostrap */
uintptr_t lapic_final = 0;                 /**< MMIO region to use for APIC access. */

#define cpuid(in,a,b,c,d) do { asm volatile ("cpuid" : "=a"(a),"=b"(b),"=c"(c),"=d"(d) : "a"(in)); } while(0)

/**
 * @brief Obtains processor name strings from cpuid
 *
 * We store the processor names for each core (they might be different...)
 * so we can display them nicely in /proc/cpuinfo
 */
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

	extern void syscall_entry(void);
	uint32_t efer_hi, efer_lo;
	asm volatile ("rdmsr" : "=d"(efer_hi), "=a"(efer_lo) : "c"(0xc0000080));    /* Read current EFER */
	asm volatile ("wrmsr" : : "c"(0xc0000080), "d"(efer_hi), "a"(efer_lo | 1)); /* Enable SYSCALL/SYSRET in EFER */
	asm volatile ("wrmsr" : : "c"(0xC0000081), "d"(0x1b0008), "a"(0));          /* Set segment bases in STAR */
	asm volatile ("wrmsr" : : "c"(0xC0000082),                                  /* Set SYSCALL entry point in LSTAR */
	              "d"((uintptr_t)&syscall_entry >> 32),
	              "a"((uintptr_t)&syscall_entry & 0xFFFFffff));
	asm volatile ("wrmsr" : : "c"(0xC0000084), "d"(0), "a"(0x700));             /* SFMASK: Direction flag, interrupt flag, trap flag are all cleared */
}

static void lapic_timer_initialize(void) {
	/* Enable our spurious vector register */
	*((volatile uint32_t*)(lapic_final + 0x0F0)) = 0x127;
	*((volatile uint32_t*)(lapic_final + 0x320)) = 0x7b;
	*((volatile uint32_t*)(lapic_final + 0x3e0)) = 1;

	/* Time our APIC timer against the TSC */
	uint64_t before = arch_perf_timer();
	*((volatile uint32_t*)(lapic_final + 0x380)) = 1000000;
	while (*((volatile uint32_t*)(lapic_final + 0x390)));
	uint64_t after = arch_perf_timer();

	uint64_t ms = (after-before)/arch_cpu_mhz();
	uint64_t target = 10000000000UL / ms;

	/* Enable our APIC timer to send periodic wakeup signals */
	*((volatile uint32_t*)(lapic_final + 0x3e0)) = 1;
	*((volatile uint32_t*)(lapic_final + 0x320)) = 0x7b | 0x20000;
	*((volatile uint32_t*)(lapic_final + 0x380)) = target;
}

/**
 * @brief C entrypoint for APs, called by the bootstrap.
 *
 * After an AP has entered long mode, it jumps here, where
 * we do the rest of the core setup.
 */
void ap_main(void) {

	/* Set the GS base to point to our 'this_core' struct. */
	arch_set_core_base((uintptr_t)&processor_local_data[_ap_current]);

	/* Safety check...
	 * Make sure we're actually the core we think we are...
	 */
	uint32_t ebx, _unused;
	cpuid(0x1,_unused,ebx,_unused,_unused);
	if (this_core->lapic_id != (int)(ebx >> 24)) {
		printf("smp: lapic id does not match\n");
	}

	/* lidt, initialize local FPU, set up page attributes */
	idt_ap_install();
	fpu_initialize();
	pat_initialize();

	/* Set our pml pointers */
	this_core->current_pml = &init_page_region[0];

	/* Spawn our kidle, make it our current process. */
	this_core->kernel_idle_task = spawn_kidle(0);
	this_core->current_process = this_core->kernel_idle_task;

	/* Collect CPU name strings. */
	load_processor_info();

	/* Inform BSP it can continue. */
	_ap_startup_flag = 1;

	lapic_timer_initialize();

	/* Enter scheduler */
	switch_next();
}

/**
 * @brief MMIO write for LAPIC
 * @param addr Register address to access
 * @param value DWORD to write
 */
void lapic_write(size_t addr, uint32_t value) {
	*((volatile uint32_t*)(lapic_final + addr)) = value;
	asm volatile ("":::"memory");
}

/**
 * @brief MMIO read for LAPIC
 * @param addr Register address to access
 * @return DWORD
 */
uint32_t lapic_read(size_t addr) {
	return *((volatile uint32_t*)(lapic_final + addr));
}

/**
 * @brief Send an inter-processor interrupt.
 *
 * Sends an IPI and waits for the LAPIC to signal the IPI was sent.
 *
 * @param int The interrupt to send.
 * @param val Flags to control how the IPI should be delivered
 */
void lapic_send_ipi(int i, uint32_t val) {
	lapic_write(0x310, i << 24);
	lapic_write(0x300, val);
	do { asm volatile ("pause" : : : "memory"); } while (lapic_read(0x300) & (1 << 12));
}

/**
 * @brief Quick dumb hex parser.
 *
 * Just to support acpi= command line flag for overriding
 * the scan address for ACPI tables...
 *
 * @param c String of hexadecimal characters, optionally prefixed with '0x'
 * @return Unsigned integer interpretation of @p c
 */
uintptr_t xtoi(const char * c) {
	uintptr_t out = 0;
	if (c[0] == '0' && c[1] == 'x') {
		c += 2;
	}

	while (*c) {
		out *= 0x10;
		if (*c >= '0' && *c <= '9') {
			out += (*c - '0');
		} else if (*c >= 'a' && *c <= 'f') {
			out += (*c - 'a' + 0xa);
		} else if (*c >= 'A' && *c <= 'F') {
			out += (*c - 'A' + 0xa);
		}
		c++;
	}

	return out;
}

/**
 * @brief Called on main startup to initialize other cores.
 *
 * We always do this ourselves. We support a few different
 * bootloader conventions, and most of them don't support
 * starting up APs for us.
 */
void smp_initialize(void) {
	/* Locate ACPI tables */
	uintptr_t scan = 0xE0000;
	uintptr_t scan_top = 0x100000;
	int good = 0;

	extern struct multiboot * mboot_struct;
	extern int mboot_is_2;
	if (mboot_is_2) {
		/* A multiboot2 loader should give us a "firmware table" address
		 * that should allow us to find the RSDP. */
		extern void * mboot2_find_tag(void * fromStruct, uint32_t type);

		/* First try for an RSDPv1 */
		scan = (uintptr_t)mboot2_find_tag(mboot_struct, 14);

		/* If we didn't get one of those, try for an RSDPv2 */
		if (!scan) scan = (uintptr_t)mboot2_find_tag(mboot_struct, 15);
		/* If we didn't get one of _those_, we should really be bailing here... */

		/* Account for the tag header. */
		scan += 8;
		scan_top = scan + 0x100000;
	} else if (mboot_struct->config_table) {
		/*
		 * @warning This is specific to ToaruOS's native loader.
		 * We steal the config_table entry in our EFI loader to pass the RSDP,
		 * just like a multiboot2 loader would...
		 */
		scan = mboot_struct->config_table;
		scan_top = scan + 0x100000;
	} else if (args_present("acpi")) {
		/* If all else fails, you can provide the address yourself on the command line */
		scan = xtoi(args_value("acpi"));
		scan_top = scan + 0x100000;
	}

	/* Look for it the RSDP */
	for (; scan < scan_top; scan += 16) {
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

	/* I don't know why we do this here... */
	load_processor_info();

	/* Did we still not find our table? */
	if (!good) {
		dprintf("smp: No RSD PTR found\n");
		goto _pit_fallback;
	}

	/* Map the ACPI RSDP */
	struct rsdp_descriptor * rsdp = (struct rsdp_descriptor *)mmu_map_from_physical(scan);

	/* Validate the checksum */
	uint8_t check = 0;
	uint8_t * tmp;
	for (tmp = (uint8_t *)rsdp; (uintptr_t)tmp < (uintptr_t)rsdp + sizeof(struct rsdp_descriptor); tmp++) {
		check += *tmp;
	}

	/* Did the checksum fail? */
	if (check != 0 && !args_present("noacpichecksum")) {
		dprintf("smp: Bad checksum on RSDP (add 'noacpichecksum' to ignore this)\n");
		goto _pit_fallback; /* bad checksum */
	}

	/* Was SMP disabled by a commandline flag? */
	if (args_present("nosmp")) goto _pit_fallback;

	/* Map the RSDT from the address given by the RSDP */
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
							if (cores == 32) { /* TODO define this somewhere better */
								printf("smp: too many cores\n");
								goto _toomany;
							}
							processor_local_data[cores].cpu_id = cores;
							processor_local_data[cores].lapic_id = entry[3];
							cores++;
						}
						break;
					/* TODO: Other entries */
				}
			}
		}
	}

_toomany:
	if (!lapic_base) goto _pit_fallback;

	/* Allocate a virtual address with which we can poke the lapic */
	lapic_final = (uintptr_t)mmu_map_mmio_region(lapic_base, 0x1000);
	lapic_timer_initialize();

	if (cores <= 1) return;

	/* Get a page we can backup the previous contents of the bootstrap target page to, as it probably has mmap crap in multiboot2 */
	uintptr_t tmp_space = mmu_allocate_a_frame() << 12;
	memcpy(mmu_map_from_physical(tmp_space), mmu_map_from_physical(0x1000), 0x1000);

	*(uint32_t*)(&_ap_bootstrap_start[0xb])  = (uintptr_t)&init_page_region;
	*(uint32_t*)(&_ap_bootstrap_start[0x37]) = (uintptr_t)&_ap_premain;

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

		processor_count++;
	}

	/* Copy data back */
	memcpy(mmu_map_from_physical(0x1000), mmu_map_from_physical(tmp_space), 0x1000);
	mmu_frame_clear(tmp_space);

	dprintf("smp: enabled with %d cores\n", cores);
	return;

_pit_fallback:
	dprintf("pit: falling back to pit as preempt source\n");
	extern void pit_initialize(void);
	pit_initialize();
}

/**
 * @brief Send a soft IPI to all other cores.
 *
 * This is called by the scheduler when a process enters the ready queue,
 * to give other CPUs a chance to pick it up before their timer interrupt
 * fires. This is a soft interrupt: It should be ignored by the receiving
 * cores if they are busy with other things - we only want it to wake up
 * the HLT in the kernel idle task.
 *
 * TODO We could make this more fine-grained and deliver only to processors
 *      we think are ready, or to specific processors to aid in affinity?
 */
void arch_wakeup_others(void) {
	if (!lapic_final || processor_count < 2) return;
	/* Send broadcast IPI to others; this is a soft interrupt
	 * that just nudges idle cores out of their HLT states.
	 * It should be gentle enough that busy cores dont't care. */
	lapic_send_ipi(0, 0x7E | (3 << 18));
}

/**
 * @brief Trigger a TLB shootdown on other cores.
 *
 * XXX This is really dumb; we just send an IPI to everyone else
 *     and they reload CR3...
 *
 * @param vaddr Should have the address to flush, but not actually used.
 */
void arch_tlb_shootdown(uintptr_t vaddr) {
	if (!lapic_final || processor_count < 2) return;

	/*
	 * We should be checking if this address can be sensibly
	 * mapped somewhere else before IPIing everyone...
	 */

	lapic_send_ipi(0, 0x7C | (3 << 18));
}
