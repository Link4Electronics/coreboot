## SPDX-License-Identifier: GPL-2.0-only

ramstage-y += model_6dx_init.c
subdirs-y += ../../x86/name

cpu_microcode_bins += 3rdparty/blobs/cpu/intel/model_6dx/microcode.bin
