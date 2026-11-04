# Bank A + proven MSC framework — read-only hardware POC

Separate CMake target SPRINGBOARD_BANK_MSC / springboard_bank_msc, on visor.
No production baseline sources are changed. No automatic flashing.

main.c derives from msc_main.c: same MSC backend/import/activation, flash ranges,
Core1-before-USB boot shim, recovery ROM, USB descriptors, RAM exception repair.
Adds contiguous CS1/ROM lookup banks; initializes immutable CS1 ID/version and
unsupported-write status once before Core1 launch. No Core0 CS1 worker exists.

program.c retains all Experiment A instruction words/configuration, but enables
only PIO0 SM0/1/3 cancellation/conflict observers, PIO2 SM0 reset guard and PIO1
SM0..3 owner/release guards. Write producers remain disabled. Park drains all
three blocks. workers.S instruction bytes match standalone Experiment A.

run_linked_matrix.py preserves the unchanged268-case matrix, using actual linked
bank addresses and the prior FULL-write functional fixture. It tests the kernel,
not write support in this read-only firmware. test_readonly.py separately checks
the real masks/table, ignored writes and no capture FIFO activity (90 cases).
Startup tests execute the actual init_registers, actual ROM/PIO helpers with
--real-pio, check correct logical bank argument, enable masks and stop/fault
behavior. test_initializer.py also compares all91 instructions and51 config fields.

Ready only for one explicitly authorized installation proof. Slower than msc-003;
no physical Prism sampling margin or compatibility is established. Do not run
CS1 Module B Run four: this candidate advertises no write capability.

Artifacts, hashes, scope, rollback and minimal operator steps are in the private
plan docs/visor/EXPERIMENT_A_READONLY_PREFLIGHT.md (p4m-013).
