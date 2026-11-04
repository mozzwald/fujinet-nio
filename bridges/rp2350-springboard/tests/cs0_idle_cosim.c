/* Test-only projections of the frozen ROM harness, with/without a passive
 * observer. No extra instruction executes in the ARM responder. */
#ifdef SB_WITH_CS1_PASSIVE
#define sb_pio_rearm_program_init sb_rom_plus_cs1_probe_init
#endif
#include "persistent_cosim.c"
#ifdef SB_WITH_CS1_PASSIVE
#undef sb_pio_rearm_program_init
#endif
#define EMIT(v) do { if(n<capacity)out[n]=(uint32_t)(v); ++n; } while(0)
unsigned sb_idle_snapshot(uint32_t *out,unsigned capacity) {
    unsigned n=0;
    EMIT(s.e->gpio.gpio_output_state);EMIT(s.e->gpio.gpio_output_state>>32);
    EMIT(s.e->gpio.gpio_direction);EMIT(s.e->gpio.gpio_direction>>32);
    for(unsigned b=0;b<2;b++) {
        epio_block_state_t *block=&s.e->block[b];
        EMIT(block->gpio_base);EMIT(block->irq.irq);
        EMIT(block->irq.irq_to_set);EMIT(block->irq.irq_to_clear);
        for(unsigned i=0;i<4;i++) {
            epio_sm_state_t *m=&block->sm[i];
            EMIT(m->reg.clkdiv);EMIT(m->reg.execctrl);EMIT(m->reg.shiftctrl);EMIT(m->reg.pinctrl);
            EMIT(m->x);EMIT(m->y);EMIT(m->isr);EMIT(m->osr);
            EMIT(m->isr_count);EMIT(m->osr_count);EMIT(m->pc);EMIT(m->delay);
            EMIT(m->stalled);EMIT(m->enabled);EMIT(m->exec_pending);EMIT(m->exec_instr);
            EMIT(m->fifo.tx_fifo_count);EMIT(m->fifo.rx_fifo_count);
            for(unsigned j=0;j<MAX_FIFO_DEPTH;j++) {
                EMIT(m->fifo.tx_fifo[j]);EMIT(m->fifo.rx_fifo[j]);
            }
        }
        for(unsigned i=0;i<32;i++)EMIT(block->instr[i]);
    }
    return n;
}
#undef EMIT
unsigned sb_idle_probe_quiet(void) {
    epio_sm_state_t *p=&s.e->block[2].sm[0];
#ifdef SB_WITH_CS1_PASSIVE
    return p->pc==0&&p->stalled&&p->enabled&&!p->fifo.rx_fifo_count&&
           !p->fifo.tx_fifo_count&&!s.e->block[2].irq.irq;
#else
    return !p->enabled&&!p->fifo.rx_fifo_count&&!p->fifo.tx_fifo_count&&
           !s.e->block[2].irq.irq;
#endif
}
unsigned sb_idle_probe_pending(void){return epio_rx_fifo_depth(s.e,2,0);}
uint32_t sb_idle_probe_pop(void){
    uint32_t value=epio_pop_rx_fifo(s.e,2,0);epio_clear_block_irq(s.e,2,0);return value;
}
void sb_idle_probe_mutate(void){
    /* Negative control: miswire CS1 to CS0 and signal PIO1 cancellation. */
    epio_set_instr(s.e,2,0,APIO_WAIT_GPIO_LOW(0));
    epio_set_instr(s.e,2,1,APIO_IRQ_SET_PREV(0));
}
