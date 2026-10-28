# Frozen, hardware-proven CS0 ROM drive baseline

This is the source for the `msc-003` ROM-drive firmware verified on the Visor
Prism: USB CDC + MSC enumeration, uploaded Springboard ROM service, automatic
Setup installation into RAM, and RAM test application operation. The bus engine
comes from the October25 persistent-response milestone. Active CS1 service is
not part of this baseline.

Core source:

- `src/persistent_reply.S`: persistent Core1 SRAM word lookup/FIFO protocol.
- `src/pio_rearm_program.c`: sole data-output owner and independent release guards.
- `src/boot_autoarm_entry.S`: Core1-first startup.
- `src/msc_main.c`: ROM import/loading, boot arming, SRAM execution and USB startup.
- `src/msc_{store,image,descriptors}.*`, `src/rom_store.*`: storage/parser/USB support.
- `msc-rom/tusb_config.h`, `msc-rom/partitions.ld`: composite USB/flash boundaries.

The assembly/PIO/startup sources were already committed before the MSC loader;
this checkpoint commits the missing loader/build/storage/tool/test support. Its
runtime files match the archived msc-002 source manifest, with the two documented
msc-003 source updates: `msc_main.c` and `test-msc-usb-startup.py`.

Frozen original UF2 SHA256:
`3005edefaf45cbf3ee180dd464f0cc3bcf936c7c6e54ae25a6963ee4b720167b`.
The archived artifact remains the exact rollback image; a source rebuild is not
assumed byte-identical without checking compiler, SDK and build paths/options.
No UF2, ROM image or external SDK/manual is added to the source repository.

A later separately built passive-CS1 observer firmware uses the same288-byte
ROM ARM kernel, PIO program and startup policy. That optional observer and active
CS1 experiments are outside this baseline commit. Its installation succeeds,
but it provides no CS1 register read/write service.

## Build and checks

Use pinned dependencies described by the bridge README, a verified recovery ROM
and its manifest from the established Palm workspace:

```sh
scripts/build-rom-msc.sh /absolute/recovery.rom /absolute/manifest.json /absolute/NEW-build
```

Host mode includes the existing bridge tests plus `rom_store` and `msc_store`:

```sh
cmake -S . -B build/baseline-host -DBRIDGE_MODE=host -DCMAKE_BUILD_TYPE=Release
cmake --build build/baseline-host --parallel 4
ctest --test-dir build/baseline-host --output-on-failure
python3 scripts/test-msc-layout.py
python3 scripts/test-rom-msc-tool.py
```

`test-rom-msc.py` exercises actual portable FAT/private-slot code, including
interrupted updates, CRC/size/metadata rejection and rollback. The compiled MSC
callbacks, actual SDK USB startup, boot policy and independent ELF/BIN/UF2
verifiers remain separate scripts. Some require cached Unicorn/pyelftools and
the existing sibling Palm verifier. Synthetic instruction-cost models and
mapped-register hooks do not establish physical sampling deadlines; the recent
CS1 investigations found an additional layout-sensitive Unicorn READ/WRITE-hook
ITSTATE defect. Do not accept simulator instruction skipping as hardware evidence.

All scripts are offline unless an explicit host-tool device/mount command is
chosen. No build or test automatically flashes, resets or reinserts hardware.
Keep the physical baseline artifact and original evidence unchanged.
