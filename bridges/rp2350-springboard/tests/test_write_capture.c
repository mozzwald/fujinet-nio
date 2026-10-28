#include "write_capture.h"
#include "pio_rearm_program.h"
#include "bus_model.h"
#include <apio.h>
#include <epio_priv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define RESET (UINT64_C(1)<<45)
#define DATA (UINT64_C(0xffff)<<20)
#define MARKER (UINT64_C(1)<<39)
#define IDLE (RESET|(UINT64_C(15)<<16))
#define CHECK(c) do{if(!(c)){fprintf(stderr,"write capture line%d phase%u kind%u: %s\n",__LINE__,phase,kind,#c);exit(1);}}while(0)
static unsigned phase,kind,cases;
static uint64_t pins(uint32_t address,unsigned nibble,unsigned value) {
    return RESET|((uint64_t)nibble<<16)|((uint64_t)value<<20)|
        (address&0xfffeu)|((uint64_t)(address&0x70000u)<<20)|
        ((uint64_t)(address&0xf80000u)<<21);
}
static epio_t *create(void) {
    sb_pio_rearm_program_init(true);
    sb_write_capture_program_init(true);
    epio_t *e=epio_init();if(!e)exit(2);
    epio_set_gpio_output_control(e,39,1);epio_update_from_apio(e);
    /* The existing read fixtures handle optional side-set markers separately.
     * Strip only side-set bits in their model; do not alter write instructions. */
    for(unsigned i=0;i<32;++i)epio_set_instr(e,1,i,epio_get_instr(e,1,i)&~0x1800u);
    return e;
}
static void step(epio_t *e,uint64_t value,unsigned clocks) {
    epio_drive_gpios_ext(e,((UINT64_C(1)<<48)-1)&~MARKER,value);
    while(clocks--){epio_step_cycles(e,1);CHECK(!(e->gpio.gpio_direction&DATA));}
}
static struct wc_frame frame(epio_t *e) {
    struct wc_frame f={0};f.low_depth=epio_rx_fifo_depth(e,2,0);
    f.high_depth=epio_rx_fifo_depth(e,0,3);f.end_depth=epio_rx_fifo_depth(e,2,1);
    if(f.low_depth==2){f.low=epio_pop_rx_fifo(e,2,0);f.low_end=epio_pop_rx_fifo(e,2,0);}
    if(f.high_depth==2){f.high=epio_pop_rx_fifo(e,0,3);f.high_end=epio_pop_rx_fifo(e,0,3);}
    if(f.end_depth==1)f.outcome=epio_pop_rx_fifo(e,2,1);
    f.pulses=0u-e->block[2].sm[3].x;
    return f;
}
int main(void) {
    for(phase=0;phase<8;++phase)for(kind=0;kind<4;++kind){
        uint32_t addr=kind==3?0x1234:kind==2?0xfffe:0;
        unsigned value=kind==0?0:kind==1?0xffff:kind==2?0x1234:0xa55a;
        epio_t *e=create();step(e,IDLE,40+phase);
        step(e,pins(addr,5,value^0xffff),12);
        step(e,pins(addr,5,value),68);
        step(e,pins(addr,kind&1?13:15,value),12);step(e,IDLE,40);
        struct wc_frame f=frame(e);enum wc_result r=wc_validate(&f);
        if(r!=WC_OK)fprintf(stderr,"frame %08x/%u %08x/%u outcome%08x count%u depths%u/%u/%u result%d\n",f.low,f.low_end,f.high,f.high_end,f.outcome,f.pulses,f.low_depth,f.high_depth,f.end_depth,r);
        CHECK(r==WC_OK);CHECK(sb_address(f.low,f.high)==addr);
        CHECK(((f.high>>4)&0xffff)==value);++cases;epio_free(e);
    }
    for(phase=0;phase<8;++phase)for(kind=1;kind<=6;++kind){
        epio_t *e=create();step(e,IDLE,40+phase);step(e,pins(0,5,0x1234),68);
        step(e,pins(0,7,0x1234),kind);step(e,IDLE,40);
        struct wc_frame f=frame(e);CHECK(wc_validate(&f)!=WC_OK);++cases;epio_free(e);
    }
    /* Exactly one selected WE pulse per capture window. A second pulse must
     * invalidate the complete window, not pair first-address/second-data. */
    for(phase=0;phase<8;++phase)for(unsigned gap=5;gap<=12;++gap){
        kind=10;epio_t *e=create();step(e,IDLE,40+phase);
        step(e,pins(0,5,0x1234),68);step(e,IDLE,gap);
        step(e,pins(2,5,0xabcd),68);step(e,IDLE,40);
        struct wc_frame f=frame(e);CHECK(f.pulses==2);CHECK(wc_validate(&f)==WC_MULTIPLE);
        ++cases;epio_free(e);
    }
    for(phase=0;phase<8;++phase)for(kind=0;kind<3;++kind){
        epio_t *e=create();step(e,IDLE,40+phase);step(e,pins(0,5,0x1234),68);
        step(e,pins(0,kind==0?1:kind==1?4:5,0x1234),30);
        step(e,IDLE,40);struct wc_frame f=frame(e);
        if(kind==2)f.reset_edge=true;
        CHECK(wc_validate(&f)!=WC_OK);++cases;epio_free(e);
    }
    for(phase=0;phase<8;++phase)for(kind=0;kind<4;++kind){
        epio_t *e=create();step(e,IDLE,40+phase);
        /* Unselected WE activity must not count as a selected write. */
        step(e,pins(0,7,0xdead),24);step(e,IDLE,20);
        step(e,pins(0,kind&1?7:13,0xdead),12);
        step(e,pins(kind&2?0x923456:2,5,0x5aa5),68);
        step(e,IDLE,40);struct wc_frame f=frame(e);
        CHECK(wc_validate(&f)==WC_OK);
        CHECK(sb_address(f.low,f.high)==(kind&2?0x923456u:2u));
        CHECK(((f.high>>4)&0xffff)==0x5aa5);++cases;epio_free(e);
    }
    /* Incomplete, held write, reset/fault and malformed frames never qualify. */
    kind=20;struct wc_frame f={0};CHECK(wc_validate(&f)==WC_INCOMPLETE);
    f.reset_edge=true;CHECK(wc_validate(&f)==WC_RESET);f.reset_edge=false;
    f.cs0_edge=true;CHECK(wc_validate(&f)==WC_CS0_ACTIVITY);f.cs0_edge=false;
    f.oe_edge=true;CHECK(wc_validate(&f)==WC_READ_ACTIVITY);f.oe_edge=false;
    f.fifo_fault=true;CHECK(wc_validate(&f)==WC_FIFO);
    printf("One-shot write capture: %u phase/data/cancel/multiple cases PASS; no data outputs\n",cases);
    return 0;
}
