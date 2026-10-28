#include "pio_rearm_program.h"
#include <apio.h>

void sb_pio_rearm_program_init(bool enable) {
    APIO_ENABLE_GPIOS();APIO_ENABLE_PIOS();
    for(int pin=0;pin<=46;++pin) {
        if(pin==39)continue;
        APIO_GPIO_INPUT_ONLY(pin);APIO_GPIO_PULL_NONE(pin);
    }
    APIO_ASM_INIT();APIO_SET_BLOCK_VAR(1);APIO_GPIOBASE_16();APIO_SET_SM_VAR(0);
    unsigned short program[]={
        APIO_MOV_OSR_PINS,                    /*0: qualify before snapshot */
        APIO_OUT_X(4),
        APIO_JMP_X_NOT_Y(0),
        APIO_IRQ_CLEAR(0)|(5u<<8),            /*3: settle old guard acknowledgments */
        APIO_IN_PINS(32),                     /*4: ownership starts here */
        APIO_PUSH_BLOCK,                      /*5: request */
        APIO_PULL_BLOCK,                      /*6: late reply remains this owner's */
        APIO_OUT_PINS(16)|(SB_PIO_REPLY_MARKER?0x1800u:0), /*7: preload, Hi-Z; optional reply marker */
        APIO_MOV_OSR_PINS,
        APIO_OUT_X(4),
        APIO_JMP_X_NOT_Y(16),
        APIO_JMP_PIN(13),                     /* RESET released */
        APIO_JMP(16),
        (APIO_MOV_PINDIRS_STATUS|8u)|(SB_PIO_REPLY_MARKER?0:0x1800u), /*13: ~cancel; normal enable marker */
        APIO_MOV_X_STATUS|8u,                 /*14: ~cancel: all-ones if unmasked */
        APIO_JMP(18),
        APIO_SET_X(0),                        /*16: invalid live qualification */
        APIO_IRQ_SET(0),
        APIO_WAIT_IRQ_HIGH(0),                /*18: consumes ending; CPU independent */
        APIO_MOV_PINDIRS_NULL|0x1000u,         /*19: optional marker low */
        APIO_MOV_ISR_X,
        APIO_PUSH_BLOCK,                      /*21: completion0/dropped, ~0/enabled */
        APIO_JMP(0),                          /*22: WAIT1IRQ already acknowledged */
        APIO_WAIT_PIN_HIGH(0),                /*23: CS0/OE end; initially idle-high is safe */
        APIO_IRQ_SET(0),                      /*24: sticky cancellation */
        APIO_MOV_PINDIRS_NULL|0x1000u,         /*25: same-block independent clamp */
        APIO_WAIT_PIN_LOW(0),                 /*26: arm next falling edge, then wrap23 */
        APIO_WAIT_PIN_LOW(0),                 /*27: RESET assertion */
        APIO_JMP(24),                         /*28: share IRQ/clamp; SM3 wraps at25 */
    };
    _Static_assert(sizeof(program)/sizeof(program[0])==SB_PR_INSTRUCTIONS,"PIO layout");
    APIO_WRAP_BOTTOM();
    for(unsigned i=0;i<SB_PR_INSTRUCTIONS;++i) {
        if(i==22)APIO_WRAP_TOP();
        APIO_ADD_INSTR(program[i]);
    }
    APIO_SM_CLKDIV_SET(1,0);
    APIO_SM_EXECCTRL_SET((1u<<30)|APIO_EXECCTRL_JMP_PIN(29)|APIO_STATUS_SEL_IRQ|APIO_STATUS_N(0));
    APIO_SM_SHIFTCTRL_SET(APIO_IN_COUNT(30)|APIO_IN_SHIFTDIR_L|APIO_OUT_SHIFTDIR_R);
    APIO_SM_PINCTRL_SET(APIO_IN_BASE(0)|APIO_OUT_BASE(4)|APIO_OUT_COUNT(16)|
        APIO_SET_BASE(23)|APIO_SET_COUNT(1)|APIO_SIDE_SET_BASE(23)|APIO_SIDE_SET_COUNT(2));
    APIO_SM_EXEC_INSTR(APIO_MOV_PINDIRS_NULL);
    APIO_SM_EXEC_INSTR(APIO_SET_Y(10));
    APIO_SM_EXEC_INSTR(APIO_SET_PINS(0));
    APIO_SM_EXEC_INSTR(APIO_SET_PIN_DIRS(1));
    APIO_SM_JMP_TO_START();
    /* Edge guards keep their original PIO1 pad ownership and IRQ0. An idle
     * high CS0/OE initially cancels/clamps, then waits for low before watching
     * high again. The owner still clears IRQ0 with its original five delays;
     * no guard can reassert from the old high level after observing the low.
     * RESET wraps immediately after the clamp, reasserting while held low.
     * No guard consumes IRQ0: WAIT1IRQ in SM0 remains its sole consumer. */
    for(unsigned sm=1;sm<4;++sm) {
        APIO_SET_BLOCK_FROM_VAR(1,sm==3?SB_PR_RESET:SB_PR_WATCH);APIO_SET_SM_VAR(sm);
        APIO_SET_BLOCK_FROM_VAR(1,sm==3?SB_PR_RESET:SB_PR_WATCH);APIO_WRAP_BOTTOM();
        APIO_SET_BLOCK_FROM_VAR(1,sm==3?25:26);APIO_WRAP_TOP();
        APIO_SET_BLOCK_FROM_VAR(1,sm==3?28:26);
        APIO_END(); /* program already loaded by SM0 */
        APIO_SM_CLKDIV_SET(1,0);
        APIO_SM_EXECCTRL_SET(1u<<30);
        APIO_SM_SHIFTCTRL_SET(APIO_IN_COUNT(30));
        APIO_SM_PINCTRL_SET(APIO_IN_BASE(sm==1?0:sm==2?2:29)|
            APIO_OUT_BASE(4)|APIO_OUT_COUNT(16)|APIO_SIDE_SET_BASE(23)|APIO_SIDE_SET_COUNT(2));
        APIO_SM_JMP_TO_START();
    }
    APIO_SET_BLOCK_FROM_VAR(1,SB_PR_INSTRUCTIONS);APIO_END_BLOCK();
    /* The main no longer polls all controls while driving. Preserve WE/CS1
     * cancellation with input-only PIO0 watchers; pads remain assigned PIO1.
     * NEXT IRQ0 reaches PIO1 without CPU/MMIO traffic. */
    APIO_SET_BLOCK_VAR(0);APIO_GPIOBASE_16();APIO_SET_SM_VAR(0);
    APIO_WRAP_BOTTOM();APIO_ADD_INSTR(APIO_WAIT_PIN_LOW(0));
    APIO_ADD_INSTR(APIO_IRQ_SET_NEXT(0));
    APIO_WRAP_TOP();APIO_ADD_INSTR(APIO_WAIT_PIN_HIGH(0));
    for(unsigned sm=0;sm<2;++sm) {
        APIO_SET_BLOCK_FROM_VAR(0,0);APIO_SET_SM_VAR(sm);
        APIO_WRAP_BOTTOM();APIO_SET_BLOCK_FROM_VAR(0,2);APIO_WRAP_TOP();
        APIO_SM_CLKDIV_SET(1,0);APIO_SM_EXECCTRL_SET(0);
        APIO_SM_SHIFTCTRL_SET(APIO_IN_COUNT(30));
        APIO_SM_PINCTRL_SET(APIO_IN_BASE(sm?1:3));
        APIO_SM_JMP_TO_START();
    }
    APIO_SET_BLOCK_FROM_VAR(0,3);APIO_END_BLOCK();
    for(int pin=20;pin<=35;++pin)APIO_GPIO_INPUT_OUTPUT(pin,1);
    APIO_GPIO_INPUT_OUTPUT(39,1);
    if(enable){APIO_ENABLE_SMS(0,3);APIO_ENABLE_SMS(1,15);}
}
