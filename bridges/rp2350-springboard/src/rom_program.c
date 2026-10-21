#include "rom_program.h"
#include <apio.h>

#if SB_ROM_DRIVE_MARKER
#define DRIVE_SIDE (1u << 12) /* one mandatory side-set bit, zero delay */
#else
#define DRIVE_SIDE 0u
#endif

void sb_rom_program_init(bool enable) {
    APIO_ENABLE_GPIOS();
    APIO_ENABLE_PIOS();
    for (int pin = 0; pin <= 46; ++pin) {
        if (pin == 39) continue;
        APIO_GPIO_INPUT_ONLY(pin);
        APIO_GPIO_PULL_NONE(pin);
    }
    APIO_ASM_INIT();
    APIO_SET_BLOCK_VAR(1);
    APIO_GPIOBASE_16();
    APIO_SET_SM_VAR(0);
    /* Fixed addresses are local to this otherwise unused PIO block. MOV
     * PINDIRS is an RP2350 instruction, not the RP2040 reserved encoding.
     * Only GP20..35 are output-capable. The CPU never sets their direction.
     * One request, one reply, then park on IRQ until CPU drains both FIFOs.
     * Even a missing reply cannot block the control/release polling loop.
     */
    enum { SELECT = 2, SAMPLE = 3, PENDING = 8, READY = 13,
           REPLY = 16, ENABLE = 23, DRIVE = 25, RELEASE = 29 };
    const unsigned short program[] = {
        APIO_MOV_PINDIRS_NULL,                      /* 0 */
        APIO_SET_Y(10),                             /* 1 */
        APIO_WAIT_GPIO_HIGH(0),                     /* SELECT */
        APIO_MOV_OSR_PINS,                          /* SAMPLE */
        APIO_OUT_X(4),
        APIO_JMP_X_NOT_Y(SAMPLE),
        APIO_IN_PINS(32),
        APIO_PUSH_BLOCK,
        APIO_MOV_OSR_PINS,                          /* PENDING */
        APIO_OUT_X(4),
        APIO_JMP_X_NOT_Y(RELEASE),
        APIO_JMP_PIN(READY),                        /* /RESET released */
        APIO_JMP(RELEASE),
        APIO_MOV_X_STATUS,                          /* READY: TX depth < 1 */
        APIO_JMP_NOT_X(REPLY),
        APIO_JMP(PENDING),
        APIO_PULL_BLOCK,                            /* REPLY: cannot block */
        APIO_OUT_PINS(16),                          /* preload while Hi-Z */
        APIO_MOV_OSR_PINS,                          /* requalify after pull */
        APIO_OUT_X(4),
        APIO_JMP_X_NOT_Y(RELEASE),
        APIO_JMP_PIN(ENABLE),
        APIO_JMP(RELEASE),
        APIO_MOV_PINDIRS_NOT_NULL | DRIVE_SIDE,     /* ENABLE */
        APIO_IRQ_SET(1) | DRIVE_SIDE,               /* outputs were enabled */
        APIO_MOV_OSR_PINS | DRIVE_SIDE,             /* DRIVE */
        APIO_OUT_X(4) | DRIVE_SIDE,
        APIO_JMP_X_NOT_Y(RELEASE) | DRIVE_SIDE,
        APIO_JMP_PIN(DRIVE) | DRIVE_SIDE,
        APIO_MOV_PINDIRS_NULL,                      /* RELEASE */
        APIO_IRQ_SET_WAIT(0),                       /* CPU ack only at Hi-Z */
        APIO_JMP(SELECT),
    };
    _Static_assert(sizeof(program) / sizeof(program[0]) == 32,
                   "Update fixed program labels when changing instructions");
    APIO_WRAP_BOTTOM();
    for (unsigned i = 0; i + 1 < sizeof(program) / sizeof(program[0]); ++i)
        APIO_ADD_INSTR(program[i]);
    /* APIO marks the address BEFORE the wrap instruction. Marking after all
     * 32 instructions would encode wrap-top32 as zero and loop on startup. */
    APIO_WRAP_TOP();
    APIO_ADD_INSTR(program[sizeof(program) / sizeof(program[0]) - 1]);
    APIO_SM_CLKDIV_SET(1, 0);
    APIO_SM_EXECCTRL_SET(APIO_EXECCTRL_JMP_PIN(29) |
                        APIO_STATUS_SEL_TXLEVEL | APIO_STATUS_N(1));
    /* Include GP16..45: controls, data, all high address bits and RESET.
     * IN_COUNT masks IN as well as MOV on silicon. Use explicit 30 rather
     * than encoded 32 (zero), which this pinned emulator misreads for MOV. */
    APIO_SM_SHIFTCTRL_SET(APIO_IN_COUNT(30) | APIO_IN_SHIFTDIR_L | APIO_OUT_SHIFTDIR_R);
#if SB_ROM_DRIVE_MARKER
    APIO_SM_PINCTRL_SET(APIO_IN_BASE(0) | APIO_OUT_BASE(4) | APIO_OUT_COUNT(16) |
                        APIO_SET_BASE(23) | APIO_SET_COUNT(1) |
                        APIO_SIDE_SET_BASE(23) | APIO_SIDE_SET_COUNT(1));
    /* Set the marker latch low before enabling its direction/mux. The SET
     * mapping is GP39 only; MOV PINDIRS still affects only GP20..35. */
    APIO_SM_EXEC_INSTR(APIO_SET_PINS(0));
    APIO_SM_EXEC_INSTR(APIO_SET_PIN_DIRS(1));
#else
    APIO_SM_PINCTRL_SET(APIO_IN_BASE(0) | APIO_OUT_BASE(4) | APIO_OUT_COUNT(16));
#endif
    APIO_SM_JMP_TO_START();
    APIO_END_BLOCK();
    /* Caller must begin with the SM disabled and its PINDIRS zero (hardware
     * reset on first use; parked RELEASE for rearm). No bus pin outside data
     * is ever assigned to a PIO output. */
    for (int pin = 20; pin <= 35; ++pin)
        APIO_GPIO_INPUT_OUTPUT(pin, 1);
#if SB_ROM_DRIVE_MARKER
    APIO_GPIO_INPUT_OUTPUT(SB_ROM_DRIVE_MARKER_PIN, 1);
#endif
    if (enable) {
        APIO_ENABLE_SMS(1, 1);
    }
}
