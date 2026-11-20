/* SPDX-License-Identifier: GPL-2.0-only */

#include <acpi/acpi.h>
#include <device/device.h>
#include <northbridge/intel/i945/i945.h>
#include <drivers/intel/gma/int15.h>
#include <ec/acpi/ec.h>
#include <smbios.h>

/* The iMac4,1 has no IGD panel; the Apple TV 1st gen has no IGD at all and
 * routes its display through the discrete GPU on the PEG port. */
#define HAS_IGD_INT15	(!CONFIG(BOARD_APPLE_IMAC41) && !CONFIG(BOARD_APPLE_ATV1))

/* The Apple TV 1st gen has no embedded controller: power, thermal and LED
 * supervision is done by an autonomous NXP 89V54RD2 that has no host-visible
 * interface, so there is nothing to talk to on ports 0x62/0x66. */
#define HAS_EC		(!CONFIG(BOARD_APPLE_ATV1))

#if HAS_IGD_INT15

#define PANEL INT15_5F35_CL_DISPLAY_DEFAULT

static void mainboard_init(struct device *dev)
{
	install_intel_vga_int15_handler(GMA_INT15_ACTIVE_LFP_INT_LVDS, GMA_INT15_PANEL_FIT_DEFAULT, PANEL, 3);
}

static void mainboard_enable(struct device *dev)
{
	dev->ops->init = mainboard_init;
}

#endif

#if HAS_EC
static void mainboard_final(void *chip_info)
{
	ec_set_bit(0x10, 2); /* switch off led */
}
#endif

#if CONFIG(BOARD_APPLE_ATV1)
/* SMBIOS has no set-top box enclosure type; the vendor firmware reports the
 * ATV1 as "Space-saving" (0x0f), which describes the case as well. */
smbios_enclosure_type smbios_mainboard_enclosure_type(void)
{
	return SMBIOS_ENCLOSURE_SPACE_SAVING;
}

/* The vendor FADT identifies the board as an appliance PC. */
void mainboard_fill_fadt(acpi_fadt_t *fadt)
{
	fadt->preferred_pm_profile = PM_APPLIANCE_PC;
}
#endif

struct chip_operations mainboard_ops = {
#if HAS_IGD_INT15
	.enable_dev = mainboard_enable,
#endif
#if HAS_EC
	.final = mainboard_final,
#endif
};
