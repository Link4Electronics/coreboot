/* SPDX-License-Identifier: GPL-2.0-only */

#include <cpu/x86/smm.h>
#include <southbridge/intel/common/pmutil.h>

#if !CONFIG(BOARD_APPLE_ATV1)

/*
 * The Apple TV 1st gen has no embedded controller, so there is no EC SCI to
 * route.  GPI12 is also an output on this board.
 */
#define GPE_EC_SCI	12

int mainboard_smi_apmc(u8 data)
{
	switch (data) {
	case APM_CNT_ACPI_ENABLE:
		/* route H8SCI to SCI */
		gpi_route_interrupt(GPE_EC_SCI, GPI_IS_SCI);
		break;
	case APM_CNT_ACPI_DISABLE:
		/* route H8SCI# to SMI */
		gpi_route_interrupt(GPE_EC_SCI, GPI_IS_SMI);
		break;
	default:
		break;
	}
	return 0;
}

#endif
