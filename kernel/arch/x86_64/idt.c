/**
 * @file kernel/arch/x86_64/idt.c
 * @brief x86-64 Interrupt Descriptor Table management
 *
 * This is the C side of all interrupt handling. See
 * also @ref irq.S which has the assembly entrypoints.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021 K. Lange
 */
#include <kernel/types.h>
#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/vfs.h>
#include <kernel/version.h>
#include <kernel/process.h>
#include <kernel/signal.h>
#include <kernel/misc.h>
#include <kernel/time.h>
#include <kernel/ptrace.h>
#include <kernel/hashmap.h>
#include <kernel/module.h>
#include <kernel/ksym.h>
#include <kernel/mmu.h>
#include <kernel/syscall.h>

#include <sys/time.h>
#include <sys/utsname.h>
#include <sys/ptrace.h>

#include <kernel/arch/x86_64/ports.h>
#include <kernel/arch/x86_64/pml.h>
#include <kernel/arch/x86_64/regs.h>
#include <kernel/arch/x86_64/irq.h>

static struct idt_pointer idtp;
static idt_entry_t idt[256];

/**
 * @brief Initialize a gate, since there's some address swizzling involved...
 */
void idt_set_gate(uint8_t num, interrupt_handler_t handler, uint16_t selector, uint8_t flags, int userspace) {
	uintptr_t base = (uintptr_t)handler;
	idt[num].base_low  = (base & 0xFFFF);
	idt[num].base_mid  = (base >> 16) & 0xFFFF;
	idt[num].base_high = (base >> 32) & 0xFFFFFFFF;
	idt[num].selector = selector;
	idt[num].zero = 0;
	idt[num].pad = 0;
	idt[num].flags = flags | (userspace ? 0x60 : 0);
}

/**
 * @brief Initializes the IDT and sets up gates for all interrupts.
 */
void idt_install(void) {
	idtp.limit = sizeof(idt);
	idtp.base  = (uintptr_t)&idt;

	/** ISRs */
	idt_set_gate(0,  _isr0,  0x08, 0x8E, 0);
	idt_set_gate(1,  _isr1,  0x08, 0x8E, 0);
	idt_set_gate(2,  _isr2,  0x08, 0x8E, 0);
	idt_set_gate(3,  _isr3,  0x08, 0x8E, 0);
	idt_set_gate(4,  _isr4,  0x08, 0x8E, 0);
	idt_set_gate(5,  _isr5,  0x08, 0x8E, 0);
	idt_set_gate(6,  _isr6,  0x08, 0x8E, 0);
	idt_set_gate(7,  _isr7,  0x08, 0x8E, 0);
	idt_set_gate(8,  _isr8,  0x08, 0x8E, 0);
	idt_set_gate(9,  _isr9,  0x08, 0x8E, 0);
	idt_set_gate(10, _isr10, 0x08, 0x8E, 0);
	idt_set_gate(11, _isr11, 0x08, 0x8E, 0);
	idt_set_gate(12, _isr12, 0x08, 0x8E, 0);
	idt_set_gate(13, _isr13, 0x08, 0x8E, 0);
	idt_set_gate(14, _isr14, 0x08, 0x8E, 0);
	idt_set_gate(15, _isr15, 0x08, 0x8E, 0);
	idt_set_gate(16, _isr16, 0x08, 0x8E, 0);
	idt_set_gate(17, _isr17, 0x08, 0x8E, 0);
	idt_set_gate(18, _isr18, 0x08, 0x8E, 0);
	idt_set_gate(19, _isr19, 0x08, 0x8E, 0);
	idt_set_gate(20, _isr20, 0x08, 0x8E, 0);
	idt_set_gate(21, _isr21, 0x08, 0x8E, 0);
	idt_set_gate(22, _isr22, 0x08, 0x8E, 0);
	idt_set_gate(23, _isr23, 0x08, 0x8E, 0);
	idt_set_gate(24, _isr24, 0x08, 0x8E, 0);
	idt_set_gate(25, _isr25, 0x08, 0x8E, 0);
	idt_set_gate(26, _isr26, 0x08, 0x8E, 0);
	idt_set_gate(27, _isr27, 0x08, 0x8E, 0);
	idt_set_gate(28, _isr28, 0x08, 0x8E, 0);
	idt_set_gate(29, _isr29, 0x08, 0x8E, 0);
	idt_set_gate(30, _isr30, 0x08, 0x8E, 0);
	idt_set_gate(31, _isr31, 0x08, 0x8E, 0);
	idt_set_gate(32, _irq0,  0x08, 0x8E, 0);
	idt_set_gate(33, _irq1,  0x08, 0x8E, 0);
	idt_set_gate(34, _irq2,  0x08, 0x8E, 0);
	idt_set_gate(35, _irq3,  0x08, 0x8E, 0);
	idt_set_gate(36, _irq4,  0x08, 0x8E, 0);
	idt_set_gate(37, _irq5,  0x08, 0x8E, 0);
	idt_set_gate(38, _irq6,  0x08, 0x8E, 0);
	idt_set_gate(39, _irq7,  0x08, 0x8E, 0);
	idt_set_gate(40, _irq8,  0x08, 0x8E, 0);
	idt_set_gate(41, _irq9,  0x08, 0x8E, 0);
	idt_set_gate(42, _irq10, 0x08, 0x8E, 0);
	idt_set_gate(43, _irq11, 0x08, 0x8E, 0);
	idt_set_gate(44, _irq12, 0x08, 0x8E, 0);
	idt_set_gate(45, _irq13, 0x08, 0x8E, 0);
	idt_set_gate(46, _irq14, 0x08, 0x8E, 0);
	idt_set_gate(47, _irq15, 0x08, 0x8E, 0);

	idt_set_gate(123, _isr123, 0x08, 0x8E, 0); /* Clock interrupt for other processors */
	idt_set_gate(124, _isr124, 0x08, 0x8E, 0); /* Bad TLB shootdown. */
	idt_set_gate(125, _isr125, 0x08, 0x8E, 0); /* Halts everyone. */
	idt_set_gate(126, _isr126, 0x08, 0x8E, 0); /* Does nothing, used to exit wait-for-interrupt sleep. */
	idt_set_gate(127, _isr127, 0x08, 0x8E, 1); /* Legacy system call entry point, called by userspace. */

	asm volatile (
		"lidt %0"
		: : "m"(idtp)
	);
}

/**
 * @brief Quicker call to lidt for APs, when the IDT is already set up.
 *
 * We use the same idt in all cores, so there's not much to do here.
 */
void idt_ap_install(void) {
	idtp.limit = sizeof(idt);
	idtp.base  = (uintptr_t)&idt;
	asm volatile (
		"lidt %0"
		: : "m"(idtp)
	);
}

/** External IRQ management */
#define IRQ_CHAIN_SIZE  16
#define IRQ_CHAIN_DEPTH 4
static irq_handler_chain_t irq_routines[IRQ_CHAIN_SIZE * IRQ_CHAIN_DEPTH] = { NULL };
static const char * _irq_handler_descriptions[IRQ_CHAIN_SIZE * IRQ_CHAIN_DEPTH] = { NULL };

/**
 * @brief Examine the IRQ handler chain to see what handles an IRQ.
 *
 * This is a debug function used by the procfs /proc/irq callback.
 * Can be called with different @p chain values to get all of the
 * handlers when there is more than one.
 *
 * @param irq The interrupt number (0~15)
 * @param chain Handler chain depth (0~4)
 * @return The name of the handler.
 */
const char * get_irq_handler(int irq, int chain) {
	if (irq >= IRQ_CHAIN_SIZE) return NULL;
	if (chain >= IRQ_CHAIN_DEPTH) return NULL;
	return _irq_handler_descriptions[IRQ_CHAIN_SIZE * chain + irq];
}

/**
 * @brief Install an IRQ handler.
 *
 * TODO Shouldn't this return a status code? What if we have too many
 *      IRQs installed? What if @p irq is invalid (>16)?
 *
 * TODO Should we provide callers with a unique reference to their IRQ vector
 *      so it can be removed later?
 *
 * @param irq The IRQ number to handle (0~15)
 * @param handler Function to install as a callback for this IRQ
 * @param desc Textual description for debugging.
 */
void irq_install_handler(size_t irq, irq_handler_chain_t handler, const char * desc) {
	for (size_t i = 0; i < IRQ_CHAIN_DEPTH; i++) {
		if (irq_routines[i * IRQ_CHAIN_SIZE + irq])
			continue;
		irq_routines[i * IRQ_CHAIN_SIZE + irq] = handler;
		_irq_handler_descriptions[i * IRQ_CHAIN_SIZE + irq ] = desc;
		break;
	}
}

/* We used to have a function here that incorrectly uninstalled IRQ handlers... */

/**
 * @brief Examine the module table to find which module owns an address.
 *
 * Looks through the loaded module list to find what module
 * owns @p addr, setting @p name and returning the corresponding module
 * entry. Since we know how big modules are in memory, we can also know
 * if an address doesn't belong to any module, in which case we return NULL.
 *
 * @param addr Address to look for
 * @param name (out) Name of the matching module
 * @return Pointer to LoadedModule for the matched module, or NULL.
 */
static struct LoadedModule * find_module(uintptr_t addr, char ** name) {
	hashmap_t * modules = modules_get_list();

	for (size_t i = 0; i < modules->size; ++i) {
		hashmap_entry_t * x = modules->entries[i];
		while (x) {
			struct LoadedModule * info = x->value;
			if (info->baseAddress <= addr && addr <= info->baseAddress + info->loadedSize) {
				*name = (char*)x->key;
				return info;
			}
			x = x->next;
		}
	}

	return NULL;
}

/**
 * @brief Use brute force to determine if an address is mapped.
 *
 * Examines the current page table to see if @p base and up to @p size
 * is a valid region of memory. Useful for determining if a stack entry
 * is a valid base pointer to a calling frame.
 *
 * @param base Address to validate
 * @param size How many bytes after @p base are going to be examined.
 * @return 1 if the range is mapped, 0 otherwise.
 */
static int validate_pointer(uintptr_t base, size_t size) {
	uintptr_t end  = size ? (base + (size - 1)) : base;
	uintptr_t page_base = base >> 12;
	uintptr_t page_end  =  end >> 12;
	for (uintptr_t page = page_base; page <= page_end; ++page) {
		if ((page & 0xffff800000000) != 0 && (page & 0xffff800000000) != 0xffff800000000) return 0;
		union PML * page_entry = mmu_get_page_other(this_core->current_process->thread.page_directory->directory, page << 12);
		if (!page_entry) return 0;
		if (!page_entry->bits.present) return 0;
	}
	return 1;
}

extern char end[];

/**
 * @brief Find the closest preceding symbol to an address.
 *
 * Scans the kernel symbol table to find the closest preceding
 * symbol to the address @p ip and stores its name in @p name,
 * returning the actual address of the symbol.
 *
 * As this uses the kernel symbol linkage table, it is only aware
 * of exported functions and objects, and can not provide any
 * information on static functions.
 *
 * @param ip Address to scan for
 * @param name (out) Name of matching symbol
 * @return Address of matching symbol
 */
static uintptr_t matching_symbol(uintptr_t ip, char ** name) {
	hashmap_t * symbols = ksym_get_map();
	uintptr_t best_match = 0;
	for (size_t i = 0; i < symbols->size; ++i) {
		hashmap_entry_t * x = symbols->entries[i];
		while (x) {
			void* sym_addr = x->value;
			char* sym_name = x->key;
			if ((uintptr_t)sym_addr < ip && (uintptr_t)sym_addr > best_match) {
				best_match = (uintptr_t)sym_addr;
				*name = sym_name;
			}
			x = x->next;
		}
	}
	return best_match;
}

/**
 * @brief Display a traceback from the given ip and stack base.
 *
 * Walks the stack referenced by @p bp and attempts to find
 * kernel symbol names or module names. Stops when it reaches
 * a return address that looks invalid.
 *
 * You probably want to @see arch_fatal_prepare before calling
 * this to make sure you get a readable output.
 *
 * Note that symbol names are the closest symbol before the
 * given address, and will only ever be exported symbols,
 * so static functions will give the wrong name.
 *
 * We don't track symbols from modules at all at the moment,
 * so for addresses in module space the best we can do is
 * provide the name of the model and the offset into the loaded
 * file, so that's what we do.
 *
 * @param ip  IP address to assume is the top of the backtrace.
 * @param bp  Stack frame pointer.
 */
static void dump_traceback(uintptr_t ip, uintptr_t bp) {
	int depth = 0;
	int max_depth = 20;

	while (bp && ip && depth < max_depth) {
		dprintf(" 0x%016zx ", ip);
		if (ip >= 0xffffffff80000000UL) {
			char * name = NULL;
			struct LoadedModule * mod = find_module(ip, &name);
			if (mod) {
				dprintf("\a in module '%s', base address %#zx (offset %#zx)\n",
					name, mod->baseAddress, ip - mod->baseAddress);
			} else {
				dprintf("\a (unknown)\n");
			}
		} else if (ip >= (uintptr_t)&end && ip <= 0x800000000000) {
			dprintf("\a in userspace\n");
		} else if (ip <= (uintptr_t)&end) {
			/* Find symbol match */
			char * name;
			uintptr_t addr = matching_symbol(ip, &name);
			if (!addr) {
				dprintf("\a (no match)\n");
			} else {
				dprintf("\a %s+0x%zx\n", name, ip-addr);
			}
		} else {
			dprintf("\a (unknown)\n");
		}
		if (!validate_pointer(bp, sizeof(uintptr_t)) || !validate_pointer(bp + sizeof(uintptr_t), sizeof(uintptr_t))) {
			break;
		}
		ip = *(uintptr_t*)(bp + sizeof(uintptr_t));
		bp = *(uintptr_t*)(bp);
		depth++;
	}
}

/**
 * @brief Display a traceback from the rip and rbp of a register state.
 *
 * Primarily used to dump tracebacks that led to unexpected interrupts.
 *
 * @param r Interrupt register context
 */
static void safe_dump_traceback(struct regs * r) {
	dump_traceback(r->rip, r->rbp);
}

/**
 * @brief Display a traceback from the current call context.
 */
void arch_dump_traceback(void) {
	dump_traceback((uintptr_t)arch_dump_traceback+1, (uintptr_t)__builtin_frame_address(0));
}

/**
 * @brief Map in more pages for a userspace stack.
 *
 * Allows for soft expansion of the stack downards on a page fault.
 *
 * @param fromAddr The low address to map, should be page aligned.
 */
static int map_more_stack(uintptr_t fromAddr) {
	volatile process_t * volatile proc = this_core->current_process;

	/* Is this thread the process leader? */
	if (proc->group != 0) {
		proc = process_from_pid(proc->group);
	}

	if (!proc) return 0;

	/* Make sure nothing else is going to mess with this process's page tables */
	spin_lock(proc->image.lock);

	/* Map more stack! */
	for (uintptr_t i = fromAddr; i < proc->image.userstack; i += 0x1000) {
		union PML * page = mmu_get_page(i, MMU_GET_MAKE);
		mmu_frame_allocate(page, MMU_FLAG_WRITABLE);
	}

	/* Update the saved stack address */
	proc->image.userstack = fromAddr;

	spin_unlock(proc->image.lock);
	return 1;
}

/**
 * @brief Handle fatal exceptions.
 *
 * Prepares for a fatal event, prints information on the running
 * process and the cause of the panic, dumps the register state,
 * prints a backtrace, and then hard loops.
 *
 * @param desc Textual description of the panic cause.
 * @param r    Interrupt register context
 * @param faulting_address When available, the address that was accessed leading to this fault.
 */
static void panic(const char * desc, struct regs * r, uintptr_t faulting_address) {
	/* Stop all other cores */
	arch_fatal_prepare();

	/* Print the description, current process, cause */
	dprintf("\033[31mPanic!\033[0m %s pid=%d (%s) at %#zx\n", desc,
		this_core->current_process ? (int)this_core->current_process->id : 0,
		this_core->current_process ? this_core->current_process->name : "kernel",
		faulting_address
	);

	/* Dump register state */
	dprintf(
		"Registers at interrupt:\n"
		"  $rip=0x%016lx\n"
		"  $rsi=0x%016lx,$rdi=0x%016lx,$rbp=0x%016lx,$rsp=0x%016lx\n"
		"  $rax=0x%016lx,$rbx=0x%016lx,$rcx=0x%016lx,$rdx=0x%016lx\n"
		"  $r8= 0x%016lx,$r9= 0x%016lx,$r10=0x%016lx,$r11=0x%016lx\n"
		"  $r12=0x%016lx,$r13=0x%016lx,$r14=0x%016lx,$r15=0x%016lx\n"
		"  cs=0x%016lx  ss=0x%016lx rflags=0x%016lx int=0x%02lx err=0x%02lx\n",
		r->rip,
		r->rsi, r->rdi, r->rbp, r->rsp,
		r->rax, r->rbx, r->rcx, r->rdx,
		r->r8, r->r9, r->r10, r->r11,
		r->r12, r->r13, r->r14, r->r15,
		r->cs, r->ss, r->rflags, r->int_no, r->err_code
	);

	/* Dump GS segment register information */
	uint32_t gs_base_low, gs_base_high;
	asm volatile ( "rdmsr" : "=a" (gs_base_low), "=d" (gs_base_high): "c" (0xc0000101) );
	uint32_t kgs_base_low, kgs_base_high;
	asm volatile ( "rdmsr" : "=a" (kgs_base_low), "=d" (kgs_base_high): "c" (0xc0000102) );
	dprintf("  gs=0x%08x%08x kgs=0x%08x%08x\n",
		gs_base_high, gs_base_low, kgs_base_high, kgs_base_low);

	/* Walk the call stack from before the interrupt */
	safe_dump_traceback(r);

	/* Stop this core */
	arch_fatal();
}

/**
 * @brief Debug interrupt
 *
 * Called when a CPU is single-stepping. We need to reset
 * the single-step flag in RFLAGS and if we were actually
 * debugging the current process we need to trigger a ptrace
 * SINGLESTEP event. This should also return immediately
 * from the syscall handler.
 *
 * @param r Interrupt register context
 * @return Register context, which should be unmodified.
 */
static struct regs * _debug_int(struct regs * r) {
	/* Unset the debug flag */
	r->rflags &= ~(1 << 8);

	/* If the current process was debugging, trigger a SINGLESTEP event. */
	if (this_core->current_process->flags & PROC_FLAG_TRACE_SIGNALS) {
		ptrace_signal(SIGTRAP, PTRACE_EVENT_SINGLESTEP);
	}

	/* Return from interrupt */
	return r;
}

/**
 * @brief Double fault should always panic.
 */
static void _double_fault(struct regs * r) {
	panic("Double fault", r, 0);
}

/**
 * @brief GPF handler.
 *
 * Mostly this is separated from other exceptions because
 * GPF should cause SIGSEGV rather than SIGILL? I think?
 *
 * @param r Interrupt register context
 */
static void _general_protection_fault(struct regs * r) {
	/* Were we in the kernel? */
	if (!this_core->current_process || r->cs == 0x08) {
		/* Then that's a panic. */
		panic("GPF in kernel", r, 0);
	}

	/* Else, segfault the current process. */
	send_signal(this_core->current_process->id, SIGSEGV, 1);
}

/**
 * @brief Page fault handler.
 *
 * Handles magic return addresses, stack expansions, maybe
 * later will handle COW or mmap'd filed... otherwise,
 * mostly segfaults.
 *
 * @param r Interrupt register context
 */
static void _page_fault(struct regs * r) {
	/* Obtain the "cause" address */
	uintptr_t faulting_address;
	asm volatile("mov %%cr2, %0" : "=r"(faulting_address));

	/* 8DEADBEEFh is the magic ret-from-sig address. */
	if (faulting_address == 0x8DEADBEEF) {
		return_from_signal_handler(r);
		return;
	}

	if ((r->err_code & 3) == 3) {
		/* This is probably a COW page? */
		extern int mmu_copy_on_write(uintptr_t address);
		if (!mmu_copy_on_write(faulting_address)) return;
	}

	/* Was this a kernel page fault? Those are always a panic. */
	if (!this_core->current_process || r->cs == 0x08) {
		panic("Page fault in kernel", r, faulting_address);
	}

	/* Page was present but not writable */

	/* Quietly map more stack if it was a viable stack address. */
	if (faulting_address < 0x800000000000 && faulting_address > 0x700000000000) {
		if (map_more_stack(faulting_address & 0xFFFFffffFFFFf000)) return;
	}

	/* Otherwise, segfault the current process. */
	send_signal(this_core->current_process->id, SIGSEGV, 1);
}

/**
 * @brief Legacy system call entrypoint.
 *
 * We don't have a non-legacy entrypoint, but this use of
 * an interrupt to make syscalls is considered "legacy"
 * by the existence of its replacement (SYSCALL/SYSRET).
 *
 * @param r Interrupt register context, which contains syscall arguments.
 * @return Register state after system call, which contains return value.
 */
static struct regs * _syscall_entrypoint(struct regs * r) {
	/* syscall_handler will modify r to set return value. */
	syscall_handler(r);

	/*
	 * I'm not actually sure if we're still cli'ing in any of the
	 * syscall handlers, but definitely make sure we're not allowing
	 * interrupts to remain disabled upon return from a system call.
	 */
	asm volatile("sti");

	return r;
}

/**
 * @brief AP-local timer signal.
 *
 * Update clocks and switch task gracefully.
 *
 * @param r Interrupt register context
 * @return Register state after resume from task task switch.
 */
static struct regs * _local_timer(struct regs * r) {
	extern void arch_update_clock(void);
	arch_update_clock();
	switch_task(1);
	return r;
}

/**
 * @brief Handle an exception interrupt.
 *
 * @param r           Interrupt register context
 * @param description Textual description of the exception, for panic messages.
 */
static void _exception(struct regs * r, const char * description) {
	/* If we were in kernel space, this is a panic */
	if (!this_core->current_process || r->cs == 0x08) {
		panic(description, r, r->int_no);
	}
	/* Otherwise, these interrupts should trigger SIGILL */
	send_signal(this_core->current_process->id, SIGILL, 1);
}

/**
 * @brief Handle an installable interrupt. This handles PIC IRQs
 *        that need to be acknowledged.
 *
 * @param r    Interrupt register context
 * @param irq  Translated IRQ number
 */
static void _handle_irq(struct regs * r, int irq) {
	for (size_t i = 0; i < IRQ_CHAIN_DEPTH; i++) {
		irq_handler_chain_t handler = irq_routines[i * IRQ_CHAIN_SIZE + irq];
		if (!handler) break;
		if (handler(r)) return;
	}

	/* Unhandled */
	irq_ack(irq);
}

#define EXC(i,n) case i: _exception(r, n); break;
#define IRQ(i) case i: _handle_irq(r,i-32); break;

struct regs * isr_handler_inner(struct regs * r) {
	switch (r->int_no) {
		EXC(0,"divide-by-zero");
		case 1: return _debug_int(r);
		/* NMI doesn't reach here, we use it as a panic signal. */
		EXC(3,"breakpoint"); /* TODO: This should map to a ptrace event */
		EXC(4,"overflow");
		EXC(5,"bound range exceeded");
		EXC(6,"invalid opcode");
		EXC(7,"device not available");
		case 8: _double_fault(r); break;
		/* 9 is a legacy exception that shouldn't happen */
		EXC(10,"invalid TSS");
		EXC(11,"segment not present");
		EXC(12,"stack-segment fault");
		case 13: _general_protection_fault(r); break;
		case 14: _page_fault(r); break;
		/* 15 is reserved */
		EXC(16,"floating point exception");
		EXC(17,"alignment check");
		EXC(18,"machine check");
		EXC(19,"SIMD floating-point exception");
		EXC(20,"virtualization exception");
		EXC(21,"control protection exception");
		/* 22 through 27 are reserved */
		EXC(28,"hypervisor injection exception");
		EXC(29,"VMM communication exception");
		EXC(30,"security exception");
		/* 31 is reserved */

		/* 16 IRQs that go to the general IRQ chain */
		IRQ(32);
		IRQ(33);
		IRQ(34);
		IRQ(35);
		IRQ(36);
		IRQ(37);
		IRQ(38);
		case 39: break; /* Except the spurious IRQ, just ignore that */
		IRQ(40);
		IRQ(41);
		IRQ(42);
		IRQ(43);
		IRQ(44);
		IRQ(45);
		IRQ(46);
		IRQ(47);

		/* Local interrupts that make it here. */
		case 123: return _local_timer(r);
		case 127: return _syscall_entrypoint(r);

		/* Other interrupts that don't make it here:
		 *   124: TLB shootdown, we just reload CR3 in the handler.
		 *   125: Fatal signal, jumps straight to a cli/hlt loop, though I think this just yields an NMI instead?
		 *   126: Quiet wakeup, do we even use this anymore?
		 */

		default: panic("Unexpected interrupt",r,0);
	}

	if (this_core->current_process == this_core->kernel_idle_task && process_queue && process_queue->head) {
		/* If this is kidle and we got here, instead of finishing the interrupt
		 * we can just switch task and there will probably be something else
		 * to run that was awoken by the interrupt. */
		switch_next();
	}

	return r;
}

struct regs * isr_handler(struct regs * r) {
	int from_userspace = r->cs != 0x08;
	this_core->interrupt_registers = r;

	if (from_userspace && this_core->current_process) {
		this_core->current_process->time_switch = arch_perf_timer();
	}

	struct regs * out = isr_handler_inner(r);

	if (from_userspace && this_core->current_process) {
		process_check_signals(out);
	}

	return out;

}
