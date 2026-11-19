# Apple TV (1st generation) — coreboot port notes

Working notes for porting coreboot to the Apple TV 1st gen, board DMI
`AppleTV1,1` / `Mac-F4228DC8`, firmware `ATV11.88Z.00DA.B04.0912220919`.

**Status: in tree, building, and booting to SeaBIOS under qemu-atv; not yet flashed.**
The variant, the socket-M CPU model, the SPD hook and the SPI boot path are implemented
and `apple/i945_macs` builds a 2 MiB image for `AppleTV1,1`. Nothing has been run on the
physical board yet. Everything below is either measured on the physical board, derived
from the two flash dumps, or derived from code in this tree; anything that is an
inference is marked as such.

The companion repository `../qemu-atv/` contains a QEMU fork that boots the original
firmware, plus the reverse-engineering tooling that produced most of the register facts
here. Its `AGENTS.md` is the long-form record; this file is the port-facing summary.

## HARD RULE — never reprogram the ICH7 GPIO block

**Do not clear or rewrite the ICH7 GPIO registers at I/O `0x500`–`0x53F`**
(`GPIOBASE`, enabled by config `0x48` = `0x0501`). On ICH7 the GPIO block exists
**only** as that I/O window — it is *not* mirrored into the RCBA (measured: the RCBA
dword at `+0x3400` is the `RC` register, `0x00000004`, and none of the three GPIO
register values appear anywhere in the 16 KiB RCBA dump).

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
| Ethernet | Realtek RTL-8100/8101L/8139 `10ec:8139` at `02:03.0` (behind `00:1e.0`) |
| Storage | ICH7 PATA `8086:27df` at `00:1f.1` (**no SATA/AHCI** — `00:1f.2` is absent) |
| USB | UHCI `8086:27c8` at `00:1d.0`, EHCI `8086:27cc` at `00:1d.7`; built-in IR receiver is a USB HID `05ac:8241` |
| SMBus | `8086:27da` at `00:1f.3`, I/O base `0xefa0`, **no devices respond at any address** |
| Flash | 2 MiB, top-boot, mapped `0xffe00000`–`0xffffffff`, **no Intel descriptor** |
| Display out | HDMI only (SiI1930 TMDS); max 1080p60 |
| Other | NXP 89V54RD2 system MCU (see the HARD RULE) |

## Physical part numbers (read off the board)

| | part | decode |
| --- | --- | --- |
| System RAM | Nanya **NT5TU32M16BG-3C**, 4 devices | 512 Mb, **32M × 16**, 4 banks, **13 row (A0–A12) / 10 column (A0–A9)** address bits, 8192 refresh, burst 4/8, 1.8 V SSTL-1.8, 333 MHz max ⇒ **DDR2-667 5-5-5** |
| Graphics RAM | Samsung **K4J55323QI-BC14**, 2 devices | 256 Mb GDDR3, 2M × 32 × 4 banks, 136-ball FBGA, 4096 refresh, **700 MHz / 1.4 Gb/s per pin** |
| Ethernet | Realtek **RTL8100C** | **10/100** MAC, 8139-compatible (`10ec:8139`), behind the PCI bridge at `02:03.0` |

This closes the geometry question in the SPD section below. `C0DRA0 = 3` means
`columnsrows = 0xAD`, i.e. 13 row and 10 column bits, and the 512 Mb **x16** Nanya part is
the one addressed with A0–A12 / A0–A9; the x8 sibling (`NT5TU64M8…`) uses A0–A13, 14
rows, which would have produced `dra = 4`. So: **x16 confirmed**, 4 devices × 16 bits =
one 64-bit channel, 4 × 512 Mb = 2 Gb = 256 MB.

The device is a DDR2-667 5-5-5 part, but the shipping firmware runs it at **DDR2-400**:
`memclk()` computes `((CLKCFG >> 4) & 7) - 1` and `CLKCFG = 0x00010020` gives `2 - 1 = 1`
⇒ 400, while `fsbclk()` reads `CLKCFG & 7 = 0` ⇒ 400 MHz FSB. That matches the flash
`MemoryConfig` field `+0x12 = 0x0190 = 400` MT/s.

Two chipset straps make the channel questions need no board code at all, because both are
read from `CAPID0+4` (`00:00.0` config `0xe4`) = `0xa10a2503`:

* `sdram_capabilities_dual_channel()` = `!(reg >> 24 & 1)` = `!(0xa1 & 1)` = **0 → single channel**.
* `sdram_capabilities_max_supported_memory_frequency()` = bits 2:0 = 3 → **533** ceiling.

The two GDDR3 parts only matter if VRAM timing is ever modelled (out of scope, see the
video notes): 2 × 32 bits = a 64-bit memory bus and 2 × 256 Mb = **64 MB**, which is what
`nouveau` reports (`fb: 64 MiB GDDR3`) and what the QEMU shim already models.

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

The **PIRQ router is left in APIC-only mode**: `PIRQA_ROUT`..`PIRQD_ROUT` (config
`0x60`–`0x63`) and `PIRQE_ROUT`..`PIRQH_ROUT` (config `0x68`–`0x6b`) are all `0x80`, so
no PIRQ is routed to a legacy IRQ. (`0x64` is `SERIRQ_CNTL`, not a PIRQ routing
register; it reads `0xd0`.) That is correct for this board because the ACPI `_PRT` maps
`PIRQA..PIRQG` to GSIs 16–22. `i945_macs/devicetree.cb` sets `pirqa_routing`..
`pirqh_routing` to `0x0b` (IRQ 11) for the Mac variants; the ATV's overridetree uses
`0x80`. (Matches the QEMU finding that a PIC-only guest gets no PCI interrupts from this
firmware — by design.)

### ICH7 RCBA `0xfed1c000` — 16 KiB, measured

The RCBA is the chipset's private register block (neither PCI config nor the GPIO
window). Full dump: `atv/dumps/real-rcba.bin` in the companion repository. 142 of the
4096 dwords are nonzero, clustered in twelve 256-byte pages (`0x0000`, `0x0100`,
`0x0200`, `0x1d00`, `0x1e00`, `0x2000`, `0x2200`, `0x3000`, `0x3100`, `0x3400`, `0x3e00`,
`0x3f00`). Register names below are the ones this tree uses
(`src/southbridge/intel/i82801gx/i82801gx.h`):

| register | RCBA offset | measured | note |
| --- | --- | --- | --- |
| `TCTL` | `0x3000` | `0x00000000` | thermal control off |
| SPI | `0x3020`…`0x3088` | see next section | |
| `D31IP`…`D27IP` | `0x3100`…`0x3110` | `0x00042210` `0x00002100` `0x10004321` `0x00214321` `0x00000001` | interrupt polarity, internal functions |
| `D31IR`…`D27IR` | `0x3140`…`0x3148` | `0x0232` `0x3246` `0x0235` `0x3201` `0x3216` | per-function PIRQ routing, matches the DSDT `_PRT` |
| `RC` | `0x3400` | `0x00000004` | |
| `HPTC` | `0x3404` | `0x00000080` | bit 7 = HPET enable, bits 15:14 = 0 → `0xfed00000`. Corroborated: the flash ships an ACPI `HPET` table and `/proc/iomem` shows `fed00000-fed003ff : HPET 0` |
| `GCS` | `0x3410` | `0x00880461` | bit 5 (`No reset`) already set — the same bit `i82801gx/early_init.c` sets |
| `BUC` | `0x3414` | `0x00000002` | |
| `FD` | `0x3418` | `0x00300ee5` | function-disable strap; `FD_LPCB` (bit 14) **clear** → LPC bridge enabled, as it must be |
| `CG` | `0x341c` | `0xcf88000f` | |
| `IOTR0`…`IOTR3` | `0x1e80`…`0x1e98` | nonzero | I/O trap ranges in use |
| unnamed | `0x2010`…`0x2034` | nonzero | not named by this tree's header — unidentified |

The `D31IR`…`D27IR` values are not zero; they encode the same PIRQ assignment as the
shipping DSDT `PCI0._PRT`. With `A` in bits 3:0, `B` in 7:4, `C` in 11:8 and `D` in
15:12:

```
D31IR = 0x0232 -> 1f A/B/D = PIRQC/PIRQD/PIRQA = GSI 18/19/16
D30IR = 0x3246 -> 1e A/B   = PIRQG/PIRQE       = GSI 22/20
D29IR = 0x0235 -> 1d A-D   = PIRQF/PIRQD/PIRQC/PIRQA = GSI 21/19/18/16
D28IR = 0x3201 -> 1c A-D   = PIRQB/PIRQA/PIRQC/PIRQD = GSI 17/16/18/19
D27IR = 0x3216 -> 1b A     = PIRQG             = GSI 22
```

APIC-only mode is selected by the `PIRQn_ROUT` registers, not by `D*IR`.
`i945_macs/early_init.c` programs the same five values, so the runtime `_PRT` that
`intel_acpi_gen_def_acpi_pirq()` generates from `D*IR` matches the vendor firmware.

### The flash is SPI, and it is descriptorless

**Settled by measurement — `BOOT_DEVICE_SPI_FLASH` is correct, FWH is not used.** The
ICH7 SPI sequencer lives at **`RCBA + 0x3020`**, not at `+0x3800` (that offset is ICH9
and later; `src/southbridge/intel/common/spi.c` branches on the two). Reading `+0x3800`
returns zeros on this part and once produced a wrong "no SPI here" conclusion.

`RCBA+0x3020` decoded with the layout of `struct ich7_spi_regs`
(`src/southbridge/intel/common/spi.c`):

```
SPIS   +0x00 (u16) = 0x8000      SPIC +0x02 (u16) = 0x4020
SPIA   +0x04       = 0x00000000
BBAR   +0x50       = 0x00000000   boot flash region not programmed
PREOP  +0x54 (u16) = 0x5006      OPTYPE +0x56 (u16) = 0x7f48
OPMENU +0x58       = 9f 03 05 04 20 d8 02 01
PBR0/1/2 +0x60     = 0x80f9fe00  0x80000000  0x80ffffd0
```

`OPMENU` is the standard ICH7 set: `9f` RDID, `03` READ, `05` RDSR, `04` WRDI, `20` 4 KiB
erase, `d8` 64 KiB erase, `02` page program, `01` WRSR — i.e. an ordinary SPI NOR part,
not one of the JEDEC-weird ones.  It does **not** contain fast read (`0x0b`), so the ATV1
variant selects `SPI_FLASH_NO_FAST_READ`; `0x03` is present and works whether the
controller is locked or not.  This also covers the AAI opcode menu that
`spi_finalize_ops()` installs for SST parts, which drops `0x0b` as well.

Two write protections are active in the shipping firmware:

* `SPIS` bit 15 = **`FLOCKDN`** (coreboot reads this as `HSFS_FLOCKDN`), so the PBR
  protection registers are locked until the next platform reset.
* `00:1f.0` `BIOS_CNTL` (`0xdc`) = `0x0000` → `BIOSWE = 0`, `BLE = 0`.

The three `PBR` values do **not** decode into ranges that fit a 2 MiB flash under
coreboot's ICH7 formula (`spi_fpr()`: base in bits 11:0, limit in bits 23:12, both in
4 KiB units, bit 31 = write-protect enable), so their real field layout is **not
established** — the raw values are recorded rather than interpreted. They do not matter
for bring-up: the flash is programmed externally with a T48 programmer, and after the
first coreboot boot these registers are ours to program.

What this changes in the port:

* `select BOOT_DEVICE_SPI_FLASH` (and therefore
  `select SOUTHBRIDGE_INTEL_COMMON_SPI_ICH7` from `i82801gx/Kconfig`) instead of any FWH
  option. There is **no** FWH driver in this tree (`BOOT_DEVICE_FWH`/`fwh_idsel` do not
  exist), which is another reason a FWH conclusion would have been a dead end.
* `INTEL_TOP_SWAP_BOOTBLOCK_SIZE` applies: the bootblock sits at the top of the 2 MiB
  part, which is exactly how the Apple image is laid out (`_FVH` files at `0x1d0000`,
  reset vector at `0xfffffff0`).

### ACPI PM block at I/O `0x400`–`0x47F`

`PMBASE` (`00:1f.0` config `0x40`) = `0x0401` → I/O `0x400`. Dump:
`atv/dumps/real-pm-0x400.bin`. Nonzero dwords, as left by a running Linux:

```
0x400 = 0x01204401   0x404 = 0x00000003   0x408 = 0x005bb549   0x428 = 0x2a010000
0x42c = 0x04000804   0x430 = 0x00000033   0x434 = 0x00004100   0x438 = 0x2a010000
0x444 = 0x000003e0   0x454 = 0x001c1336   0x460 = 0x00000004   0x468 = 0x00080800
0x470 = 0x00320003
```

Most of this is OS-owned runtime state (SCI enable, timer, TCO), so it says little about
the firmware's own configuration. The straps that *do* matter for `HAVE_ACPI_RESUME` are
in `00:1f.0` **config** space (`0xa0`–`0xa4`), not in I/O space:

```
GEN_PMCON_1 (0xa0) = 0x0200    GEN_PMCON_2 (0xa2) = 0x0000    GEN_PMCON_3 (0xa4) = 0x0001
```

`GEN_PMCON_3` bit 0 = `SLEEP_AFTER_POWER_FAIL` set; `RTC_POWER_FAILED` (bit 1) and
`RTC_BATTERY_DEAD` (bit 2) both clear → the RTC is good and S3 resume data is valid.

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

### Flash variable store (measured on both dumps)

The 192 KiB store occupies `0x1a0000`–`0x1cffff`. It is a circular append log:
records are appended, superseded records are marked deleted, and the first record
sits at `0x1a0058`. Each record is a 32-byte header (`aa 55`, attributes word,
state dword, name length in bytes, data length, 16-byte GUID) followed by a
NUL-terminated UTF-16 name and the payload.

The two independent dumps differ **only** inside this store — 6548 bytes in
`0x1aac2a`–`0x1adf19`, no code difference at all:

| dump | records | live |
| --- | --- | --- |
| T48, chip desoldered (`appletv.bin`) | 179 | 37 |
| flashrom (`atv-flash.bin`) | 202 | 43 |

The extra flashrom records are later `Setup`/`MTC`/`MemoryConfig` appends. In both
images **the only live variable is `MemoryConfig`**; `Setup` (64/72 records),
`MTC` (55/63), `MemoryTypeInformation` (13/14), `AAPL,PathProperties0000`,
`AcpiGlobalVariable`, `Timeout`, `ConOut`, `rtc-offset`, `BootOrder` and
`BootFFFF` exist only as deleted records. The "no boot options" statement is
therefore about the live set, not about the raw bytes.

`MemoryConfig` is 264 bytes (`0x108`) in the EFI global variable namespace
(`8be4df61-93ca-11d2-aa0d-00e098032b8c`). The history contains six distinct
payloads and they differ in **exactly one byte, `+0x100`**, which takes values
`0x25`–`0x32`; every DRAM field is identical in all 45 `MemoryConfig` records
across both dumps:

```
+0x0a = 42      (105 ns in 2.5 ns units — matches SPD tRFC)
+0x0b = 8       (record count)
+0x12 = 0x0190  (400 MT/s)
```

So `+0x100` is not a timing field (it looks like a boot counter), and the two
dumps are equivalent for raminit purposes.


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

The SPD is generated by `atv/spdgen.py` in the companion repository
(`atv/venv/bin/python atv/spdgen.py --c` prints the array, `--bin` writes the 128
bytes). It is a **Nanya NT5TU32M16BG-3C**: DDR2-667 5-5-5, 512 Mb, 32M × 16, 4 banks,
A0–A12 rows / A0–A9 columns, 8192 refresh, burst 4/8, SSTL-1.8, tAC 0.45 ns. Four devices
make the board's single 256 MB rank on a 64-bit channel.

| bytes | contents |
| --- | --- |
| 0–8 | `80 08 08 0d 0a 00 40 00 05` — 13 rows, 10 cols, 1 rank, 64-bit, SSTL-1.8 |
| 9/23/25 | `30 38 50` — tCK 3.0 / 3.8 / 5.0 ns for CL5 / CL4 / CL3 |
| 10/24/26 | `45` — tAC 0.45 ns at each CL |
| 16/17/18 | `0c 04 1c` — BL4+BL8, 4 banks, CL3/CL4/CL5 offered |
| 27/28/29 | `3c 1e 3c` — tRP 15 ns, tRRD 7.5 ns, tRCD 15 ns |
| 30/31 | `2d 40` — tRAS 45 ns, rank density 256 MB |
| 34–38 | `40 40 3c 28 1e` — tDS/tDH 0.4 ns, tWR 15 ns, tWTR 10 ns, tRTP 7.5 ns |
| 41/42/43 | `3c 69 80` — tRC 60 ns, tRFC 105 ns, tCKmax 8 ns |
| 62/63 | `12` SPD rev 1.2, then the checksum (sum of bytes 0–62) |

**`ddr2.c` uses three different time encodings and they are not interchangeable.**
Getting this wrong is silent — the SPD still decodes, just 10× off:

* `spd_decode_tck_time()` (bytes 9/23/25/43): high nibble = whole ns, low nibble = tenths.
  `0x50` → `1280` = `TCK_200MHZ`, `0x30` → `768` = `TCK_333MHZ`.
* `spd_decode_bcd_time()` (bytes 10/24/26/34/35): **ns × 100**, so 0.45 ns is `0x45`.
  A naive "0.1 ns in BCD" guess produces `0x04` and 0.04 ns.
* `spd_decode_quarter_time()` (bytes 27/28/29/36/37/38): value = **ns × 4**.

Predicted outcome, traced through `choose_tclk()` with `CAPID0+4` giving a 533 ceiling:
`ctrl_min_tclk = 1280/533 = 960` (and `normalize_tck()` leaves 960 as `TCK_266MHZ`);
`cas_mask = 0x1C` so `spd_get_msbs()` starts at 4 with `tclk = 768 < 960`, drops to 3 with
`tclk = 972 ≥ 960`; `normalize_tck(972)` rounds **up** to `1280 = TCK_200MHZ`.
⇒ **400 MT/s at CAS 3**, which is exactly what the vendor firmware runs.

**Do not set `CONFIG_MAXIMUM_SUPPORTED_FREQUENCY=400` "to be safe".** It raises
`ctrl_min_tclk` to 1280, the search walks down to index 2, and `sysinfo->cas` becomes 2,
which trips `die("Could not find common memory frequency and CAS")`. The ceiling is
already right from the strap.

**The SPD has since been cross-checked against the vendor DRAM registers.** Running the
array through the same path (`spd_decode_ddr2()`, `choose_tclk()` with the 533 ceiling,
`derive_timings()`) yields CAS 3 at 400 MT/s with tRAS 9 / tRP 3 / tRCD 3 / tWR 3 /
tRFC 21 clocks and one 256 MB rank. Feeding those into the `C0DRT0` formula of
`sdram_set_timing_and_control()` gives

```
C0DRT0 predicted = 0x987418c8
C0DRT0 measured  = 0x987418c8   (vendor firmware MCHBAR)
```

an exact match of the register that encodes CAS, burst length, tWR, tRP, the
B2B write-to-read spacing and tRD. `C0DRT1` matches as well
(`0x00485611` predicted vs `0x01405611` measured), except that the vendor programs
tRAS as 8 cycles instead of 9 and `[27:24]` holds the receive-enable nibble that
coreboot fills during its own RE training. One extra tRAS cycle is conservative.
This is the strongest offline evidence that the static SPD is correct.

Feeding the bytes in is done with a `__weak` board hook rather than a patch to the SMBus
path. The natural seam already exists: `mainboard_get_spd_map()` in
`src/northbridge/intel/i945/romstage.c` is weak and board-overridable, and
`sdram_initialize()` already accepts an address map. The weak
`mainboard_get_spd(int socket, u8 *raw_spd, size_t len)` lets a board supply SPD bytes
without touching SMBus at all; returning zero leaves every existing board on the current
path. The bytes stay a static array next to the board (not a binary blob) so each field
stays reviewable.

Do not attempt an SMBus EEPROM emulation — there is no device to emulate; the search for
one already failed on real hardware.

Microcode: the Dothan `6D8` `rev 0x21` update is the third blob in the flash microcode
volume (flash offset `0x19d100`, 2 KiB, `0x800` stride with zeroed header size fields).
`atv/ucode.py` in the companion repository extracts it.

## Mainboard implementation

The port is a **variant of `apple/i945_macs`**, which already selects what this board
needs:

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

1. **Done.** Restore CPU support. `src/cpu/intel/socket_m/` selects
   `CPU_INTEL_MODEL_6DX` again and `src/cpu/intel/model_6dx/` is back in the tree.
2. **Done.** `src/mainboard/apple/i945_macs/variants/atv1/` exists with `board_info.txt`,
   `overridetree.cb`, `hda_verb.c` and `spd.c`, plus `BOARD_APPLE_ATV1`,
   `MAINBOARD_PART_NUMBER "AppleTV1,1"` and `VARIANT_DIR "atv1"`.
3. **Done.** The variant supplies an all-`NULL` GPIO map, so `setup_pch_gpios()`
   performs no writes (see the HARD RULE).
4. **Done.** `pirqa_routing`..`pirqh_routing` are `0x80` in the variant's
   `overridetree.cb`, matching the board (APIC-only).
5. **Decided.** No IGD: `02.0`/`02.1` are off and the display is the G72M on the PEG
   port. Linux `nouveau` modesets it without a video BIOS; a VGA option ROM would only
   matter for a legacy-INT10 payload, so none is added.  SeaBIOS is such a payload, so
   it has no display in this configuration; the vendor VBIOS (`m63-vbios.rom`,
   `10de:01d7`) can be added locally with `CONFIG_VGA_BIOS` and `VGA_BIOS_FILE` when a
   pre-kernel console is needed.
6. **Done.** `BOOT_DEVICE_SPI_FLASH` is selected and the image is laid out top-boot
   (`INTEL_TOP_SWAP_BOOTBLOCK_SIZE = 0x10000`).
7. **Done.** `SPI_FLASH_NO_FAST_READ` is selected so that `spi_flash_read()` uses
   `0x03`, which both the vendor and coreboot opcode menus contain.
8. **Done.** The build enables `CONFIG_USBDEBUG`.  ICH7 exposes the debug
   capability on `00:1d.7` and hardwires physical port 1.

### QEMU verification

The companion `qemu-atv` machine boots this image to SeaBIOS.  Its ATV machine backs
cache-as-RAM with a RAM region configured by machine properties.  The vendor firmware
uses `0xfef00000`/`0x40000`; this port's i945 CAR is `0xfefc0000`/`0x8000`, so pass the
coreboot geometry explicitly:

```
ATV_CAR_BASE=0xfefc0000 ATV_CAR_SIZE=0x10000 \
    ../qemu-atv/atv/atvboot.sh -bios build/coreboot.rom
```

The model does not implement the ICH7 SPI sequencer, so `spi_flash_probe()` fails there
(`SF: Failed to send command 9f`) and only the memory-mapped CBFS path is exercised.  It
also has no EHCI controller, so USB debug cannot be tested; both need the physical board.
QEMU's `pentium-m` reports platform ID 0, while the Dothan update's `pf` is `0x8`
(platform 3), so the `microcode: failed because no ucode was found` message in the QEMU
log is a CPU-model mismatch; the real part should load the update.

### Bring-up visibility

There is no Super I/O and no UART on this board, so `CONSOLE_SERIAL` has nowhere to go.
The only early output available is POST codes on port `0x80` (needs a POST card) or the
ICH7 USB debug port.  The build enables `CONFIG_USBDEBUG`; connect a standard EHCI debug
dongle (Net20DC, or a BeagleBone/Raspberry Pi gadget) directly to the external USB port
and capture the host side at 115200 8n1.  ICH7 hardwires the debug port to physical port
1, which the vendor OS confirms (`ehci-pci 0000:00:1d.7: debug port 1`); the built-in IR
receiver is on UHCI port 2.  Do not put a hub between the dongle and the port.
`CONSOLE_SERIAL` is kept for QEMU but has no hardware behind it.  Do not expect serial
console output from the first image.

## Measurements still to take (all read-only, on the real board)

1. ~~`RCBA + 0x0000`–`0x3FFF`~~ — **done**, see "ICH7 RCBA" above
   (`atv/dumps/real-rcba.bin`).
2. ~~I/O `0x400`–`0x47F`~~ — **done** (`atv/dumps/real-pm-0x400.bin`). Note
   `GEN_PMCON_1/2/3` are in `00:1f.0` **config** space at `0xa0`–`0xa4`, not in this I/O
   window; those values came from the `lspci -xxx` capture instead.
3. ~~Whether the flash is SPI or FWH~~ — **done**: SPI, descriptorless.
4. ~~The DRAM device organisation~~ — **done, by physical inspection**: Nanya
   NT5TU32M16BG-3C is a 512 Mb **x16** part addressed with A0–A12 / A0–A9, i.e. exactly
   the 13 rows / 10 columns `C0DRA0 = 3` implies. Four of them are the 256 MB rank.
5. ~~The flash part number~~ — not needed. The size comes from the 2 MiB dump, and
   `OPMENU` already shows both 4 KiB and 64 KiB erase opcodes are programmed. The T48
   will print the exact part when the chip comes off; nothing in the port depends on it.

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
