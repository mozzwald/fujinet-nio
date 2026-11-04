/* HOST feasibility only. Input-only CS1 read descriptor; NOT a responder.
 * Keep the proven ROM initializer, owner, kernel and cancellation watchers.
 * The consumer must reject cancelled/reset/mismatched snapshots. No active
 * read-response interface, firmware target, ISR or Core0 adapter exists here. */
#include "pio_rearm_program.h"
#include <apio.h>
void sb_cs1_captured_init(bool enable) {
    sb_pio_rearm_program_init(false);
    APIO_ASM_CONTINUE();
    /* PIO0 BASE16 stays unchanged. Words0..2 and SM0/1 are ROM-owned. */
    APIO_SET_BLOCK_FROM_VAR(0,3); APIO_SET_SM_VAR(2); APIO_START(); APIO_WRAP_BOTTOM();
    APIO_ADD_INSTR(APIO_WAIT_IRQ_HIGH(7));
    APIO_ADD_INSTR(APIO_IN_PINS(32));
    APIO_ADD_INSTR(APIO_PUSH_BLOCK);
    APIO_WRAP_TOP(); APIO_ADD_INSTR(APIO_IRQ_SET_PREV(2));
    APIO_SM_CLKDIV_SET(1,0); APIO_SM_EXECCTRL_SET(0);
    APIO_SM_SHIFTCTRL_SET(APIO_IN_SHIFTDIR_L); APIO_SM_PINCTRL_SET(0);
    APIO_SM_JMP_TO_START();
    APIO_SET_SM_VAR(3); APIO_START(); APIO_WRAP_BOTTOM();
    APIO_ADD_INSTR(APIO_WAIT_PIN_LOW(0)); /* RESET GPIO45 */
    APIO_ADD_INSTR(APIO_IRQ_SET_PREV(0));
    APIO_ADD_INSTR(APIO_IRQ_SET_PREV(4)); /* sticky epoch fault */
    APIO_WRAP_TOP(); APIO_ADD_INSTR(APIO_WAIT_PIN_HIGH(0));
    APIO_SM_CLKDIV_SET(1,0); APIO_SM_EXECCTRL_SET(0);
    APIO_SM_SHIFTCTRL_SET(0); APIO_SM_PINCTRL_SET(APIO_IN_BASE(29));
    APIO_SM_JMP_TO_START();
    APIO_SET_BLOCK_FROM_VAR(0,11); APIO_END_BLOCK_FROM(3);

    APIO_SET_BLOCK_VAR(2); APIO_GPIOBASE_0(); APIO_SET_SM_VAR(0); APIO_START(); APIO_WRAP_BOTTOM();
    const unsigned short low[]={
        APIO_WAIT_GPIO_LOW(17), /*0: no work while CS1 inactive */
        APIO_IRQ_CLEAR(0),      /*1: before low address capture */
        APIO_IN_PINS(32),       /*2: A1..15 plus captured select identity */
        APIO_MOV_OSR_ISR,
        APIO_OUT_NULL(16),
        APIO_OUT_X(4),
        APIO_SET_Y(9),
        APIO_JMP_X_NOT_Y(0),    /*7: CS0=1 CS1=0 OE=0 WE=1 */
        APIO_IRQ_SET_NEXT(7),   /*8: high-window capture request */
        APIO_WAIT_IRQ_HIGH(2),  /*9: high data must be queued first */
        APIO_PUSH_BLOCK,        /*10: low data */
        APIO_IRQ_SET(1),        /*11: complete descriptor notification */
        APIO_WAIT_IRQ_HIGH(3),  /*12: explicit consumer acknowledgement */
        APIO_IRQ_CLEAR(1),
        APIO_WAIT_GPIO_HIGH(17),
        APIO_JMP(0),
        APIO_WAIT_GPIO_LOW(17), /*16..18: CS1 ending history */
        APIO_WAIT_GPIO_HIGH(17),
        APIO_IRQ_SET(0),
        APIO_WAIT_GPIO_LOW(18), /*19..21: OE ending history */
        APIO_WAIT_GPIO_HIGH(18),
        APIO_IRQ_SET(0),
        APIO_WAIT_GPIO_LOW(19), /*22..24: WE invalidates read */
        APIO_IRQ_SET(0),
        APIO_WAIT_GPIO_HIGH(19),
    };
    _Static_assert(sizeof(low)/sizeof(low[0])==25,"PIO2 instruction budget");
    for(unsigned i=0;i<25;++i){if(i==15)APIO_WRAP_TOP();APIO_ADD_INSTR(low[i]);}
    APIO_SM_CLKDIV_SET(1,0); APIO_SM_EXECCTRL_SET(0);
    APIO_SM_SHIFTCTRL_SET(APIO_IN_SHIFTDIR_L|APIO_OUT_SHIFTDIR_R); APIO_SM_PINCTRL_SET(0);
    APIO_SM_JMP_TO_START();
    for(unsigned sm=1;sm<4;++sm){
        unsigned start=sm==1?16:sm==2?19:22;
        APIO_SET_BLOCK_FROM_VAR(2,start); APIO_SET_SM_VAR(sm); APIO_WRAP_BOTTOM();
        APIO_SET_BLOCK_FROM_VAR(2,start+2); APIO_WRAP_TOP();
        APIO_SM_CLKDIV_SET(1,0); APIO_SM_EXECCTRL_SET(0);
        APIO_SM_SHIFTCTRL_SET(0); APIO_SM_PINCTRL_SET(0); APIO_SM_JMP_TO_START();
    }
    APIO_SET_BLOCK_FROM_VAR(2,25); APIO_END_BLOCK();
    if(enable){APIO_ENABLE_SMS(0,15);APIO_ENABLE_SMS(1,15);APIO_ENABLE_SMS(2,15);}
}
