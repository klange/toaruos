/**
 * @file  kernel/arch/x86_64/user.c
 * @brief Various assembly snippets for jumping to usermode and back.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021 K. Lange
 */
#include <stdint.h>
#include <kernel/symboltable.h>
#include <kernel/process.h>
#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/version.h>
#include <kernel/pci.h>
#include <kernel/args.h>
#include <kernel/gzip.h>
#include <kernel/ramdisk.h>
#include <kernel/vfs.h>
#include <kernel/mmu.h>
#include <kernel/generic.h>
#include <kernel/pipe.h>

#include <kernel/arch/aarch64/regs.h>

extern void framebuffer_initialize(void);
extern void fbterm_initialize(void);
extern void mmu_init(size_t memsize, size_t phys, uintptr_t firstFreePage, uintptr_t endOfInitrd);

static uint32_t swizzle(uint32_t from) {
	uint8_t a = from >> 24;
	uint8_t b = from >> 16;
	uint8_t c = from >> 8;
	uint8_t d = from;
	return (d << 24) | (c << 16) | (b << 8) | (a);
}

static uint64_t swizzle64(uint64_t from) {
	uint8_t a = from >> 56;
	uint8_t b = from >> 48;
	uint8_t c = from >> 40;
	uint8_t d = from >> 32;
	uint8_t e = from >> 24;
	uint8_t f = from >> 16;
	uint8_t g = from >> 8;
	uint8_t h = from;
	return ((uint64_t)h << 56) | ((uint64_t)g << 48) | ((uint64_t)f << 40) | ((uint64_t)e << 32) | (d << 24) | (c << 16) | (b << 8) | (a);
}

static uint16_t swizzle16(uint16_t from) {
	uint8_t a = from >> 8;
	uint8_t b = from;
	return (b << 8) | (a);
}

struct fdt_header {
	uint32_t magic;
	uint32_t totalsize;
	uint32_t off_dt_struct;
	uint32_t off_dt_strings;
	uint32_t off_mem_rsvmap;
	uint32_t version;
	uint32_t last_comp_version;
	uint32_t boot_cpuid_phys;
	uint32_t size_dt_strings;
	uint32_t size_dt_struct;
};

static uint32_t * parse_node(uint32_t * node, char * strings, int x) {
	while (swizzle(*node) == 4) node++;
	if (swizzle(*node) == 9) return NULL;
	if (swizzle(*node) != 1) {
		printf("Not a node? Got %x\n", swizzle(*node));
		return NULL;
	}

	/* Skip the BEGIN_NODE */
	node++;

	for (int i = 0; i < x; ++i) printf("  ");

	while (1) {
		char * x = (char*)node;
		if (x[0]) { printf("%c",x[0]); } else { node++; break; }
		if (x[1]) { printf("%c",x[1]); } else { node++; break; }
		if (x[2]) { printf("%c",x[2]); } else { node++; break; }
		if (x[3]) { printf("%c",x[3]); } else { node++; break; }
		node++;
	}
	printf("\n");

	while (1) {
		while (swizzle(*node) == 4) node++;
		if (swizzle(*node) == 2) return node+1;
		if (swizzle(*node) == 3) {
			for (int i = 0; i < x; ++i) printf("  ");
			uint32_t len = swizzle(node[1]);
			uint32_t nameoff = swizzle(node[2]);
			printf("  property %s len=%u\n", strings + nameoff, len);
			node += 3;
			node += (len + 3) / 4;
		} else if (swizzle(*node) == 1) {
			node = parse_node(node, strings, x + 1);
		}
	}

}

static void dump_dtb(uintptr_t addr) {

	struct fdt_header * fdt = (struct fdt_header*)addr;

#define P(o) dprintf(#o " = %#x\n", swizzle(fdt-> o))
	P(magic);
	P(totalsize);
	P(off_dt_struct);
	P(off_dt_strings);
	P(off_mem_rsvmap);
	P(version);
	P(last_comp_version);
	P(boot_cpuid_phys);
	P(size_dt_strings);
	P(size_dt_struct);

	char * dtb_strings = (char *)(addr + swizzle(fdt->off_dt_strings));
	uint32_t * dtb_struct = (uint32_t *)(addr + swizzle(fdt->off_dt_struct));

	parse_node(dtb_struct, dtb_strings, 0);
}

static uint32_t * find_subnode(uint32_t * node, char * strings, const char * name, uint32_t ** node_out, int (*cmp)(const char* a, const char *b)) {
	while (swizzle(*node) == 4) node++;
	if (swizzle(*node) == 9) return NULL;
	if (swizzle(*node) != 1) return NULL;
	node++;

	if (cmp((char*)node,name)) {
		*node_out = node;
		return NULL;
	}

	while ((*node & 0xFF000000) && (*node & 0xFF0000) && (*node & 0xFF00) && (*node & 0xFF)) node++;
	node++;

	while (1) {
		while (swizzle(*node) == 4) node++;
		if (swizzle(*node) == 2) return node+1;
		if (swizzle(*node) == 3) {
			uint32_t len = swizzle(node[1]);
			node += 3;
			node += (len + 3) / 4;
		} else if (swizzle(*node) == 1) {
			node = find_subnode(node, strings, name, node_out, cmp);
			if (!node) return NULL;
		}
	}
}

static uint32_t * find_node_int(const char * name, int (*cmp)(const char*,const char*)) {
	uintptr_t addr = (uintptr_t)mmu_map_from_physical(0x40000000);
	struct fdt_header * fdt = (struct fdt_header*)addr;
	char * dtb_strings = (char *)(addr + swizzle(fdt->off_dt_strings));
	uint32_t * dtb_struct = (uint32_t *)(addr + swizzle(fdt->off_dt_struct));

	uint32_t * out = NULL;
	find_subnode(dtb_struct, dtb_strings, name, &out, cmp);
	return out;
}

static int base_cmp(const char *a, const char *b) {
	return !strcmp(a,b);
}
static uint32_t * find_node(const char * name) {
	return find_node_int(name,base_cmp);
}

static int prefix_cmp(const char *a, const char *b) {
	return !memcmp(a,b,strlen(b));
}

static uint32_t * find_node_prefix(const char * name) {
	return find_node_int(name,prefix_cmp);
}

static uint32_t * node_find_property_int(uint32_t * node, char * strings, const char * property, uint32_t ** out) {
	while ((*node & 0xFF000000) && (*node & 0xFF0000) && (*node & 0xFF00) && (*node & 0xFF)) node++;
	node++;

	while (1) {
		while (swizzle(*node) == 4) node++;
		if (swizzle(*node) == 2) return node+1;
		if (swizzle(*node) == 3) {
			uint32_t len = swizzle(node[1]);
			uint32_t nameoff = swizzle(node[2]);
			if (!strcmp(strings + nameoff, property)) {
				*out = &node[1];
				return NULL;
			}
			node += 3;
			node += (len + 3) / 4;
		} else if (swizzle(*node) == 1) {
			node = node_find_property_int(node+1, strings, property, out);
			if (!node) return NULL;
		}
	}
}

static uint32_t * node_find_property(uint32_t * node, const char * property) {
	uintptr_t addr = (uintptr_t)mmu_map_from_physical(0x40000000);
	struct fdt_header * fdt = (struct fdt_header*)addr;
	char * dtb_strings = (char *)(addr + swizzle(fdt->off_dt_strings));
	uint32_t * out = NULL;
	node_find_property_int(node, dtb_strings, property, &out);
	return out;
}

/**
 * @brief Enter userspace.
 *
 * Called by process startup.
 * Does not return.
 *
 * @param entrypoint Address to "return" to in userspace.
 * @param argc       Number of arguments to provide to the new process.
 * @param argv       Argument array to pass to the new process; make sure this is user-accessible!
 * @param envp       Environment strings array
 * @param stack      Userspace stack address.
 */
void arch_enter_user(uintptr_t entrypoint, int argc, char * argv[], char * envp[], uintptr_t stack) {
	asm volatile(
		"msr ELR_EL1, %0\n" /* entrypoint */
		"msr SP_EL0, %1\n" /* stack */
		"msr SPSR_EL1, %2\n" /* EL 0 */
		::
		"r"(entrypoint), "r"(stack), "r"(0));

	register uint64_t x0 __asm__("x0") = argc;
	register uint64_t x1 __asm__("x1") = (uintptr_t)argv;
	register uint64_t x2 __asm__("x2") = (uintptr_t)envp;

	asm volatile(
		"eret" :: "r"(x0), "r"(x1), "r"(x2));

	__builtin_unreachable();
}

/**
 * @brief Enter a userspace signal handler.
 *
 * Similar to @c arch_enter_user but also setups up magic return addresses.
 *
 * Since signal handlers do to take complicated argument arrays, this only
 * supplies a @p signum argument.
 *
 * Does not return.
 *
 * @param entrypoint Userspace address of the signal handler, set by the process.
 * @param signum     Signal number that caused this entry.
 */
void arch_enter_signal_handler(uintptr_t entrypoint, int signum) {
	/* TODO */
	printf("%s() called\n", __func__);
	while (1);
	__builtin_unreachable();
}

/**
 * @brief Save FPU registers for this thread.
 */
void arch_restore_floating(process_t * proc) {
	asm volatile (
		"ldr q0 , [%0, #(0 * 16)]\n"
		"ldr q1 , [%0, #(1 * 16)]\n"
		"ldr q2 , [%0, #(2 * 16)]\n"
		"ldr q3 , [%0, #(3 * 16)]\n"
		"ldr q4 , [%0, #(4 * 16)]\n"
		"ldr q5 , [%0, #(5 * 16)]\n"
		"ldr q6 , [%0, #(6 * 16)]\n"
		"ldr q7 , [%0, #(7 * 16)]\n"
		"ldr q8 , [%0, #(8 * 16)]\n"
		"ldr q9 , [%0, #(9 * 16)]\n"
		"ldr q10, [%0, #(10 * 16)]\n"
		"ldr q11, [%0, #(11 * 16)]\n"
		"ldr q12, [%0, #(12 * 16)]\n"
		"ldr q13, [%0, #(13 * 16)]\n"
		"ldr q14, [%0, #(14 * 16)]\n"
		"ldr q15, [%0, #(15 * 16)]\n"
		"ldr q16, [%0, #(16 * 16)]\n"
		"ldr q17, [%0, #(17 * 16)]\n"
		"ldr q18, [%0, #(18 * 16)]\n"
		"ldr q19, [%0, #(19 * 16)]\n"
		"ldr q20, [%0, #(20 * 16)]\n"
		"ldr q21, [%0, #(21 * 16)]\n"
		"ldr q22, [%0, #(22 * 16)]\n"
		"ldr q23, [%0, #(23 * 16)]\n"
		"ldr q24, [%0, #(24 * 16)]\n"
		"ldr q25, [%0, #(25 * 16)]\n"
		"ldr q26, [%0, #(26 * 16)]\n"
		"ldr q27, [%0, #(27 * 16)]\n"
		"ldr q28, [%0, #(28 * 16)]\n"
		"ldr q29, [%0, #(29 * 16)]\n"
		"ldr q30, [%0, #(30 * 16)]\n"
		"ldr q31, [%0, #(31 * 16)]\n"
		::"r"(&proc->thread.fp_regs));
}

/**
 * @brief Restore FPU registers for this thread.
 */
void arch_save_floating(process_t * proc) {
	asm volatile (
		"str q0 , [%0, #(0 * 16)]\n"
		"str q1 , [%0, #(1 * 16)]\n"
		"str q2 , [%0, #(2 * 16)]\n"
		"str q3 , [%0, #(3 * 16)]\n"
		"str q4 , [%0, #(4 * 16)]\n"
		"str q5 , [%0, #(5 * 16)]\n"
		"str q6 , [%0, #(6 * 16)]\n"
		"str q7 , [%0, #(7 * 16)]\n"
		"str q8 , [%0, #(8 * 16)]\n"
		"str q9 , [%0, #(9 * 16)]\n"
		"str q10, [%0, #(10 * 16)]\n"
		"str q11, [%0, #(11 * 16)]\n"
		"str q12, [%0, #(12 * 16)]\n"
		"str q13, [%0, #(13 * 16)]\n"
		"str q14, [%0, #(14 * 16)]\n"
		"str q15, [%0, #(15 * 16)]\n"
		"str q16, [%0, #(16 * 16)]\n"
		"str q17, [%0, #(17 * 16)]\n"
		"str q18, [%0, #(18 * 16)]\n"
		"str q19, [%0, #(19 * 16)]\n"
		"str q20, [%0, #(20 * 16)]\n"
		"str q21, [%0, #(21 * 16)]\n"
		"str q22, [%0, #(22 * 16)]\n"
		"str q23, [%0, #(23 * 16)]\n"
		"str q24, [%0, #(24 * 16)]\n"
		"str q25, [%0, #(25 * 16)]\n"
		"str q26, [%0, #(26 * 16)]\n"
		"str q27, [%0, #(27 * 16)]\n"
		"str q28, [%0, #(28 * 16)]\n"
		"str q29, [%0, #(29 * 16)]\n"
		"str q30, [%0, #(30 * 16)]\n"
		"str q31, [%0, #(31 * 16)]\n"
		::"r"(&proc->thread.fp_regs):"memory");
}

/**
 * @brief Prepare for a fatal event by stopping all other cores.
 */
void arch_fatal_prepare(void) {
	/* TODO Stop other cores */
}

/**
 * @brief Halt all processors, including this one.
 * @see arch_fatal_prepare
 */
void arch_fatal(void) {
	arch_fatal_prepare();
	printf("-- fatal panic\n");
	/* TODO */
	while (1) {}
}

/**
 * @brief Reboot the computer.
 *
 * At least on 'virt', there's a system control
 * register we can write to to reboot or at least
 * do a full shutdown.
 */
long arch_reboot(void) {
	return 0;
}

void aarch64_regs(struct regs *r) {
#define reg(a,b) printf(" X%02d=0x%016zx X%02d=0x%016zx\n",a,r->x ## a, b, r->x ## b)
	reg(0,1);
	reg(2,3);
	reg(4,5);
	reg(6,7);
	reg(8,9);
	reg(10,11);
	reg(12,13);
	reg(14,15);
	reg(16,17);
	reg(18,19);
	reg(20,21);
	reg(22,23);
	reg(24,25);
	reg(26,27);
	reg(28,29);
	printf(" X30=0x%016zx  SP=0x%016zx\n", r->x30, r->user_sp);
#undef reg
}

void aarch64_context(process_t * proc) {
	printf("  SP=0x%016zx BP(x29)=0x%016zx\n", proc->thread.context.sp, proc->thread.context.bp);
	printf("  IP=0x%016zx TLSBASE=0x%016zx\n", proc->thread.context.ip, proc->thread.context.tls_base);
	printf(" X19=0x%016zx     X20=%016zx\n", proc->thread.context.saved[0], proc->thread.context.saved[1]);
	printf(" X21=0x%016zx     X22=%016zx\n", proc->thread.context.saved[2], proc->thread.context.saved[3]);
	printf(" X23=0x%016zx     X24=%016zx\n", proc->thread.context.saved[4], proc->thread.context.saved[5]);
	printf(" X25=0x%016zx     X26=%016zx\n", proc->thread.context.saved[6], proc->thread.context.saved[7]);
	printf(" X27=0x%016zx     X28=%016zx\n", proc->thread.context.saved[8], proc->thread.context.saved[9]);
	printf(" ELR=0x%016zx    SPSR=%016zx\n", proc->thread.context.saved[10], proc->thread.context.saved[11]);
}

/* Syscall parameter accessors */
void arch_syscall_return(struct regs * r, long retval) { r->x0 = retval; }
long arch_syscall_number(struct regs * r) { return r->x0; }
long arch_syscall_arg0(struct regs * r)   { return r->x1; }
long arch_syscall_arg1(struct regs * r)   { return r->x2; }
long arch_syscall_arg2(struct regs * r)   { return r->x3; }
long arch_syscall_arg3(struct regs * r)   { return r->x4; }
long arch_syscall_arg4(struct regs * r)   { return r->x5; }
long arch_stack_pointer(struct regs * r)  { printf("%s() called\n", __func__); return 0; /* TODO */ }
long arch_user_ip(struct regs * r)        { printf("%s() called\n", __func__); return 0; /* TODO */ }

/* No port i/o on arm, but these are still littered around some
 * drivers we need to remove... */
unsigned short inports(unsigned short _port) { return 0; }
unsigned int inportl(unsigned short _port)   { return 0; }
unsigned char inportb(unsigned short _port)  { return 0; }
void inportsm(unsigned short port, unsigned char * data, unsigned long size) {
}

void outports(unsigned short _port, unsigned short _data) {
}

void outportl(unsigned short _port, unsigned int _data) {
}

void outportb(unsigned short _port, unsigned char _data) {
}

void outportsm(unsigned short port, unsigned char * data, unsigned long size) {
}

void arch_framebuffer_initialize(void) {
	/* I'm not sure we have any options here...
	 * lfbvideo calls this expecting it to fill in information
	 * on a preferred video mode; maybe dtb has that? */
}

const char * arch_get_cmdline(void) {
	/* this should be available from dtb directly as a string */
	return "start=live-session";
}

const char * arch_get_loader(void) {
	return "";
}

/* These should probably assembly. */
void arch_enter_tasklet(void) {
	/* Pop two arguments, jump to the second one. */
	printf("%s() called\n", __func__);
}

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

static struct fwcfg_dma {
	volatile uint32_t control;
	volatile uint32_t length;
	volatile uint64_t address;
} dma __attribute__((aligned(4096)));

static void fwcfg_load_initrd(uintptr_t * ramdisk_phys_base, size_t * ramdisk_size) {
	uintptr_t z = 0;
	size_t z_pages= 0;
	uintptr_t uz = 0;
	size_t uz_pages = 0;

	extern char end[];
	uintptr_t ramdisk_map_start = ((uintptr_t)&end - 0xffffffff80000000UL) + 0x80000000;

	/* See if we can find a qemu fw_cfg interface, we can use that for a ramdisk */
	uint32_t * fw_cfg = find_node_prefix("fw-cfg");
	if (fw_cfg) {
		dprintf("fw-cfg: found interface\n");
		/* best guess until we bother parsing these */
		uint32_t * regs = node_find_property(fw_cfg, "reg");
		if (regs) {
			volatile uint8_t * fw_cfg_addr = (volatile uint8_t*)(uintptr_t)(mmu_map_from_physical(swizzle(regs[3])));
			volatile uint64_t * fw_cfg_data = (volatile uint64_t *)fw_cfg_addr;
			volatile uint32_t * fw_cfg_32   = (volatile uint32_t *)fw_cfg_addr;
			volatile uint16_t * fw_cfg_sel  = (volatile uint16_t *)(fw_cfg_addr + 8);

			*fw_cfg_sel = 0;

			uint64_t response = fw_cfg_data[0];
			(void)response;

			/* Needs to be big-endian */
			*fw_cfg_sel = swizzle16(0x19);

			/* count response is 32-bit BE */
			uint32_t count = swizzle(fw_cfg_32[0]);

			struct fw_cfg_file {
				uint32_t size;
				uint16_t select;
				uint16_t reserved;
				char name[56];
			};

			struct fw_cfg_file file;
			uint8_t * tmp = (uint8_t *)&file;

			/* Read count entries */
			for (unsigned int i = 0; i < count; ++i) {
				for (unsigned int j = 0; j < sizeof(struct fw_cfg_file); ++j) {
					tmp[j] = fw_cfg_addr[0];
				}

				/* endian swap to get file size and selector ID */
				file.size = swizzle(file.size);
				file.select = swizzle16(file.select);

				if (!strcmp(file.name,"opt/org.toaruos.initrd")) {
					dprintf("fw-cfg: initrd found\n");
					z_pages = (file.size + 0xFFF) / 0x1000;
					z = ramdisk_map_start;
					ramdisk_map_start += z_pages * 0x1000;
					uint8_t * x = mmu_map_from_physical(z);

					dma.control = swizzle((file.select << 16) | (1 << 3) | (1 << 1));
					dma.length  = swizzle(file.size);
					dma.address = swizzle64(z);

					asm volatile ("isb" ::: "memory");
					fw_cfg_data[2] = swizzle64(mmu_map_to_physical(NULL,(uint64_t)&dma));
					asm volatile ("isb" ::: "memory");

					if (dma.control) {
						dprintf("fw-cfg: Error on DMA read (control: %#x)\n", dma.control);
						return;
					}

					dprintf("fw-cfg: initrd loaded x=%#zx\n", (uintptr_t)x);

					if (x[0] == 0x1F && x[1] == 0x8B) {
						dprintf("fw-cfg: will attempt to read size from %#zx\n", (uintptr_t)(x + file.size - sizeof(uint32_t)));
						uint32_t size;
						memcpy(&size, (x + file.size - sizeof(uint32_t)), sizeof(uint32_t));
						dprintf("fw-cfg: compressed ramdisk unpacks to %u bytes\n", size);

						uz_pages = (size + 0xFFF) / 0x1000;
						uz = ramdisk_map_start;
						ramdisk_map_start += uz_pages * 0x1000;
						uint8_t * ramdisk = mmu_map_from_physical(uz);

						gzip_inputPtr = x;
						gzip_outputPtr = ramdisk;
						if (gzip_decompress()) {
							dprintf("fw-cfg: gzip failure, not mounting ramdisk\n");
							return;
						}

						memmove(x, ramdisk, size);

						dprintf("fw-cfg: Unpacked ramdisk at %#zx\n", (uintptr_t)ramdisk);
						*ramdisk_phys_base = z;
						*ramdisk_size = size;
					} else {
						dprintf("fw-cfg: Ramdisk at %#zx\n", (uintptr_t)x);
						*ramdisk_phys_base = z;
						*ramdisk_size = file.size;
					}

					return;
				}
			}
		}
	}
}

static void dtb_locate_cmdline(void) {
	uint32_t * chosen = find_node("chosen");
	if (chosen) {
		uint32_t * prop = node_find_property(chosen, "bootargs");
		if (prop) {
			args_parse((char*)&prop[2]);
		}
	}
}


static void exception_handlers(void) {
	extern char _exception_vector[];

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

	if (elr == 0xFFFFB00F && far == 0xFFFFB00F) {
		task_exit(0);
		__builtin_unreachable();
	}

	if ((esr >> 26) == 0x15) {
		#if 0
		printf("%s: syscall(%ld) puts sp at %#zx with base of %#zx\n", this_core->current_process->name, r->x0, (uintptr_t)r,
			this_core->current_process->image.stack);
		printf("SVC call (%d)\n", r->x0);
		printf(" x0: %#zx", r->x0);
		printf(" x1: %#zx", r->x1);
		printf(" x2: %#zx\n", r->x2);
		printf(" x3: %#zx", r->x3);
		printf(" x4: %#zx", r->x4);
		printf(" x5: %#zx\n", r->x5);
		#endif

		update_clock();

		extern void syscall_handler(struct regs *);
		syscall_handler(r);

		#if 0
		printf("Regs at return should be:\n");
		printf(" x0: %#zx", r->x0);
		printf(" x1: %#zx", r->x1);
		printf(" x2: %#zx\n", r->x2);
		printf(" x3: %#zx", r->x3);
		printf(" x4: %#zx", r->x4);
		printf(" x5: %#zx\n", r->x5);
		#endif
		return;
	}

	if (far == 0x1de7ec7edbadc0de) {
		/* KVM is mad at us */
		printf("kvm: blip (esr=%#zx, elr=%#zx; pid=%d [%s])\n", esr, elr, this_core->current_process->id, this_core->current_process->name);
		return;
	}

	printf("In process %d (%s)\n", this_core->current_process->id, this_core->current_process->name);
	printf("ESR: %#zx FAR: %#zx ELR: %#zx SPSR: %#zx\n", esr, far, elr, spsr);
	aarch64_regs(r);

	uint64_t tpidr_el0;
	asm volatile ("mrs %0, TPIDR_EL0" : "=r"(tpidr_el0));
	printf("  TPIDR_EL0=%#zx\n", tpidr_el0);


	while (1);

	task_exit(1);
}

void aarch64_fault_enter(struct regs * r) {
	uint64_t esr, far, elr, spsr;
	asm volatile ("mrs %0, ESR_EL1" : "=r"(esr));
	asm volatile ("mrs %0, FAR_EL1" : "=r"(far));
	asm volatile ("mrs %0, ELR_EL1" : "=r"(elr));
	asm volatile ("mrs %0, SPSR_EL1" : "=r"(spsr));

	//printf("In process %d (%s)\n", this_core->current_process->id, this_core->current_process->name);
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
	volatile uint32_t * clock_addr = mmu_map_from_physical(0x09010000);

	clock_addr[4] = 0;
	clock_addr[7] = 1;
	
	#if 0
	asm volatile ("msr CNTKCTL_EL1, %0" ::
		"r"(
			(1 << 17) /* lower rate */
			| (23 << 4) /* uh lowest bit? */
			| (1 << 1) /* enable event stream */
		));
	#endif
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
	asm volatile ("wfe");

	update_clock();

	asm volatile (
		".globl _ret_from_preempt_source\n"
		"_ret_from_preempt_source:"
	);

	switch_next();
}

static fs_node_t * mouse_pipe;
static fs_node_t * keyboard_pipe;

static void fake_input(void) {
	mouse_pipe = make_pipe(128);
	mouse_pipe->flags = FS_CHARDEVICE;
	vfs_mount("/dev/mouse", mouse_pipe);

	keyboard_pipe = make_pipe(128);
	keyboard_pipe->flags = FS_CHARDEVICE;
	vfs_mount("/dev/kbd", keyboard_pipe);
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
 * Figure out 1) how much actual memory we have and 2) what the last
 * address of physical memory is.
 */
static void dtb_memory_size(size_t * memsize, size_t * physsize) {
	uint32_t * memory = find_node_prefix("memory");
	if (!memory) {
		printf("dtb: Could not find memory node.\n");
		arch_fatal();
	}

	uint32_t * regs = node_find_property(memory, "reg");
	if (!regs) {
		printf("dtb: memory node has no regs\n");
		arch_fatal();
	}

	/* Eventually, same with fw-cfg, we'll need to actually figure out
	 * the size of the 'reg' cells, but at least right now it's been
	 * 2 address, 2 size. */
	uint64_t mem_addr = (uint64_t)swizzle(regs[3]) | ((uint64_t)swizzle(regs[2]) << 32UL);
	uint64_t mem_size = (uint64_t)swizzle(regs[5]) | ((uint64_t)swizzle(regs[4]) << 32UL);

	*memsize = mem_size;
	*physsize = mem_addr + mem_size;
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

	/* TODO Install drivers that may need to sleep here */
	fake_input();

	generic_main();

	return 0;
}
