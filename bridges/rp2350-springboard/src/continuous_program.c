#include "continuous_program.h"
#include <apio.h>

void sb_continuous_program_init(bool enable) {
    APIO_ENABLE_GPIOS();
    APIO_ENABLE_PIOS();
    for(int pin=0;pin<=46;++pin) {
        if(pin==39) continue;
        APIO_GPIO_INPUT_ONLY(pin); APIO_GPIO_PULL_NONE(pin);
    }
    APIO_ASM_INIT();
    APIO_SET_BLOCK_VAR(1); APIO_GPIOBASE_16(); APIO_SET_SM_VAR(0);
    unsigned short program[]={
        APIO_MOV_OSR_PINS,                 /*0: complete read qualification */
        APIO_OUT_X(4),
        APIO_JMP_X_NOT_Y(0),
        APIO_IN_PINS(32),
        APIO_PUSH_BLOCK,
        APIO_MOV_X_STATUS,                 /*5: wait TX while guards stay live */
        APIO_JMP_NOT_X(8),
        APIO_JMP(5),
        APIO_PULL_BLOCK,                   /*8: depth proved nonzero */
        APIO_OUT_PINS(16),                 /*9: preload, still Hi-Z */
        APIO_MOV_OSR_PINS,
        APIO_OUT_X(4),
        APIO_JMP_X_NOT_Y(21),
        APIO_JMP_PIN(15),                  /* RESET released */
        APIO_JMP(21),
        APIO_MOV_PINDIRS_NOT_NULL,         /*15: main drive command */
        APIO_IRQ_SET(1),
        APIO_MOV_OSR_PINS,                 /*17: ordinary full-control release */
        APIO_OUT_X(4),
        APIO_JMP_X_NOT_Y(21),
        APIO_JMP_PIN(17),
        APIO_MOV_PINDIRS_NULL,             /*21: release before IRQ park */
        APIO_IRQ_SET_WAIT(0),
        APIO_WAIT_GPIO_LOW(0),             /*23: qualify watchers with CS0 */
        APIO_WAIT_PIN_LOW(0),              /*24: own CS0/OE active */
        APIO_WAIT_PIN_HIGH(0),             /*25: one-clock ending is sticky */
        APIO_IRQ_SET(0),                   /*26: wake RX-first owner */
        APIO_MOV_PINDIRS_NULL|0x1000,      /*27: optional side-set clears marker */
        APIO_WAIT_PIN_LOW(0),              /*28: independent reset watcher */
        APIO_JMP(26),
    };
    _Static_assert(sizeof(program)/sizeof(program[0])==SB_CONT_INSTRUCTIONS,"PIO layout");
    for(unsigned i=15;i<=20;++i) program[i]|=0x1000;
    APIO_WRAP_BOTTOM();
    for(unsigned i=0;i<=22;++i) {
        if(i==22) APIO_WRAP_TOP();
        APIO_ADD_INSTR(program[i]);
    }
    APIO_SM_CLKDIV_SET(1,0);
    APIO_SM_EXECCTRL_SET(APIO_EXECCTRL_JMP_PIN(29)|APIO_STATUS_SEL_TXLEVEL|APIO_STATUS_N(1));
    APIO_SM_SHIFTCTRL_SET(APIO_IN_COUNT(30)|APIO_IN_SHIFTDIR_L|APIO_OUT_SHIFTDIR_R);
    APIO_SM_PINCTRL_SET(APIO_IN_BASE(0)|APIO_OUT_BASE(4)|APIO_OUT_COUNT(16)|
        APIO_SET_BASE(23)|APIO_SET_COUNT(1)|APIO_SIDE_SET_BASE(23)|APIO_SIDE_SET_COUNT(1));
    APIO_SM_EXEC_INSTR(APIO_MOV_PINDIRS_NULL);
    APIO_SM_EXEC_INSTR(APIO_SET_Y(10));
    APIO_SM_EXEC_INSTR(APIO_SET_PINS(0));
    APIO_SM_EXEC_INSTR(APIO_SET_PIN_DIRS(1));
    APIO_SM_JMP_TO_START();
    for(unsigned sm=1;sm<4;++sm) {
        APIO_SET_BLOCK_FROM_VAR(1,sm==3?28:23); APIO_SET_SM_VAR(sm);
        if(sm==3) {
            APIO_SET_BLOCK_FROM_VAR(1,27); APIO_WRAP_BOTTOM(); APIO_WRAP_TOP();
            APIO_SET_BLOCK_FROM_VAR(1,28); APIO_ADD_INSTR(program[28]);
            APIO_END(); APIO_ADD_INSTR(program[29]);
        } else {
            for(unsigned i=23;i<=27;++i) {
                if(i==27) {APIO_WRAP_BOTTOM();APIO_WRAP_TOP();}
                APIO_ADD_INSTR(program[i]);
            }
        }
        APIO_SM_CLKDIV_SET(1,0);
        /* Optional side-set: blocked WAITs do not overwrite main's marker.
         * The clamp instruction writes both data Hi-Z and marker-low every
         * clock. Higher numbered guard writes dominate simultaneous enables. */
        APIO_SM_EXECCTRL_SET(1u<<30);
        APIO_SM_SHIFTCTRL_SET(APIO_IN_COUNT(30)|APIO_IN_SHIFTDIR_L|APIO_OUT_SHIFTDIR_R);
        APIO_SM_PINCTRL_SET(APIO_IN_BASE(sm==1?0:sm==2?2:29)|
            APIO_OUT_BASE(4)|APIO_OUT_COUNT(16)|APIO_SIDE_SET_BASE(23)|APIO_SIDE_SET_COUNT(2));
        APIO_SM_JMP_TO_START();
    }
    APIO_END_BLOCK();
    for(int pin=20;pin<=35;++pin) APIO_GPIO_INPUT_OUTPUT(pin,1);
    APIO_GPIO_INPUT_OUTPUT(39,1);
    if(enable) {APIO_ENABLE_SMS(1,15);}
}
