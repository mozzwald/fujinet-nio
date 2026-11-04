/* Host/initializer audit only. No firmware target links this observer.
 * It records an untrusted CS1 edge; it neither replies nor commits a write.
 * The baseline PIO0/PIO1 programs, mappings and output owner are untouched. */
#include "pio_rearm_program.h"
#include <apio.h>
void sb_rom_plus_cs1_probe_init(bool enable) {
    sb_pio_rearm_program_init(false);
    APIO_ASM_CONTINUE();
    APIO_SET_BLOCK_VAR(2); APIO_GPIOBASE_16(); APIO_SET_SM_VAR(0);
    APIO_START(); APIO_WRAP_BOTTOM();
    APIO_ADD_INSTR(APIO_WAIT_GPIO_LOW(1)); /* GPIO17; no work while CS1 high */
    APIO_ADD_INSTR(APIO_IN_PINS(32));
    APIO_ADD_INSTR(APIO_MOV_OSR_ISR);
    APIO_ADD_INSTR(APIO_OUT_X(4));
    APIO_ADD_INSTR(APIO_SET_Y(9));          /* CS1 read-shaped controls */
    APIO_ADD_INSTR(APIO_JMP_X_NOT_Y(7));
    APIO_ADD_INSTR(APIO_JMP(9));
    APIO_ADD_INSTR(APIO_SET_Y(5));          /* CS1 write-shaped controls */
    APIO_ADD_INSTR(APIO_JMP_X_NOT_Y(0));
    APIO_ADD_INSTR(APIO_JMP_PIN(11));       /* RESET released */
    APIO_ADD_INSTR(APIO_JMP(13));
    APIO_ADD_INSTR(APIO_PUSH_BLOCK);
    APIO_ADD_INSTR(APIO_IRQ_SET(0));        /* local only, never PIO1 */
    APIO_WRAP_TOP(); APIO_ADD_INSTR(APIO_WAIT_GPIO_HIGH(1));
    APIO_SM_CLKDIV_SET(1,0); APIO_SM_EXECCTRL_SET(APIO_EXECCTRL_JMP_PIN(29));
    APIO_SM_SHIFTCTRL_SET(APIO_IN_COUNT(30)|APIO_IN_SHIFTDIR_L|APIO_OUT_SHIFTDIR_R);
    APIO_SM_PINCTRL_SET(APIO_IN_BASE(0));
    APIO_SM_JMP_TO_START(); APIO_END_BLOCK();
    if(enable) {
        APIO_ENABLE_SMS(0,3); APIO_ENABLE_SMS(1,15); APIO_ENABLE_SMS(2,1);
    }
}
