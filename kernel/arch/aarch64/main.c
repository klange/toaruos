/**
 * @file  kernel/arch/aarch64/main.c
 * @brief Kernel C entry point and initialization for QEMU aarch64 'virt' machine.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021-2022 K. Lange
 */
#include <stdint.h>
#include <kernel/symboltable.h>
#include <kernel/process.h>
#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/version.h>
#include <kernel/pci.h>
#include <kernel/args.h>
#include <kernel/ramdisk.h>
#include <kernel/vfs.h>
#include <kernel/mmu.h>
#include <kernel/generic.h>
#include <kernel/video.h>

#include <kernel/arch/aarch64/regs.h>
#include <kernel/arch/aarch64/dtb.h>

extern void fbterm_initialize(void);
extern void mmu_init(size_t memsize, size_t phys, uintptr_t firstFreePage, uintptr_t endOfInitrd);
extern void aarch64_regs(struct regs *r);
extern void fwcfg_load_initrd(uintptr_t * ramdisk_phys_base, size_t * ramdisk_size);
extern void virtio_input(void);

/* ARM says the system clock tick rate is generally in
 * the range of 1-50MHz. Since we throw around integer
 * MHz ratings that's not great, so let's give it a few
 * more digits for long-term accuracy? */
uint64_t sys_timer_freq = 0;
uint64_t arch_boot_time = 0; /**< No idea where we're going to source this from, need an RTC. */
uint64_t basis_time = 0;
#define SUBSECONDS_PER_SECOND 1000000

uint64_t arch_perf_timer(void) {
	uint64_t val;
	asm volatile ("mrs %0,CNTPCT_EL0" : "=r"(val));
	return val * 100;
}

size_t arch_cpu_mhz(void) {
	return sys_timer_freq;
}

static void arch_clock_initialize() {
	void * clock_addr = mmu_map_from_physical(0x09010000);
	uint64_t val;
	asm volatile ("mrs %0,CNTFRQ_EL0" : "=r"(val));
	sys_timer_freq = val / 10000;
	arch_boot_time = *(volatile uint32_t*)clock_addr;
	basis_time = arch_perf_timer() / sys_timer_freq;

	dprintf("timer: Using %ld MHz as arch_perf_timer frequency.\n", arch_cpu_mhz());
}

static void update_ticks(uint64_t ticks, uint64_t *timer_ticks, uint64_t *timer_subticks) {
	*timer_subticks = ticks - basis_time; /* should be basis time from when we read RTC */
	*timer_ticks = *timer_subticks / SUBSECONDS_PER_SECOND;
	*timer_subticks = *timer_subticks % SUBSECONDS_PER_SECOND;
}

int gettimeofday(struct timeval * t, void *z) {
	uint64_t tsc = arch_perf_timer();
	uint64_t timer_ticks, timer_subticks;
	update_ticks(tsc / sys_timer_freq, &timer_ticks, &timer_subticks);
	t->tv_sec = arch_boot_time + timer_ticks;
	t->tv_usec = timer_subticks;
	return 0;
}

uint64_t now(void) {
	struct timeval t;
	gettimeofday(&t, NULL);
	return t.tv_sec;
}

void relative_time(unsigned long seconds, unsigned long subseconds, unsigned long * out_seconds, unsigned long * out_subseconds) {
	if (!arch_boot_time) {
		*out_seconds = 0;
		*out_subseconds = 0;
		return;
	}

	uint64_t tsc = arch_perf_timer();
	uint64_t timer_ticks, timer_subticks;
	update_ticks(tsc / sys_timer_freq, &timer_ticks, &timer_subticks);
	if (subseconds + timer_subticks >= SUBSECONDS_PER_SECOND) {
		*out_seconds    = timer_ticks + seconds + (subseconds + timer_subticks) / SUBSECONDS_PER_SECOND;
		*out_subseconds = (subseconds + timer_subticks) % SUBSECONDS_PER_SECOND;
	} else {
		*out_seconds    = timer_ticks + seconds;
		*out_subseconds = timer_subticks + subseconds;
	}
}

void arch_dump_traceback(void) {
	
}


static volatile unsigned int * _log_device_addr = 0;
static size_t _early_log_write(size_t size, uint8_t * buffer) {
	for (unsigned int i = 0; i < size; ++i) {
		*_log_device_addr = buffer[i];
	}
	return size;
}

static void early_log_initialize(void) {
	_log_device_addr = mmu_map_from_physical(0x09000000);
	printf_output = &_early_log_write;
}

void arch_set_core_base(uintptr_t base) {
	asm volatile ("msr TPIDR_EL1,%0" : : "r"(base));
	asm volatile ("mrs x18, TPIDR_EL1");
}

void arch_set_tls_base(uintptr_t tlsbase) {
	asm volatile ("msr TPIDR_EL0,%0" : : "r"(tlsbase));
}

void arch_set_kernel_stack(uintptr_t stack) {
	this_core->sp_el1 = stack;
}

void arch_wakeup_others(void) {
	/* wakeup */
}

static void scan_hit_list(uint32_t device, uint16_t vendorid, uint16_t deviceid, void * extra) {
	printf("%02x:%02x.%d (%04x, %04x:%04x)\n",
			(int)pci_extract_bus(device),
			(int)pci_extract_slot(device),
			(int)pci_extract_func(device),
			(int)pci_find_type(device),
			vendorid,
			deviceid);

	printf(" BAR0: 0x%08x", pci_read_field(device, PCI_BAR0, 4));
	printf(" BAR1: 0x%08x", pci_read_field(device, PCI_BAR1, 4));
	printf(" BAR2: 0x%08x", pci_read_field(device, PCI_BAR2, 4));
	printf(" BAR3: 0x%08x", pci_read_field(device, PCI_BAR3, 4));
	printf(" BAR4: 0x%08x", pci_read_field(device, PCI_BAR4, 4));
	printf(" BAR5: 0x%08x\n", pci_read_field(device, PCI_BAR5, 4));

	printf(" IRQ Line: %d", pci_read_field(device, 0x3C, 1));
	printf(" IRQ Pin: %d", pci_read_field(device, 0x3D, 1));
	printf(" Interrupt: %d", pci_get_interrupt(device));
	printf(" Status: 0x%04x\n", pci_read_field(device, PCI_STATUS, 2));
}

static void list_dir(const char * dir) {
	fs_node_t * root = kopen(dir,0);
	if (root) {
		uint64_t index = 0;
		dprintf("listing %s: ", dir);
		while (1) {
			struct dirent * d = readdir_fs(root, index);
			if (!d) break;

			dprintf("\a  %s", d->d_name);

			free(d);
			index++;
		}
		dprintf("\a\n");
	}
	close_fs(root);
}

char * _arch_args = NULL;
static void dtb_locate_cmdline(void) {
	uint32_t * chosen = find_node("chosen");
	if (chosen) {
		uint32_t * prop = node_find_property(chosen, "bootargs");
		if (prop) {
			_arch_args = (char*)&prop[2];
			args_parse((char*)&prop[2]);
		}
	}
}

static volatile uint32_t * gic_regs;
static volatile uint32_t * gicc_regs;

static void exception_handlers(void) {
	extern char _exception_vector[];

	const uintptr_t gic_base = (uintptr_t)mmu_map_from_physical(0x08000000); /* TODO get this from dtb */
	gic_regs = (volatile uint32_t*)gic_base;

	const uintptr_t gicc_base = (uintptr_t)mmu_map_from_physical(0x08010000);
	gicc_regs = (volatile uint32_t*)gicc_base;

	asm volatile("msr VBAR_EL1, %0" :: "r"(&_exception_vector));
}

static void update_clock(void);
void aarch64_sync_enter(struct regs * r) {
	uint64_t esr, far, elr, spsr;
	asm volatile ("mrs %0, ESR_EL1" : "=r"(esr));
	asm volatile ("mrs %0, FAR_EL1" : "=r"(far));
	asm volatile ("mrs %0, ELR_EL1" : "=r"(elr));
	asm volatile ("mrs %0, SPSR_EL1" : "=r"(spsr));

	if (this_core->current_process) {
		this_core->current_process->time_switch = arch_perf_timer();
	}

	/* Magic thread exit */
	if (elr == 0xFFFFB00F && far == 0xFFFFB00F) {
		task_exit(0);
		__builtin_unreachable();
	}

	/* System call */
	if ((esr >> 26) == 0x15) {
		extern void syscall_handler(struct regs *);
		syscall_handler(r);
		return;
	}

	/* KVM is mad at us; usually means our code is broken or we neglected a cache. */
	if (far == 0x1de7ec7edbadc0de) {
		printf("kvm: blip (esr=%#zx, elr=%#zx; pid=%d [%s])\n", esr, elr, this_core->current_process->id, this_core->current_process->name);
		return;
	}

	/* Unexpected fault, eg. page fault. */
	printf("In process %d (%s)\n", this_core->current_process->id, this_core->current_process->name);
	printf("ESR: %#zx FAR: %#zx ELR: %#zx SPSR: %#zx\n", esr, far, elr, spsr);
	aarch64_regs(r);
	uint64_t tpidr_el0;
	asm volatile ("mrs %0, TPIDR_EL0" : "=r"(tpidr_el0));
	printf("  TPIDR_EL0=%#zx\n", tpidr_el0);

	while (1);
	task_exit(1);
}

#define TIMER_IRQ 27
static void set_tick(void) {
	asm volatile (
		"mrs x0, CNTFRQ_EL0\n"
		"mov x1, 100\n"
		"udiv x0, x0, x1\n"
		"msr CNTV_TVAL_EL0, x0\n"
		:::"x0","x1");
}

void aarch64_irq_enter(struct regs * r) {
	uint32_t pending = gic_regs[160];

	if (this_core->current_process) {
		this_core->current_process->time_switch = arch_perf_timer();
	}

	if (pending & (1 << TIMER_IRQ)) {
		update_clock();
		set_tick();
		gic_regs[160] &= (1 << TIMER_IRQ);
		switch_task(1);
		return;
	} else if (!pending) {
		return;
	}

	printf("Unexpected interrupt = %#x\n", pending);

	while (1);
}

void aarch64_fault_enter(struct regs * r) {
	uint64_t esr, far, elr, spsr;
	asm volatile ("mrs %0, ESR_EL1" : "=r"(esr));
	asm volatile ("mrs %0, FAR_EL1" : "=r"(far));
	asm volatile ("mrs %0, ELR_EL1" : "=r"(elr));
	asm volatile ("mrs %0, SPSR_EL1" : "=r"(spsr));

	printf("EL1-EL1 fault handler\n");
	printf("In process %d (%s)\n", this_core->current_process->id, this_core->current_process->name);
	printf("ESR: %#zx FAR: %#zx ELR: %#zx SPSR: %#zx\n", esr, far, elr, spsr);
	aarch64_regs(r);

	uint64_t tpidr_el0;
	asm volatile ("mrs %0, TPIDR_EL0" : "=r"(tpidr_el0));
	printf("  TPIDR_EL0=%#zx\n", tpidr_el0);

	while (1);
}

static void fpu_enable(void) {
	uint64_t cpacr_el1;
	asm volatile ("mrs %0, CPACR_EL1" : "=r"(cpacr_el1));
	cpacr_el1 |= (3 << 20) | (3 << 16);
	asm volatile ("msr CPACR_EL1, %0" :: "r"(cpacr_el1));
}

static void timer_start(void) {
	/* mask irqs */
	asm volatile ("msr DAIFSet, #0b1111");

	/* Enable one of the timers. */
	set_tick();
	asm volatile (
		"mov x0, 1\n"
		"msr CNTV_CTL_EL0, x0\n"
		:::"x0");

	/* enable */
	gic_regs[0] = 1;
	gicc_regs[0] = 1;

	/* priority mask */
	gicc_regs[1] = 0xff;

	/* enable interrupts */
	gic_regs[64] = (1 << TIMER_IRQ);

	/* clear this one */
	gic_regs[160] = (1 << TIMER_IRQ);
}

static uint64_t time_slice_basis = 0; /**< When the last clock update happened */
static void update_clock(void) {
	uint64_t clock_ticks = arch_perf_timer() / sys_timer_freq;
	uint64_t timer_ticks, timer_subticks;
	update_ticks(clock_ticks, &timer_ticks, &timer_subticks);

	if (time_slice_basis + SUBSECONDS_PER_SECOND/4 <= clock_ticks) {
		update_process_usage(clock_ticks - time_slice_basis, sys_timer_freq);
		time_slice_basis = clock_ticks;
	}

	wakeup_sleepers(timer_ticks, timer_subticks);
}

/**
 * @brief Called in a loop by kernel idle tasks.
 */
void arch_pause(void) {
	/* TODO: This needs to make sure interrupt delivery is enabled.
	 *       Though that would also require us to be turn it off
	 *       in the first place... get around to this when we have
	 *       literally anything set up in the GIC */
	asm volatile ("wfi");

	//this_core->current_process->time_switch = arch_perf_timer();
	update_clock();
	set_tick();

	asm volatile (
		".globl _ret_from_preempt_source\n"
		"_ret_from_preempt_source:"
	);

	switch_next();
}

void arch_clear_icache(uintptr_t start, uintptr_t end) {
	for (uintptr_t x = start; x < end; x += 64) {
		if (!mmu_validate_user_pointer((void*)x, 64, MMU_PTR_WRITE)) continue;
		asm volatile ("dc cvau, %0" :: "r"(x));
	}
	for (uintptr_t x = start; x < end; x += 64) {
		if (!mmu_validate_user_pointer((void*)x, 64, MMU_PTR_WRITE)) continue;
		asm volatile ("ic ivau, %0" :: "r"(x));
	}
}


/**
 * Main kernel C entrypoint for qemu's -machine virt
 *
 * By this point, a 'bootstub' has already set up some
 * initial page tables so the linear physical mapping
 * is where we would normally expect it to be, we're
 * at -2GiB, and there's some other mappings so that
 * a bit of RAM is 1:1.
 */
int kmain(void) {
	early_log_initialize();

	dprintf("%s %d.%d.%d-%s %s %s\n",
		__kernel_name,
		__kernel_version_major,
		__kernel_version_minor,
		__kernel_version_lower,
		__kernel_version_suffix,
		__kernel_version_codename,
		__kernel_arch);

	/* Initialize TPIDR_EL1 */
	arch_set_core_base((uintptr_t)&processor_local_data[0]);

	/* Set up the system timer and get an RTC time. */
	arch_clock_initialize();

	/* Set up exception handlers early... */
	exception_handlers();

	/* TODO load boot data from DTB?
	 *      We want to know memory information... */
	uintptr_t ramdisk_phys_base = 0;
	size_t ramdisk_size = 0;
	fwcfg_load_initrd(&ramdisk_phys_base, &ramdisk_size);

	/* TODO get initial memory map data?
	 *    Eh, we can probably just probe the existing tables... maybe... */
	extern char end[];
	size_t memsize, physsize;
	dtb_memory_size(&memsize, &physsize);
	mmu_init(
		memsize, physsize,
		0x40100000 /* Should be end of DTB, but we're really just guessing */,
		(uintptr_t)&end + ramdisk_size - 0xffffffff80000000UL);

	/* Find the cmdline */
	dtb_locate_cmdline();

	/* TODO Set up all the other arch-specific stuff here */
	fpu_enable();

	generic_startup();

	/* Initialize the framebuffer and fbterm here */
	framebuffer_initialize();
	fbterm_initialize();

	/* TODO Start other cores here */

	/* Ramdisk */
	ramdisk_mount(ramdisk_phys_base, ramdisk_size);

	/* TODO Start preemption source here */
	timer_start();

	/* Install drivers that may need to sleep here */
	virtio_input();

	generic_main();

	return 0;
}

