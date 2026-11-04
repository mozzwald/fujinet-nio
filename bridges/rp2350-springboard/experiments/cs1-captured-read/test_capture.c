#include <apio.h>
#include <epio_priv.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
void sb_cs1_captured_init(bool);
#define RESET (UINT64_C(1)<<45)
#define IDLE (RESET | (UINT64_C(15)<<16))
#define READ1 (RESET | (UINT64_C(9)<<16))
#define READ0 (RESET | (UINT64_C(10)<<16))
#define DATA (UINT64_C(0xffff)<<20)
static epio_t *fresh(void){sb_cs1_captured_init(true);epio_t *e=epio_init();assert(e);epio_update_from_apio(e);return e;}
static void drive(epio_t *e,uint64_t pins,unsigned n){epio_drive_gpios_ext(e,(UINT64_C(1)<<48)-1,pins);epio_step_cycles(e,n);assert(!(e->gpio.gpio_direction&DATA));}
static bool ready(epio_t *e){return (e->block[2].irq.irq&2)!=0;}
static bool take(epio_t *e,unsigned *index){
 assert(ready(e));assert(epio_rx_fifo_depth(e,0,2)==1);assert(epio_rx_fifo_depth(e,2,0)==1);
 uint32_t hi=epio_pop_rx_fifo(e,0,2),lo=epio_pop_rx_fifo(e,2,0);
 bool ok=(hi&0x2000000f)==0x20000009 && ((lo>>16)&15)==9 && !(hi&0x1f700000) && !(e->block[2].irq.irq&17);
 *index=(lo>>1)&65535;epio_set_block_irq(e,2,3);return ok;
}
int main(void){unsigned n=0,index;
 for(unsigned phase=0;phase<8;++phase)for(unsigned address=0;address<=0xfffe;address+=2){
  if(address!=0&&address!=8&&address!=10&&address!=0x7ffe&&address!=0xfffe)continue;
  epio_t *e=fresh();drive(e,IDLE,16+phase);assert(!ready(e));
  drive(e,READ1|address,32);assert(take(e,&index));assert(index==0x8000+(address>>1));
  drive(e,IDLE,16);assert(!ready(e));assert(!epio_rx_fifo_depth(e,0,2));assert(!epio_rx_fifo_depth(e,2,0));
  drive(e,READ1|10,32);assert(take(e,&index));assert(index==0x8005);epio_free(e);++n;
 }
 /* Prompt acknowledgement: capture every read at measured-gap model values. */
 for(unsigned phase=0;phase<8;++phase)for(unsigned gap=5;gap<=8;++gap){
  epio_t *e=fresh();drive(e,IDLE,16+phase);
  for(unsigned j=0;j<4;++j){drive(e,READ1|(j*2),32);assert(take(e,&index));assert(index==0x8000+j);drive(e,READ1|(j*2),32);drive(e,IDLE,gap);}
  epio_free(e);++n;
 }
 /* Early endings at every capture/handshake phase must never be accepted. */
 for(unsigned width=1;width<=24;++width){epio_t *e=fresh();drive(e,IDLE,16);drive(e,READ1|8,width);drive(e,IDLE,32);
  if(ready(e)){assert(!take(e,&index));}
  epio_free(e);++n;
 }
 /* Observe the complete descriptor timing without a CPU service-time claim. */
 unsigned ready_min=1000,ready_max=0;
 for(unsigned phase=0;phase<8;++phase){epio_t *e=fresh();drive(e,IDLE,16+phase);unsigned t;
  for(t=1;t<100;++t){drive(e,READ1|8,1);if(ready(e))break;}
  assert(t<100);if(t<ready_min)ready_min=t;if(t>ready_max)ready_max=t;assert(take(e,&index));epio_free(e);++n;
 }
 /* CPU stall: one bounded descriptor, old address retained, no next owner. */
 for(unsigned gap=5;gap<=8;++gap){epio_t *e=fresh();drive(e,IDLE,16);drive(e,READ1|8,32);assert(ready(e));
  drive(e,IDLE,gap);drive(e,READ1|10,64);assert(!take(e,&index));assert(index==0x8004);
  drive(e,IDLE,16);drive(e,READ1|12,32);assert(take(e,&index));assert(index==0x8006);epio_free(e);++n;}
 for(unsigned ending=0;ending<4;++ending){epio_t *e=fresh();drive(e,IDLE,16);drive(e,READ1|8,32);
  uint64_t v=ending==0?IDLE:ending==1?(READ1|(UINT64_C(1)<<18)):ending==2?(READ1&~RESET):(READ1&~(UINT64_C(1)<<19));
  drive(e,v,16);assert(!take(e,&index));epio_free(e);++n;}
 /* CS before OE: no valid record until read qualified. */
 {epio_t *e=fresh();drive(e,IDLE,16);drive(e,READ1|(UINT64_C(1)<<18),24);assert(!ready(e));drive(e,READ1|8,32);assert(take(e,&index));epio_free(e);++n;}
 /* Distinct high address window retained, never consumed as a low index. */
 for(unsigned bit=36;bit<=44;++bit){if(bit==39)continue;epio_t *e=fresh();drive(e,IDLE,16);drive(e,READ1|8|(UINT64_C(1)<<bit),32);assert(ready(e));assert(!take(e,&index));epio_free(e);++n;}
 /* A live CS0 cycle alone creates no peripheral request. */
 {epio_t *e=fresh();drive(e,IDLE,16);drive(e,READ0|8,64);assert(!ready(e));assert(!epio_rx_fifo_depth(e,0,2));assert(!epio_rx_fifo_depth(e,2,0));epio_free(e);++n;}
 printf("PASS %u input-only captured-read cases; descriptor-ready %u..%u modeled clocks (no input synchronizer); no output-response or firmware qualification\n",n,ready_min,ready_max);
}
