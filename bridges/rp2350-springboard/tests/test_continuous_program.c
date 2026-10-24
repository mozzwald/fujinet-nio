#include "continuous_program.h"
#include <apio.h>
#include <epio_priv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"%s:%d %s\n",__FILE__,__LINE__,#x);exit(1);}}while(0)
#define DATA (UINT64_C(0xffff)<<20)
#define MARKER (UINT64_C(1)<<39)
#define RESET (UINT64_C(1)<<45)
#define IDLE (RESET|(UINT64_C(15)<<16))
#define READ (RESET|(UINT64_C(10)<<16))
#define EXTERNAL (((UINT64_C(1)<<48)-1)&~(DATA|MARKER))
static uint16_t code[32];
static bool driven(epio_t *e) {return (e->gpio.gpio_direction&DATA)!=0;}
static void step(epio_t *e,unsigned n) {
    while(n--) {
        bool marker=(e->gpio.gpio_output_state&MARKER)!=0;
        for(unsigned sm=0;sm<4;++sm) if(e->block[1].sm[sm].enabled) {
            unsigned pc=e->block[1].sm[sm].pc;CHECK(pc<30);
            if(!sm) marker=(code[pc]&0x1000)!=0;
            else if(code[pc]&0x1000) marker=false;
        }
        epio_step_cycles(e,1);
        if(marker)e->gpio.gpio_output_state|=MARKER;
        else e->gpio.gpio_output_state&=~MARKER;
        CHECK(!(e->gpio.gpio_direction&~(DATA|MARKER)));
        CHECK((e->gpio.gpio_direction&DATA)==0 || (e->gpio.gpio_direction&DATA)==DATA);
        CHECK(marker==driven(e));
    }
}
static void bus(epio_t *e,uint64_t value,unsigned n) {epio_drive_gpios_ext(e,EXTERNAL,value);step(e,n);}
static epio_t *fresh(void) {
    sb_continuous_program_init(true);epio_t *e=epio_init();CHECK(e);
    epio_set_gpio_output_control(e,39,1);epio_update_from_apio(e);
    CHECK(epio_get_gpio_output_control(e,1)==(DATA|MARKER));
    for(unsigned i=0;i<32;++i) {
        code[i]=epio_get_instr(e,1,i);
        epio_set_instr(e,1,i,code[i]&~0x1000u);
    }
    bus(e,IDLE,20);CHECK(!driven(e));return e;
}
static void begin(epio_t *e,uint32_t low,uint16_t word,bool reply) {
    bus(e,READ|low,0);unsigned n=0;
    while(!epio_rx_fifo_depth(e,1,0) && n++<40)step(e,1);
    CHECK(n<40 && !driven(e));
    CHECK((epio_pop_rx_fifo(e,1,0)&0x2000000f)==0x2000000a);
    if(reply)epio_push_tx_fifo(e,1,0,word);
}
static void rearm(epio_t *e) {
    /* Owner disables SMs only after direction is low; drains all old replies
     * before resetting guards and clearing the sticky epoch IRQ. */
    CHECK(!driven(e));
    for(unsigned sm=0;sm<4;++sm) {
        epio_sm_reg_t cfg=e->block[1].sm[sm].reg;
        memset(&e->block[1].sm[sm],0,sizeof(e->block[1].sm[sm]));
        e->block[1].sm[sm].reg=cfg;
        e->block[1].sm[sm].pc=sm==0?0:sm==3?28:23;
        e->block[1].sm[sm].enabled=1;
    }
    e->block[1].sm[0].y=10;
    memset(&e->block[1].irq,0,sizeof(e->block[1].irq));
}
int main(void) {
    unsigned cases=0;
    const uint64_t invalid[]={RESET|(UINT64_C(11)<<16),RESET|(UINT64_C(14)<<16),RESET|(UINT64_C(6)<<16),
        RESET|(UINT64_C(2)<<16),RESET|(UINT64_C(8)<<16),UINT64_C(10)<<16};
    for(unsigned which=0;which<2;++which)for(unsigned width=1;width<=16;++width)
    for(unsigned phase=0;phase<16;++phase) {
        epio_t *e=fresh();begin(e,0x2468,0xa55a,true);step(e,50+phase);CHECK(driven(e));
        bus(e,invalid[which],0);
        unsigned released=0;
        for(unsigned t=1;t<=width+20;++t) {
            if(t==width+1)bus(e,READ|0x468a,0);
            step(e,1);if(!driven(e) && !released)released=t;
        }
        CHECK(released && released<=3);
        CHECK(!driven(e) && epio_peek_block_irq_num(e,1,0));
        epio_push_tx_fifo(e,1,0,0xdead);step(e,100);CHECK(!driven(e));
        bus(e,IDLE,30);rearm(e);begin(e,0x468a,0x5aa5,true);step(e,40);
        CHECK(driven(e) && ((e->gpio.gpio_output_state&DATA)>>20)==0x5aa5);
        bus(e,IDLE,30);CHECK(!driven(e));epio_free(e);++cases;
    }
    printf("Sticky CS0/OE: %u rapid reselect phases; no persistent stale drive\n",cases);
    cases=0;
    for(unsigned which=0;which<2;++which)for(unsigned width=1;width<=4;++width)
    for(unsigned phase=0;phase<80;++phase) {
        epio_t *e=fresh();begin(e,0x2468,0xa55a,true);step(e,phase);
        bus(e,invalid[which],width);bus(e,READ|0x468a,20);
        CHECK(!driven(e) && epio_peek_block_irq_num(e,1,0));
        epio_push_tx_fifo(e,1,0,0xdead);step(e,100);CHECK(!driven(e));
        bus(e,IDLE,30);rearm(e);begin(e,0x468a,0x5aa5,true);step(e,40);
        CHECK(driven(e) && ((e->gpio.gpio_output_state&DATA)>>20)==0x5aa5);
        epio_free(e);++cases;
    }
    printf("Preload/enable short-gap ownership: %u phases PASS\n",cases);
    cases=0;
    for(unsigned k=0;k<6;++k)for(unsigned phase=0;phase<80;++phase) {
        epio_t *e=fresh();begin(e,0x2468,0x0000,true);step(e,phase);
        bus(e,invalid[k],40);CHECK(!driven(e) && epio_peek_block_irq_num(e,1,0));
        epio_push_tx_fifo(e,1,0,0xffff);bus(e,READ|0x468a,100);CHECK(!driven(e));
        bus(e,IDLE,30);rearm(e);begin(e,0x468a,0xffff,true);step(e,40);
        CHECK(driven(e) && ((e->gpio.gpio_output_state&DATA)>>20)==0xffff);
        epio_free(e);++cases;
    }
    printf("Cancellation/reply ownership: %u phases; next word not contaminated\n",cases);
    epio_t *e=fresh();begin(e,0,0,false);step(e,10000);CHECK(!driven(e));
    bus(e,IDLE,30);CHECK(!driven(e) && epio_peek_block_irq_num(e,1,0));epio_free(e);
    e=fresh();bus(e,READ,1);bus(e,IDLE,30);
    CHECK(!driven(e));epio_free(e);
    /* Stop forces the highest-priority guard into its sticky clamp, independently
     * of the stalled service core. CPU cleanup may follow without a driven SM. */
    e=fresh();begin(e,0,0xffff,true);step(e,40);CHECK(driven(e));
    e->block[1].sm[3].pc=SB_CONT_CLAMP;step(e,1);CHECK(!driven(e));
    epio_push_tx_fifo(e,1,0,0xdead);step(e,1000);CHECK(!driven(e));epio_free(e);
    /* Re-arm while the next read is already selected: the owner must capture
     * a fresh snapshot, not consume the cancelled epoch's queued reply. */
    for(unsigned phase=0;phase<40;++phase) {
        e=fresh();begin(e,0xfffe,0x1234,true);step(e,phase);
        bus(e,IDLE,20);CHECK(!driven(e));
        epio_push_tx_fifo(e,1,0,0xdead);
        bus(e,READ|0x0002,0);rearm(e);
        unsigned wait=0;
        while(!epio_rx_fifo_depth(e,1,0)&&wait++<40)step(e,1);
        CHECK(wait<40 && !driven(e));
        epio_pop_rx_fifo(e,1,0);epio_push_tx_fifo(e,1,0,0xbeef);step(e,40);
        CHECK(driven(e)&&((e->gpio.gpio_output_state&DATA)>>20)==0xbeef);
        bus(e,IDLE,20);CHECK(!driven(e));
        /* Same address in a separate epoch is a legal new request. */
        rearm(e);begin(e,2,0x0000,true);step(e,40);
        CHECK(driven(e)&&!(e->gpio.gpio_output_state&DATA));
        bus(e,READ&~RESET,30);CHECK(!driven(e));
        epio_push_tx_fifo(e,1,0,0xffff);step(e,100);CHECK(!driven(e));
        epio_free(e);
    }
    puts("Selected rearm/repeated address/reset: 40 phases PASS");
    puts("Continuous PIO model PASS; synchronizers/pad timing unmodeled");return 0;
}
