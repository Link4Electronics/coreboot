/* SPDX-License-Identifier: GPL-2.0-only */

#include <console/console.h>
#include <cpu/cpu.h>
#include <cpu/intel/microcode.h>
#include <cpu/x86/cache.h>
#include <cpu/x86/lapic.h>
#include <cpu/x86/mtrr.h>
#include <cpu/x86/name.h>
#include <device/device.h>

static void model_6dx_init(struct device *cpu)
{
	char processor_name[49];

	/* Turn on caching if we haven't already */
	enable_cache();

	/* Update the microcode */
	intel_update_microcode_from_cbfs();

	/* Print processor name */
	fill_processor_name(processor_name);
	printk(BIOS_INFO, "CPU: %s.\n", processor_name);

	/* Setup MTRRs */
	x86_setup_mtrrs();
	x86_mtrr_check();

	/* Enable the local CPU APIC */
	enable_lapic();
}

static struct device_operations cpu_dev_ops = {
	.init     = model_6dx_init,
};

static const struct cpu_device_id cpu_table[] = {
	/* Pentium M (Banias/Dothan) */
	{ X86_VENDOR_INTEL, 0x06d0, CPUID_ALL_STEPPINGS_MASK },
	CPU_TABLE_END
};

static const struct cpu_driver driver __cpu_driver = {
	.ops      = &cpu_dev_ops,
	.id_table = cpu_table,
};
