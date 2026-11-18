# ESP32-S3 Springboard USB CDC host

The opt-in `fujibus-link-usbhost-s3-n16r8` profile runs NIO on an ESP32-S3
hosting the RP2350 Springboard bridge. It selects `UsbCdcHost`, `FujiBusNative`
and the existing [packet link](packet_link.md). The wire protocols are unchanged.

## Hardware and setup

- ESP32-S3-CAM development board, N16R8: 16 MiB flash, 8 MiB octal PSRAM.
- Connect the computer to the board's USB-UART port for programming, UART0 logs
  and diagnostics (`/dev/ttyUSB0`, 115200 baud; upload at 460800 baud).
- Connect the native USB-C port to the RP2350 Springboard. In the prototype,
  the computer powers the S3, which supplies the Springboard through USB.
- Storage uses internal flash. This profile assigns no SD, camera, LED or button
  pins. Preserve the Springboard's Palm-side 360 ns minimum access-time policy.

The profile uses QIO flash mode to match the N16R8 board. DIO caused a PSRAM
startup fault on the prototype's revision 0.2 chip. PlatformIO Espressif32 6.10.0
resolves to ESP-IDF 5.4.0; the manifest pins the compatible CDC host component to
`espressif/usb_host_cdc_acm` 2.1.0. Its generated dependency lock is ignored by
the repository.

Select the profile using the normal generator:

```sh
./build.sh -s fujibus-link-usbhost-s3-n16r8 -y
```

Setup defaults the local console selection to `noconsole`. Set the following in
`platformio.local.ini` before building (keep its `build_board` entry):

```ini
[fujinet]
console_type = consoleuart

[env]
build_flags += -DFN_DEBUG
upload_port = /dev/ttyUSB0
monitor_port = /dev/ttyUSB0
```

```sh
./build.sh -b
./build.sh -u
./build.sh -m
```

The SDK defaults reserve native USB for the host: TinyUSB CDC and USB Serial/JTAG
consoles are disabled. UART0 provides early startup errors, application logs and
the diagnostic console. Logs cannot enter the NIO data stream.

## Transport and recovery

A platform worker owns CDC open, DTR, bounded transmission and closing. Separate
host-library and CDC-driver tasks service USB events. An absent bridge does not
block the cooperative NIO core.

Selection checks VID/PID `2e8a:0009` and the NIO IAD, ACM communication interface
0, union descriptor and data interface 1. Bulk endpoints are discovered from
descriptors. Console interfaces 2/3 and the Raspberry Pi reset interface are left
alone. The host asserts DTR after line coding; the nominal baud is not USB speed.

For packet capacity C, a complete record occupies C+10 bytes. RX buffers two
records; TX queues one record in addition to any record owned by the worker.
USB callback boundaries do not define records. RX remains readable until drained.
TX has a 250 ms bound; CDC control operations use the component's 5 s bound in
the worker. Global packet-link timeout defaults are unchanged.

Disconnect, overflow, queue rejection or failed TX discards the session and its
queued bytes. Epoch tokens reject stale callbacks. Driver calls occur outside
the state mutex, and only the worker uses or closes handles. Reopening toggles
DTR and requires fresh packet-link synchronization. Shared lifecycle handling
uses `Channel::connection_epoch()` and `write_for_epoch()`; it contains no ESP32
API calls. See [physical stream changes](packet_link.md#physical-stream-changes).

## Diagnostics and current support

`usb.status` shows connection state, selected function/endpoints, epoch, RX
backlog, connection/reconnect/error/overflow/TX counters, byte counts and the last
error. It is read-only and adds no configurable values. `connected` means CDC
open with DTR asserted; use `link.status` to inspect packet synchronization.

The separate-board baseline supports Palm Clock Test, Wi-Fi, TNFS browsing and
PRC installation. S3-only reset and resynchronization have been exercised.
POSIX tests and representative S3 USB device/UART builds pass. Physical
unplug/replug, RP2350-only reset and sustained mixed traffic remain unqualified;
an isolated TX failure/reconnect was observed without a trace of its trigger.
Only one directly attached bridge is supported. Hubs, multiple bridges and
integrated-module power qualification are outside this baseline.
