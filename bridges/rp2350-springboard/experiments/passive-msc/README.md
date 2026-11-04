# msc-003 plus passive CS1 observer — isolated firmware candidate

SPRINGBOARD_PASSIVE_MSC / springboard_passive_msc. No automatic hardware access.
Original msc_main.c, pio_rearm_program.c, persistent_reply.S, boot shim and loader
sources remain unchanged. Linker --wrap redirects exactly the initialization
call through wrapper.c; the prior14-word observer is included unchanged.

Wrapper touches only PIO2 outside the original initializer: stop/clear its local
FIFO/IRQ, mask both IRQ outputs, configure the old input-only observer, enable
SM0. Original ROM park/idle/start and all steady read/completion work stay intact.
Observer may continue input-only while ROM is parked. No Core0 consumer, ISR,
DMA, periodic polling or register response exists. Local FIFO may fill/stall;
its pending snapshots are untrusted and have no ROM/software effects.

test_linked_isolation.py compares both actual linked ELFs, not the same ELF
against two fixtures; requires exact kernel address/bytes/literals and preserves
all334 cases/assertions. test_initializer.py checks actual baseline GPIO/PIO0/1
writes plus actual14-word PIO2/configuration/IRQ masks. test_pressure.py proves
noninterference with a full local FIFO and no consumer. audit_differences.py
records actual code/literal/buffer relocations; test_startup.py executes real
helpers and original startup/stop/fault expectations.

Current gates pass. Physical Setup installation remains unverified. No CS1
register or NIO tests are part of acceptance. No changes to current stored ROM.
Private preflight/report: docs/visor/MSC003_PASSIVE_OBSERVER_PREFLIGHT.md, p4m-014.
