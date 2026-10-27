#include "cs1_registers.h"
#include "async_stage_a.h"
static uint16_t latched_read;
static uint16_t stage_expected(const struct sb_cs1_registers *s,uint32_t address) {
    (void)s;return address>=65536?0xffff:latched_read;
}
#define sb_cs1_register_read stage_expected
#include "cs1_cosim.c"
#undef sb_cs1_register_read
static uint16_t table[VMB_TABLE_BYTES/2];
static struct sb_stage_a worker;
void sb_async_co_init(void) {sb_cs1_co_init();sb_stage_a_init(&worker,table);}
void sb_async_co_latch(unsigned value) {latched_read=(uint16_t)value;}
uint32_t sb_async_co_table(unsigned index) {return table[index];}
uint32_t sb_async_co_accepted(void) {return worker.accepted;}
uint32_t sb_async_co_fault(void) {return worker.capture.fault;}
void sb_async_co_retire(void) {
    uint32_t faults=((co.capture_fault || (co.fdebug[0]&4) || (co.fdebug[2]&6))?SB_CS1_FAULT_FIFO:0) |
        (epio_peek_block_irq_num(co.e,2,5)?SB_CS1_FAULT_CONTROL:0);
    if(faults) {sb_stage_a_fault(&worker,VMB_ERR_CAPTURE);return;}
    if(epio_rx_fifo_depth(co.e,0,2)<1 || epio_rx_fifo_depth(co.e,2,1)<1 ||
       epio_rx_fifo_depth(co.e,2,2)<2)return;
    uint32_t lo=epio_pop_rx_fifo(co.e,0,2),hi=epio_pop_rx_fifo(co.e,2,1);
    uint32_t epoch=epio_pop_rx_fifo(co.e,2,2),outcome=epio_pop_rx_fifo(co.e,2,2);
    sb_stage_a_retire(&worker,lo,hi,epoch,outcome,faults);
}
/* Model relative PIO-block startup phase; blocks cannot be enabled by one
 * atomic register write on hardware. Idle controls remain stable throughout. */
void sb_async_co_skew(unsigned block,unsigned clocks) {
    bool saved[3][4];
    for(unsigned b=0;b<3;++b)for(unsigned sm=0;sm<4;++sm) {
        saved[b][sm]=co.e->block[b].sm[sm].enabled;
        if(b!=block)co.e->block[b].sm[sm].enabled=false;
    }
    step(&co,clocks);
    for(unsigned b=0;b<3;++b)for(unsigned sm=0;sm<4;++sm)
        co.e->block[b].sm[sm].enabled=saved[b][sm];
}
