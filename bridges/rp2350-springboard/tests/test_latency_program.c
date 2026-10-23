#include "latency_program.h"
#include "rom_program.h"
#include <apio.h>
#include <epio_priv.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);exit(1);}}while(0)
#define DATA (UINT64_C(0xffff)<<20)
#define MARKER (UINT64_C(1)<<39)
#define RESET (UINT64_C(1)<<45)
#define IDLE (RESET|(UINT64_C(15)<<16))
#define READ (RESET|(UINT64_C(10)<<16))
#define EXTERNAL (((UINT64_C(1)<<48)-1)&~(DATA|MARKER))
static uint16_t baseline[32], encoded[32];
static enum sb_latency_marker profile;
static bool driven(epio_t *e) {return (e->gpio.gpio_direction&DATA)!=0;}
static void step(epio_t *e,unsigned n) {
    while(n--) {
        unsigned pc=e->block[1].sm[0].pc;
        CHECK(pc<32 && !e->block[1].sm[0].delay);
        bool high=sb_latency_side(pc,profile);
        CHECK(high==((encoded[pc]&0x1000)!=0));
        epio_step_cycles(e,1);
        if(high)e->gpio.gpio_output_state|=MARKER;else e->gpio.gpio_output_state&=~MARKER;
        CHECK((e->gpio.gpio_direction&~(DATA|MARKER))==0);
        CHECK((e->gpio.gpio_direction&DATA)==0 || (e->gpio.gpio_direction&DATA)==DATA);
        CHECK((e->gpio.gpio_direction&MARKER)==MARKER);
        if(profile==SB_LAT_DRIVE)CHECK(high==driven(e));
    }
}
static void bus(epio_t *e,uint64_t value,unsigned n) {epio_drive_gpios_ext(e,EXTERNAL,value);step(e,n);}
static epio_t *fresh(unsigned mode,unsigned marker) {
    profile=(enum sb_latency_marker)marker;
    sb_latency_program_init(true,(enum sb_latency_response)mode,profile);
    epio_t *e=epio_init();CHECK(e);
    /* EPIO applies injected SET before mux and lacks side-set; retain the
     * same explicit adapter as the baseline marker test. No pad/skew model. */
    epio_set_gpio_output_control(e,39,1);epio_update_from_apio(e);
    CHECK(epio_get_gpio_output_control(e,1)==(DATA|MARKER));
    epio_sm_reg_t cfg;epio_get_sm_reg(e,1,0,&cfg);
    CHECK(APIO_OUT_COUNT_FROM_REG(cfg.pinctrl)==16);
    CHECK(APIO_OUT_BASE_FROM_REG(cfg.pinctrl)==4);
    CHECK(APIO_SIDE_SET_BASE_FROM_REG(cfg.pinctrl)==23);
    CHECK(APIO_SIDE_SET_COUNT_FROM_REG(cfg.pinctrl)==1);
    CHECK(!APIO_EXECCTRL_SIDE_EN_FROM_REG(cfg.execctrl));
    for(unsigned i=0;i<32;++i) {
        encoded[i]=epio_get_instr(e,1,i);
        CHECK(!(encoded[i]&0x0f00));
        uint16_t word=encoded[i]&~0x1000u;
        if(mode>=SB_LAT_PIO_ZERO && i==13)CHECK(word==APIO_MOV_X_NULL);
        else if(mode>=SB_LAT_PIO_ZERO && i==16)CHECK(word==(mode==SB_LAT_PIO_ZERO?APIO_MOV_OSR_NULL:APIO_MOV_SRC_INVERT(APIO_MOV_OSR_NULL)));
        else CHECK(word==baseline[i]);
        epio_set_instr(e,1,i,word);
    }
    bus(e,READ,100);CHECK(!driven(e) && epio_rx_fifo_depth(e,1,0)==0);
    bus(e,IDLE,30);CHECK(!driven(e));return e;
}
static void begin(epio_t *e,unsigned mode,uint16_t word) {
    bus(e,READ|0x2468,0);
    unsigned ticks=0;
    while(!epio_rx_fifo_depth(e,1,0) && ticks++<40)step(e,1);
    CHECK(ticks<40 && !driven(e));
    if(mode<SB_LAT_PIO_ZERO) {
        CHECK((epio_pop_rx_fifo(e,1,0)&15)==10);
        epio_push_tx_fifo(e,1,0,word);
    }
}
static void ack(epio_t *e) {
    CHECK(!driven(e) && epio_peek_block_irq_num(e,1,0));
    while(epio_rx_fifo_depth(e,1,0))epio_pop_rx_fifo(e,1,0);
    while(epio_tx_fifo_depth(e,1,0))epio_pop_tx_fifo(e,1,0);
    epio_clear_block_irq(e,1,1);epio_clear_block_irq(e,1,0);
}
static void rx_first_cancelled_requests(void) {
    /* The new CPU engine consumes RX before inspecting IRQ0. Prove the
     * accepted request is still queued when cancellation parks the SM, and a
     * subsequent late CPU publication cannot escape that park. The publisher
     * must flush before ack; the next successful read must get its own word.
     * Model timing is PIO clocks, not a CPU/peripheral latency prediction. */
    const uint64_t invalid[]={IDLE,RESET|(UINT64_C(14)<<16),RESET|(UINT64_C(6)<<16),
        RESET|(UINT64_C(2)<<16),RESET|(UINT64_C(8)<<16),UINT64_C(10)<<16};
    for(unsigned marker=0;marker<4;++marker)
    for(unsigned k=0;k<6;++k) for(unsigned delay=0;delay<64;++delay) {
        epio_t *e=fresh(SB_LAT_CPU_ROM,marker);
        bus(e,READ,0);
        unsigned n=0;
        while(!epio_rx_fifo_depth(e,1,0) && n++<40)step(e,1);
        CHECK(n<40 && !driven(e));
        step(e,delay);bus(e,invalid[k],40);
        CHECK(epio_rx_fifo_depth(e,1,0)==1);
        CHECK(epio_peek_block_irq_num(e,1,0) && !driven(e));
        uint32_t snapshot=epio_pop_rx_fifo(e,1,0);
        CHECK((snapshot&0x2000000fu)==0x2000000au);
        epio_push_tx_fifo(e,1,0,0xdead);
        bus(e,READ,100);
        CHECK(!driven(e) && epio_tx_fifo_depth(e,1,0)==1);
        ack(e);step(e,30);CHECK(!driven(e));
        bus(e,IDLE,30);begin(e,SB_LAT_CPU_ROM,0xa55a);step(e,40);
        CHECK(driven(e) && ((e->gpio.gpio_output_state&DATA)>>20)==0xa55a);
        bus(e,IDLE,30);ack(e);epio_free(e);
    }
    puts("RX-first late publication:1536 cancelled requests flushed before ack; next word preserved.");
}
int main(void) {
    sb_rom_program_init(false);epio_t *base=epio_from_apio();
    for(unsigned i=0;i<32;++i)baseline[i]=epio_get_instr(base,1,i);
    epio_free(base);
    rx_first_cancelled_requests();
    unsigned worst=0,known_short_gaps=0;
    const uint64_t invalid[]={IDLE,RESET|(UINT64_C(14)<<16),RESET|(UINT64_C(6)<<16),RESET|(UINT64_C(2)<<16),RESET|(UINT64_C(8)<<16),UINT64_C(10)<<16};
    for(unsigned mode=0;mode<5;++mode) for(unsigned marker=0;marker<4;++marker) {
        /* Selected at startup: require deselection before accepting. */
        epio_t *e=fresh(mode,marker);bus(e,IDLE,0);epio_free(e);
        /* Exact logical preload spacing, distinct from pad-valid timing. */
        e=fresh(mode,marker);begin(e,mode,mode==SB_LAT_PIO_ONES?0xffff:0);
        unsigned tick=0,pull_tick=999,preload_tick=999,enable_tick=999;
        while(!driven(e) && tick<80) {
            unsigned pc=e->block[1].sm[0].pc;
            if(pc==16)pull_tick=tick;
            if(pc==17)preload_tick=tick;
            if(pc==23)enable_tick=tick;
            step(e,1);++tick;
        }
        CHECK(driven(e) && enable_tick-preload_tick==5 && enable_tick-pull_tick==6);
        epio_free(e);
        for(unsigned k=0;k<6;++k) for(unsigned phase=0;phase<80;++phase) {
            e=fresh(mode,marker);uint16_t word=mode==SB_LAT_PIO_ONES?0xffff:mode==SB_LAT_PIO_ZERO?0:0xa55a;
            e->gpio.gpio_output_state=(e->gpio.gpio_output_state&~DATA)|((uint64_t)(uint16_t)~word<<20);
            begin(e,mode,word);
            for(unsigned n=0;n<phase;++n) {
                unsigned pc=e->block[1].sm[0].pc;step(e,1);
                if(pc==17 || driven(e))CHECK(((e->gpio.gpio_output_state&DATA)>>20)==word);
                if(pc==17)CHECK(!driven(e));
            }
            bus(e,invalid[k],0);
            unsigned n=0;
            while(!epio_peek_block_irq_num(e,1,0) && n++<40)step(e,1);
            CHECK(n<=12 && !driven(e));if(n>worst)worst=n;
            step(e,300);CHECK(!driven(e));
            epio_push_tx_fifo(e,1,0,0xdead);epio_push_tx_fifo(e,1,0,0xbeef);
            bus(e,READ,30);CHECK(!driven(e));ack(e);step(e,30);CHECK(!driven(e));
            bus(e,IDLE,30);begin(e,mode,word);step(e,40);CHECK(driven(e));
            CHECK(((e->gpio.gpio_output_state&DATA)>>20)==word);
            bus(e,IDLE,30);ack(e);epio_free(e);
        }
        /* CPU absent during complete PIO constant response: RX remains queued,
         * data is right and release/parking still happens without a TX word. */
        if(mode>=SB_LAT_PIO_ZERO) {
            e=fresh(mode,marker);begin(e,mode,0);step(e,80);CHECK(driven(e));
            CHECK(epio_rx_fifo_depth(e,1,0)==1 && epio_tx_fifo_depth(e,1,0)==0);
            bus(e,IDLE,30);CHECK(!driven(e));step(e,1000);CHECK(!driven(e));epio_free(e);
        } else {
            e=fresh(mode,marker);bus(e,READ,100);CHECK(!driven(e));bus(e,IDLE,30);CHECK(!driven(e));epio_free(e);
        }
        /* RX backpressure blocks PUSH while already Hi-Z. */
        e=fresh(mode,marker);
        for(unsigned i=0;i<4;++i)epio_push_rx_fifo(e,1,0,0);
        bus(e,READ,100);CHECK(!driven(e));bus(e,IDLE,100);CHECK(!driven(e));epio_free(e);
        bool missed=false;
        for(unsigned phase=0;phase<8;++phase) {
            e=fresh(mode,marker);begin(e,mode,0xa55a);step(e,50+phase);CHECK(driven(e));
            bus(e,IDLE,1);bus(e,READ,20);missed|=driven(e);bus(e,IDLE,40);CHECK(!driven(e));epio_free(e);
        }
        CHECK(missed);++known_short_gaps;
    }
    printf("Latency modes/profiles:9600 cancellation phases PASS; worst park=%u clocks\n",worst);
    printf("Known one-clock deselect blind interval reproduced in %u mode/profile combinations\n",known_short_gaps);
    puts("PIO constants need no CPU TX reply; baseline CPU words and guards otherwise unchanged. Hardware/skew/synchronizers unmodeled.");
    return 0;
}
