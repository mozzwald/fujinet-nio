/* Project ONLY resources owned by the frozen ROM. Added SMs/unused instruction
 * slots are separately checked by test_capture; never hide original SM state. */
#ifdef SB_CAPTURED_READ
#define sb_pio_rearm_program_init sb_cs1_captured_init
#endif
#include "../../tests/persistent_cosim.c"
#ifdef SB_CAPTURED_READ
#undef sb_pio_rearm_program_init
#endif
#define EMIT(v) do{if(n<capacity)out[n]=(uint32_t)(v);++n;}while(0)
unsigned sb_idle_snapshot(uint32_t *out,unsigned capacity){unsigned n=0;
 EMIT(s.e->gpio.gpio_output_state);EMIT(s.e->gpio.gpio_output_state>>32);
 EMIT(s.e->gpio.gpio_direction);EMIT(s.e->gpio.gpio_direction>>32);
 for(unsigned b=0;b<2;++b){epio_block_state_t *block=&s.e->block[b];
  EMIT(block->gpio_base);EMIT(block->irq.irq);EMIT(block->irq.irq_to_set);EMIT(block->irq.irq_to_clear);
  for(unsigned i=0;i<(b?4:2);++i){epio_sm_state_t *m=&block->sm[i];
   EMIT(m->reg.clkdiv);EMIT(m->reg.execctrl);EMIT(m->reg.shiftctrl);EMIT(m->reg.pinctrl);
   EMIT(m->x);EMIT(m->y);EMIT(m->isr);EMIT(m->osr);EMIT(m->isr_count);EMIT(m->osr_count);
   EMIT(m->pc);EMIT(m->delay);EMIT(m->stalled);EMIT(m->enabled);EMIT(m->exec_pending);EMIT(m->exec_instr);
   EMIT(m->fifo.tx_fifo_count);EMIT(m->fifo.rx_fifo_count);
   for(unsigned j=0;j<MAX_FIFO_DEPTH;++j){EMIT(m->fifo.tx_fifo[j]);EMIT(m->fifo.rx_fifo[j]);}
  }
  for(unsigned i=0;i<(b?32:3);++i)EMIT(block->instr[i]);
 }return n;
}
unsigned sb_idle_probe_quiet(void){
#ifdef SB_CAPTURED_READ
 epio_sm_state_t *low=&s.e->block[2].sm[0],*high=&s.e->block[0].sm[2];
 /* Reset's private history may latch; no descriptor/work notification exists.
  * The high capture must remain at its rendezvous and never publish. */
 return low->pc==0&&low->stalled&&low->enabled&&!low->fifo.rx_fifo_count&&
  high->pc==3&&high->stalled&&!high->fifo.rx_fifo_count&&
  !(s.e->block[2].irq.irq&~17u);
#else
 return !s.e->block[2].sm[0].enabled;
#endif
}
