## SPDX-License-Identifier: GPL-2.0-only

romstage-y += gpio.c
bootblock-y += early_init.c
romstage-y += early_init.c

ramstage-y += cstates.c
ramstage-y += variants/$(VARIANT_DIR)/hda_verb.c
romstage-$(CONFIG_BOARD_APPLE_ATV1) += variants/atv1/spd.c

# The ATV1's Dothan is soldered to the board, so there is no point in
# carrying microcode for the other Socket M CPU families.  This overrides
# the list that the 6EX and 6FX drivers add.  It has no effect when
# CONFIG_CPU_MICROCODE_CBFS_EXTERNAL_BINS is used.
ifeq ($(CONFIG_BOARD_APPLE_ATV1),y)
$(obj)/cpu_microcode_blob.bin: cpu_microcode_bins := \
	3rdparty/blobs/cpu/intel/model_6dx/microcode.bin
endif
