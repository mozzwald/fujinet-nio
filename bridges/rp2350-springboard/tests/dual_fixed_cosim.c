/* Test-only fixed-read bridge: execute candidate PIO while Unicorn runs the actual
 * linked ARM function. Assertions match the native rearm safety checks. */
#include "pio_rearm_program.h"
#include <apio.h>
#include <epio_priv.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define DATA (UINT64_C(0xffff)<<20)
#define MARKER (UINT64_C(1)<<39)
#define RESET (UINT64_C(1)<<45)
#define IDLE (RESET|(UINT64_C(15)<<16))
struct tag {unsigned kind,owner;};
struct event {uint32_t clock,kind,owner,value;};
static struct {
    epio_t *e;uint16_t code[32];unsigned t,owner,txowner,loadedowner,snapshotowner;
    unsigned address,lastend,bound,head,tail,error,events,region;
    uint64_t pins,pipe[2];unsigned pipeowner[2];bool selected;
    struct tag tags[16];struct event log[16384];
} s;
static void event(unsigned kind,unsigned owner,unsigned value) {
    if(s.events<16384)s.log[s.events++]=(struct event){s.t,kind,owner,value};
    else s.error=1;
}
uint32_t sb_cosim_word(unsigned addr) {
    if(addr>=65536)return 0xffff;
    if(addr==8)return 0xfeed;
    if(addr==10)return 0xbeef;
    if(addr==0x444)return 0xa174;
    if(addr==0x446)return 0x5c8f;
    if(addr==0x100)return 0;
    if(addr==0x102)return 0xffff;
    return (uint16_t)((addr*40503u)^(addr>>3)^0xa55a);
}
int sb_cosim_init(void) {
    if(s.e)epio_free(s.e);
    memset(&s,0,sizeof(s));
    sb_pio_rearm_program_init(true);s.e=epio_init();if(!s.e)return -1;
    epio_set_gpio_output_control(s.e,39,1);epio_update_from_apio(s.e);
    for(unsigned i=0;i<32;++i){s.code[i]=epio_get_instr(s.e,1,i);epio_set_instr(s.e,1,i,s.code[i]&~0x1800u);}
    s.pins=s.pipe[0]=s.pipe[1]=IDLE;s.bound=5;return 0;
}
void sb_cosim_bus(uint64_t pins,unsigned address) {
    unsigned region=(unsigned)((pins>>16)&15);
    bool selected=(region==10 || region==9) && (pins&RESET);
    bool changed=s.selected && selected && s.region!=region;
    if(s.selected&&(!selected || changed)){s.lastend=s.t;s.bound=(pins&RESET)?5:7;}
    if(selected&&(!s.selected || changed))++s.owner;
    s.selected=selected;s.region=region;s.pins=pins;s.address=address;
}
int sb_cosim_step(unsigned clocks) {
    while(clocks--) {
        unsigned owner=s.pipeowner[1];
        epio_drive_gpios_ext(s.e,((UINT64_C(1)<<48)-1)&~(DATA|MARKER),s.pipe[1]);
        s.pipe[1]=s.pipe[0];s.pipe[0]=s.pins;
        s.pipeowner[1]=s.pipeowner[0];s.pipeowner[0]=s.owner;
        epio_sm_state_t *m=&s.e->block[1].sm[0];unsigned pc=m->pc;
        bool executing=m->enabled&&!m->delay;
        unsigned rx=epio_rx_fifo_depth(s.e,1,0),tx=epio_tx_fifo_depth(s.e,1,0);
        bool before=(s.e->gpio.gpio_direction&DATA)!=0;
        bool cancelled=epio_peek_block_irq_num(s.e,1,0);
        if(executing&&pc==SB_PR_SNAPSHOT)s.snapshotowner=owner;
        if(executing&&pc==SB_PR_PULL&&tx)s.loadedowner=s.txowner;
        if(executing&&pc==SB_PR_PRELOAD&&before)s.error=2;
        uint64_t marker=s.e->gpio.gpio_output_state&MARKER;
        for(unsigned sm=0;sm<4;++sm){
            epio_sm_state_t *a=&s.e->block[1].sm[sm];
            if(a->enabled&&!a->delay&&(s.code[a->pc]&0x1000)){
                if(s.code[a->pc]&0x800)s.e->gpio.gpio_output_state|=MARKER;
                else s.e->gpio.gpio_output_state&=~MARKER;
            }
        }
        epio_step_cycles(s.e,1);
        bool driven=(s.e->gpio.gpio_direction&DATA)!=0;
        if(executing&&pc==SB_PR_ENABLE&&cancelled&&driven)s.error=3;
        if(epio_rx_fifo_depth(s.e,1,0)>2||epio_tx_fifo_depth(s.e,1,0)>1)s.error=4;
        if(epio_rx_fifo_depth(s.e,1,0)>rx){
            unsigned kind=pc==SB_PR_REQUEST?1:pc==SB_PR_COMPLETE?2:0;
            if(!kind||s.tail-s.head>=16)s.error=5;
            s.tags[s.tail++%16]=(struct tag){kind,s.snapshotowner};
            event(kind,s.snapshotowner,0);
        }
        if(s.e->gpio.gpio_direction&~(DATA|MARKER))s.error=6;
        if(driven&&(s.e->gpio.gpio_direction&DATA)!=DATA)s.error=7;
        if(driven&&!s.selected&&s.t-s.lastend>s.bound)s.error=8;
        /* At observed gaps the old owner's drive must never enter a new read. */
        if(driven&&s.selected&&s.loadedowner!=s.owner)s.error=9;
        if(driven&&s.selected&&((s.e->gpio.gpio_output_state&DATA)>>20)!=(s.region==9?0xbeef:sb_cosim_word(s.address)))s.error=10;
        if(!before&&driven)event(4,s.loadedowner,(s.e->gpio.gpio_output_state&DATA)>>20);
        if((s.e->gpio.gpio_output_state&MARKER)!=marker)event(5,s.loadedowner,!!(s.e->gpio.gpio_output_state&MARKER));
        ++s.t;
        if(before&&!driven)event(7,s.loadedowner,0);
        if(s.error)return (int)s.error;
    }
    return 0;
}
uint32_t sb_cosim_fstat(void) {return epio_rx_fifo_depth(s.e,1,0)?0:0x100;}
uint64_t sb_cosim_rx(void) {
    if(s.head==s.tail){s.error=11;return 0;}
    struct tag tag=s.tags[s.head++%16];uint32_t value=epio_pop_rx_fifo(s.e,1,0);
    event(6,tag.owner,tag.kind);
    return ((uint64_t)((tag.owner<<2)|tag.kind)<<32)|value;
}
void sb_cosim_tx(uint32_t value,unsigned owner) {
    if(epio_tx_fifo_depth(s.e,1,0)){s.error=12;return;}
    s.txowner=owner;epio_push_tx_fifo(s.e,1,0,value);event(3,owner,value);
}
void sb_cosim_cancel(void) {epio_set_block_irq(s.e,1,0);}
uint32_t sb_cosim_gpio(void) {return (uint32_t)s.pins;}
uint32_t sb_cosim_clock(void) {return s.t;}
uint32_t sb_cosim_driven(void) {return !!(s.e->gpio.gpio_direction&DATA);}
uint32_t sb_cosim_events(void) {return s.events;}
const struct event *sb_cosim_log(void) {return s.log;}
