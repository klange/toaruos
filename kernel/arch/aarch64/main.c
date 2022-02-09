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
#include <kernel/signal.h>
#include <kernel/misc.h>
#include <kernel/ptrace.h>
#include <kernel/ksym.h>

#include <sys/ptrace.h>

#include <kernel/arch/aarch64/regs.h>
#include <kernel/arch/aarch64/dtb.h>

extern void fbterm_initialize(void);
extern void mmu_init(size_t memsize, size_t phys, uintptr_t firstFreePage, uintptr_t endOfInitrd);
extern void aarch64_regs(struct regs *r);
extern void fwcfg_load_initrd(uintptr_t * ramdisk_phys_base, size_t * ramdisk_size);
extern void virtio_input(void);
extern void aarch64_smp_start(void);

static volatile uint32_t * gic_regs;
static volatile uint32_t * gicc_regs;

/* ARM says the system clock tick rate is generally in
 * the range of 1-50MHz. Since we throw around integer
 * MHz ratings that's not great, so let's give it a few
 * more digits for long-term accuracy? */
uint64_t sys_timer_freq = 0;
uint64_t arch_boot_time = 0; /**< No idea where we're going to source this from, need an RTC. */
uint64_t basis_time = 0;
#define SUBSECONDS_PER_SECOND 1000000

/**
 * TODO can this be marked 'inline'?
 *
 * Read the system timer timestamp.
 */
uint64_t arch_perf_timer(void) {
	uint64_t val;
	asm volatile ("mrs %0,CNTPCT_EL0" : "=r"(val));
	return val * 100;
}

/**
 * @warning This function is incorrectly named.
 * @brief Get the frequency of the perf timer.
 *
 * This is not the CPU frequency. We do present it as such for x86-64,
 * and I think for our TSC timing that is generally true, but not here.
 */
size_t arch_cpu_mhz(void) {
	return sys_timer_freq;
}

/**
 * @brief Figure out the rate of the system timer and get boot time from RTC.
 *
 * We use the system timer as our performance tracker, as it operates at few
 * megahertz at worst which is good enough for us. We do want slightly bigger
 * numbers to make our integer divisions more accurate...
 */
static void arch_clock_initialize() {

	/* QEMU RTC */
	void * clock_addr = mmu_map_from_physical(0x09010000);

	/* Get frequency of system timer */
	uint64_t val;
	asm volatile ("mrs %0,CNTFRQ_EL0" : "=r"(val));
	sys_timer_freq = val / 10000;

	/* Get boot time from RTC */
	arch_boot_time = *(volatile uint32_t*)clock_addr;

	/* Get the "basis time" - the perf timestamp we got the wallclock time at */
	basis_time = arch_perf_timer() / sys_timer_freq;

	/* Report the reference clock speed */
	dprintf("timer: Using %ld MHz as arch_perf_timer frequency.\n", arch_cpu_mhz());
}

static void update_ticks(uint64_t ticks, uint64_t *timer_ticks, uint64_t *timer_subticks) {
	*timer_subticks = ticks - basis_time;
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

#define TIMER_IRQ 27
static void set_tick(void) {
	asm volatile (
		"mrs x0, CNTFRQ_EL0\n"
		"mov x1, 100\n"
		"udiv x0, x0, x1\n"
		"msr CNTV_TVAL_EL0, x0\n"
		:::"x0","x1");
}

void timer_start(void) {
	/* mask irqs */
	asm volatile ("msr DAIFSet, #0b1111");

	/* Enable the local timer */
	set_tick();

	asm volatile (
		"mov x0, 1\n"
		"msr CNTV_CTL_EL0, x0\n"
		:::"x0");

	/* This is global, we only need to do this once... */
	gic_regs[0] = 1;

	/* This is specific to this CPU */
	gicc_regs[0] = 1;
	gicc_regs[1] = 0x1ff;

	/* Timer interrupts are private peripherals, so each CPU gets one */
	gic_regs[64] = 0xFFFFffff; //(1 << TIMER_IRQ);
	gic_regs[160] = 0xFFFFffff; //(1 << TIMER_IRQ);

	/* These are shared? */
	gic_regs[65]  = 0xFFFFFFFF;

	for (int i = 520; i <= 521; ++i) {
		gic_regs[i] |= 0x07070707;
	}
}

static volatile uint64_t time_slice_basis = 0; /**< When the last clock update happened */
static spin_lock_t ticker_lock;
static void update_clock(void) {
	uint64_t clock_ticks = arch_perf_timer() / sys_timer_freq;
	uint64_t timer_ticks, timer_subticks;
	update_ticks(clock_ticks, &timer_ticks, &timer_subticks);

	spin_lock(ticker_lock);
	if (time_slice_basis + SUBSECONDS_PER_SECOND/4 <= clock_ticks) {
		update_process_usage(clock_ticks - time_slice_basis, sys_timer_freq);
		time_slice_basis = clock_ticks;
	}
	spin_unlock(ticker_lock);

	wakeup_sleepers(timer_ticks, timer_subticks);
}

static volatile unsigned int * _log_device_addr = 0;
static size_t _early_log_write(size_t size, uint8_t * buffer) {
	for (unsigned int i = 0; i < size; ++i) {
		*_log_device_addr = buffer[i];
	}
	return size;
}

static void early_log_initialize(void) {
	/* QEMU UART */
	_log_device_addr = mmu_map_from_physical(0x09000000);
	printf_output = &_early_log_write;
}

void arch_set_core_base(uintptr_t base) {
	/* It doesn't actually seem that this register has
	 * any real meaning, it's just available for us
	 * to load with our thread pointer. It's possible
	 * that the 'mrs' for it is just as fast as regular
	 * register reference? */
	asm volatile ("msr TPIDR_EL1,%0" : : "r"(base));

	/* this_cpu pointer, which we can tell gcc is reserved
	 * by our ABI and then bind as a 'register' variable. */
	asm volatile ("mrs x18, TPIDR_EL1");
}

void arch_set_tls_base(uintptr_t tlsbase) {
	asm volatile ("msr TPIDR_EL0,%0" : : "r"(tlsbase));
}

void arch_set_kernel_stack(uintptr_t stack) {
	/* This is currently unused... it seems we're handling
	 * things correctly and getting the right stack already,
	 * but XXX should look into this later. */
	this_core->sp_el1 = stack;
}

void arch_wakeup_others(void) {
	/* wakeup */
}

char * _arch_args = NULL;
static void dtb_locate_cmdline(void) {
	uint32_t * chosen = dtb_find_node("chosen");
	if (chosen) {
		uint32_t * prop = dtb_node_find_property(chosen, "bootargs");
		if (prop) {
			_arch_args = (char*)&prop[2];
			args_parse((char*)&prop[2]);
		}
	}
}

static void exception_handlers(void) {
	extern char _exception_vector[];

	const uintptr_t gic_base = (uintptr_t)mmu_map_from_physical(0x08000000); /* TODO get this from dtb */
	gic_regs = (volatile uint32_t*)gic_base;

	const uintptr_t gicc_base = (uintptr_t)mmu_map_from_physical(0x08010000);
	gicc_regs = (volatile uint32_t*)gicc_base;

	asm volatile("msr VBAR_EL1, %0" :: "r"(&_exception_vector));
}

void aarch64_sync_enter(struct regs * r) {
	uint64_t esr, far, elr, spsr;
	asm volatile ("mrs %0, ESR_EL1" : "=r"(esr));
	asm volatile ("mrs %0, FAR_EL1" : "=r"(far));
	asm volatile ("mrs %0, ELR_EL1" : "=r"(elr));
	asm volatile ("mrs %0, SPSR_EL1" : "=r"(spsr));

	if (this_core->current_process) {
		this_core->current_process->time_switch = arch_perf_timer();
	}

	if ((esr >> 26) == 0x32) {
		/* Single step trap */
		uint64_t val;
		asm volatile("mrs %0, MDSCR_EL1" : "=r"(val));
		val &= ~(1 << 0);
		asm volatile("msr MDSCR_EL1, %0" :: "r"(val));

		if (this_core->current_process->flags & PROC_FLAG_TRACE_SIGNALS) {
			ptrace_signal(SIGTRAP, PTRACE_EVENT_SINGLESTEP);
		}

		return;
	}

	/* Magic thread exit */
	if (elr == 0xFFFFB00F && far == 0xFFFFB00F) {
		task_exit(0);
		__builtin_unreachable();
	}

	/* Magic signal return */
	if (elr == 0x8DEADBEEF && far == 0x8DEADBEEF) {
		return_from_signal_handler();
		return;
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
	dprintf("In process %d (%s)\n", this_core->current_process->id, this_core->current_process->name);
	dprintf("ESR: %#zx FAR: %#zx ELR: %#zx SPSR: %#zx\n", esr, far, elr, spsr);
	aarch64_regs(r);
	uint64_t tpidr_el0;
	asm volatile ("mrs %0, TPIDR_EL0" : "=r"(tpidr_el0));
	dprintf("  TPIDR_EL0=%#zx\n", tpidr_el0);

	send_signal(this_core->current_process->id, SIGSEGV, 1);
}

char _ret_from_preempt_source[1];

struct irq_callback {
	void (*callback)(process_t * this, int irq, void *data);
	process_t * owner;
	void * data;
};

extern struct irq_callback irq_callbacks[];


#define EOI(x) do { gicc_regs[4] = (x); } while (0)
static void aarch64_interrupt_dispatch(int from_wfi) {
	uint32_t iar = gicc_regs[3];
	uint32_t irq = iar & 0x3FF;
	//uint32_t cpu = (iar >> 10) & 0x7;

	switch (irq) {
		case TIMER_IRQ:
			update_clock();
			set_tick();
			EOI(iar);
			if (from_wfi) {
				switch_next();
			} else {
				switch_task(1);
			}
			return;

		case 32:
		case 33:
		case 34:
		case 35:
		case 36:
		case 37:
		case 38:
		case 39:
		case 40:
		case 41:
		case 42:
		{
			struct irq_callback * cb = &irq_callbacks[irq-32];
			if (cb->owner) {
				cb->callback(cb->owner, irq-32, cb->data);
			}
			EOI(iar);
			return;
		}

		case 1022:
		case 1023:
			return;

		default:
			dprintf("gic: Unhandled interrupt: %d\n", irq);
			EOI(iar);
			return;
	}
}

void aarch64_irq_enter(struct regs * r) {
	if (this_core->current_process) {
		this_core->current_process->time_switch = arch_perf_timer();
	}

	aarch64_interrupt_dispatch(0);
}

/**
 * @brief Kernel fault handler.
 */
void aarch64_fault_enter(struct regs * r) {
	uint64_t esr, far, elr, spsr;
	asm volatile ("mrs %0, ESR_EL1" : "=r"(esr));
	asm volatile ("mrs %0, FAR_EL1" : "=r"(far));
	asm volatile ("mrs %0, ELR_EL1" : "=r"(elr));
	asm volatile ("mrs %0, SPSR_EL1" : "=r"(spsr));

	dprintf("EL1-EL1 fault handler, core %d\n", this_core->cpu_id);
	dprintf("In process %d (%s)\n", this_core->current_process->id, this_core->current_process->name);
	dprintf("ESR: %#zx FAR: %#zx ELR: %#zx SPSR: %#zx\n", esr, far, elr, spsr);
	aarch64_regs(r);

	uint64_t tpidr_el0;
	asm volatile ("mrs %0, TPIDR_EL0" : "=r"(tpidr_el0));
	dprintf("  TPIDR_EL0=%#zx\n", tpidr_el0);

	extern void aarch64_safe_dump_traceback(uintptr_t elr, struct regs * r);
	aarch64_safe_dump_traceback(elr, r);

	while (1);
}

/**
 * @brief Enable FPU and NEON (SIMD)
 *
 * This enables the FPU in EL0. I'm not sure if we can enable it
 * there but not in EL1... that would be nice to avoid accidentally
 * introducing FPU code in the kernel that would corrupt our FPU state.
 */
void fpu_enable(void) {
	uint64_t cpacr_el1;
	asm volatile ("mrs %0, CPACR_EL1" : "=r"(cpacr_el1));
	cpacr_el1 |= (3 << 20) | (3 << 16);
	asm volatile ("msr CPACR_EL1, %0" :: "r"(cpacr_el1));
}

/**
 * @brief Called in a loop by kernel idle tasks.
 */
void arch_pause(void) {

	/* XXX This actually works even if we're masking interrupts, but
	 * the interrupt function won't be called, so we'll need to change
	 * it once we start getting actual hardware interrupts. */
	asm volatile ("wfi");
	aarch64_interrupt_dispatch(1);
}

/**
 * @brief Force a cache clear across an address range.
 *
 * GCC has a __clear_cache() function that is supposed to do this
 * but I haven't figured out what bits I need to set in what system
 * register to allow that to be callable from EL0, so we actually expose
 * it as a new sysfunc system call for now. We'll be generous and quietly
 * skip regions that are not accessible to the calling process.
 *
 * This is critical for the dynamic linker to reset the icache for
 * regions that have been loaded from new libraries.
 */
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

void aarch64_processor_data(void) {
	asm volatile ("mrs %0, MIDR_EL1" : "=r"(this_core->midr));
}

static void symbols_install(void) {
	ksym_install();
	kernel_symbol_t * k = (kernel_symbol_t *)&kernel_symbols_start;
	while ((uintptr_t)k < (uintptr_t)&kernel_symbols_end) {
		ksym_bind(k->name, (void*)k->addr);
		k = (kernel_symbol_t *)((uintptr_t)k + sizeof *k + strlen(k->name) + 1);
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
int kmain(uintptr_t dtb_base, uintptr_t phys_base) {
	early_log_initialize();

	console_set_output(_early_log_write);

	extern uintptr_t aarch64_kernel_phys_base;
	aarch64_kernel_phys_base = phys_base;

	extern uintptr_t aarch64_dtb_phys;
	aarch64_dtb_phys = dtb_base;

	dprintf("%s %d.%d.%d-%s %s %s\n",
		__kernel_name,
		__kernel_version_major,
		__kernel_version_minor,
		__kernel_version_lower,
		__kernel_version_suffix,
		__kernel_version_codename,
		__kernel_arch);

	dprintf("boot: dtb @ %#zx kernel @ %#zx\n",
		dtb_base, phys_base);

	/* Initialize TPIDR_EL1 */
	arch_set_core_base((uintptr_t)&processor_local_data[0]);

	/* Set up the system timer and get an RTC time. */
	arch_clock_initialize();

	/* Set up exception handlers early... */
	exception_handlers();

	/* Load ramdisk over fw-cfg. */
	uintptr_t ramdisk_phys_base = 0;
	size_t ramdisk_size = 0;
	fwcfg_load_initrd(&ramdisk_phys_base, &ramdisk_size);

	/* Probe DTB for memory layout. */
	extern char end[];
	size_t memaddr, memsize;
	dtb_memory_size(&memaddr, &memsize);

	/* Initialize the MMU based on the memory we got from dtb */
	mmu_init(
		memaddr, memsize,
		0x40100000 /* Should be end of DTB, but we're really just guessing */,
		(uintptr_t)&end + ramdisk_size - 0xffffffff80000000UL);

	/* Find the cmdline */
	dtb_locate_cmdline();

	/* Set up all the other arch-specific stuff here */
	fpu_enable();

	symbols_install();

	generic_startup();

	/* Initialize the framebuffer and fbterm here */
	framebuffer_initialize();
	fbterm_initialize();

	/* Ramdisk */
	ramdisk_mount(ramdisk_phys_base, ramdisk_size);

	/* Load MIDR */
	aarch64_processor_data();

	/* Start other cores here */
	aarch64_smp_start();

	/* Set up the system virtual timer to produce interrupts for userspace scheduling */
	timer_start();

	/* Install drivers that may need to sleep here */
	virtio_input();

	generic_main();

	return 0;
}

