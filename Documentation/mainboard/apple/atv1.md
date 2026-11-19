# Apple TV (1st generation) — coreboot port notes

Working notes for porting coreboot to the Apple TV 1st gen, board DMI
`AppleTV1,1` / `Mac-F4228DC8`, firmware `ATV11.88Z.00DA.B04.0912220919`.

**Status: pre-bring-up.** Nothing has been built or flashed yet. Everything below is
either measured on the physical board or derived from code in this tree; anything that
is an inference is marked as such.

The companion repository `../qemu-atv/` contains a QEMU fork that boots the original
firmware, plus the reverse-engineering tooling that produced most of the register facts
here. Its `AGENTS.md` is the long-form record; this file is the port-facing summary.

## HARD RULE — never reprogram the ICH7 GPIO block

**Do not clear or rewrite the ICH7 GPIO registers at I/O `0x500`–`0x53F`
(GPIOBASE) or the MMIO alias at `RCBA + 0x3400`.**

The board carries an autonomous **NXP 89V54RD2** (FlashFlex51, 8051-compatible
microcontroller) that has *no host-visible interface at all* — it is not an ACPI EC, it
does not answer on the SMBus, it has no ACPI device and therefore no driver. It is the
board's power-sequencing / front-LED / fan-and-thermal supervisor ("SMC" role), and the
ICH7 GPIO pins are its handshake lines. Reproducing the shipping firmware's GPIO
configuration is mandatory; changing it risks killing power, the fan, or the standby
state machine.

This rule is not theoretical — the shared Mac table in the tree does **not** match this
board. Measured live on the ATV via `/dev/port` at `0x500`:

| register | offset | value |
| --- | --- | --- |
| `GPIO_USE_SEL` | `0x00` | `0x1f40f7c0` |
| `GP_IO_SEL` | `0x04` | `0xe0a8af83` |
| `GP_LVL` | `0x0c` | `0xe0806ec3` |
| `GPI_INV` | `0x2c` | `0x00000482` |
| `GPIO_USE_SEL2` | `0x30` | `0x000100c0` |
| `GP_IO_SEL2` | `0x34` | `0x00000030` |
| `GP_LVL2` | `0x38` | `0x00010000` |
| `GP_RST_SEL1/2/3` | `0x60`/`0x64`/`0x68` | `0xffffffff` (unimplemented) |

Decoded: 15 pins in use (`GPIO6-10, 12-15, 22, 24-28`), **9 outputs**
(`GPIO6, 12, 14, 22, 24, 25, 26, 27, 28` — of which `GPIO6` and `GPIO14` are driven
**high**), 6 inputs (`GPIO7, 8, 9, 10, 13, 15`), invert bits `GPIO1, 7, 10`, and on
bank 2 `GPIO38, 39, 48` are outputs with `GPIO48` high, `GPIO38`/`GPIO39` low.

The existing shared table `src/mainboard/apple/i945_macs/gpio.c` differs from that in
five ways:

1. It configures `GPIO1` and `GPIO5` as GPIO; the ATV leaves them on their native
   function. `GPIO5` is even driven as an output on the non-iMac4,1 variants.
2. It makes `GPIO25`–`GPIO28` **inputs**; on the ATV they are outputs.
3. It drives `GPIO22` **high**; the ATV holds it low.
4. It drives `GPIO38`/`GPIO39` high; the ATV holds them low.
5. It inverts `GPIO13` on the MacBook variants; the ATV inverts `GPIO10` instead.

Two consequences for implementation:

* `setup_pch_gpios()` (`src/southbridge/intel/common/gpio.c`) writes each register with
  a **whole-register `outl()`** of the software bitmap, so any table that is supplied
  overwrites all 32 bits — unlisted pins become `0`, not "unchanged". Passing the Mac
  table would therefore *modify* pins the ATV never touches.
* A register that is `NULL` in `struct pch_gpio_map` is **not written at all**. So the
  safe options are, in order of preference: (a) supply an ATV-exact table derived from
  the table above, or (b) leave the pointers `NULL` so `setup_pch_gpios()` performs no
  writes whatsoever. Do **not** start from the Mac table and "adjust".

Never add a "clear GPIO" step, and do not let a payload or a later stage reset GPIOBASE
or the register block.

## Hardware

| | |
| --- | --- |
| CPU | Intel Pentium M 1.00 GHz, **Dothan**, family 6 model `0x0D` (CPUID `6D8`), stepping 8, 2 MB L2, **32-bit only** (no `lm`), microcode `0x21` |
| North bridge | Intel 945GM, `8086:27a0` rev 03 |
| South bridge | Intel ICH7-M (82801GBM), `8086:27b9` rev 02 |
| RAM | 256 MB DDR2-400, **single channel, one rank**, soldered, **no SPD EEPROM** |
| GPU | NVIDIA G72M `10de:01d7` behind the PEG root port `00:01.0` at `01:00.0`; IGD `00:02.0` exists but is unused |
| Audio | ICH7 HDA `8086:27d8`, codec feeds the Silicon Image **SiI1930CTU** HDMI transmitter |
| Ethernet | Realtek RTL-8100/8101L/8139 `10ec:8139` at `06:03.0` (behind `00:1e.0`) |
| Storage | ICH7 PATA `8086:27df` at `00:1f.1` (**no SATA/AHCI** — `00:1f.2` is absent) |
| USB | UHCI `8086:27c8` at `00:1d.0`, EHCI `8086:27cc` at `00:1d.7`; built-in IR receiver is a USB HID `05ac:8241` |
| SMBus | `8086:27da` at `00:1f.3`, I/O base `0xefa0`, **no devices respond at any address** |
| Flash | 2 MiB, top-boot, mapped `0xffe00000`–`0xffffffff`, **no Intel descriptor** |
| Display out | HDMI only (SiI1930 TMDS); max 1080p60 |
| Other | NXP 89V54RD2 system MCU (see the HARD RULE) |

## Register facts measured on the board

### Host bridge `00:00.0` (PCI config)
`EPBAR 0xfed19000`, `MCHBAR 0xfed14000`, `PCIEXBAR 0xf0000005` (MMCONF, 64 MB,
bus 00–3f), `DMIBAR 0xfed18000`, `DEVEN 0x83`, `PAM0 0x30`, `PAM1..6 0x33`,
`TOLUD 0x10` (256 MB), `CAPID0 0x91090009`, `CAPID0+4 0xa10a2503`.

Note `0xf0000000` is **PCI MMCONF, not NVRAM** — an earlier project note that treated it
as a platform store was wrong.

### ICH7 LPC `00:1f.0` (PCI config)
`PMBASE 0x400` (config `0x40` = `0x00000401`), `RCBA 0xfed1c000` (config `0xf0` =
`0xfed1c001`), **`BIOS_CNTL` (config `0xdc`) = `0x0000`** → `BIOSWE = 0`, `BLE = 0`,
i.e. flash writes are protected while the OS is running.

The **PIRQ router is left in APIC-only mode**: config `0x60`–`0x67` are all `0x80`, so
no PIRQ is routed to a legacy IRQ. That is correct for this board because the ACPI MADT
`_PRT` maps `PIRQA..PIRQG` to GSIs 16–22. `i945_macs/devicetree.cb` currently sets
`pirqa_routing`..`pirqh_routing` to `0x0b` (IRQ 11); the ATV's honest equivalent is
`0x80`. (Matches the QEMU finding that a PIC-only guest gets no PCI interrupts from this
firmware — by design.)

### MCHBAR `0xfed14000` (`ATV_MCHBAR_SEED` dumps, decoded)
```
CLKCFG   0xc00 = 0x00010020   FSB 400 MHz, DDR2-400 (bits 6:4 = 2)
DFT_STRAP1 0xe08 = 0x000202fd bit 20 = 0 -> graphics 1.05 V / 250 MHz CRCLK
C0DRT0   0x110 = 0x987418c8   C0DRT1 0x114 = 0x01405611
C0DRT2   0x118 = 0x80000230   C0DRT3 0x11c = 0x008c3088
C0DRC0   0x120 = 0x40000a06   C0DRC1 0x124 = 0xb10e1900   C0DRC2 0x128 = 0x0e000000
C0DMC    0x164 = 0x01000020   C0ODT  0x168 = 0x02024911   C0ODT2 0x16c = 0xe0018798
DCC      0x200 = 0x000f0400   CCCFT  0x208 = 0x04020108
DQSMT    0x2f4 = 0x0000200c   RCVENMT 0x2f8 = 0x00050a0f
C0DRB0..3 0x100 = 08 08 08 08  (32 MB units -> one 256 MB rank)
C0DRA0   0x108 = 0x0003       rows/cols 0xAD/0xAE (10 columns, 13/14 rows), no 2nd DIMM
C1*      0x180..            all zero -> channel B unpopulated
```
`CLKCFG` and `DFT_STRAP1` are the only two registers the firmware reads and never writes,
i.e. the only pure strap inputs.

### Flash variable store (for reference)
The 2 MiB image has a 192 KiB variable store at `0x1a0000` holding `Setup`, `MTC`,
`MemoryConfig`, `MemoryTypeInformation`. `MemoryConfig` is 264 bytes in the EFI global
namespace and carries the DRAM configuration that substitutes for the missing SPD:
`+0x0b` = record count (8), `+0x12` = `0x0190` = 400 MT/s, `+0x0a` = 42 (tRFC-shaped).

## The blocking problem: no SPD, but i945 raminit requires one

This board has **no SPD EEPROM**. Measured three ways: SMBIOS Type 17 manufacturer/part/
serial fields are all zero; an `I2C_SMBUS` read-byte ioctl returns `ENXIO` (no ACK) at
every address `0x50`–`0x57`; and a full `0x03`–`0x77` scan finds **no responder at all**
on any of the four adapters. The DRAM is soldered and Apple stored its timings in the
`MemoryConfig` variable instead.

Coreboot's `i945` raminit reads SPD over SMBus (`smbus_read_byte(device, SPD_MEMORY_TYPE)`
in `src/northbridge/intel/i945/raminit.c`). With no EEPROM the socket is marked
`SYSINFO_DIMM_NOT_POPULATED` and the sequence ends in `die("No memory installed.")`.
So a **static SPD is mandatory** before anything else can work.

Recommended approach, in order:

1. Synthesize a 64-byte DDR2 SPD that decodes to the real configuration
   (DDR2-400, 1 rank, 4 banks, 10 columns, 13/14 rows, x8 or x16, 256 MB, and timings
   consistent with the MCHBAR dump above). The `i945` raminit source doubles as the
   decoder: running its own arithmetic against the observed `C0DRT*`/`C0DRC*`/`CCCFT`
   values tells us which SPD bytes produce them.
2. Feed it through a `__weak` board hook rather than patching the SMBus path. The natural
   seam already exists: `mainboard_get_spd_map()` in
   `src/northbridge/intel/i945/romstage.c` is weak and board-overridable, and
   `sdram_initialize()` already accepts an address map. A new weak
   `mainboard_get_spd(int socket, u8 *raw_spd, size_t len)` that returns a length would
   let a board supply SPD bytes without touching SMBus at all, leaving existing boards
   on the current path.
3. Keep the blob in CBFS (or a static array) so it can be corrected without reflashing
   the whole image during bring-up.

Do not attempt an SMBus EEPROM emulation — there is no device to emulate; the search for
one already failed on real hardware.

Microcode: the Dothan `6D8` `rev 0x21` update is the third blob in the flash microcode
volume (flash offset `0x19d100`, 2 KiB, `0x800` stride with zeroed header size fields).
`atv/ucode.py` in the companion repository extracts it.

## Mainboard skeleton

The port should become a **variant of `apple/i945_macs`**, which already selects what
this board needs:

```
BOARD_APPLE_I945_MACS_COMMON selects:
  CPU_INTEL_SOCKET_M  NORTHBRIDGE_INTEL_I945  NORTHBRIDGE_INTEL_SUBTYPE_I945GM
  SOUTHBRIDGE_INTEL_I82801GX  NO_UART_ON_SUPERIO  EC_ACPI
  HAVE_OPTION_TABLE  HAVE_CMOS_DEFAULT  HAVE_MP_TABLE
  BOARD_ROMSIZE_KB_2048  INTEL_INT15  HAVE_ACPI_TABLES  HAVE_ACPI_RESUME
```

`BOARD_ROMSIZE_KB_2048` already matches the 2 MiB flash, and `NO_UART_ON_SUPERIO` +
`EC_ACPI` are right (there is no Super I/O and no EC).

Work items, in order:

1. Restore CPU support. `src/cpu/intel/socket_m/` exists but its Kconfig selects only
   `CPU_INTEL_MODEL_6EX` and `CPU_INTEL_MODEL_6FX`; **`model_6dx` was removed** by
   `1083a4d2324c` ("cpu/intel: Remove models 69x and 6dx", 2019) and `socket_m`'s
   socket-479 Kconfig by `ec6d07a3309c`. Restore both, then add
   `select CPU_INTEL_MODEL_6DX` to the socket_M Kconfig. Reverting the two commits is
   cleaner than hand-porting:
   ```
   git revert --no-commit 1083a4d2324c
   git revert --no-commit ec6d07a3309c
   ```
2. Add `src/mainboard/apple/i945_macs/variants/atv1/` with `board_info.txt`,
   `overridetree.cb`, and (if required) `hda_verb.c`, plus the matching
   `BOARD_APPLE_ATV1` Kconfig entry, `MAINBOARD_PART_NUMBER "AppleTV1,1"`, and
   `VARIANT_DIR "atv1"` in `src/mainboard/apple/i945_macs/Kconfig`.
3. Give the variant its **own GPIO table** — or none (see the HARD RULE).
4. Set `pirqa_routing`..`pirqh_routing` to `0x80` in the variant's `overridetree.cb` to
   match the board (APIC-only), unless the payload needs legacy IRQs.
5. Decide the GPU story. The ATV has no IGD panel and the real GPU is on the PEG port,
   so the i945 IGD display registers in `overridetree.cb` (`GMA_STATIC_DISPLAYS`,
   `gpu_hotplug`, LVDS/backlight delays) do not describe this board. Linux `nouveau`
   can modeset the G72M without a video BIOS; a VGA BIOS option ROM would only matter
   for a payload that needs legacy VGA.
6. Boot device: configure `BOOT_DEVICE_SPI_FLASH`. `i82801gx` already provides ICH7 SPI
   support when that is selected (`select SOUTHBRIDGE_INTEL_COMMON_SPI_ICH7`), and
   `INTEL_TOP_SWAP_BOOTBLOCK_SIZE` is available. **Open question:** whether this board
   wires the flash to the ICH7 SPI controller or to FWH/LPC. There is no Intel
   descriptor in the dump, which argues for FWH or for descriptorless SPI. Resolve by
   reading `RCBA + 0x3800` (`SPIBAR`, `HSFS`/`FRAP`) on the real board and by reading
   the flash chip's part number off the board. `BIOS_CNTL.BIOSWE = 0` means the flash is
   write-protected in the running system, so any in-system flashing needs that opened
   first.

## Measurements still to take (all read-only, on the real board)

1. `RCBA + 0x0000`–`0x3FFF` (16 KiB "root complex register block") — the private chipset
   configuration that `i82801gx` programs. Having the vendor values lets us diff instead
   of guess. Covers GPIO at `+0x3400` and SPIBAR at `+0x3800`.
2. I/O `0x400`–`0x47F` (ACPI PM block: `GEN_PMCON_1/2/3`, `PM1_*`, `GPE0`, `TCO`) — the
   PM straps and sleep-state configuration.
3. Whether the flash is SPI or FWH (item 6 above).
4. The DRAM device organisation, to pin the synthetic SPD: `C0DRA0 = 3` narrows it to
   10 columns × 13/14 rows, but x8-vs-x16 needs the physical chip markings (2× 1 Gb x16,
   4× 512 Mb x16, or 8× 256 Mb x8 on a 64-bit channel all fit 256 MB).

## Rules for this fork

* `AGENTS.md` in this repository is **upstream coreboot's** file and defines mandatory
  policy (notably: AI-generated commits must carry a `AI-generated` trailer, and Gerrit
  review comments must be prefixed `[AI-generated] ` and must never reply to human
  reviewers). Do not overwrite or repurpose it; project notes live here instead.
* Do not commit unless explicitly asked.
* Changes should stay upstreamable: board-specific behaviour belongs behind a Kconfig /
  weak-function seam, not behind an unconditional edit in shared northbridge or
  southbridge code.
* Never write to the system MCU, and never repurpose the GPIO block it uses.
