/* Experiment B isolated low-window capture only. Not a bus responder.
 * Host supplies trigger/end flags; the required real rendezvous is NOT modeled
 * as free. No firmware target links this, no GPIO direction is ever enabled. */
#include <apio.h>
#include <epio_priv.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
static void program(unsigned bits) {
    APIO_ENABLE_GPIOS(); APIO_ENABLE_PIOS();
    APIO_ASM_INIT(); APIO_SET_BLOCK_VAR(0); APIO_GPIOBASE_0(); APIO_SET_SM_VAR(3); APIO_START();
    APIO_WRAP_BOTTOM();
    APIO_ADD_INSTR(APIO_WAIT_IRQ_HIGH(0));
    APIO_ADD_INSTR(APIO_MOV_ISR_NULL);
    APIO_ADD_INSTR(APIO_IN_PINS(bits));
    APIO_ADD_INSTR(APIO_IRQ_SET(1));
    APIO_ADD_INSTR(APIO_WAIT_IRQ_HIGH(2));
    APIO_WRAP_TOP(); APIO_ADD_INSTR(APIO_JMP(0));
    APIO_SM_CLKDIV_SET(1,0); APIO_SM_EXECCTRL_SET(0);
    APIO_SM_SHIFTCTRL_SET(APIO_AUTOPUSH|APIO_PUSH_THRESH(bits)|APIO_IN_COUNT(bits)|APIO_IN_SHIFTDIR_L);
    APIO_SM_PINCTRL_SET(APIO_IN_BASE(bits==16?1:0)); APIO_SM_JMP_TO_START(); APIO_END_BLOCK();
    APIO_ENABLE_SMS(0,8);
}
static uint32_t capture(uint64_t pins,unsigned bits) {
    program(bits); epio_t *e=epio_init(); assert(e); epio_update_from_apio(e);
    epio_drive_gpios_ext(e,(UINT64_C(1)<<48)-1,pins);
    epio_step_cycles(e,3); assert(epio_rx_fifo_depth(e,0,3)==0);
    epio_set_block_irq(e,0,0);
    epio_step_cycles(e,5); assert(epio_rx_fifo_depth(e,0,3)==1);
    assert(e->gpio.gpio_direction==0);
    epio_drive_gpios_ext(e,(UINT64_C(1)<<48)-1,0); epio_step_cycles(e,40);
    assert(epio_rx_fifo_depth(e,0,3)==1); /* captured once, never a late live read */
    uint32_t v=epio_pop_rx_fifo(e,0,3); epio_free(e); return v;
}
int main(void) {
    unsigned count=0;
    for(unsigned ctrl=0;ctrl<16;++ctrl)for(unsigned low=0;low<65536;low+=2) {
        /* Sample representative boundaries for actual PIO; algebra is exhaustive
         * in check_upper.py. All control shapes remain deliberately untrusted. */
        if(low!=0 && low!=8 && low!=10 && low!=0x7ffe && low!=0xfffe)continue;
        uint64_t p=((uint64_t)ctrl<<16)|low;
        assert(capture(p,20)==(uint32_t)p);
        assert(capture(p,16)==(((ctrl&1)<<15)|(low>>1)));count+=2;
    }
    uint32_t base=capture((UINT64_C(9)<<16)|8,16);
    assert(base==capture((UINT64_C(9)<<16)|8|(UINT64_C(1)<<36),16));
    assert(base==capture((UINT64_C(9)<<16)|8|(UINT64_C(1)<<45),16));
    printf("PASS %u isolated raw/index tokens; upper address and RESET demonstrably absent; no bus-service qualification\n",count);
}
