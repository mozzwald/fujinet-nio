#include "cs1_program.h"
#include <apio.h>

_Static_assert(SB_CS1_LOW_START + SB_CS1_WRITE_WORDS <= 32, "PIO0 allocation");
_Static_assert(SB_CS1_HIGH_START + SB_CS1_HIGH_WORDS == SB_CS1_END_START, "PIO2 capture allocation");
_Static_assert(SB_CS1_END_START + 7 == SB_CS1_CANCEL_START, "PIO2 edge allocation");
_Static_assert(SB_CS1_CANCEL_START + 3 <= 32, "PIO2 instruction limit");

/* Isolated first layout. No firmware target uses it. Native tests must prove
 * ownership, cancellation and matching captures before any integration.
 */
void sb_cs1_program_init(bool enable) {
    APIO_ENABLE_GPIOS(); APIO_ENABLE_PIOS();
    for (int pin = 0; pin <= 46; ++pin) {
        if (pin == 39) continue;
        APIO_GPIO_INPUT_ONLY(pin); APIO_GPIO_PULL_NONE(pin);
    }
    APIO_ASM_INIT(); APIO_SET_BLOCK_VAR(1); APIO_GPIOBASE_16();
    const unsigned short read_program[] = {
        APIO_MOV_OSR_PINS,                    /* 0 */
        APIO_OUT_X(4),
        APIO_SET_Y(9),                        /* CS1 qualified read */
        APIO_JMP_X_NOT_Y(5),
        APIO_JMP(7),
        APIO_SET_Y(10),                       /* CS0 qualified read */
        APIO_JMP_X_NOT_Y(0),
        APIO_IRQ_CLEAR(0) | (5u << 8),
        APIO_IN_PINS(32),                     /* 8: autopush request */
        APIO_PULL_BLOCK,
        APIO_OUT_PINS(16),                    /* 10: preload while Hi-Z */
        APIO_MOV_OSR_PINS,
        APIO_OUT_X(4),
        APIO_JMP_X_NOT_Y(19),                  /* same selected owner */
        APIO_JMP_PIN(16),                     /* RESET released */
        APIO_JMP(19),
        APIO_MOV_PINDIRS_STATUS | 8u,         /* 16: ~cancel */
        APIO_MOV_X_STATUS | 8u,
        APIO_JMP(21),
        APIO_SET_X(0),
        APIO_IRQ_SET(0),
        APIO_WAIT_IRQ_HIGH(0),
        APIO_MOV_PINDIRS_NULL,
        APIO_IN_X(32),                        /* 23: autopush completion */
        APIO_JMP(0),
        APIO_WAIT_PIN_LOW(0),                 /* 25: direct ending guards */
        APIO_WAIT_PIN_HIGH(0),
        APIO_IRQ_SET(0),
        APIO_MOV_PINDIRS_NULL,
    };
    _Static_assert(sizeof(read_program) / sizeof(read_program[0]) == SB_CS1_READ_WORDS,
                   "CS1 read layout");
    APIO_SET_SM_VAR(0); APIO_WRAP_BOTTOM();
    for (unsigned i = 0; i < SB_CS1_READ_WORDS; ++i) {
        if (i == 24) APIO_WRAP_TOP();
        APIO_ADD_INSTR(read_program[i]);
    }
    APIO_SM_CLKDIV_SET(1, 0);
    APIO_SM_EXECCTRL_SET((1u << 30) | APIO_EXECCTRL_JMP_PIN(29) |
                        APIO_STATUS_SEL_IRQ | APIO_STATUS_N(0));
    APIO_SM_SHIFTCTRL_SET(APIO_AUTOPUSH | APIO_IN_COUNT(30) |
                         APIO_IN_SHIFTDIR_L | APIO_OUT_SHIFTDIR_R);
    APIO_SM_PINCTRL_SET(APIO_IN_BASE(0) | APIO_OUT_BASE(4) | APIO_OUT_COUNT(16));
    APIO_SM_EXEC_INSTR(APIO_MOV_PINDIRS_NULL); APIO_SM_JMP_TO_START();
    for (unsigned sm = 1; sm < 4; ++sm) {
        APIO_SET_BLOCK_FROM_VAR(1, 25); APIO_SET_SM_VAR(sm); APIO_START(); APIO_WRAP_BOTTOM();
        APIO_SET_BLOCK_FROM_VAR(1, 28); APIO_WRAP_TOP();
        APIO_SM_CLKDIV_SET(1, 0); APIO_SM_EXECCTRL_SET(1u << 30);
        APIO_SM_SHIFTCTRL_SET(0);
        APIO_SM_PINCTRL_SET(APIO_IN_BASE(sm - 1) | APIO_OUT_BASE(4) | APIO_OUT_COUNT(16));
        APIO_SM_JMP_TO_START();
    }
    APIO_SET_BLOCK_FROM_VAR(1, SB_CS1_READ_WORDS); APIO_END_BLOCK();

    /* WE cancels reads immediately, then announces a CS1 write before its
     * snapshots are published. Y=1 is the qualified OE/WE write code. */
    APIO_SET_BLOCK_VAR(0); APIO_GPIOBASE_0(); APIO_SET_SM_VAR(0);
    APIO_WRAP_BOTTOM();
    APIO_ADD_INSTR(APIO_WAIT_GPIO_LOW(19));
    APIO_ADD_INSTR(APIO_IRQ_SET_NEXT(0));
    APIO_ADD_INSTR(APIO_JMP_PIN(4)); /* CS1 inactive: no early write event */
    APIO_ADD_INSTR(APIO_IRQ_SET_NEXT(4));
    APIO_ADD_INSTR(APIO_MOV_OSR_PINS);
    APIO_ADD_INSTR(APIO_OUT_NULL(18));
    APIO_ADD_INSTR(APIO_OUT_X(2));
    APIO_ADD_INSTR(APIO_JMP_NOT_X(10));
    APIO_ADD_INSTR(APIO_JMP_X_NOT_Y(0));
    APIO_ADD_INSTR(APIO_JMP(4));
    APIO_ADD_INSTR(APIO_IRQ_SET_PREV(5));
    APIO_ADD_INSTR(APIO_IRQ_SET_NEXT(3));
    APIO_WRAP_TOP(); APIO_ADD_INSTR(APIO_WAIT_GPIO_HIGH(19));
    APIO_SM_CLKDIV_SET(1, 0); APIO_SM_EXECCTRL_SET(APIO_EXECCTRL_JMP_PIN(17));
    APIO_SM_SHIFTCTRL_SET(APIO_IN_COUNT(30) | APIO_OUT_SHIFTDIR_R);
    APIO_SM_PINCTRL_SET(0); APIO_SM_EXEC_INSTR(APIO_SET_Y(1)); APIO_SM_JMP_TO_START();
    APIO_SET_SM_VAR(1); APIO_START(); APIO_WRAP_BOTTOM();
    APIO_ADD_INSTR(APIO_WAIT_PIN_LOW(0)); /* 13 */
    APIO_ADD_INSTR(APIO_JMP_PIN(18));
    APIO_ADD_INSTR(APIO_IRQ_SET_NEXT(0));
    APIO_ADD_INSTR(APIO_IRQ_SET_PREV(5));
    APIO_ADD_INSTR(APIO_IRQ_SET_NEXT(3));
    APIO_WRAP_TOP(); APIO_ADD_INSTR(APIO_WAIT_PIN_HIGH(0));
    APIO_SM_CLKDIV_SET(1, 0);
    APIO_SM_EXECCTRL_SET(APIO_EXECCTRL_JMP_PIN(17));
    APIO_SM_SHIFTCTRL_SET(0); APIO_SM_PINCTRL_SET(APIO_IN_BASE(16));
    APIO_SM_JMP_TO_START();
    APIO_SET_BLOCK_FROM_VAR(0, 13); APIO_SET_SM_VAR(3); APIO_WRAP_BOTTOM();
    APIO_SET_BLOCK_FROM_VAR(0, 18); APIO_WRAP_TOP();
    APIO_SM_CLKDIV_SET(1, 0);
    APIO_SM_EXECCTRL_SET(APIO_EXECCTRL_JMP_PIN(16));
    APIO_SM_SHIFTCTRL_SET(0); APIO_SM_PINCTRL_SET(APIO_IN_BASE(17));
    APIO_SM_JMP_TO_START();

    /* Independent high-window reset guard. */
    APIO_SET_BLOCK_VAR(2); APIO_GPIOBASE_16(); APIO_SET_SM_VAR(0);
    APIO_WRAP_BOTTOM();
    APIO_ADD_INSTR(APIO_WAIT_GPIO_LOW(29));
    APIO_ADD_INSTR(APIO_IRQ_SET_PREV(0)); /* preserve read-release latency */
    APIO_ADD_INSTR(APIO_IRQ_SET(3)); /* cancel current write */
    APIO_ADD_INSTR(APIO_IRQ_SET(5)); /* sticky reset: never cleared per write */
    APIO_ADD_INSTR(APIO_IRQ_SET_PREV(3)); /* fault work is sticky across owners */
    APIO_WRAP_TOP(); APIO_ADD_INSTR(APIO_WAIT_GPIO_HIGH(29));
    APIO_SM_CLKDIV_SET(1, 0); APIO_SM_EXECCTRL_SET(0);
    APIO_SM_SHIFTCTRL_SET(0); APIO_SM_PINCTRL_SET(0); APIO_SM_JMP_TO_START();

    /* Active-write snapshots remain in ISR across the handshakes. Nominal
     * low-window padding mirrors the two high-window IRQ WAIT handshakes;
     * unequal stalls invalidate the session through sticky RXSTALL checks.
     * Both consumers observe completion non-destructively. The next start is
     * not sampled until the authoritative epoch/outcome is in its FIFO.
     */
    for (unsigned block = 0; block <= 2; block += 2) {
        unsigned start = block ? SB_CS1_HIGH_START : SB_CS1_LOW_START;
        APIO_SET_BLOCK_FROM_VAR(block, start);
        APIO_SET_SM_VAR(block ? SB_CS1_HIGH_SM : SB_CS1_LOW_SM);
        APIO_START(); APIO_WRAP_BOTTOM();
        APIO_ADD_INSTR(APIO_IN_PINS(32));
        APIO_ADD_INSTR(APIO_MOV_OSR_ISR);
        APIO_ADD_INSTR(block ? APIO_NOP : APIO_OUT_NULL(16));
        APIO_ADD_INSTR(APIO_OUT_X(4));
        APIO_ADD_INSTR(APIO_JMP_X_NOT_Y(start));
        APIO_ADD_INSTR(block ? APIO_IRQ_CLEAR(6) : (APIO_NOP | (4u<<8)));
        APIO_ADD_INSTR(block ? APIO_IRQ_CLEAR(3) : APIO_NOP);
        APIO_ADD_INSTR(block ? APIO_IRQ_SET_WAIT(4) : APIO_NOP);
        APIO_ADD_INSTR(block ? APIO_IRQ_SET_WAIT(2) : APIO_NOP);
        APIO_ADD_INSTR(APIO_PUSH_BLOCK);
        if (block) APIO_ADD_INSTR(APIO_IRQ_SET_PREV(1)); /* data precedes work notification */
        APIO_ADD_INSTR(APIO_MOV_X_STATUS);    /* completed edge record, not live levels */
        APIO_ADD_INSTR(APIO_JMP_NOT_X(start + (block ? 11 : 10)));
        APIO_WRAP_TOP(); APIO_ADD_INSTR(APIO_JMP(start));
        APIO_SM_CLKDIV_SET(1, 0);
        APIO_SM_EXECCTRL_SET(APIO_STATUS_SEL_IRQ | APIO_STATUS_N(6) |
                            (block ? 0 : APIO_STATUS_N_IRQ_PREVPIO));
        APIO_SM_SHIFTCTRL_SET(APIO_IN_SHIFTDIR_L | APIO_OUT_SHIFTDIR_R);
        APIO_SM_PINCTRL_SET(0); APIO_SM_EXEC_INSTR(APIO_SET_Y(5));
        APIO_SM_JMP_TO_START();
        APIO_SET_BLOCK_FROM_VAR(block, start + (block ? SB_CS1_HIGH_WORDS : SB_CS1_WRITE_WORDS)); APIO_END_BLOCK();
    }
    /* Per-write edge history. WAIT-high consumes a distinct start flag per
     * watcher; there is no competitive acknowledgement. IRQ updates become
     * visible at cycle boundaries. At equal sampled WE/CS1 endings, the WE
     * MOV STATUS reads the prior cancellation flag, before the CS IRQ sets it.
     * CS1 earlier by one observed clock is already latched and is rejected.
     */
    APIO_SET_BLOCK_FROM_VAR(2, SB_CS1_END_START); APIO_SET_SM_VAR(SB_CS1_END_SM);
    APIO_WRAP_BOTTOM();
    APIO_ADD_INSTR(APIO_WAIT_IRQ_HIGH(2));
    APIO_ADD_INSTR(APIO_IN_X(32)); /* autopush epoch */
    APIO_ADD_INSTR(APIO_WAIT_GPIO_HIGH(3));
    APIO_ADD_INSTR(APIO_MOV_ISR_STATUS);
    APIO_ADD_INSTR(APIO_PUSH_BLOCK);
    APIO_ADD_INSTR(APIO_IRQ_SET(6)); /* both windows may finalize only now */
    APIO_WRAP_TOP(); APIO_ADD_INSTR(APIO_JMP_X_DEC(SB_CS1_END_START));
    APIO_SM_CLKDIV_SET(1,0);
    APIO_SM_EXECCTRL_SET(APIO_STATUS_SEL_IRQ | APIO_STATUS_N(3));
    APIO_SM_SHIFTCTRL_SET(APIO_AUTOPUSH); APIO_SM_PINCTRL_SET(0);
    APIO_SM_EXEC_INSTR(APIO_SET_X(0)); APIO_SM_JMP_TO_START();
    APIO_SET_SM_VAR(SB_CS1_CANCEL_SM); APIO_WRAP_BOTTOM();
    APIO_ADD_INSTR(APIO_WAIT_IRQ_HIGH(4));
    APIO_ADD_INSTR(APIO_WAIT_GPIO_HIGH(1));
    APIO_WRAP_TOP(); APIO_ADD_INSTR(APIO_IRQ_SET(3));
    APIO_SM_CLKDIV_SET(1,0); APIO_SM_EXECCTRL_SET(0);
    APIO_SM_SHIFTCTRL_SET(0); APIO_SM_PINCTRL_SET(0); APIO_SM_JMP_TO_START();
    APIO_END_BLOCK();
    for (int pin = 20; pin <= 35; ++pin) APIO_GPIO_INPUT_OUTPUT(pin, 1);
    if (enable) {
        APIO_ENABLE_SMS(0, 15); APIO_ENABLE_SMS(1, 15); APIO_ENABLE_SMS(2, 15);
    }
}
