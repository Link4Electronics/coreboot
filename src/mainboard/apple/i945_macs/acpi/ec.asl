/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * The Apple TV 1st gen has no embedded controller.  Power, thermal and LED
 * supervision is done by an autonomous NXP 89V54RD2 MCU that has no
 * host-visible interface.  Do not advertise a PNP0C09 EC: it would make the
 * OS drive ports 0x62/0x66 that nothing answers on.
 */
#if !CONFIG(BOARD_APPLE_ATV1)

#define LIDS_OFFSET 0x01
#define HPAC_OFFSET 0x01
#define WKLD_OFFSET 0x02

#include <ec/apple/acpi/ec.asl>

#endif
