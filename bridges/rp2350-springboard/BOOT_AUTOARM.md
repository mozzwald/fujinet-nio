# One-shot boot autoarm ROM candidate

Separate build target `springboard_rom_boot_autoarm`; never replaces the manual
`springboard_rom_persistent` target or its sources. `boot_autoarm_main.c` keeps
the proven service body and GPIO/clock setup, changing only the Core1 entry
pointer, adding read-only boot-policy pointers and `boot_autoarm=1` identity.
The tiny `boot_autoarm_entry.S` shim requests arm once if ROM verification passed,
then tail-branches into the original service. It runs before any USB console is
needed. All setup, idle CS0/OE/reset gating, PIO release, cancellation, FIFO
ownership, kernel accounting and explicit stop/fault behavior remain in that
service. A deliberate stop or fault does not trigger an automatic retry.

Build only (three absolute paths):

```sh
bash scripts/build-boot-autoarm.sh /absolute/ramsetup-p2.rom \
  /absolute/rom-manifest.json /absolute/separate-build-dir
```

Keep Release, waveshare_core2350b/rp2350-arm-s,150MHz, original GPIO/pulls/drive,
preload marker and the verified p2-002 Setup ROM. The CMake boot flag defaults
OFF and requires persistent PIO-rearm. USB debugging remains available but never
blocks service. `autoarm_status.py` in the Palm tools queries q/s only; do not use
manual-arm/stop collectors to inspect a ROM currently in use by PalmOS.

Validation recipes (cached dependencies supported):

```sh
uv run --offline --with unicorn==2.1.4 --with pyelftools python \
  scripts/test-boot-autoarm.py /absolute/autoarm.elf /absolute/ramsetup-p2.rom \
  --output /absolute/startup.json
uv run --offline --with unicorn==2.1.4 --with pyelftools python \
  scripts/test-persistent-reply.py /absolute/autoarm.elf \
  /absolute/host-build/libpersistent_cosim.so --output /absolute/response.json
```

Startup suite executes the linked shim/service, stubbing peripheral init and
response kernel: valid ROM with no console, invalid ROM, CS0/OE asserted,
held reset, release, stop-before-start and sticky owner fault/no retry. Existing
response suite executes the actual linked kernel against the unchanged PIO model
(359 cases). `verify-autoarm-firmware.py` in the Palm tools checks ELF/BIN/UF2,
embedded ROM, exact entry instructions/pointers/main reference, startup proof
binding, unchanged critical engine addresses/bytes and table bank alignment.
Cold pad state is input/disarmed, but startup deliberately requests arm: do not
mislabel this build as remaining disarmed after boot.

No physical hotplug qualification is claimed. With RP2350 independently USB
powered, removing Springboard does not necessarily reboot it; it should remain
in continuous service, without another arm command. Removed-connector control
pins can float because baseline pulls are unchanged. Pin bias, actual deselect/
release through insertion/removal, back-power isolation and simultaneous cold
power-up/recognition timing remain physical gates. The observed Prism soft-reset
hold did not produce the expected responder reset-wait rendezvous; do not use it
as proof that Springboard RESET is asserted. Do not assume a preload marker
proves data-bus Hi-Z. Preserve the manual-arm UF2 and October25 fallback.

Private plan contains exact frozen artifacts, hashes and the careful sled/reseat
procedure. No firmware is flashed by these scripts, and no CS1 work is included.
