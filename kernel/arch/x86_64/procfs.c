/**
 * @file  kernel/arch/x86_64/procfs.c
 * @brief x86-64 procfs files
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2026 K. Lange
 */
#include <kernel/process.h>
#include <kernel/list.h>
#include <kernel/procfs.h>
#include <kernel/arch/x86_64/irq.h>
#include <kernel/arch/x86_64/ports.h>

static void pat_func(fs_node_t *node) {
	uint32_t pat_value_low, pat_value_high;
	asm volatile ( "rdmsr" : "=a" (pat_value_low), "=d" (pat_value_high): "c" (0x277) );
	uint64_t pat_values = ((uint64_t)pat_value_high << 32) | (pat_value_low);

	const char * pat_names[] = {
		"uncacheable (UC)",
		"write combining (WC)",
		"Reserved",
		"Reserved",
		"write through (WT)",
		"write protected (WP)",
		"write back (WB)",
		"uncached (UC-)"
	};

	int pa_0 = (pat_values >>  0) & 0x7;
	int pa_1 = (pat_values >>  8) & 0x7;
	int pa_2 = (pat_values >> 16) & 0x7;
	int pa_3 = (pat_values >> 24) & 0x7;
	int pa_4 = (pat_values >> 32) & 0x7;
	int pa_5 = (pat_values >> 40) & 0x7;
	int pa_6 = (pat_values >> 48) & 0x7;
	int pa_7 = (pat_values >> 56) & 0x7;

	procfs_printf(node,
			"PA0: %d %s\n"
			"PA1: %d %s\n"
			"PA2: %d %s\n"
			"PA3: %d %s\n"
			"PA4: %d %s\n"
			"PA5: %d %s\n"
			"PA6: %d %s\n"
			"PA7: %d %s\n",
			pa_0, pat_names[pa_0],
			pa_1, pat_names[pa_1],
			pa_2, pat_names[pa_2],
			pa_3, pat_names[pa_3],
			pa_4, pat_names[pa_4],
			pa_5, pat_names[pa_5],
			pa_6, pat_names[pa_6],
			pa_7, pat_names[pa_7]
	);
}

static void irq_func(fs_node_t *node) {
	for (int i = 0; i < 16; ++i) {
		procfs_printf(node, "irq %d: ", i);
		for (int j = 0; j < 4; ++j) {
			const char * t = get_irq_handler(i, j);
			if (!t) break;
			procfs_printf(node, "%s%s", j ? "," : "", t);
		}
		procfs_printf(node, "\n");
	}

	outportb(0x20, 0x0b);
	outportb(0xa0, 0x0b);
	procfs_printf(node, "isr=0x%04x\n", (inportb(0xA0) << 8) | inportb(0x20));

	outportb(0x20, 0x0a);
	outportb(0xa0, 0x0a);
	procfs_printf(node, "irr=0x%04x\n", (inportb(0xA0) << 8) | inportb(0x20));

	procfs_printf(node, "imr=0x%04x\n", (inportb(0xA1) << 8) | inportb(0x21));
}

static void cpuinfo_func(fs_node_t *node) {
	for (int i = 0; i < processor_count; ++i) {
		procfs_printf(node,
				"Processor: %d\n"
				"Manufacturer: %s\n"
				"MHz: %zd\n"
				"Family: %d\n"
				"Model: %d\n"
				"Model name: %s\n"
				"LAPIC id: %d\n"
				"\n",
				processor_local_data[i].cpu_id,
				processor_local_data[i].cpu_manufacturer,
				arch_cpu_mhz(), /* TODO Should this be per-cpu? */
				processor_local_data[i].cpu_family,
				processor_local_data[i].cpu_model,
				processor_local_data[i].cpu_model_name,
				processor_local_data[i].lapic_id
				);
	}
}

static struct procfs_entry procfs_pat = { 0, "pat", pat_func, 0 };
static struct procfs_entry procfs_irq = { 0, "irq", irq_func, 0 };
static struct procfs_entry procfs_cpuinfo = { 0, "cpuinfo", cpuinfo_func, 0 };

void procfs_install_x86_64(void) {
	procfs_install(&procfs_pat);
	procfs_install(&procfs_irq);
	procfs_install(&procfs_cpuinfo);
}
