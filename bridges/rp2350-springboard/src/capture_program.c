#include "capture_program.h"
#include <apio.h>

void sb_capture_program_init(void) {
    APIO_ENABLE_GPIOS();
    APIO_ENABLE_PIOS();
    /* Do not use APIO_GPIO_INIT: it initializes all pins, including PSRAM CS.
     */
    for (int pin = 0; pin <= 46; ++pin) {
        if (pin == 39)
            continue;
        APIO_GPIO_INPUT_ONLY(pin);
        APIO_GPIO_PULL_NONE(pin);
    }
    APIO_ASM_INIT();
    for (unsigned block = 0; block < 2; ++block) {
        APIO_SET_BLOCK_VAR(block);
        if (block == 0) {
            APIO_GPIOBASE_0();
        } else {
            APIO_GPIOBASE_16();
        }
        for (unsigned sm = 0; sm < 2; ++sm) {
            APIO_SET_SM_VAR(sm);
            const unsigned cs = (block == 0 ? 16 : 0) + sm;
            APIO_WRAP_BOTTOM();
            APIO_ADD_INSTR(APIO_WAIT_GPIO_HIGH(cs));
            APIO_ADD_INSTR(APIO_WAIT_GPIO_LOW(cs));
            APIO_ADD_INSTR(APIO_IN_PINS(32));
            /* Blocking PUSH makes loss detectable through RXSTALL. A stalled
             * observer can miss cycles, so its trace is invalid after RXSTALL.
             */
            APIO_WRAP_TOP();
            APIO_ADD_INSTR(APIO_PUSH_BLOCK);
            APIO_SM_CLKDIV_SET(1, 0);
            APIO_SM_EXECCTRL_SET(0);
            APIO_SM_SHIFTCTRL_SET(APIO_IN_SHIFTDIR_L);
            APIO_SM_PINCTRL_SET(APIO_IN_BASE(0));
            APIO_SM_JMP_TO_START();
        }
        APIO_END_BLOCK();
    }
    APIO_ENABLE_SMS(0, 3);
    APIO_ENABLE_SMS(1, 3);
}
