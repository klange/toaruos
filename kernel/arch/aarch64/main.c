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
#include <errno.h>

#include <sys/ptrace.h>

#include <kernel/arch/aarch64/regs.h>
#include <kernel/arch/aarch64/dtb.h>
#include <kernel/arch/aarch64/gic.h>
#include <kernel/arch/aarch64/rpi.h>

extern void fbterm_initialize(void);
extern void mmu_init(size_t memsize, size_t phys, uintptr_t firstFreePage, uintptr_t endOfInitrd);
extern void aarch64_regs(struct regs *r);
extern void fwcfg_load_initrd(uintptr_t * ramdisk_phys_base, size_t * ramdisk_size);
extern void virtio_input(void);
extern void aarch64_smp_start(void);

extern char end[];
extern char * _arch_args;

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
static void arch_clock_initialize(uintptr_t rpi_tag) {

	/* Get frequency of system timer */
	uint64_t val;
	asm volatile ("mrs %0,CNTFRQ_EL0" : "=r"(val));
	sys_timer_freq = val / 10000;

	/* Get boot time from RTC */
	if (rpi_tag) {
		arch_boot_time = 1644908027UL;
	} else {
		/* QEMU RTC */
		void * clock_addr = mmu_map_from_physical(0x09010000);
		arch_boot_time = *(volatile uint32_t*)clock_addr;
	}

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

static spin_lock_t _time_set_lock;

int settimeofday(struct timeval * t, void *z) {
	if (!t) return -EINVAL;
	if (t->tv_sec < 0 || t->tv_usec < 0 || t->tv_usec > 1000000) return -EINVAL;

	spin_lock(_time_set_lock);
	uint64_t clock_time = now();
	arch_boot_time += t->tv_sec - clock_time;
	spin_unlock(_time_set_lock);

	return 0;
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
		"mov x1, 100\n" // without this, one second
		"udiv x0, x0, x1\n"
		"msr CNTV_TVAL_EL0, x0\n"
		"mov x0, 1\n"
		"msr CNTV_CTL_EL0, x0\n"
		:::"x0","x1");
}

void timer_start(void) {
	/* mask irqs */
	asm volatile ("msr DAIFSet, #0b1111");

	/* Enable the local timer */
	set_tick();

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
	gic_regs[66]  = 0xFFFFFFFF;
	gic_regs[67]  = 0xFFFFFFFF;

	gic_regs[520] = 0x07070707;
	gic_regs[521] = 0x07070707;
	gic_regs[543] = 0x07070707;
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

static void exception_handlers(void) {
	extern char _exception_vector[];

	asm volatile("msr VBAR_EL1, %0" :: "r"(&_exception_vector));
}

void aarch64_sync_enter(struct regs * r) {
	uint64_t esr, far, elr, spsr;
	asm volatile ("mrs %0, ESR_EL1" : "=r"(esr));
	asm volatile ("mrs %0, FAR_EL1" : "=r"(far));
	asm volatile ("mrs %0, ELR_EL1" : "=r"(elr));
	asm volatile ("mrs %0, SPSR_EL1" : "=r"(spsr));

	#if 0
	dprintf("EL0-EL1 sync: %d (%s) ESR: %#zx FAR: %#zx ELR: %#zx SPSR: %#zx\n",
		this_core ? (this_core->current_process ? this_core->current_process->id : -1) : -1,
		this_core ? (this_core->current_process ? this_core->current_process->name : "?") : "?",
		esr, far, elr, spsr);
	#endif

	if (esr == 0x2000000) {
		arch_fatal_prepare();
		dprintf("Unknown exception: ESR: %#zx FAR: %#zx ELR: %#zx SPSR: %#zx\n", esr, far, elr, spsr);
		dprintf("Instruction at ELR: 0x%08x\n", *(uint32_t*)elr);
		arch_dump_traceback();
		aarch64_regs(r);
		arch_fatal();
	}

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

		goto _resume_user;
	}

	/* Magic signal return */
	if (elr == 0x8DEADBEEF && far == 0x8DEADBEEF) {
		return_from_signal_handler(r);
		goto _resume_user;
	}

	/* System call */
	if ((esr >> 26) == 0x15) {
		//dprintf("pid %d syscall %zd elr=%#zx\n",
		//	this_core->current_process->id, r->x0, elr);
		extern void syscall_handler(struct regs *);
		syscall_handler(r);
		goto _resume_user;
	}

	/* KVM is mad at us; usually means our code is broken or we neglected a cache. */
	if (far == 0x1de7ec7edbadc0de) {
		printf("kvm: blip (esr=%#zx, elr=%#zx; pid=%d [%s])\n", esr, elr, this_core->current_process->id, this_core->current_process->name);
		goto _resume_user;
	}

	/* Unexpected fault, eg. page fault. */
	dprintf("In process %d (%s)\n", this_core->current_process->id, this_core->current_process->name);
	dprintf("ESR: %#zx FAR: %#zx ELR: %#zx SPSR: %#zx\n", esr, far, elr, spsr);
	aarch64_regs(r);
	uint64_t tpidr_el0;
	asm volatile ("mrs %0, TPIDR_EL0" : "=r"(tpidr_el0));
	dprintf("  TPIDR_EL0=%#zx\n", tpidr_el0);

	send_signal(this_core->current_process->id, SIGSEGV, 1);

_resume_user:
	process_check_signals(r);
}

static void spin(void) {
	while (1) {
		asm volatile ("wfi");
	}
}

char _ret_from_preempt_source[1];

#define EOI(x) do { \
	gicc_regs[4] = (x); \
} while (0)
void aarch64_interrupt_dispatch(int from_wfi) {
	uint32_t iar = gicc_regs[3];
	uint32_t irq = iar & 0x3FF;
	/* Currently we aren't using the CPU value and I'm not sure we have any use for it, we know who we are? */
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

		case 1:
			EOI(iar);
			if (from_wfi) switch_next();
			break;

		/* Arbitrarily chosen SGI for panic signal from another core */
		case 2:
			spin();
			break;

		case 1022:
		case 1023:
			return;

		default:
			if (irq >= 32 && irq < 1022) {
				struct irq_callback * cb = irq_callbacks[irq-32];
				if (cb) {
					while (cb) {
						int res = cb->callback(cb->owner, irq-32, cb->data);
						if (res) break;
						cb = cb->next;
					}
					/* Maybe warn? We have a lot of spurious irqs, though */
				} else {
					dprintf("irq: unhandled irq %d\n", irq);
				}
				EOI(iar);
			} else {
				dprintf("gic: Unhandled interrupt: %d\n", irq);
				EOI(iar);
			}
			return;
	}
}

void aarch64_irq_enter(struct regs * r) {
	if (this_core->current_process) {
		this_core->current_process->time_switch = arch_perf_timer();
	}

	aarch64_interrupt_dispatch(0);

	process_check_signals(r);
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

	arch_fatal_prepare();

	dprintf("EL1-EL1 fault handler, core %d\n", this_core->cpu_id);
	if (this_core && this_core->current_process) {
		dprintf("In process %d (%s)\n", this_core->current_process->id, this_core->current_process->name);
	}
	dprintf("ESR: %#zx FAR: %#zx ELR: %#zx SPSR: %#zx\n", esr, far, elr, spsr);
	aarch64_regs(r);

	uint64_t tpidr_el0;
	asm volatile ("mrs %0, TPIDR_EL0" : "=r"(tpidr_el0));
	dprintf("  TPIDR_EL0=%#zx\n", tpidr_el0);

	extern void aarch64_safe_dump_traceback(uintptr_t elr, struct regs * r);
	aarch64_safe_dump_traceback(elr, r);

	arch_fatal();
}

void aarch64_sp0_fault_enter(struct regs * r) {
	arch_fatal_prepare();
	dprintf("EL1-EL1 sp0 entry?\n");
	arch_fatal();
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

	/* Enable access to physical timer */
	uint64_t clken = 0;
	asm volatile ("mrs %0,CNTKCTL_EL1" : "=r"(clken));
	clken |= (1 << 0);
	asm volatile ("msr CNTKCTL_EL1,%0" :: "r"(clken));
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
int kmain(uintptr_t dtb_base, uintptr_t phys_base, uintptr_t rpi_tag) {

	extern uintptr_t aarch64_kernel_phys_base;
	aarch64_kernel_phys_base = phys_base;

	extern uintptr_t aarch64_dtb_phys;
	aarch64_dtb_phys = dtb_base;

	if (rpi_tag) {
		extern uint8_t * lfb_vid_memory;
		extern uint16_t lfb_resolution_x;
		extern uint16_t lfb_resolution_y;
		extern uint16_t lfb_resolution_b;
		extern uint32_t lfb_resolution_s;
		extern size_t lfb_memsize;

		struct rpitag * tag = (struct rpitag*)rpi_tag;

		lfb_vid_memory = mmu_map_from_physical(tag->phys_addr);
		lfb_resolution_x = tag->x;
		lfb_resolution_y = tag->y;
		lfb_resolution_s = tag->s;
		lfb_resolution_b = tag->b;
		lfb_memsize = tag->size;

		fbterm_initialize();
	} else {
		early_log_initialize();
	}

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
	arch_clock_initialize(rpi_tag);

	/* Set up exception handlers early... */
	exception_handlers();

	/* Load ramdisk over fw-cfg. */
	uintptr_t ramdisk_phys_base = 0;
	size_t ramdisk_size = 0;
	if (rpi_tag) {
		/* XXX Should this whole set of things be a "platform_init()" thing, where we
		 *     figure out the platform and just do the stuff? */
		struct rpitag * tag = (struct rpitag*)rpi_tag;
		rpi_load_ramdisk(tag, &ramdisk_phys_base, &ramdisk_size);

		/* TODO figure out memory size - mailbox commands */
		mmu_init(0, 512 * 1024 * 1024,
			0x80000,
			(uintptr_t)&end + ramdisk_size - 0xffffffff80000000UL);

		dprintf("rpi: mmu reinitialized\n");

		rpi_set_cmdline(&_arch_args);

	} else {
		/*
		 * TODO virt shim should load the ramdisk for us, so we can use the same code
		 *      as we have for the RPi above and not have to use fwcfg to load it...
		 */
		fwcfg_load_initrd(&ramdisk_phys_base, &ramdisk_size);

		/* Probe DTB for memory layout. */
		size_t memaddr, memsize;
		dtb_memory_size(&memaddr, &memsize);

		/* Initialize the MMU based on the memory we got from dtb */
		mmu_init(
			memaddr, memsize,
			0x40100000 /* Should be end of DTB, but we're really just guessing */,
			(uintptr_t)&end + ramdisk_size - 0xffffffff80000000UL);

		/* Find the cmdline */
		dtb_locate_cmdline(&_arch_args);
	}

	gic_map_regs(rpi_tag);

	/* Set up all the other arch-specific stuff here */
	fpu_enable();

	symbols_install();

	generic_startup();

	/* Initialize the framebuffer and fbterm here */
	framebuffer_initialize();

	if (!rpi_tag) {
		fbterm_initialize();
	}

	/* Ramdisk */
	ramdisk_mount(ramdisk_phys_base, ramdisk_size);

	extern void dtb_device(void);
	dtb_device();

	/* Load MIDR */
	aarch64_processor_data();

	/* Set up the system virtual timer to produce interrupts for userspace scheduling */
	timer_start();

	/* Start other cores here */
	if (!rpi_tag ){
		aarch64_smp_start();

		/* Install drivers that may need to sleep here */
		virtio_input();

		/* Set up serial input */
		extern void pl011_start(void);
		pl011_start();
	} else {

		extern void rpi_smp_init(void);
		rpi_smp_init();

		extern void null_input(void);
		null_input();

		extern void miniuart_start(void);
		miniuart_start();
	}

	generic_main();

	return 0;
}

